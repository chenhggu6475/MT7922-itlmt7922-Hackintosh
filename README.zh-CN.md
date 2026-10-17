# itlmt7922

[English](README.md)

面向 macOS 15、x86_64 的 MT7922 / RZ616 实验性 Wi-Fi 驱动。系统中显示为 Ethernet，通过 **ItlWiFi7922.app** 菜单程序扫描和连接网络。

本项目基于 [thanhnndev/ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh)。原作者的 macOS 驱动、协议辅助代码和菜单程序是此次移植的基础；芯片相关部分已适配 MT7922 的 Connac2 硬件与固件接口。详见[移植说明](docs/PORTING.md)和[来源声明](NOTICE.md)。

## 当前状态

**[v0.2.1 预发布版](https://github.com/hbyonly1/itlmt7922/releases/tag/v0.2.1)** 已在 MT7922 / RZ616（`14c3:0616`）、macOS **15.8.1 / 24H32**、5 GHz WPA2 网络上实测。扫描、连接、DHCP 和双向收发正常。与我的 M1 Mac 速率相当。

| 项目 | 当前范围 |
| --- | --- |
| 硬件匹配 | `14c3:0616`；同时匹配 `14c3:7922`，后者尚无本项目实测 |
| 系统与架构 | 目标 macOS 15、x86_64；已测 15.8.1 / 24H32 |
| 网络 | 2.4 / 5 GHz、WPA2-Personal / AES-CCMP；本次硬件报告覆盖 5 GHz |
| 网络接口 | Ethernet 后端，使用配套菜单程序 |
| 默认配置 | 等级 3：HT/VHT，双向 Block Ack / A-MPDU |
| 未验证 | HE、160 MHz、6 GHz、WPA3-only、系统原生 Wi-Fi 菜单、AWDL/AirDrop、睡眠唤醒及长期稳定性 |

本仓库包含 Wi-Fi 驱动和应用；蓝牙驱动单独维护。

## 安装

1. 从 [v0.2.1 预发布页](https://github.com/hbyonly1/itlmt7922/releases/tag/v0.2.1)下载推荐的完整压缩包 **`itlmt7922-0.2.1.zip`**，保留可启动的 EFI 备份。发布附件的哈希见 `SHA256SUMS.txt`。
2. 将 `itlmt7922.kext` 放入 `EFI/OC/Kexts`。
3. 将 [OpenCore-merge-only.plist](docs/OpenCore-merge-only.plist) 中唯一的 `Kernel/Add` 条目加入现有配置。此文件是合并片段，不能替换完整 EFI 配置。
4. 退出旧菜单程序，打开 `ItlWiFi7922.app`。应用显示在菜单栏；等待首次扫描，选择 WPA2 网络并输入密码。

默认配置已执行完整连接流程，无需添加启动参数。菜单中的连接速率是估算值。

保存的密码以明文存放于 `~/Library/Application Support/ItlWiFi7922/networks.json`，文件权限为 `0600`，所在目录为 `0700`。请勿将该文件上传到问题报告。

连接失败时，在解压目录运行 `bash collect-logs.sh`；源码目录中的命令为 `bash scripts/collect-logs.sh`。输出包含网络名称、MAC 和 IP，分享前请检查并脱敏。报告需要的信息见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 构建与验证

请在源码目录中执行。需要 macOS 和 Xcode 命令行工具。构建脚本会在缺少依赖时将固定版本的 [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) 下载至 `build/MacKernelSDK`；也可通过 `MACKERNELSDK` 指定已有副本：

```sh
bash scripts/build.sh
sh tests/run_codec_tests.sh
sh tests/run_protocol_tests.sh
sh tests/run_power_tests.sh
python3 scripts/package.py
```

构建输出 `build/itlmt7922.kext` 和 `build/ItlWiFi7922.app`，检查固件哈希，并对应用进行 ad-hoc 签名。打包命令将发布压缩包及校验文件写入 `dist/`。主机测试通过 ASan/UBSan 检查 Connac2 描述符、收包解析、BA、WPA2 辅助代码和功率表边界；兼容性仍需相应硬件实测。

性能修复见 [0.2.1 发布说明](docs/RELEASE-0.2.1.md)。源码保留原作者的 [Clear BSD 许可证](LICENSE)；固件及 mt76 参考材料的独立声明见 [NOTICE.md](NOTICE.md)。
