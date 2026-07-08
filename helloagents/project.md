# 项目技术约定

## 技术栈
- 核心: C / C++ 混合工程，CMake 构建，Rockchip IPC SDK 与 `arm-rockchip830-linux-uclibcgnueabihf-*` 交叉编译工具链
- 平台: Linux 用户态 IPC 程序，目标芯片以 RV1106 / RV1103 系列为主
- 多媒体: RK MPP、PAL/Capture、FFmpeg、media-server(`libmpeg` / `librtp`)
- 协议: GB28181、GAT1400、SIP/SDP、RTP/PS，仓库内还包含 ONVIF、RTSP 和 Tuya 相关模块

## 代码亮点
- 启动编排比较清楚：`CSofia` 先做配置和默认值初始化，再按产品模式进入媒体、网络、存储、协议的固定主链，正常模式下会先 `VideoParamInit()` 再 `VideoInit()`，最后才拉起 `ProtocolManager::Init()/Start()`。
- 配置是统一入口，不是散写：主程序、协议模块和媒体模块都围绕 `IConfigManager`、`CFG_*` 和本地 ini/HTTP 配置做 `query -> compare -> apply`，减少了“改了但没落地”这类典型设备侧问题。
- 协议、媒体、存储的职责边界清楚：`ProtocolManager` 负责会话与报文编排，`VideoEncodeControl` 负责编码状态比较与应用，`StorageManager` 负责录像索引和文件流，便于分层排错和局部替换。
- 平台适配做得实：`App/Storage/common` 与 `App/Storage/lg` 两套实现、板型宏、第三方库和协议 SDK 的组织方式都比较工程化，适合同一套业务代码覆盖不同硬件变体。
- 这不是“算法型工程”，但很强“设备型工程”：它的价值主要体现在真实联调闭环、协议兼容、运行态可控和问题定位能力，而不是算法复杂度。

## 对 I4 的启发
- 先把“事实源”定死：代码、日志、抓包、知识库各自承担什么角色要提前分清，知识库只存可回溯结论，不拿向量库当真相源。
- 模块边界要能落到文件和日志上：协议、媒体、存储、配置尽量一层一职责，出问题时能直接定位到具体模块而不是全局猜。
- 所有动态行为都走 `query -> compare -> apply`：I4 这类设备工程最怕“写了但没生效”，统一比较当前态和目标态再落地，能显著减少联调噪音。
- 选题记录要可复用：每次调试、抓包、review 结论最好沉淀成 case 或模块文档，并带上文件路径和证据，这样后续检索比靠口口相传稳得多。

## 开发约定
- 代码规范: 历史 C/C++ 风格为主，增量修改以现有风格为准
- 命名约定: 类/结构体多为大驼峰；函数和成员命名混用历史风格；协议配置结构集中在 `App/Protocol/config/ProtocolExternalConfig.h`
- 模块边界: `App/Protocol/*` 为业务桥接层，`third_party/platform_sdk_port/*` 为移植后的协议 SDK/通用库，`RK/` 为工具链目录，不属于业务源码
- 配置来源: 协议配置优先从本地 HTTP 配置服务 `http://127.0.0.1:18080/openapi/v1/ipc/protocol` 拉取；GB28181 / GAT1400 注册核心参数同时支持 `/userdata/conf/Config/GB/gb28181.ini`、`/userdata/conf/Config/GB/zero_config.ini` 与 `/userdata/conf/Config/GB/gat1400.ini`
- 交叉编译环境与输出目录尽量隔离，避免污染同机其他 SoC 工程
- SIP/媒体协商相关修复优先保持行为兼容，先做最小变更
- 涉及 GB28181 行为差异的修复或新功能，先对照 `helloagents/wiki/modules/gb28181-2022-baseline.md`、`helloagents/wiki/modules/protocol.md` 与 `helloagents/wiki/modules/terminal_requirements.md`

## 错误与日志
- 策略: 以返回码和运行日志为主，失败时通常立即打印 `printf`/`AppErr` 并由上层决定是否回滚或重试
- 日志: 协议模块统一使用带前缀日志，如 `[ProtocolManager]`、`[GB28181]`、`[GAT1400]`
- 运行状态: GB28181 和 GAT1400 都依赖内部线程与回调驱动，排查应优先看注册、保活、流会话、订阅、控制五类日志
- 协议协商失败必须输出阶段性日志，至少覆盖请求进入、参数解析、应答发送、媒体启动

## 测试与流程
- 测试: 仓库内未见完整自动化测试体系，协议模块以静态验证、隔离构建、联调抓包和设备端回归为主
- 协议验收基线: GB28181 / GAT1400 改动除对照国家标准外，还必须对照 `终端需求说明书.pdf`、`终端需求白皮书.pdf` 与 `测试用例.xlsx`
- 构建: 根工程入口为 `rk_gb/CMakeLists.txt`，业务可执行程序定义在 `rk_gb/App/CMakeLists.txt`
- 交叉编译: 优先使用隔离 build 目录与命令级 `PATH` 注入，不优先使用会污染源码树固定目录的 `build.sh`
- 提交: 历史仓库风格偏工程交付型，提交信息建议按“模块 + 动作 + 结果”组织
- 自动化: GitHub issue 自动巡检/自动修复采用 `issue-triage.yml + issue-repair.yml` 两段式工作流，repair 绑定专用 self-hosted runner，并以 PR 形式收口

## 自动化约定
- 认证: 不使用账号密码；仓库工作流默认使用 `GITHUB_TOKEN`，更高权限场景建议使用 GitHub App 或细粒度 PAT
- 安全边界: issue 原文不得直接拼 shell；自动修复只处理白名单问题；默认不直推主分支
- 构建验证: 自动修复完成后必须执行 RK830 隔离交叉编译验证
