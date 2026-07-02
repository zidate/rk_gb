# rk_gb IPC 项目概览

> 本文件包含项目级别的核心信息。详细模块文档见 `modules/` 目录。

## 1. 项目概述

### 目标与背景
该项目是一个基于 Rockchip 平台的 IPC 设备软件工程。目录中同时包含交叉编译工具链、设备应用、协议接入、媒体采集与第三方组件。

本次知识库重点聚焦：
- `RK/` 交叉编译工具链的角色
- `rk_gb/` 业务代码主体
- GB28181 注册、实时流、回放、下载、广播、对讲、监听、设备控制链路
- GAT1400 注册、保活、订阅、对象上报与 lower SDK 导出链路
- 终端需求、零配置专题与 GitHub issue 自动化配套能力

### 范围
- 范围内: 启动入口、构建入口、协议模块、配置来源、第三方依赖关系
- 范围外: 算法业务细节、完整硬件驱动实现、云平台业务流程、量产工具细节

### 干系人
- 负责人: 未在仓库中显式标注
- 主要使用者: IPC 设备软件开发、协议联调、平台对接人员

## 2. 模块索引

| 模块名称 | 职责 | 状态 | 文档 |
|---------|------|------|------|
| BuildRuntime | 说明工具链、构建入口、运行启动链 | ✅稳定 | [modules/build_runtime.md](modules/build_runtime.md) |
| RKSoCIpcPlatform | 说明 RK SoC IPC 平台身份、板型宏、SDK 库边界与启动主链 | ✅稳定 | [modules/rk_soc_ipc_platform.md](modules/rk_soc_ipc_platform.md) |
| Network | 说明以太网/Wi-Fi DHCP 客户端、内核 IPv6、BusyBox 网络 applet 和 DHCPv6 缺口 | 🚧待补 DHCPv6 | [modules/network.md](modules/network.md) |
| RKMediaPipeline | 说明 PAL/DMC、编码配置、实时流、录像、回放/下载和 OSD/翻转链路 | ✅稳定 | [modules/rk_media_pipeline.md](modules/rk_media_pipeline.md) |
| RKDebugPlaybook | 说明构建、日志、抓包、实时流、录像回放和编码配置排查路径 | ✅稳定 | [modules/rk_debug_playbook.md](modules/rk_debug_playbook.md) |
| Protocol | 说明协议编排、配置加载、会话生命周期与近期实现约束 | ✅稳定 | [modules/protocol.md](modules/protocol.md) |
| GB28181 | 负责 SIP 注册、实时流、回放、下载、广播、监听、控制 | ✅稳定 | [modules/gb28181.md](modules/gb28181.md) |
| GB28181-2022 Baseline | 负责 `GB/T 28181-2022` 调试基线、抓包检查项与工程落点 | ✅稳定 | [modules/gb28181-2022-baseline.md](modules/gb28181-2022-baseline.md) |
| GB28181 Talk Review | 负责对讲链路审查结论、风险判断与待办事项 | 🚧持续完善 | [modules/gb28181-talk-review.md](modules/gb28181-talk-review.md) |
| GB28181 SnapshotSubscribe Review | 负责远程抓拍、目录/报警/移动位置订阅审查结论与待办事项 | 🚧持续完善 | [modules/gb28181-snapshot-subscribe-review.md](modules/gb28181-snapshot-subscribe-review.md) |
| GAT1400 | 负责 VIID 注册、保活、订阅、本地 HTTP 服务和对象上报 | 🚧部分接线 | [modules/gat1400.md](modules/gat1400.md) |
| ExternalModuleDemos | 负责外部模块调用协议配置、在线状态和 1400 上报入口的最小示例 | ✅文档示例 | [modules/external_module_demos.md](modules/external_module_demos.md) |
| TerminalRequirements | 负责终端白皮书、测试用例和当前实现的对齐基线 | ✅已建立基线 | [modules/terminal_requirements.md](modules/terminal_requirements.md) |
| ZeroConfig | 负责零配置、注册重定向、出厂预置和装维接入专题知识 | ✅已建立专题 | [modules/zero_config.md](modules/zero_config.md) |
| GitHubAutomation | 负责 issue 自动巡检、自动修复入口和 PR 收口 | 🚧已落骨架 | [modules/github_automation.md](modules/github_automation.md) |

## 3. 快速链接
- [技术约定](../project.md)
- [架构设计](arch.md)
- [API 手册](api.md)
- [数据模型](data.md)
- [RK SoC IPC 平台基线](modules/rk_soc_ipc_platform.md)
- [RK 媒体链路](modules/rk_media_pipeline.md)
- [RK IPC 排查手册](modules/rk_debug_playbook.md)
- [Protocol 模块](modules/protocol.md)
- [外部模块接入 Demo](modules/external_module_demos.md)
- [GB/T 28181-2022 调试基线](modules/gb28181-2022-baseline.md)
- [GB28181 对讲审查记录](modules/gb28181-talk-review.md)
- [GB28181 远程抓拍与目录订阅审查记录](modules/gb28181-snapshot-subscribe-review.md)
- [变更历史](../history/index.md)

## 4. 当前结论

- `RK/` 基本是交叉编译工具链目录，不是 IPC 业务代码主体。
- `rk_gb/` 是主项目目录，构建入口为 `rk_gb/CMakeLists.txt`，可执行程序主体在 `rk_gb/App/`。
- 当前 RK 平台知识库已补齐板型、工具链、PAL/DMC、编码配置、录像存储、回放 codec 和排查路径；媒体问题应优先沿“协议解析 -> 编码配置 -> DMC 回调 -> 存储/发送”切分。
- 协议统一由 `CSofia` 在正常启动路径中调用 `ProtocolManager::Instance().Init()/Start()` 拉起；外部模块也统一直接访问 `ProtocolManager::Instance()`。
- GB28181 已形成注册、实时流、回放、下载、广播、升级重启等主要闭环；对讲、远程抓拍和订阅链路另有专项审查文档持续跟踪。
- GAT1400 已具备客户端、订阅、本地 HTTP 服务、失败补传和 lower SDK 导出能力，但设备时间同步已统一收口到 GB28181。
- 终端产品验收不能只看国标，必须同时回到 `终端需求说明书.pdf`、`终端需求白皮书.pdf` 和 `测试用例.xlsx`。
- “零配置”在本项目语境下应理解为“免现场手工录参”，而不是“设备零出厂预置”。
- GitHub issue 自动化方案已进入工作流骨架实施阶段，修复结果默认通过 PR 收口。
