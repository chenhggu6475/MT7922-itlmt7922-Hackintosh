# MT7922 port

The macOS baseline is [thanhnndev/ga403wm-hackintosh at `0b620b9c47e321adf73d1e0765c835b0095f7fb1`](https://github.com/thanhnndev/ga403wm-hackintosh/tree/0b620b9c47e321adf73d1e0765c835b0095f7fb1). The Connac2 reference is [openwrt/mt76 at `467ae2e3f190baf737e15e318906da0dc7d0ab91`](https://github.com/openwrt/mt76/tree/467ae2e3f190baf737e15e318906da0dc7d0ab91), particularly `mt7921/`, `mt792x_dma.c` and `mt76_connac*`. The Linux driver is used as a hardware/protocol reference, not loaded into macOS.

The port keeps the original macOS network interface, user-client ABI, menu app, WPA2 and protocol helpers. It replaces the chip-specific paths needed to move from MT7925 Connac3 to MT7922 Connac2:

| Area | Port work |
| --- | --- |
| PCIe and DMA | Connac2 register mapping, ownership/reset sequence, WFDMA rings and safe DMA shutdown before memory release |
| Firmware and MCU | MT7922 patch/RAM images, command flags/envelopes, 36-byte event header, response sequence/status checks and capability parsing |
| Scan and connection | Connac2 scan, domain, RX setup, BSS/STA/RLM and nested WTBL layouts |
| Data and keys | Connac2 TXWI/TXP and RXD groups/padding, TXFREE-owned buffer lifetime, KEY_V2 PTK/GTK installation and CCMP validation |
| Power and queues | CLC selection, 161-entry SKU tables, SAR limits and EDCA mappings |
| macOS lifecycle | Session setup before publishing link-up, cancellation, reconnection, firmware fault recovery and diagnostic properties |

Portable codecs keep descriptor and packet-layout checks testable outside the kernel. Tests exercise malformed lengths and truncation as well as valid frames. Power tests use the packaged RAM firmware, including its unsupported extended CLC table indices, which are bounded and skipped rather than causing valid table selection to abort.

## Performance follow-up

The first connected version, 0.2.0, measured **4.51 Mbps download / 29.62 Mbps upload**, versus **384 / 89 Mbps** on a comparison Mac. Its log showed a 5 GHz, 80 MHz, two-stream link at level 1, `BA=0/0`, and repeated rejection of the AP's ADDBA requests. The menu's estimated link rate did not describe actual throughput.

Version 0.2.1:

- Starts at level 3, enabling receive and transmit BA/A-MPDU; fallback from 3 to 2 retains receive BA.
- Receives valid A-MSDUs independently of the optional A-MSDU-in-A-MPDU negotiation switch.
- Completes whole A-MSDUs and final split subframes immediately, and records valid receive activity.
- Programs the shared WTBL aggregation exponent using the larger HT/VHT value, matching mt76.
- Adds receive-drop, DMA and actual receive-PHY diagnostics; rejects delayed-policy BA responses because the implementation uses immediate BA.

The user reported 0.2.1 download about **10 Mbps** and upload about **24 Mbps** above the comparison Mac under the same 5 GHz test conditions. Exact new absolute rates were not supplied. Multiple changes were applied together: the old log directly establishes that BA was disabled, but the separate throughput contribution of BA, A-MSDU and the other fixes has not been measured.

## Verified boundary

The hardware report covers one RZ616 (`14c3:0616`), macOS 15.8.1 / 24H32, and WPA2 on 5 GHz. Host tests and builds do not establish compatibility with every MT7922 board or firmware combination. HE, 160 MHz, 6 GHz, WPA3-only, native Wi-Fi integration, sleep/wake and long-duration stability are outside this release's verified scope. Cross-descriptor RX fragments remain a follow-up item.

This is a standalone Wi-Fi port. The release includes an Ethernet backend and its menu app; Bluetooth support is outside this repository.
