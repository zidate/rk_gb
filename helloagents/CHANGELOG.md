# Changelog

本文件记录项目所有重要变更。
格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 修复
- 按 issue 45 补齐 GAT1400 注册配置 `enabled`：`gat1400.ini` 新增 `enable` 持久化字段，`ProtocolManager::Start()` 与 `RestartGatRegisterService()` 现按开关决定是否启动或注销停服；同时复核 GB28181 现有 `enabled` 语义，确认 `RestartGbRegisterService()` 已支持关闭后停服且不重启。

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
- 新增 `App/Media/GAT1400CaptureControl.*` 抓拍桥接层，供编码侧 / 算法侧以“人脸 / 机动车 + 图片 / 视频 / 文件”事件方式向 1400 模块投递待上传数据，并补齐调用方接入使用说明。

### 变更
- 将 `feature/dg_ipc_replay_20260415` 分支中的默认 `packaging/` 目录收敛为单个 `packaging.tar.xz` 归档；`build.sh` 现会在目标打包目录缺失但同名归档存在时自动解压恢复，`.gitignore` 也同步忽略解压后的 `packaging/` 工作目录，减少板级二进制资源对 Git 差异和历史浏览的噪声。
- 在独立回放分支中先补齐 `feature/gb-zero-config-macro-switch-20260326` 的本地/远端独有提交，再吸收 `feature/dg_ipc` 的 IPC 适配改动；回放过程中仅清理 `cmake-build`、调试目录、压缩包和资料类垃圾文件，保留仓库中原本版本化管理的板级库、固件与打包资源。
- 按 issue 44 继续收口 GB28181 报警通知的设备编号来源：结合 `245.pcap`、`246.pcap` 与 `debug.log` 可见，报警 XML 应与设备当前真实连接的 SIP 本端 ID 保持一致。当前除了 `ProtocolManager::NotifyGbAlarm()` 会优先读取 `GB28181ClientSDK` 的 `local_code` 归一化 `DeviceID/AlarmID` 外，最终 `CGB28181XmlParser::PackAlarmNotify()` 组包时也会优先使用运行态 `m_local_code` 写 `<DeviceID>`，避免上层仍带旧 `C044...` 时报警 `NOTIFY` 体继续发错 ID。
- 按 issue 43 最新评论完成 GAT1400 收口：`ProtocolManager` 当前只保留 `NotifyGatFaces()` / `NotifyGatMotorVehicles()` / `NotifyGatNonMotorVehicles()` 3 个结构化对象上报接口，分别对接 `PostFaces/PostMotorVehicles/PostNonMotorVehicles`；已注册时直接发送，未注册时直接写入现有失败补传队列。同时删除 `ProtocolManager::NotifyGatAlarm()`、keepalive demo，以及 `GAT1400CaptureControl` / `GAT1400CaptureEvent` / `GAT1400ClientService::NotifyCaptureEvent()` 这条未再被调用的抓拍桥接链路。
- 按 `GB/T 28181-2022` 附录 G 收口 GB28181 实时点播的 `f=` 处理：`ProtocolManager` 现保持按 `a=streamnumber` / 本地默认配置选择目标码流，再解析 `INVITE` SDP 里的 `f=` 视频参数并通过现有编码参数接口下发到该码流；当前只先打通协议层入口与日志，不额外扩展底层编码器动态重配实现。
- 按 issue 42 最新评论继续收口零配置与标准国标配置边界：`register_mode=zero_config` 时运行态固定使用代码内置的重定向 `server_id/server_ip/server_port`，不再复用 `gb28181.ini` 中的 `username/server_ip/server_port`；同时 `SetGbRegisterConfig()` 不再顺带写 `zero_config.ini`，避免标准配置接口污染零配置入口文件。
- 按 issue 42 收口零配置本地配置模型：`zero_config.ini` 现只落盘 `string_code/mac_address` 两个字段，其余 `line_id/redirect_domain/redirect_server_id/custom_protocol_version/manufacturer/model` 统一走代码默认值；同时新增 `ProtocolManager::GetGbZeroConfig()/SetGbZeroConfig()` 供其他模块直接读写零配置入口参数。
- 将 GB28181 注册主链切到更接近 `GB/T 28181-2022` 的口径：`ProtocolManager` 启动 SDK 时改传 `kGB2022Version`，`GBClientImpl -> SipClientImpl -> SipEventManager` 每次 `REGISTER` 都会显式补标准头 `X-GB-Ver: 3.0`，同时把默认 `CustomProtocolVersion` 调整为 `3.0`，并让 XML 解析层把原 `2016` 扩展字段逻辑按“2016 及以上版本”复用。
- 按 issue 41 2026-04-02 最新评论继续调整 GAT1400 keepalive demo：拆成人脸、机动车两个独立 demo 函数，分别读取 `/mnt/sdcard/test.jpeg` + `/mnt/sdcard/face.jpeg`、`/mnt/sdcard/test2.jpeg` + `/mnt/sdcard/motor.jpeg`，并将两类全景图统一按 `1920x1080` 上报。
- 根据平台联调口径重做 GAT1400 keepalive demo：保活成功后不再走 `POST /VIID/DispositionNotifications`，改为直接上报 `POST /VIID/Faces` 与 `POST /VIID/MotorVehicles`；当前读取 `/mnt/sdcard/test.jpeg`，将同一份 Base64 mock 成人脸场景图/抓拍图以及机动车大图/车牌图/特写图，并限制每次服务启动后最多触发 `1` 次。同时补齐 `GAT1400Json` 的 `FaceObject.ShotTime`、自动采集时间字段透传、`MotorVehicleObject.Speed/Direction/VehicleLength/PassTime` 和两位数 `SubImage.Type` 序列化，使发往平台的 JSON 更贴近联调样例。
- 调整 GAT1400 keepalive demo 的平台联调路径：保活成功后不再走 `NotifyCaptureEvent()/PostFaces/PostImages`，改为直接 `POST /VIID/DispositionNotifications`；当前使用 `PersonObject + SubImage(Data)` 组最小告警通知，默认读取 `/mnt/sdcard/test.jpeg`，并继续限制整次服务启动后最多触发 `5` 次。
- 修正上一提交的 GAT1400 keepalive demo：当前仍保留 `NotifyCaptureEvent()` 触发方式与 `FACE_DETECT_EVENT`，但不再发送视频，只保留本地图片 `/mnt/sdcard/test.jpeg` 的 demo 上送。
- 按 issue 41 2026-04-01 最新评论调整 GAT1400 keepalive demo：删除旧的 `SendKeepaliveDemoUploadOnce()` 直传实现，改为在保活成功后构造 `GAT1400CaptureEvent` 并调用 `NotifyCaptureEvent()`；demo 事件现固定为 `FACE_DETECT_EVENT`，默认关联 `/mnt/sdcard/test.jpeg` 和 `/mnt/sdcard/DCIM/2026/03/24/20260324174542-20260324174845.mp4`，每次服务启动后最多触发 `5` 次。
- 调整 `GAT1400CaptureControl` 代码归属：`GAT1400CaptureControl.h/.cpp` 从 `App/Media/` 迁移到 `App/Protocol/gat1400/`，并同步更新 `GAT1400ClientService` include 与主工程 / `sdk_port` 构建脚本中的源文件归属。
- 新增 `ProtocolManager::NotifyGatAlarm()` 作为 1400 抓拍业务通知入口，业务侧现在可直接把 `GAT1400CaptureEvent` 交给协议模块；当前若 1400 已注册则直接尝试上传，否则进入内存队列，待后续注册/保活成功后继续处理。
- 收口 GAT1400 抓拍桥接触发模型：`GAT1400ClientService` 不再继承 `GAT1400CaptureObserver`，`GAT1400CaptureControl` 只保留进程内队列；抓拍事件改为在注册成功和保活成功后由 1400 服务显式 `DrainPendingCaptureEvents()` 消费上传。
- 收敛 GAT1400 keepalive demo 实现：在不改动现有 `/Data` Base64 上传语义的前提下，demo 改为首次保活成功后直接构造一条人脸和一张图片，复用现有 `PostFaces/PostImages` 做最小联调调用。
- 调整 GAT1400 媒体本体上传语义：`PostImages/PostVideoSlices/PostFiles` 现会先剥离元数据 JSON 中的 `Data` 字段，再单独 `POST /VIID/*/{ID}/Data` 上传 Base64 本体；`PostBinaryData` 的请求头同步改为 `application/octet-stream`。同时新增首次保活成功后的单次本地联调 demo，人脸事件会关联上传 `/mnt/sdcard/test.jpeg`，失败不重试到后续保活。
- 收口 GB28181 / GAT1400 对外部模块配置的运行态缓存：`ProtocolManager` 不再把 `gb_osd/gb_video/gb_image` 当作 OSD、编码参数、画面反转的运行态来源，改为统一从 `VideoOsdControl` / `VideoEncodeControl` / `VideoImageControl` 查询；`GAT1400ClientService` 也不再长期缓存 `m_gb_register / m_device_id`，而是按需解析当前 1400 设备 ID。
- 补齐 GB28181 画面反转控制链路的媒体查询闭环：`ProtocolManager::HandleGbConfigControl` 处理平台 `FrameMirror/ImageFlip` 设置时，现会先显式调用 `media::QueryVideoImageFlipMode()` 读取运行态，再按需调用 `media::ApplyVideoImageFlipMode()` 下发，确保 GB 模块到媒体接口的 `query -> apply` 边界完整可见。
- 补齐 GB28181 OSD / 编码参数控制链路的媒体查询闭环：`ProtocolManager::HandleGbConfigControl` 处理 `OSDConfig/SVACEncodeConfig/SVACDecodeConfig/VideoParamAttribute` 设置时，现会先显式调用 `media::QueryVideoOsdState()` / `media::QueryVideoEncodeState()` 读取运行态，再按最终目标配置决定 `skip_apply` 或调用 `ApplyVideoOsdConfig()` / `ApplyVideoEncodeStreamConfig()` 下发；其中 OSD 运行态额外补齐了 `date_style/time_style` 读取，便于协议层显式比对时间格式落地状态。
- 收口 GB28181 协议层散落硬编码：新增 `App/Protocol/gb28181/GB28181ProtocolConstants.h` 统一默认设备 ID、注册/保活缺省值、分辨率摘要、音频 payload/mime、`ConfigType/CmdType` 字符串与 `FrameMirror` 映射，`ProtocolManager`、`GB28181XmlParser`、`GB28181BroadcastBridge` 现复用同一套定义。
- 将 GB28181 画面反转的正式协议口径从旧 `VideoParamOpt/ImageFlip` 切到平台联调实际使用的 `FrameMirror`，并把设备侧 `get/set` 统一下沉到 `App/Media/VideoImageControl.*`；当前按 `0=还原`、`1=左右`、`2=上下`、`3=中心` 映射到底层 `close/mirror/flip/centrosymmetric`，同时保留旧 `ImageFlip` 兼容。
- 按 `GB/T 28181-2022` 对齐 GB28181 编码参数 XML：新增 `ConfigDownload/DeviceConfig` 的 `VideoParamAttribute` 查询与设置链路，`VideoFormat/Resolution/FrameRate/BitRateType/VideoBitRate` 现按 2022 字段回包与解析，并通过 `App/Media/VideoEncodeControl.*` 下发 `codec/fps/bitrate/bitrate_type/resolution`。
- 将 GB28181 编码参数访问链路从 `ProtocolManager` 下沉到 `App/Media/VideoEncodeControl.*`：GB 模块现在通过媒体接口统一读取主/辅码流 `codec/fps/bitrate/gop/resolution` 运行态，并保留 `ApplyVideoEncodeStreamConfig()` 作为后续设备侧应用入口，不再直接调用 `rk_video_*` / `CaptureGetResolution`。
- 将 GB28181 OSD 的设备落地职责从 `ProtocolManager` 下沉到 `App/Media/VideoOsdControl.*`：GB 模块现在只保留协议字段映射、配置持久化和查询应答组包，媒体/编码侧统一负责 `CaptureSetOSDSwitch`、`rk_osd_*` 的默认适配实现。
- 统一 OSD get/set 结构到 `media::VideoOsdState`：`ApplyVideoOsdConfig()` 与 `QueryVideoOsdState()` 现共用同一套状态模型，新增 `VideoOsdTextItem` / `VideoOsdState.text_items[]` 承载白皮书 `OSDConfig.SumNum + Item[]` 多文本协议态，GB 协议层不再维护单独的 `VideoOsdConfig` 中间结构。
- 补齐 GB28181 白皮书 `OSDConfig` 多文本协议兼容：`ConfigDownload + OSDConfig` 现可按运行态回 `SumNum + Item[]`，`DeviceConfig + OSDConfig` 也可解析并保存多条文本项；当前 SoC 仍只尽力落第 `1` 条文本及其坐标，额外文本项由 `VideoOsdState` 缓存并用于后续查询回显，`TimeType=1` 继续只做协议保留与回显。
- 补充 `helloagents/wiki/modules/gb28181.md` 中的 GB28181 OSD 对接说明，明确当前 OSD 获取/设置联调已通、实际生效路径为 `GB28181ClientReceiverAdapter -> ProtocolManager -> Capture/rk_osd_*`，并标注 `DevInterface` 的 OSD 四个虚接口尚未作为当前 GB OSD 正式入口使用。
- 将 GB28181 “标准国标 / 零配置” 切换方式从编译期开关改为 `gb28181.ini::register_mode` 运行时控制，并同步补齐 `HttpConfigProvider` 的 `gb_register_mode` 字段；`register_mode=standard` 时忽略 `zero_config.ini` 缺失，`register_mode=zero_config` 时按零配置流程校验与启动。
- 将零配置字段 `StringCode/Mac/Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model` 从 `gb28181.ini` 拆到独立 `/userdata/conf/Config/GB/zero_config.ini`；当 `register_mode=zero_config` 且缺少该文件时，配置加载会直接记录日志并返回错误，不再做兼容迁移或自动生成。
- 根据 `zero_config.txt` 提供的零配置 `302` 接入参数，更新代码内置默认入口值；覆盖 `redirect_server_id/server_ip/server_port/string_code/password/mac_address`，并保持 `register_mode=standard` 默认行为不变。
- 修复 issue40：零配置启动时不再把 `StringCode` 误按纯数字国标编码校验，允许 `C044...` 这类字母数字串码通过本地身份检查并进入重定向注册流程。
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
- 将 1400 抓拍上传职责从协议服务直连扩成“媒体侧入队 + 1400 自动消费”的边界：`GAT1400ClientService` 现注册为 `GAT1400CaptureControl` 观察者，启动后会自动 drain 内存队列，并按“结构化对象 -> 图片/视频/文件”顺序复用现有 `Post*` 上传链路。
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
- 修复 GB28181 第二次对讲/广播“有码流但设备侧没声音”的播放恢复时机问题：`GB28181BroadcastBridge` 现按会话边界管理扬声器，旧连接关闭时同步清掉 `AUDIO_TALK_TYPE`，新的 `UDP transport apply` 或 `TCP connect/accept` 成功后再明确打开扬声器，避免第一轮结束后仍残留旧播放态或错误依赖逐帧兜底。
- 修复 GB28181 广播下行 `g711a/g711u` 解码目标缓冲的对齐写法：`GB28181BroadcastBridge::DecodeToPcm16()` 现先写入 `int16_t` 临时缓冲，再拷回播放链使用的 PCM 字节流，避免直接按 `short*` 写进 `std::vector<char>` 带来的对齐风险。
- 修复 GB28181 广播主动 `INVITE` 和实时预览 TCP 协商里的旧状态/方向错误：广播建链不再复用上一轮广播会话缓存的 transport，实时预览收到 `setup:active` 时设备侧改为回 `tcp-passive`、先监听本地端口并在发流前按需 `accept`，避免广播 `passive/passive` 和预览错误主动 `connect` 导致的 `403`。
- 修复 GB28181 `Catalog` 查询响应/订阅通知目录项字段长期缺失的问题：`ProtocolManager` 现统一填充 `Name/Manufacturer/Model/Owner/CivilCode/Address/IPAddress/RegisterWay/Parental/Status/Event` 等基础信息，GB28181 XML 打包层也同步把 `CatalogInfo` 中已填字段真实编码进报文，不再把 `Owner/CivilCode/Address/Parental/RegisterWay` 等值丢成空标签或默认值。
- 修复 GAT1400 keepalive demo 在图片缺失或某一路上报失败时会在后续心跳中持续重试、导致平台看到重复人脸/车辆事件的问题；当前改为每次服务启动后每类 demo 仅尝试一次，不再回退重试计数。
- 修复 GB28181 TCP RTP/PS 发送在短时回压下因 `EAGAIN/EWOULDBLOCK` 立即停流的问题：`GB28181RtpPsSender` 现会在 `3s` 总窗口内等待 socket 可写后重试，超过窗口或遇到非重试型错误才返回失败。
- 修复 GB28181 SIP 客户端本地监听端口错误复用 `gb_register.server_port` 的问题：启动时改为随机本地端口，并在 `SipEventManager` / `SipClientImpl` 回写实际监听端口，确保 `REGISTER` 等出站请求的 `From/Contact` 与真实监听端口一致。
- 修复 GB28181 实时预览与录像回放/下载共用同一 RTP/PS sender 导致的互斥问题，改为 live/replay 独立 sender、独立 SSRC/local port，并将停流收口到按 handle 精确命中，支持 `1` 路实时预览与 `1` 路回放/下载同时运行。
- 修复 GB28181 平台按白皮书 `OSDConfig` 查询/设置前端 OSD 时设备侧不识别的问题，避免 `ConfigDownload` 返回空配置和 `DeviceConfig` 因无已解析配置项落到 `-82`。
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
- 修复 GB28181 远程升级下载仍依赖外部 `curl` 命令的问题：`ProtocolManager` 现内置最小 HTTP 下载实现，支持总超时、重定向与流式落盘；当前协议边界仍只支持 `http`，`https` 会在下载阶段直接失败。
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
- 按 issue 44 的后续收口意见，删除 `App/Protocol/gb28181/GbAlarmIdentity.*` 与对应最小测试入口，把报警 `DeviceID/AlarmID` 归一化逻辑直接内联回 `ProtocolManager.cpp`，保留“运行态 `gbCode` 优先”的修复行为，同时继续压缩嵌入式构建面。
- 删除 GB28181 广播下行排障期间临时加入的原始音频落盘调试逻辑，去掉 `GB28181BroadcastBridge` 中写 `/mnt/sdcard/gb_broadcast_rx_*` 的函数和相关状态变量，避免 demo 版本继续带着额外 I/O 分支。
- 删除历史遗留的 `ProtocolFeatureSwitch.h` 与 `PROTOCOL_HAS_GB28181_CLIENT_SDK` 编译期开关：当前产品线固定直接编入 GB28181 Client SDK，`App/CMakeLists.txt` / `App/Protocol/gb28181/sdk_port/CMakeLists.txt` 的重复宏定义已去掉，`ProtocolManager` 里围绕该宏的恒真条件编译也已收口为单一路径。
- 删除 `ProtocolManager` 中零调用的 `const GetGatClientService()` / `const GetGbClientReceiver()` 重载，并清理 `LowerGAT1400SDK` 对应的冗余 `const` 壳声明；在同工具链、同宏条件下，`ProtocolManager.o` 的 `dec` 从 `167103` 降到 `167071`。
- 删除 `ProtocolManager` 中零调用的私有方法 `ClearGbBroadcastSessionState()`，继续收口广播链路中的死 helper。
- 删除 `ProtocolManager` 中零调用的 `PushListenAudioFrame()`、`ApplyGbBroadcastSdpOffer()`、`SetGbBroadcastPcmCallback()` 转发方法，并清理 `TrimWhitespaceCopy()`、`NormalizeGbOsdTextTemplate()` 的重复前置声明，继续收口协议层无意义编译内容。
- 删除 `ProtocolManager` 中无调用的 `GBManager_Start()`、`GBManager_Stop()`、`OnGbConfigChanged()` 包装方法，并去掉 `ClearGbUpgradePendingState()` 的未使用参数；在同编译宏条件下，`ProtocolManager.cpp` 的目标文件 `dec` 从 `236017` 进一步降到 `234806`。
- 删除 `IExternalConfigProvider` 空抽象层，并收口 `LocalConfigProvider` 中未被活代码调用的 `QueryCapabilities`、`SubscribeChange`、`SetMockConfig`；在同编译宏条件下，`LocalConfigProvider.cpp` 与 `ProtocolManager.cpp` 的目标文件 `dec` 分别从 `76121` 降到 `72816`、从 `236499` 降到 `236017`。
- 移除未被活源码和当前构建入口使用的 `App/Protocol/config/HttpConfigProvider.*`，协议配置知识库同步收口到 `LocalConfigProvider` 单一路径。
- 清理最近 GB OSD 多文本改动中的未使用 helper、无效前置声明和重复日志展开；在同编译宏条件下，`ProtocolManager.cpp` 的目标文件 `dec` 从 `237955` 收缩到 `236490`。
- 继续收敛 `ProtocolManager::HandleGbConfigControl()` 中 `image_flip` 与 `video_param_attribute` 的重复 `skip_apply / dispatch` 日志展开；在同编译宏条件下，`ProtocolManager.cpp` 的目标文件 `dec` 从 `236490` 进一步收缩到 `235760`，相对本轮瘦身前累计减少 `2195` 字节。
- 优化 issue bot 本机定时任务的可执行文件发现逻辑，自动写入 `CODEX_BIN` / `GH_BIN` 绝对路径，降低 cron 环境缺少 PATH 时的失败率。
- 静默忽略 `SIGPIPE`，并移除 GB live 音频 ES、PS 输出、RTP 发送统计的周期性刷屏日志。
