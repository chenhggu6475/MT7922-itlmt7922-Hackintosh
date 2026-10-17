# itlmt7922

[简体中文](README.zh-CN.md)

Experimental MT7922 / RZ616 Wi-Fi driver for macOS 15 on x86_64. It presents an Ethernet interface and uses **ItlWiFi7922.app** for scanning and joining networks.

This port builds on [thanhnndev/ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh). The original author's macOS driver, protocol helpers and menu app made this work possible. Chip-specific code was adapted to the MT7922 Connac2 hardware and firmware interface. See [porting notes](docs/PORTING.md) and [source attribution](NOTICE.md).

## Status

**[v0.2.1 prerelease](https://github.com/hbyonly1/itlmt7922/releases/tag/v0.2.1)** has been tested on an MT7922 / RZ616 (`14c3:0616`) running macOS **15.8.1 / 24H32**, with a 5 GHz WPA2 network. Scanning, joining, DHCP and bidirectional traffic worked. Throughput was comparable to my M1 Mac.

| Item | Current scope |
| --- | --- |
| Hardware match | `14c3:0616`; `14c3:7922` is also matched but has not been tested here |
| OS / architecture | Targets macOS 15, x86_64; tested on 15.8.1 / 24H32 |
| Networks | 2.4 / 5 GHz, WPA2-Personal with AES-CCMP; hardware report covers 5 GHz |
| Interface | Ethernet backend with the supplied menu app |
| Default profile | Level 3: HT/VHT with transmit and receive Block Ack / A-MPDU |
| Unverified | HE, 160 MHz, 6 GHz, WPA3-only, native macOS Wi-Fi UI, AWDL/AirDrop, sleep/wake and long-term stability |

This repository contains the Wi-Fi driver and its app. Bluetooth drivers are maintained separately.

## Install

1. Download the recommended combined archive, **`itlmt7922-0.2.1.zip`**, from the [v0.2.1 prerelease](https://github.com/hbyonly1/itlmt7922/releases/tag/v0.2.1) and keep a bootable EFI backup. Release asset hashes are in `SHA256SUMS.txt`.
2. Copy `itlmt7922.kext` to `EFI/OC/Kexts`.
3. Merge the single `Kernel/Add` entry from [OpenCore-merge-only.plist](docs/OpenCore-merge-only.plist) into your existing configuration. This file is a merge fragment, not a complete EFI configuration.
4. Quit the old menu app and open `ItlWiFi7922.app`. It runs in the menu bar. Wait for the first scan, select a WPA2 network and enter its password.

The default mode already performs full connection setup; no boot arguments are required. The link rate shown in the menu is an estimate.

Saved passwords are stored as plaintext in `~/Library/Application Support/ItlWiFi7922/networks.json`, with file mode `0600` in a `0700` directory. Do not include that file in reports.

If a connection fails, run `bash collect-logs.sh` from the unpacked release, or `bash scripts/collect-logs.sh` from a source checkout. Review the output before sharing: it includes network names, MAC addresses and IP addresses. See [CONTRIBUTING.md](CONTRIBUTING.md) for a useful hardware report.

## Build and verify

From a source checkout, use macOS with Xcode command-line tools. The build fetches a pinned [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) into `build/MacKernelSDK` when absent, or uses an existing checkout selected with `MACKERNELSDK`:

```sh
bash scripts/build.sh
sh tests/run_codec_tests.sh
sh tests/run_protocol_tests.sh
sh tests/run_power_tests.sh
python3 scripts/package.py
```

The build produces `build/itlmt7922.kext` and `build/ItlWiFi7922.app`, checks the firmware hashes and uses ad-hoc app signing. Packaging writes release ZIPs and checksums to `dist/`. The host tests cover Connac2 descriptors, receive parsing, BA, WPA2 helpers and power-table bounds with ASan/UBSan. They complement hardware testing.

See [0.2.1 release notes](docs/RELEASE-0.2.1.md) for the performance changes. Source notices are preserved under [The Clear BSD License](LICENSE); firmware and referenced mt76 material have their own notices listed in [NOTICE.md](NOTICE.md).
