# v0.2.1 — MT7922 Wi-Fi prerelease

[Download v0.2.1](https://github.com/hbyonly1/itlmt7922/releases/tag/v0.2.1) · [Installation](../README.md#install) · [中文安装说明](../README.zh-CN.md#安装)

The recommended **`itlmt7922-0.2.1.zip`** includes `itlmt7922.kext`, `ItlWiFi7922.app`, an OpenCore merge fragment, documentation, licenses and log collection. Source and host tests are available in the repository and GitHub source archives. Separate kext/app ZIPs, `SHA256SUMS.txt` and `build-manifest.json` are also provided. This release supports the Ethernet-backed WPA2 connection path; Bluetooth drivers are outside its scope.

## Changes

- MT7922 Connac2 PCIe/DMA initialization, dedicated firmware, MCU command/event layouts, scan and connection setup.
- Connac2 data descriptors, CCMP key installation, TXFREE buffer ownership and recovery handling.
- Default level 3 with receive/transmit BA/A-MPDU; level 2 remains available during fallback.
- Mandatory valid A-MSDU reception separated from optional aggregation negotiation; whole/final A-MSDU packets complete promptly.
- Corrected HT/VHT WTBL aggregation exponent and added receive-drop, DMA and receive-PHY diagnostics.
- CLC/SKU/SAR and EDCA adapted to MT7922, with current-firmware power parsing covered by host tests.

The tested bundle identifiers remain `local.experimental.itlmt7922` and `local.experimental.ItlWiFi7922`.

## Hardware evidence

One user tested an **MT7922 / RZ616, PCI `14c3:0616`**, on **macOS 15.8.1 / 24H32**, using a **5 GHz WPA2** network. Scan, join, DHCP and bidirectional traffic worked.

Version 0.2.0 measured 4.51 Mbps down / 29.62 Mbps up; the comparison Mac measured 384 / 89 Mbps. Its log showed BA disabled and repeated ADDBA rejection. After the 0.2.1 changes, the user reported download about 10 Mbps and upload about 24 Mbps above that Mac in the same test conditions. Exact new absolute values were not supplied, and the individual changes were not measured separately.

The build and host tests validate x86_64 compilation, firmware hashes and protocol/parser boundaries with ASan/UBSan. They do not extend the one-device hardware result to all cards or networks.

## Scope and known limits

Targets macOS 15 on x86_64, 2.4/5 GHz, WPA2-Personal/AES-CCMP, with the supplied menu app. The `14c3:7922` match is included but untested here. HE, 160 MHz, 6 GHz, WPA3-only, native macOS Wi-Fi UI, AWDL/AirDrop, sleep/wake and long-term stability are outside this release's verified scope. Cross-descriptor receive fragments need further work.

The app stores saved passwords as plaintext in a local `networks.json` file with `0600` permissions. Logs contain network identifiers; redact them before publishing. Installation uses a single OpenCore `Kernel/Add` merge entry, not a replacement EFI.

Credits and exact source/firmware references are in [NOTICE.md](../NOTICE.md). Performance reasoning and port details are in [PORTING.md](PORTING.md).
