# Changelog

本文件记录项目所有重要变更。
格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增
- 初始化 `helloagents/` 知识库。
- 新增 `rk_gb` 项目总览、架构、接口、数据模型文档。
- 新增 `gb28181`、`gat1400`、构建运行模块文档，沉淀 RK IPC 协议分析结果。
- 新增基于 `GAT1400.pdf` 的 GAT1400 / VIID 协议知识库基线，补齐规范要求、实现映射和开发 / 审核清单。
- 新增 `helloagents/wiki/modules/terminal_requirements.md`，沉淀《中国移动定制版终端需求白皮书》与 `测试用例.xlsx` 的需求-测试-实现对齐结果。
- 新增 `helloagents/wiki/modules/zero_config.md`，将终端白皮书中的“零配置 / 注册重定向”抽成独立专题，沉淀定义、装维闭环、出厂预配置、SIP 重定向报文要求和验收清单。
- 新增 `GB/T 28181-2022` 调试基线知识库，整理注册、保活、实时流、回放、下载、校时、广播等关键流程，并回链到 `Protocol` 模块文档和项目技术约定。
- 新增 `GB28181` 对讲业务审查知识库，补充当前代码实现、风险判断和分级 TODO，作为后续对讲联调与重构依据。
- 新增 `GB28181` 远程抓拍与目录订阅审查知识库，整理当前实现边界、风险判断、排查 checklist 和分级 TODO，作为后续补齐 9.11/9.14 相关能力的依据。
- 新增 `.github/workflows/issue-triage.yml`、`.github/workflows/issue-repair.yml` 和 `tools/issue_bot/*`，落下 issue 自动巡检 / 自动修复工作流骨架。
- 新增 `.github/workflows/issue-runner-preflight.yml`，提供 self-hosted runner 的手工预检入口。
- 新增 `helloagents/wiki/modules/github_automation.md`，沉淀 GitHub 自动化模块文档。
- 新增 `tools/issue_bot/local_smoke_test.sh` 与 `tools/issue_bot/example_fix.sh`，支持本地 mock issue 演练 repair 工作流。
- 新增 `tools/issue_bot/runner_preflight.sh` 与 `tools/issue_bot/runner_env.example`，支持 self-hosted runner 上线前环境预检。
- 新增 `tools/issue_bot/regression_suite.sh`，支持本地一键回归 triage / preflight / smoke repair 主链路。
- 增加 issue bot 本机定时巡检脚本、Codex 修复器包装脚本与 cron 安装脚本，默认基于 `silver` 分支在隔离仓库中执行 triage / repair。

### 变更
- 将 GB28181 “标准国标 / 零配置” 切换方式从编译期开关改为 `gb28181.ini::register_mode` 运行时控制，并同步补齐 `HttpConfigProvider` 的 `gb_register_mode` 字段；`register_mode=standard` 时忽略 `zero_config.ini` 缺失，`register_mode=zero_config` 时按零配置流程校验与启动。
- 将零配置字段 `StringCode/Mac/Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model` 从 `gb28181.ini` 拆到独立 `/userdata/conf/Config/GB/zero_config.ini`；当 `register_mode=zero_config` 且缺少该文件时，配置加载会直接记录日志并返回错误，不再做兼容迁移或自动生成。
- 根据 `zero_config.txt` 提供的零配置 `302` 接入参数，更新代码内置默认入口值；覆盖 `redirect_server_id/server_ip/server_port/string_code/password/mac_address`，并保持 `register_mode=standard` 默认行为不变。
- 明确知识库口径：零配置 `302` 返回的正式平台 `ServerIp/ServerPort/ServerDomain/ServerId/deviceId` 只保存在 `GBClientImpl` 进程内存，不写入 flash；`Stop` / 进程重启 / 设备重启后清空，下次启动重新基于 flash 中的入口参数发起重定向获取。
- 更新 `helloagents/wiki/modules/zero_config.md`、`helloagents/wiki/modules/gb28181.md`、`helloagents/wiki/modules/terminal_requirements.md`、`helloagents/wiki/api.md`、`helloagents/wiki/data.md`，同步 `DeviceInfo` 的 `A.19` 扩展字段、最小能力清单和剩余真实缺陷。
- 调整 SipSDK 的响应侧 client 匹配顺序：`GetClientInfo()` 处理响应事件时改为优先使用 `event->response` 匹配对端，再回退到 `event->request`，避免心跳等自发 `MESSAGE` 收到响应时先把本机 `From` 误判成 peer 而频繁打印 `sip peer match failed`。
- 根据 issue38 于 2026-03-25 08:50:22 的最新评论，移除本地协议配置对旧 `/userdata/conf/Config/gb28181.ini` 的兼容读取/迁移逻辑，只保留 `/userdata/conf/Config/GB/gb28181.ini` 与 `/userdata/conf/Config/GB/gat1400.ini` 两个新路径。
- 将 `ProtocolManager` 收口为进程内唯一访问入口，外部模块改为直接通过 `ProtocolManager::Instance()` 取用运行中的协议服务；`GB28181ClientSDK` 所有权同步下沉到 `ProtocolManager`，不再由 `CSofia` 持有或转发。
- 更新 `helloagents/wiki/overview.md`，将 ZeroConfig 纳入模块索引，并明确“零配置”在项目语境下是“免现场手工录参”而非“零出厂预置”。
- 根据 issue38 于 2026-03-25 的最新评论，把本地注册配置目录统一迁到 `/userdata/conf/Config/GB/`：`gb28181.ini` 改为新路径，新增独立 `gat1400.ini`，并补齐 GAT1400 的 `Get/Set/Restart` 风格 flash 配置接口。
- 补齐 GAT1400 `LowerGAT1400SDK` 运行时导出层，复用主程序内 `ProtocolManager/GAT1400ClientService`，对业务侧开放 `START/STOP/REGISTER/POST_*`、订阅观察者和自定义 HTTP 透传入口。
- 补齐 GAT1400 P0 传输与恢复能力：新增 `scheme/base_path/request_timeout_ms` 地址模型、`retry_backoff_policy` 注册退避参数，以及 `queue_dir/max_pending_count/replay_interval_sec` 失败补传队列配置。
- 将 `POST /VIID/APEs` 收口为显式兼容扩展开关 `gat_upload_enable_apes_post_compat`，默认关闭，并补齐配置读写、校验和热加载。
- 重构 `GAT1400ClientService` 对上请求拼装逻辑，支持统一地址前缀、配置化超时、完整 URL 覆盖和注册成功后的失败请求回放。
- 补强 `helloagents/wiki/api.md` 与 `helloagents/wiki/data.md` 的 GAT1400 章节，使其可直接作为 1400 协议开发和审核依据。
- 补强 `helloagents/wiki/modules/gb28181.md`、`helloagents/wiki/api.md`、`helloagents/wiki/data.md`、`helloagents/wiki/overview.md`、`helloagents/project.md`，补入终端白皮书定制要求、测试映射和当前实现缺口。
- 继续补强 `helloagents/wiki/modules/terminal_requirements.md` 与 `helloagents/wiki/modules/gb28181.md`，细化 `A.11/A.16/A.19/附录G` 的字段级审核清单、白皮书内部不一致项和联调优先级。
- 继续补强 `helloagents/wiki/modules/terminal_requirements.md`，把注册重定向、基础参数扩展、设备信息扩展、告警扩展和多码流能力拆成可执行代码整改清单。
- 将 `rk_gb/CMakeLists.txt` 与 `rk_gb/Middleware/CMakeLists.txt` 的优化参数从错误的 `-o3` 修正为 `-O3`。
- 将主工程与 Middleware 的 `cmake_minimum_required` 提升到 `3.5`，兼容工作区私有 CMake 4.2.3。
- 沉淀 RK830 隔离交叉编译命令，明确不使用会污染源码树固定目录的 `build.sh` 作为首选入口。
- 在知识库中补充 GitHub Actions、self-hosted runner、issue 白名单规则和自动化安全边界。
- 修正 issue triage / repair workflow 对 `schedule` 与 `workflow_dispatch` 上下文的使用边界，并补上 repair 结束后的 worktree 清理。
- 加固 issue repair 队列语义：空队列正常退出，失败 issue 自动退出候选队列，避免定时重试风暴。
- 将 runner preflight 接入 `issue-repair.yml`，让正式 repair 前先执行基础环境核验。
- 为 issue repair / runner preflight 增加状态摘要和 artifact 上传，便于 GitHub Actions 排障。
- 为 issue triage 增加状态摘要和 artifact 上传，统一三条 workflow 的可观测性。
- 为 triage 增加失败状态和空结果状态落盘，统一本地回归与线上排障时的状态语义。

### 修复
- 修复 GB28181 `DeviceInfo` 查询应答只回基础字段的问题：补齐 `StringCode/Mac/Line/CustomProtocolVersion`，并新增 `DeviceCapabilityList/ProtocolFunctionList` 最小嵌套 XML 节点，按真实实现回报多码流、图像翻转、升级和告警缺陷。
- 补齐 GB28181 零配置注册闭环：新增 `StringCode/Mac/Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model` 配置承载，补上首次 `REGISTER` 扩展头、`302` 平台字段解析、`401 -> 302 -> 401 -> 200` 单次事务，以及 `30s / 3次 / 1min / 重新获取重定向地址` 的后台重试状态机。
- 修复 `cc1: error: too many filenames given` 的构建阻塞问题。
- 修复 `GB28181RtpPsSender.cpp` 因 `mpeg-ps.h` / `rtp-payload.h` / `rtp-profile.h` 缺失导致的交叉编译失败。
- 修复 GAT1400 “有 client、缺业务入口”的缺口，补上 SDK 到运行时协议栈的实际映射，并让订阅观察者 / 通知回发链路可被业务层消费。
- 修复 `/VIAS/*` 透传命令和未开启的 `POST /VIID/APEs` 兼容请求会误进入失败补传队列、导致反复重放的问题。
- 修复 `GAT1400ClientService` 半重构状态下 `ExecuteRequest` 调用签名不一致的问题，恢复当前工作区的编译级可用性。
- 修复 GAT1400 失败上传缺少落盘缓存、重注册后不补传以及保活失败后固定 `sleep(5)` 重试的闭环缺口。
- 修复 GB28181 报警上报主链缺口：补齐 `CAlarm -> ProtocolManager -> GB28181 SDK/XML` 的本地视频报警最小闭环，新增 `AlarmTypeParam/ExtraInfo` 承载，并让 `ResetAlarm` 同步清理 GB 报警状态缓存。
- 修复 GB28181 回放控制链路：`TEARDOWN` 现按停止会话处理，`PLAY + Scale/Range` 会正确驱动恢复发流 / 倍速 / 拖动，并移除回放启动中的硬编码 `+28800` 时间偏移。
- 禁用 GAT1400 主动校时：`GetTime()` 现为空实现，启动阶段不再向 `/VIID/System/Time` 发起请求，设备时间统一由 GB28181 校时链路负责。
- 修复 GB28181 远程升级提前上报成功的问题：`DeviceUpgrade` 现在只返回控制受理结果，最终 `DeviceUpgradeResult` 改为在升级执行并重启恢复注册后补报。
- 修复 GB28181 远程升级停留在“下载落盘但不执行”的问题，改为复用现有 `UpgradeReleaseResource` 释放链路触发本地升级执行与重启。
- 补齐 GB28181 `gb_upgrade` / `gb_reboot` 的本地配置、HTTP 配置与能力声明，保证离线配置和远程配置路径一致。
- 修复 GB28181 对讲链路仍依附广播状态的问题：新增独立 `talk session`、独立 `gb_talk.recv_port` 应答、`ACK` 后再应用媒体协商，并在支持的 transport 上启动上行采音 RTP。
- 修复 GB28181 对讲配置不生效的问题：`gb_talk.codec / recv_port / sample_rate / jitter_buffer_ms` 已接入本地配置与 HTTP 配置校验 / 序列化链路。
- 修复 GB28181 被动 `Talk INVITE` 的 `Subject` 解析过于粗糙的问题，改为按字段提取设备 ID，避免简单截断 20 字节导致设备识别错误。
- 修复 GB28181 对讲音频协商只读取首个 payload 的问题，改为在多个 `rtpmap` 中优先选择本端支持 codec。
- 修复 GB 广播主动 `INVITE` 的 `200 OK` SDP 被误判为解析失败的问题：补齐 `String2MediaInfo()` 对 `m=` payload、`a=rtpmap` 和音视频类型的回填，避免设备在收到有效应答后立即 `BYE`。
- 按 GB/T 28181-2022 第 9.12 节调整广播链路：广播 `Notify` 先返回纯 SIP `200 OK`，再独立发送 `Broadcast` 应答 `MESSAGE`，随后由设备侧主动发起音频 `INVITE` 建链，并把平台 `200 OK` 中的音频参数应用到广播桥。
- 修复 reopened 的 GB28181 广播通知响应字段错误问题：`Broadcast Notify` 解析改为回填原始 `SN`，并使用 `TargetID` 作为响应 `DeviceID` / 会话 `gb_code`，避免平台因收到 `SN=0`、空设备编号的广播响应而继续判定失败。
- 补齐 GB28181 广播业务会话闭环：广播通知接入 `ProtocolManager` 管理、音频广播 `200 OK` 优先返回设备实际可达 IP、收到 `BYE` 后自动清理并恢复广播桥待机接收状态。
- 修复 GB28181 广播通知 `MESSAGE` 在 `OnNotify()` 中被重复应答的问题，改为单次返回携带 XML 的最终响应，并保留 `400/403` 失败语义与诊断日志。
- 修复 GB28181 配置仅停留在代码默认值的问题，新增 `/userdata/conf/Config/gb28181.ini` 本地配置落盘、缺省自动生成和开关控制能力。
- 修复 issue38 国标注册配置接口只改内存不改文件的问题：`GetGbRegisterConfig()` / `SetGbRegisterConfig()` 现已直接走 `gb28181.ini`，支持 `init` 前访问、缺省自动生成，并在运行中对国标注册配置即时生效；根据最新评论约束，`gb28181.ini` 当前只持久化 6 个国标注册字段，其余协议项固定在代码中。
- 根据 issue38 于 2026-03-25 的最新评论，继续拆分 GB 注册配置生效路径：`GetGbRegisterConfig()` 只读 flash，`SetGbRegisterConfig()` 只写 flash，新增 `RestartGbRegisterService()` 单独负责从 flash 重载并按运行态停/启 GB 服务；`ProtocolManager::Start()` 前也会先刷新一次本地配置，避免 `init` 后、`start` 前仍使用旧缓存。
- 修复 GB28181 冷启动时首次注册失败后生命周期直接退出的问题：`StartGbClientLifecycle()` 对可恢复的首个注册失败改为 `defer_retry`，保留后台线程并由 `GbHeartbeatLoop()` 按 `gb_keepalive.interval_sec` 周期继续尝试注册。
- 修复 GB 画面翻转配置未接入设备主配置链路的问题，改为复用 `CFG_CAMERA_PARAM.rotateAttr` 并将 `image_flip_mode` 持久化到 `gb28181.ini`。
- 修复 GB 远程重启控制缺少冷却保护的问题，新增基于 `gb_reboot.cooldown_sec` 的防抖限制。
- 修正 GB28181 实时流在 TCP 协商场景下的 SIP / 媒体建立时序。
- 修复 GB 注册时间同步仅 dry-run 不落地的问题，并补齐录像文件列表查询响应。
- 修复 LG 存储分支录像时间搜索误用索引导致的查询结果错乱。
- 修复 GB 回放音频发送逻辑，改为按 SDP / `gb.live.audio_codec` 将回放 PCM 编码为 G711A / G711U 后再送入 RTP PS。
- 修复 GB 回放回调缺少 session 身份导致旧存储线程 EOS 可能误清空新回放 session 的竞态，并增加 `stale eos ignored` 诊断日志。
- 修复 MP4 回放 demux 将“读到 packet 但未产出有效 ES 数据”误判为 EOF 的问题，避免录像回放在首包前直接 `eos video=0 audio=0`。
- 增加录像回放索引扫描与候选文件选择日志，便于定位“未选中文件”与“选中文件但 open/read 失败”的分界点。
- 修复 GB 回放从 `index` 选择首个录像文件时的筛选逻辑，改为先排序再按时间重叠 / 后继文件选取，避免“查询能看到录像、回放却选不到文件”。

### 优化
- 优化 issue bot 本机定时任务的可执行文件发现逻辑，自动写入 `CODEX_BIN` / `GH_BIN` 绝对路径，降低 cron 环境缺少 PATH 时的失败率。
- 静默忽略 `SIGPIPE`，并移除 GB live 音频 ES、PS 输出、RTP 发送统计的周期性刷屏日志。
