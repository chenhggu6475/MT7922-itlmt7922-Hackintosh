# Source and third-party notices

This project is derived from **thanhnndev and contributors'** [ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh). It preserves the original [Clear BSD License](LICENSE), copyright notices and attribution. The macOS baseline is pinned to [`0b620b9c47e321adf73d1e0765c835b0095f7fb1`](https://github.com/thanhnndev/ga403wm-hackintosh/tree/0b620b9c47e321adf73d1e0765c835b0095f7fb1), including `kexts/itlmt7925` and `macos/ItlWiFi`.

The Connac2 hardware and protocol reference is [openwrt/mt76](https://github.com/openwrt/mt76), pinned to [`467ae2e3f190baf737e15e318906da0dc7d0ab91`](https://github.com/openwrt/mt76/tree/467ae2e3f190baf737e15e318906da0dc7d0ab91). Relevant MediaTek material includes `mt7921/{pci.c,pci_mac.c,pci_mcu.c,mac.c,mcu.c,mcu.h}`, `mt792x_dma.c`, and `mt76_connac*`. Its license is reproduced in [licenses/mt76-LICENSE.txt](licenses/mt76-LICENSE.txt); source-level notices are retained.

## Embedded firmware

The following unmodified MT7922 images come from the official [linux-firmware repository](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/mediatek) and were retrieved on 2026-10-17. They are embedded in the kext so driver startup does not depend on reading files from the installed system.

| File | SHA-256 |
| --- | --- |
| `WIFI_MT7922_patch_mcu_1_1_hdr.bin` | `7fc9075d6d54b31e0539734614ca971fa52caf41b642a3000c8a4501591a4d98` |
| `WIFI_RAM_CODE_MT7922_1.bin` | `d100e5e04c324fc8da6302112d1e0b4fac952286447d2419eabcd8fb0a1f5b93` |

Firmware is distributed under [LICENCE.mediatek](https://github.com/hbyonly1/itlmt7922/blob/v0.2.1/fw/LICENCE.mediatek), independently of the macOS source license. In the repository it is stored at `fw/LICENCE.mediatek`; release archives include it at `licenses/LICENCE.mediatek`. The build verifies [fw/SHA256SUMS](https://github.com/hbyonly1/itlmt7922/blob/v0.2.1/fw/SHA256SUMS), also copied as `licenses/FIRMWARE-SHA256SUMS` in release archives. Firmware updates need matching protocol and hardware verification.

## Build dependency

[acidanthera/MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) supplies kernel headers and libraries and is obtained separately. The development reference used commit `3f750085caa17ec3a7880f11c11bf4f48cd6a164`. Xcode command-line tools supply the compiler, macOS SDK and app-signing tools.

Keep this notice, the source copyright notices, [LICENSE](LICENSE), and the firmware/third-party license files with redistributed source and release packages.
