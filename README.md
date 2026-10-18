# itlmt7922: MT7922 / RZ616 macOS Wi-Fi Port

[简体中文](README.zh-CN.md) · [Original project](https://github.com/thanhnndev/ga403wm-hackintosh) · [Porting notes](docs/PORTING.md) · [Sources and licenses](NOTICE.md)

An experimental MT7922 / RZ616 Wi-Fi driver for **macOS 15 on x86_64**, ported from the MT7925 driver in **thanhnndev and contributors'** [ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh).

This repository packages the Wi-Fi driver and companion **ItlWiFi7922.app** separately to make building, installation and feedback easier. The current release baseline is **0.2.1**: macOS sees an Ethernet interface, while the menu bar app scans for and joins networks.

**The original author implemented the macOS driver framework, connection flow, protocol helpers and menu app. This project's main work is adapting the hardware and firmware protocols to the MT7922's Connac2 architecture, followed by throughput fixes after connectivity was established.** Original author and third-party notices are preserved.

## Current test results

Version 0.2.1 has been tested on **MT7922 / RZ616 (PCI `14c3:0616`), macOS 15.8.1 / 24H32, and a 5 GHz WPA2 network**: scanning, joining, DHCP, downloads and uploads worked. Throughput was comparable to my M1 Mac.

| Item | Current scope |
| --- | --- |
| Hardware match | `14c3:0616`; `14c3:7922` is also matched but has not been tested |
| OS and architecture | Targets macOS 15 on x86_64; tested on 15.8.1 / 24H32 |
| Networks | 2.4 / 5 GHz, WPA2-Personal / AES-CCMP; this test covered 5 GHz |
| Default mode | Ethernet backend with the ItlWiFi7922 menu app |
| Default radio profile | Level 3: HT/VHT, with Block Ack / A-MPDU in both receive and transmit directions |
| Unverified or unavailable | HE, 160 MHz, 6 GHz, WPA3-only, native macOS Wi-Fi, AWDL/AirDrop, sleep/wake and long-term stability |



### System versions

**Tested on macOS 15.8.1 / 24H32.
macOS 26 (Tahoe) is allowed to attempt loading; compatibility has not been tested.**

## Main differences from the original project

The comparison baseline is the original project's commit [`0b620b9`](https://github.com/thanhnndev/ga403wm-hackintosh/tree/0b620b9c47e321adf73d1e0765c835b0095f7fb1).

| Area | Original project | This project |
| --- | --- | --- |
| Project scope | A complete Hackintosh project for the GA403WM, including Wi-Fi, Bluetooth and other components | A standalone MT7922 Wi-Fi driver, menu app, tests and build scripts; no complete EFI |
| Target chip | The Wi-Fi driver targets MT7925, using Connac3 hardware and protocol layouts | Targets MT7922 / RZ616, adapting Connac2 registers, DMA and firmware protocols |
| Firmware | Firmware for MT7925 | Dedicated MT7922 patch / RAM firmware, embedded in the kext with pinned SHA-256 hashes |
| Firmware communication | MT7925 command envelopes, events and connection configuration | Adapts command flags, the 36-byte event header, response checks, and BSS / STA / RLM / WTBL layouts |
| Data path and keys | MT7925 descriptors and key configuration | Adapts Connac2 TXWI / TXP, RXD, TXFREE completion events and KEY_V2 key installation |
| Power and queues | Initialization and table entries for the original chip | Adapts MT7922 CLC, 161-entry SKU power tables, SAR limits and EDCA parameters |
| Usage | Includes Ethernet mode and an optional native Wi-Fi frontend | The 0.2.1 release baseline uses the hardware-tested Ethernet + app path; native experiments are maintained separately |
| Repository organization | Built and distributed as part of the complete machine project | Separate kext and app identifiers, Connac2 codecs, host tests, CI and release package checks |

The port covers hardware initialization, firmware communication, connection setup, data transfer and key handling. Adding a PCI ID to the match table alone cannot provide these adaptations.

## Porting approach

### 1. Retain the macOS framework and replace chip-specific components

The original author's macOS network interface, UserClient communication contract, menu app foundation, WPA2 helpers and protocol utilities are retained. MT7922 registers, firmware messages and data descriptors are connected to these frameworks using Connac2 formats, reducing the uncertainty of changing the hardware and system interfaces at the same time.

The hardware and protocol reference is [openwrt/mt76](https://github.com/openwrt/mt76/tree/467ae2e3f190baf737e15e318906da0dc7d0ab91), particularly `mt7921/`, `mt792x_dma.c` and `mt76_connac*`. The Linux driver serves as a reference for registers, field layouts and processing order; the code that actually runs on macOS is the kext in this repository.

### 2. Bring up hardware, firmware and connectivity in stages

First verify the PCI device, BAR mapping, chip ownership and reset, then adapt the WFDMA rings and MT7922 firmware download. Next handle capability queries, scanning, authentication and association, BSS / STA / RLM configuration, PTK / GTK installation and the four-way handshake.

At each layer, lengths, field offsets, command sequence numbers and return status must be checked. Readable registers, running firmware or discovered access points confirm only the corresponding stage; they do not establish that key installation and the data path are working.

### 3. Handle data descriptors and buffer ownership separately

The transmit path builds Connac2 TXWI / TXP descriptors and reclaims transmit buffers based on **TXFREE completion events**. DMA consuming a descriptor does not mean the firmware has finished using its data. The receive path handles RXD groups, padding, CCMP and payload boundaries before delivering data to the macOS network interface.

These codecs and boundary checks are separated into `codec/` where practical, so valid messages, truncated messages and malformed lengths can be tested outside the kernel. When stopping or releasing the device, DMA is stopped before the associated memory is handled, preventing hardware from continuing to access freed buffers.

### 4. Fix abnormal throughput

The early 0.2.0 version could connect, but under the same test conditions it achieved only **4.51 Mbps download and 29.62 Mbps upload**, versus **384 / 89 Mbps** on the comparison Mac. Logs showed a level 1 connection, `BA=0/0`, and repeated rejection of the access point's ADDBA requests. The displayed link rate did not represent actual throughput.

The main 0.2.1 fixes include:

- Start at level 3, enabling receive and transmit BA / A-MPDU; retain receive BA when falling back from level 3 to 2.
- Separate valid A-MSDU receive handling from the optional A-MSDU negotiation switch, completing delivery promptly for whole packets or the final split subframe.
- Correct the WTBL aggregation exponent by taking the larger HT / VHT value, matching the reference implementation.
- Reject delayed-policy BA responses, which are not implemented, and add packet-drop, DMA and actual receive-PHY diagnostics.

After the fixes, measured throughput was comparable to my M1 Mac. These changes were tested together, and their individual contributions have not been measured separately. The old logs directly establish that BA was disabled and ADDBA requests were being rejected.

### 5. Native functionality (unavailable)

The 0.3.0 native Wi-Fi experiment is kept in the separate local `feature/native-wifi` branch. Its goal is to add `itlmt7922Native.kext` so the system Wi-Fi menu can control connections. Native functionality is currently unavailable.

## Installation and usage

1. Keep a bootable EFI backup.
2. Copy `itlmt7922.kext` into `EFI/OC/Kexts` and merge the `Kernel/Add` entry from [OpenCore-merge-only.plist](docs/OpenCore-merge-only.plist) into your existing configuration.
3. Make sure this project's kext is enabled. If returning from the native experiment, disable the native frontend and remove `mt7922_native=1`, `mt7922_native_if=1` and `mt7922_native_dp=1`.
4. Reboot and open `ItlWiFi7922.app`. Select a network from the menu bar and enter its password.

The default configuration performs the complete connection flow without additional boot arguments. The link rate shown in the menu is an estimate; use actual speed tests to assess download and upload performance.

The app stores saved passwords as plaintext in `~/Library/Application Support/ItlWiFi7922/networks.json`, with file mode `0600` and directory mode `0700`. Do not include that file in issue reports or a public repository.

## Repository layout

| Directory | Purpose |
| --- | --- |
| `src/` | MT7922 core driver, macOS interfaces, and WPA2 / BA helpers |
| `codec/` | Connac2 descriptor and message codecs, receive parsing and boundary checks |
| `app/` | ItlWiFi7922 menu app and speed-test interface |
| `fw/` | MT7922 firmware, SHA-256 hashes and firmware license |
| `tests/` | Descriptor, packet, power-table, WPA2 / BA and release-check tests |
| `scripts/` | Build, packaging, log collection and release content checks |
| `docs/` | Porting notes, release notes and OpenCore merge fragments |
| `.github/workflows/` | Host tests, macOS builds and artifact checks; does not create Releases automatically |

## Build and verify

macOS and Xcode command-line tools are required. The script fetches a pinned [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) when the dependency is missing, or you can point `MACKERNELSDK` to an existing checkout.

```sh
bash tests/run_all.sh
bash scripts/build.sh
python3 scripts/package.py
python3 scripts/check-release.py --dist dist
```

The build produces `build/itlmt7922.kext` and `build/ItlWiFi7922.app`. Packaging writes to `dist/`, including the combined archive, separate kext / app archives, `SHA256SUMS.txt` and a build manifest. Do not mix archives from other versions into the `dist/` directory being checked.

Host tests use ASan / UBSan to check descriptors, parsing, WPA2, BA and power-table boundaries. Release checks also verify firmware hashes, empty credentials, package contents and consistent versions. These checks do not replace hardware testing on the relevant card.

## Reporting problems

Run `bash collect-logs.sh` from a release package, or `bash scripts/collect-logs.sh` from a source checkout. Logs contain nearby network names, MAC addresses and IP addresses; review and redact them before sharing.

Include the driver version, PCI ID, macOS version and build, band and security mode, and whether scanning, joining, DHCP and traffic in both directions succeed. For speed issues, also describe distance, band, test server and comparison conditions. Do not upload a complete EFI, saved password files or unreviewed logs. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits and licenses

- **thanhnndev and the original project's contributors**: the macOS MT7925 driver, network interfaces, connection framework, protocol helpers and ItlWiFi menu app form the direct foundation of this project.
- **openwrt/mt76, MediaTek and related contributors**: references for MT7922 / Connac2 hardware and firmware protocols.
- **acidanthera/MacKernelSDK**: headers and libraries required to build the kernel driver.
- **linux-firmware and MediaTek**: the MT7922 firmware used by this project and its redistribution license.

The macOS source retains the original author's [Clear BSD License](LICENSE). Licenses for firmware and referenced mt76 material are listed separately; see [NOTICE.md](NOTICE.md) for sources, pinned commits and hashes.
