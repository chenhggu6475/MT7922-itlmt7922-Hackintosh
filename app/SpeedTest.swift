// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
import Foundation
import IOKit
import Security

struct SpeedResult {
    var pingMs: Double?
    var downMbps: Double?
    var upMbps: Double?
    var error: String?
    var note: String?
    var server = ""
    var remoteIP = ""
    var path = ""
    var at = Date()
}

enum SpeedPath: String, CaseIterable {
    case internet, wifi
    var label: String { self == .internet ? "Internet (VPN allowed)" : "Wi-Fi direct" }
}

struct SpeedServer: Identifiable {
    let id: String, name: String, download: String, probe: String
    let upload: String?
}

struct CurlSample {
    var down = 0.0, up = 0.0, total = 0.0, connect = 0.0
    var http = 0, code: Int32 = -1
    var remoteIP = "", route = "", detail = ""
    var httpOK: Bool { (200..<300).contains(http) }
    var probeOK: Bool { code == 0 && httpOK }
    func validTransfer(upload: Bool) -> Bool {
        // An HTTP error body or bytes sent to a rejecting upload endpoint are not a speed result.
        httpOK && (code == 0 || code == 28) && (upload ? up : down) > 0 && total > 0
    }
    var failure: String {
        if http >= 400 { return "HTTP \(http)" }
        switch code {
        case 6: return "DNS lookup failed"
        case 7: return "connection refused / blocked"
        case 28: return "connection timed out"
        case 35, 60: return "TLS connection failed"
        case 45: return "Wi-Fi interface unavailable"
        default: return detail.isEmpty ? "curl error \(code)" : String(detail.prefix(160))
        }
    }
}

enum SpeedTest {
    // Provider-owned test files; resolve their current IPs each run rather than pinning CDN addresses.
    static let servers = [
        SpeedServer(id: "cloudflare", name: "Cloudflare", download: "https://speed.cloudflare.com/__down?bytes=50000000",
                    probe: "https://speed.cloudflare.com/__down?bytes=1024", upload: "https://speed.cloudflare.com/__up"),
        SpeedServer(id: "hetzner-sg", name: "Hetzner · Singapore", download: "https://sin-speed.hetzner.com/100MB.bin",
                    probe: "https://sin-speed.hetzner.com/100MB.bin", upload: nil),
        SpeedServer(id: "ovh-sg", name: "OVHcloud · Singapore", download: "https://sin1-sgp.speedtest.network.ovh.net/files/100Mb.dat",
                    probe: "https://sin1-sgp.speedtest.network.ovh.net/files/100Mb.dat", upload: nil),
        SpeedServer(id: "hetzner-us", name: "Hetzner · US West", download: "https://hil-speed.hetzner.com/100MB.bin",
                    probe: "https://hil-speed.hetzner.com/100MB.bin", upload: nil)
    ]

    static func wifiInterface() -> String? {
        let svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("itlmt7922Eth"))
        guard svc != 0 else { return nil }
        defer { IOObjectRelease(svc) }
        return IORegistryEntrySearchCFProperty(svc, kIOServicePlane, "BSD Name" as CFString, kCFAllocatorDefault,
                                               IOOptionBits(kIORegistryIterateRecursively)) as? String
    }

    static func route(to ip: String) -> String {
        guard !ip.isEmpty else { return "" }
        let p = Process(); p.executableURL = URL(fileURLWithPath: "/sbin/route")
        p.arguments = ["-n", "get", ip]
        let out = Pipe(); p.standardOutput = out; p.standardError = FileHandle.nullDevice
        do { try p.run() } catch { return "" }
        p.waitUntilExit()
        let text = String(data: out.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        return text.split(separator: "\n").first { $0.trimmingCharacters(in: .whitespaces).hasPrefix("interface:") }?
            .split(separator: ":").last?.trimmingCharacters(in: .whitespaces) ?? ""
    }

    static func curl(_ iface: String?, _ args: [String]) -> CurlSample {
        let p = Process(); p.executableURL = URL(fileURLWithPath: "/usr/bin/curl")
        p.arguments = ["--disable", "-4", "-sS", "--fail", "--connect-timeout", "3", "-o", "/dev/null", "-w", "%{json}"]
            + (iface.map { ["--interface", $0, "--noproxy", "*"] } ?? []) + args
        let out = Pipe(), err = Pipe(); p.standardOutput = out; p.standardError = err
        var s = CurlSample()
        do { try p.run() } catch { s.detail = error.localizedDescription; return s }
        p.waitUntilExit()
        s.code = p.terminationStatus
        s.detail = String(data: err.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard let json = try? JSONSerialization.jsonObject(with: out.fileHandleForReading.readDataToEndOfFile()) as? [String: Any] else { return s }
        func number(_ k: String) -> Double { (json[k] as? NSNumber)?.doubleValue ?? 0 }
        s.down = number("size_download"); s.up = number("size_upload"); s.total = number("time_total")
        s.connect = number("time_connect"); s.http = Int(number("http_code")); s.remoteIP = json["remote_ip"] as? String ?? ""
        return s
    }

    static func run(secs: Int = 10, serverID: String = "auto", path: SpeedPath = .internet) -> SpeedResult {
        var r = SpeedResult()
        guard let wifi = wifiInterface() else { r.error = "Wi-Fi interface unavailable"; return r }
        let iface: String? = path == .wifi ? wifi : nil
        r.path = path == .wifi ? "Wi-Fi direct · \(wifi)" : "Internet · system route"
        let candidates = serverID == "auto" ? servers : servers.filter { $0.id == serverID }
        var failures = [String](), selected: SpeedServer?
        for server in candidates {
            let probe = curl(iface, ["--max-time", "5", "--range", "0-1023", server.probe])
            if probe.probeOK {
                selected = server; r.server = server.name; r.remoteIP = probe.remoteIP
                let used = route(to: probe.remoteIP)
                if path == .internet && !used.isEmpty { r.path = used.hasPrefix("utun") ? "Internet via VPN · \(used)" : "Internet · \(used)" }
                if probe.connect > 0 { r.pingMs = probe.connect * 1000 }
                break
            }
            failures.append("\(server.name): \(probe.failure)")
        }
        guard let server = selected else {
            if path == .wifi {
                // A small system-route probe distinguishes a blocked direct path from a dead provider.
                let probe = curl(nil, ["--max-time", "5", servers[0].probe])
                let used = route(to: probe.remoteIP)
                if probe.probeOK && used.hasPrefix("utun") {
                    r.error = "VPN blocks direct Wi-Fi. Select Internet (VPN allowed), or turn off the VPN to test Wi-Fi directly."
                    return r
                }
            }
            r.error = failures.joined(separator: "\n"); return r
        }
        // A second short probe gives a less noisy TCP-connect latency; this is not ICMP ping.
        let probe = curl(iface, ["--max-time", "5", "--range", "0-1023", server.probe])
        if probe.probeOK && probe.connect > 0 { r.pingMs = min(r.pingMs ?? .infinity, probe.connect * 1000) }
        let down = curl(iface, ["--max-time", "\(max(1, secs))", server.download])
        if down.validTransfer(upload: false) { r.downMbps = down.down * 8 / down.total / 1e6 }
        else { r.error = "Download: \(down.failure)" }
        if let upload = server.upload {
            // Unique, short-lived payload: no stale 64 MB file and no first-response timing subtraction on uploads.
            let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("itlwifi-up-\(UUID().uuidString).bin")
            defer { try? FileManager.default.removeItem(at: tmp) }
            var data = Data(count: 8 << 20)
            let randomStatus = data.withUnsafeMutableBytes { SecRandomCopyBytes(kSecRandomDefault, $0.count, $0.baseAddress!) }
            do {
                guard randomStatus == errSecSuccess else { throw NSError(domain: "RandomData", code: Int(randomStatus)) }
                try data.write(to: tmp)
                let up = curl(iface, ["--max-time", "\(max(1, secs))", "-X", "POST", "-H", "Content-Type: application/octet-stream",
                                      "-H", "Expect:", "--data-binary", "@\(tmp.path)", upload])
                if up.validTransfer(upload: true) { r.upMbps = up.up * 8 / up.total / 1e6 }
                else { r.note = "Upload unavailable: \(up.failure)" }
            } catch { r.note = "Could not prepare upload test" }
        } else { r.note = "This server measures download only" }
        return r
    }

    static func fmt(_ v: Double?) -> String {
        guard let v, v.isFinite else { return "–" }
        return v >= 100 ? String(format: "%.0f", v) : v >= 10 ? String(format: "%.1f", v) : String(format: "%.2f", v)
    }
}
