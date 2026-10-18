# v0.2.2 — MT7922 / RZ616 Wi-Fi prerelease

[中文 README](../README.zh-CN.md) · [English README](../README.md) · [Source attribution](../NOTICE.md)

## 中文

本版以已在 MT7922 / RZ616、macOS 15.8.1 / 24H32、5 GHz WPA2 网络上实测的 **0.2.1** 为基础，更新项目说明和 OpenCore 加载范围，保留原有 Connac2 硬件适配与吞吐修复。README 中的 0.2.1 指已实测的驱动基线，本次附件版本为 0.2.2。

- 英文 README 按维护者当前的中文 README 逐段、逐句同步；中文 README 保持原样。
- 补充与原项目的差异、适配思路、异常吞吐修复、测试范围与来源归属。
- 随附 OpenCore 合并片段的 `MinKernel`、`MaxKernel` 均为空，允许 macOS 26 尝试加载 Ethernet 后端。已有 EFI 也需清空相应条目的这两个字段；只替换 kext 不会更新配置。
- macOS 26 的实际兼容性未测试。原生模式目前不可用，本版不包含 `itlmt7922Native.kext`。
- 驱动与 App 版本更新为 0.2.2；无线收发逻辑、固件与默认等级 3 配置保持不变。打包脚本可用 `--dist` 将新包输出到独立目录。

推荐下载 **`itlmt7922-0.2.2.zip`**，内含 `itlmt7922.kext`、`ItlWiFi7922.app`、配置合并片段、日志脚本、文档与许可证。也提供独立 kext / App 包、`SHA256SUMS.txt` 和构建清单。蓝牙驱动与完整 EFI 不包含在内。

保留可启动的 EFI 备份，安装后使用 ItlWiFi7922 菜单程序连接 WPA2 网络。固件哈希、构建和主机测试不能替代 macOS 26 上的加载、扫描、连接、DHCP 与双向收发实测。

## English

This release builds on the **0.2.1** baseline tested on MT7922 / RZ616, macOS 15.8.1 / 24H32 and a 5 GHz WPA2 network. It updates the project documentation and OpenCore loading range while retaining the Connac2 adaptations and throughput fixes. References to 0.2.1 in the README identify that hardware-tested baseline; the new release assets are version 0.2.2.

- The English README follows the maintainer's current Chinese README paragraph by paragraph and sentence by sentence. The Chinese README is unchanged.
- Documents upstream differences, the porting approach, throughput fixes, test scope and attribution.
- Both `MinKernel` and `MaxKernel` are empty in the supplied OpenCore fragment, allowing macOS 26 to attempt loading the Ethernet backend. Existing EFI configurations must also clear these fields; replacing the kext alone does not update them.
- macOS 26 compatibility has not been tested. Native mode is currently unavailable, and this release does not include `itlmt7922Native.kext`.
- Driver and app versions are updated to 0.2.2. Radio/data-path logic, firmware and the default level 3 profile are unchanged. Packaging supports an optional `--dist` output directory.

The recommended **`itlmt7922-0.2.2.zip`** contains the core kext, menu app, OpenCore merge fragment, log collector, documentation and licenses. Separate kext / app archives, `SHA256SUMS.txt` and a build manifest are also provided. No Bluetooth drivers or complete EFI are included.

Keep a bootable EFI backup and use ItlWiFi7922 to join WPA2 networks. Firmware checks, builds and host tests do not replace testing kernel loading, scanning, joining, DHCP and bidirectional traffic on macOS 26.
