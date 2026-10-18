# itlmt7922：MT7922 / RZ616 macOS Wi-Fi 移植

[English](README.md) · [原项目](https://github.com/thanhnndev/ga403wm-hackintosh) · [移植记录](docs/PORTING.md) · [来源与许可证](NOTICE.md)

面向 **macOS 15、x86_64** 的 MT7922 / RZ616 实验性 Wi-Fi 驱动，基于 **thanhnndev 和贡献者**的 [ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh) 中的 MT7925 驱动移植。

本仓库将 Wi-Fi 驱动和配套 **ItlWiFi7922.app** 单独整理，方便构建、安装和反馈。当前发布基线是 **0.2.1**：系统中显示为 Ethernet，由菜单栏 App 扫描和连接网络。

**原作者完成了 macOS 驱动框架、连接流程、协议辅助代码和菜单程序。本项目的主要工作是 MT7922 的 Connac2 硬件与固件协议适配，以及连接成功后的吞吐修复。** 原作者及第三方声明均保留。

## 当前测试情况

0.2.1 已在 **MT7922 / RZ616（PCI `14c3:0616`）、macOS 15.8.1 / 24H32、5 GHz WPA2 网络**上实测：扫描、连接、DHCP、下载和上传正常，与我的 M1 Mac 速率相当。

| 项目 | 当前范围 |
| --- | --- |
| 硬件匹配 | `14c3:0616`；同时匹配 `14c3:7922`，后者未测试 |
| 系统与架构 | 目标 macOS 15、x86_64；测试版本为 15.8.1 / 24H32 |
| 网络 | 2.4 / 5 GHz、WPA2-Personal / AES-CCMP；本次测试覆盖 5 GHz |
| 默认模式 | Ethernet 后端，使用 ItlWiFi7922 菜单程序 |
| 默认无线配置 | 等级 3：HT/VHT，接收与发送方向均使用 Block Ack / A-MPDU |
| 未验证或未提供 | HE、160 MHz、6 GHz、WPA3-only、系统原生 Wi-Fi、AWDL/AirDrop、睡眠唤醒及长期稳定性 |



### 系统版本

**已测试 macOS 15.8.1 / 24H32
macOS 26（Tahoe）允许尝试加载，兼容性未测试。**

## 与原项目的主要差异

对比基线为原项目提交 [`0b620b9`](https://github.com/thanhnndev/ga403wm-hackintosh/tree/0b620b9c47e321adf73d1e0765c835b0095f7fb1)。

| 方面 | 原项目 | 本项目 |
| --- | --- | --- |
| 项目范围 | 面向 GA403WM 的整机 Hackintosh 工程，包含 Wi-Fi、蓝牙及其他组件 | 单独的 MT7922 Wi-Fi 驱动、菜单 App、测试与构建脚本，不提供整机 EFI |
| 目标芯片 | Wi-Fi 驱动面向 MT7925，使用 Connac3 硬件与协议布局 | 面向 MT7922 / RZ616，适配 Connac2 寄存器、DMA 和固件协议 |
| 固件 | MT7925 对应固件 | 使用独立的 MT7922 patch / RAM 固件，内嵌到 kext 并固定 SHA-256 |
| 固件交互 | MT7925 的命令封装、事件及连接配置 | 适配命令标志、36 字节事件头、响应校验，以及 BSS / STA / RLM / WTBL 等布局 |
| 数据收发与密钥 | MT7925 对应的描述符及密钥配置 | 适配 Connac2 TXWI / TXP、RXD、TXFREE 完成事件和 KEY_V2 密钥安装 |
| 功率与队列 | 原芯片对应的初始化和表项 | 适配 MT7922 的 CLC、161 项 SKU 功率表、SAR 限制和 EDCA 参数 |
| 使用方式 | 包含 Ethernet 模式及可选的原生 Wi-Fi 前端 | 0.2.1 发布基线采用已实测的 Ethernet + App 路径；原生实验单独维护 |
| 工程组织 | 随整机工程构建和分发 | 独立 kext 与 App 标识、Connac2 编解码模块、主机测试、CI 和发布包检查 |

这次移植涉及硬件初始化、固件通信、连接、收发和密钥路径。仅把 PCI ID 加入匹配表，无法完成这些适配。

## 适配思路

### 1. 保留 macOS 框架，替换芯片相关部分

保留原作者的 macOS 网络接口、UserClient 通信约定、菜单 App 基础、WPA2 辅助代码和协议工具。把 MT7922 的寄存器、固件消息和数据描述符按 Connac2 格式接入这些框架，减少同时改动硬件与系统接口带来的不确定性。

硬件及协议参考来自 [openwrt/mt76](https://github.com/openwrt/mt76/tree/467ae2e3f190baf737e15e318906da0dc7d0ab91)，重点参考 `mt7921/`、`mt792x_dma.c` 和 `mt76_connac*`。Linux 驱动作为寄存器、字段布局和处理顺序的参考；实际运行于 macOS 的仍是本仓库中的 kext。

### 2. 分阶段打通硬件、固件和连接

先确认 PCI 设备、BAR 映射、芯片所有权和复位，再适配 WFDMA 环形队列及 MT7922 固件下载。随后处理能力查询、扫描、认证关联、BSS / STA / RLM 配置、PTK / GTK 安装和四次握手。

每层都需要核对长度、字段偏移、命令序号和返回状态。寄存器可读、固件启动或扫描出热点，只能说明相应阶段成功，不能据此判断后续的密钥和数据路径已经可用。

### 3. 单独处理收发描述符与缓冲区归属

发送端按 Connac2 组织 TXWI / TXP，并根据 **TXFREE 完成事件**回收发送缓冲区；DMA 已消费描述符，不等于固件已经用完数据。接收端处理 RXD 分组、填充、CCMP 和有效载荷边界，再向 macOS 网络接口交付数据。

这些编解码与边界检查尽量拆到 `codec/`，便于在内核之外测试合法消息、截断消息和异常长度。停止或释放设备时，先停止 DMA，再处理相关内存，避免硬件继续访问已释放的缓冲区。

### 4. 修复异常吞吐速率

早期 0.2.0 能连接，但同条件测速只有 **下载 4.51 Mbps、上传 29.62 Mbps**；对比 Mac 为 **384 / 89 Mbps**。日志显示连接处于等级 1，`BA=0/0`，并反复拒绝接入点的 ADDBA 请求。显示的连接速率不能代表实际吞吐。

0.2.1 的主要修复包括：

- 默认使用等级 3，启用接收与发送 BA / A-MPDU；从等级 3 回退到 2 时保留接收 BA。
- 将有效 A-MSDU 的接收处理与可选的 A-MSDU 协商开关分离，完整包或最后一个拆分子帧及时完成交付。
- 修正 WTBL 的聚合长度指数，取 HT / VHT 对应值中的较大者，与参考实现一致。
- 拒绝尚未实现的延迟策略 BA 响应，并补充丢包、DMA 和实际接收 PHY 诊断。

修复后，实测吞吐与我的 M1 Mac 速率相当。上述修改一起进入测试，尚未分别测量每项修改的独立收益；能够直接从旧日志确认的问题是 BA 未启用及 ADDBA 被拒绝。

### 5. 原生功能（不可用）

0.3.0 原生 Wi-Fi 实验代码保存在本地独立的 `feature/native-wifi` 分支中，目标是增加 `itlmt7922Native.kext`，由系统 Wi-Fi 菜单控制连接。原生功能目前不可用。

## 安装与使用

1. 保留可启动的 EFI 备份。
2. 将 `itlmt7922.kext` 放入 `EFI/OC/Kexts`，将 [OpenCore-merge-only.plist](docs/OpenCore-merge-only.plist) 中的 `Kernel/Add` 条目合并到已有配置。
3. 确认启用本项目的 kext。若从原生实验版本回退，应停用原生前端，并移除 `mt7922_native=1`、`mt7922_native_if=1`、`mt7922_native_dp=1`。
4. 重启，打开 `ItlWiFi7922.app`。在菜单栏中选择网络并输入密码。

默认配置执行完整连接流程，无需额外启动参数。菜单中的连接速率为估算值；下载和上传表现以实际测速为准。

App 将已保存的密码以明文存放于 `~/Library/Application Support/ItlWiFi7922/networks.json`，文件权限为 `0600`，目录权限为 `0700`。请勿将该文件放入问题报告或公开仓库。

## 仓库结构

| 目录 | 用途 |
| --- | --- |
| `src/` | MT7922 核心驱动、macOS 接口、WPA2 / BA 等辅助代码 |
| `codec/` | Connac2 描述符和消息编解码、接收解析与边界检查 |
| `app/` | ItlWiFi7922 菜单程序及测速界面 |
| `fw/` | MT7922 固件、SHA-256 与固件许可证 |
| `tests/` | 描述符、数据包、功率表、WPA2 / BA 和发布检查测试 |
| `scripts/` | 构建、打包、日志采集和发布内容检查 |
| `docs/` | 适配记录、版本说明和 OpenCore 合并片段 |
| `.github/workflows/` | 主机测试、macOS 构建与产物检查；不自动创建 Release |

## 构建与验证

需要 macOS 和 Xcode 命令行工具。脚本缺少依赖时会获取固定版本的 [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)，也可用 `MACKERNELSDK` 指向已有副本。

```sh
bash tests/run_all.sh
bash scripts/build.sh
python3 scripts/package.py
python3 scripts/check-release.py --dist dist
```

构建输出 `build/itlmt7922.kext` 和 `build/ItlWiFi7922.app`；打包输出到 `dist/`，包括完整包、独立 kext / App 包、`SHA256SUMS.txt` 和构建清单。不要将其他版本的压缩包混放到本次待检查的 `dist/` 中。

主机测试使用 ASan / UBSan 检查描述符、解析、WPA2、BA 和功率表边界。发布检查还核对固件哈希、空凭据、包内容和版本一致性。这些检查不能替代相应网卡上的实机验证。

## 问题反馈

发布包中运行 `bash collect-logs.sh`，源码目录中运行 `bash scripts/collect-logs.sh`。日志包含附近网络名称、MAC 和 IP，分享前请检查并脱敏。

反馈请注明驱动版本、PCI ID、macOS 版本与构建号、频段和加密方式，以及扫描、连接、DHCP、双向收发分别是否成功。测速问题还需说明距离、频段、测试节点和对比条件。不要上传完整 EFI、保存的密码文件或未经检查的日志。详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 致谢与许可证

- **thanhnndev 和原项目贡献者**：macOS MT7925 驱动、网络接口、连接框架、协议辅助代码和 ItlWiFi 菜单程序，是本项目的直接基础。
- **openwrt/mt76、MediaTek 及相关贡献者**：MT7922 / Connac2 硬件和固件协议参考。
- **acidanthera/MacKernelSDK**：内核构建所需的头文件与库。
- **linux-firmware 与 MediaTek**：本项目使用的 MT7922 固件及其分发许可。

macOS 源码保留原作者的 [Clear BSD 许可证](LICENSE)。固件与 mt76 参考材料的许可证单独列出，具体来源、固定提交和哈希见 [NOTICE.md](NOTICE.md)。
