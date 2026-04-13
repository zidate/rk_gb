# Protocol 模块

## 职责
- 管理 GB28181 与 GAT1400 生命周期。
- 处理 SIP/SDP 请求到本地媒体发送链路的映射。
- 统一输出协议日志与错误信息。

## 当前重点
- GB28181 本地配置文件读取、缺省值落盘和开关控制。
- GB 注册与鉴权稳定性。
- GB 注册成功后的时间同步与设备本地时间持久化。
- GB 实时流在 UDP/TCP 模式下的媒体协商兼容性。
- GB `RecordInfo` 查询与本地录像检索结果映射。
- 1400 注册、保活与基础能力补齐。

## 标准依据
- `GB/T 28181-2022` 的项目内调试基线见 [`gb28181-2022-baseline.md`](gb28181-2022-baseline.md)。
- 涉及注册、保活、实时流、回放、下载、广播、校时等流程时，优先以该基线文档中的章节和抓包检查项为准，再回看实现细节。
- 当前工程的关键落点集中在 `ProtocolManager`、`GBClientImpl`、`GB28181ClientReceiverAdapter`、`GB28181RtpPsSender` 和 `GB28181BroadcastBridge`。
- GB28181 对讲专项审查记录见 [`gb28181-talk-review.md`](gb28181-talk-review.md)。
- GB28181 远程抓拍与目录订阅专项审查记录见 [`gb28181-snapshot-subscribe-review.md`](gb28181-snapshot-subscribe-review.md)。

## 注意事项
- `LocalConfigProvider` 现在读取 `/userdata/conf/Config/GB/gb28181.ini`、`/userdata/conf/Config/GB/zero_config.ini` 与 `/userdata/conf/Config/GB/gat1400.ini`；其中 `gb28181.ini` / `gat1400.ini` 缺失时仍可按默认值自动生成。`gb28181.ini` 新增 `register_mode=standard|zero_config` 作为运行时模式开关：`standard` 模式下忽略 `zero_config.ini` 缺失，`zero_config` 模式下若 `zero_config.ini` 缺失则会直接记录日志并返回错误；旧的 `/userdata/conf/Config/gb28181.ini` 路径不再兼容，也不自动迁移。
- `ProtocolManager` 现已改为进程内单例；主程序在正常启动路径中通过 `ProtocolManager::Instance().Init()/Start()` 拉起协议栈，`LowerGAT1400SDK` 等外部模块也统一直接取这个单例，不再经过 `CSofia::GetProtocolManager()` 转发。
- `ProtocolManager` 当前对 GAT1400 服务实例和 GB28181 receiver 只保留运行中真实使用的非 `const` getter；此前零调用的 `const GetGatClientService()` / `const GetGbClientReceiver()` 已从协议胶水层移除。
- `GB28181ClientSDK` 的创建、绑定、释放现全部下沉到 `ProtocolManager` 私有生命周期中；`CSofia` 不再持有 SDK 指针，也不再负责 `Bind/Unbind`。
- `GetGbRegisterConfig()` 现在始终直接读取 `/userdata/conf/Config/GB/gb28181.ini + zero_config.ini` 中的本地持久化值；即使 `ProtocolManager::Init()` 之前被外部模块调用，也会先按默认值补齐配置文件再返回结果。零配置运行态专用的固定重定向 `server_id/server_ip/server_port` 不会通过这个接口回写到 `gb28181.ini`。
- `SetGbRegisterConfig()` 只负责把当前 `gb28181.ini` 对应的 7 个外部可编辑字段写回 flash：`enabled`、`register_mode`、`username`、`server_ip`、`server_port`、`device_id`、`password`；`device_name`、`expires_sec` 等其余注册参数继续沿用代码默认值，也不再顺带改写 `zero_config.ini`。
- 新增 `GetGbZeroConfig()` / `SetGbZeroConfig()`；只负责零配置入口的 `string_code`、`mac_address` flash 读写，不直接修改运行中的 GB 注册生命周期。
- 新增 `RestartGbRegisterService()` 作为单独的 GB 服务重载入口；它会从 flash 重新读取注册配置，并根据 `ProtocolManager` 当前是否已启动、GB 生命周期是否正在运行以及 `enabled` 新值决定只刷新缓存、停服或重启注册。
- 新增 `GetGatRegisterConfig()` / `SetGatRegisterConfig()` / `RestartGatRegisterService()`；语义与 GB 接口保持一致，分别负责“只读 flash”“只写 flash”和“显式把 `gat1400.ini` 重载到运行态”。
- `ProtocolManager` 当前只保留 `NotifyGatFaces()` / `NotifyGatMotorVehicles()` / `NotifyGatNonMotorVehicles()` 3 个 1400 对外上报入口，供外部模块直接上报人脸、机动车、非机动车；若 1400 未注册，则直接落现有失败补传队列。旧的 `NotifyGatAlarm()`、keepalive demo 和 `GAT1400CaptureEvent/GAT1400CaptureControl` 抓拍桥接链路均已移除。
- `ProtocolManager::Start()` 在真正拉起 RTP/广播/监听/注册链路前，会先执行一次 `ReloadExternalConfig()`，确保 `Init()` 之后、`Start()` 之前通过 `SetGbRegisterConfig()` 落盘的新值会被带入运行态。
- `StartGbClientLifecycle()` 当前固定以本地端口 `0` 启动 GB28181 SIP 客户端，由内核分配随机本地监听端口；`gb_register.server_port` 只保留远端平台 SIP 端口语义，不再复用为本地绑定端口。
- `SipEventManager` 在随机端口场景下会先用临时 socket `bind(0)` 获取一个可用端口，再用该端口走现有 `eXosip_listen_addr()` 建立正式监听，并把实际端口回写到 `ClientInfo.LocalPort`，保证 `REGISTER` / `MESSAGE` / `INVITE` 的 `From/Contact` 与真实监听端口一致。
- `StartGbClientLifecycle()` 现在把“SDK 已启动但首次 `Register()` 失败”的场景视为可恢复错误：生命周期不会直接退出，而是打印 `note=defer_retry` 日志并继续保留后台线程。
- `GbHeartbeatLoop()` 在未注册态下会按 `gb_keepalive.interval_sec` 节奏重试注册；只有 `server_ip/server_port/device_id` 这类静态配置校验失败时，GB 生命周期才会立即返回错误。
- 当前 `gb28181.ini` 只保留 6 个国标注册字段；`image_flip_mode`、`gb_talk`、`gb_reboot`、`gb_upgrade`、`gb_broadcast`、`gb_listen` 等其余 GB 协议项都不再写入本地 ini，而是固定使用代码默认值。
- 当前 `zero_config.ini` 独立持久化 `StringCode/Mac` 2 个零配置外部可编辑字段；`Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model` 固定走代码默认值，不再和 `gb28181.ini` 混存。`register_mode=zero_config` 时，运行态还会把首次重定向入口 `server_id/server_ip/server_port` 固定到代码默认值，与 `gb28181.ini` 中保存的标准国标注册参数彻底分离。
- `ProtocolManager` 现额外缓存一份“当前 GB 运行态 DeviceID”：平时仍按本地配置兜底，但一旦 SDK 的目录/报警/位置订阅回调带回真实 `gbCode`，后续 `NotifyGbAlarm()` 会优先使用这份运行态值，不再把报警 `DeviceID` 硬编码为 `gb_register.device_id`。这用于兼容零配置 `302` 之后 SDK 内部切换到正式平台 `deviceId` 的场景。
- 当前 `gat1400.ini` 只持久化 `GatRegisterParam`；`gat_upload`、`gat_capture` 等其他 1400 相关参数继续使用代码默认值或 HTTP 配置链路。
- `gb_register.enabled` 为 `0` 时，`ProtocolManager` 会跳过 GB client 生命周期启动与重注册，但 GAT1400 相关配置校验和生命周期不受影响。
- `GAT1400` 的 `GetTime()`/`GET_SYNCTIME` 当前已禁用为 no-op；设备时间统一由 GB28181 校时链路负责，启动阶段只打印 `event=get_time note=disabled` 作为诊断标记。
- GB `TeleBoot` 远程重启已接入冷却保护；短时间内重复命令会被拒绝并打印 `gb teleboot rejected` 日志。
- GB `DeviceUpgrade` 现在区分“控制命令应答”和“最终升级结果通知”两层语义：收到合法命令并完成下载校验后，只返回本次控制应答；最终 `DeviceUpgradeResult` 仅在升级执行并重启后的注册恢复阶段补报。
- GB 升级不再依赖 `/tmp/ota_upgrade_flag` 这类外部守护进程假设，已改为在包落盘后复用现有 `UpgradeReleaseResource` 事件释放业务资源，再走本地升级执行与重启路径。
- GB 升级包下载已不再依赖外部 `curl` 命令；`ProtocolManager` 现内置最小 HTTP 下载实现，支持总超时、`301/302/303/307/308` 重定向，以及按 `Content-Length` / `chunked` / 连接关闭三种方式流式落盘，避免把整包读入内存。
- 当前内置升级下载只支持 `http` URL；`https` 链路仍未接入 TLS，收到此类地址时会在下载阶段记录 `scheme_not_supported` 并按现有错误码链路返回 `download_failed`。
- `gb_reboot` / `gb_upgrade` 相关参数仍保留在运行态配置模型和 HTTP 配置链路中；当前本地 `gb28181.ini` 不再保存这些项，统一使用代码默认值。
- GB 广播通知 `MESSAGE` 只能对同一事务返回一次最终 SIP 响应；不要先发空 `200 OK` 再补带 XML 的 `Response`，否则平台只会收到前一个无业务体的应答。
- GB 广播 `Notify` 的业务 XML 响应必须回填平台原始 `SN`，并以 `TargetID` 作为响应 `DeviceID`；如果返回成 `SN=0` 或空设备编号，平台通常不会继续进入后续广播流程。
- 针对 GB/T 28181-2022 第 9.12 节广播流程，设备当前实现已调整为“`Notify` 先回纯 SIP `200 OK` -> 独立发送 `Broadcast` 应答 `MESSAGE` -> 设备主动发起 `Talk` 音频 `INVITE`”；仍保留被动 `INVITE` 处理链作为兼容兜底。
- GB 广播 `notify -> invite -> ack/bye` 现在有独立会话状态；`notify` 会校验 `TargetID` 是否匹配本机，`INVITE` 的音频 `200 OK` 会优先返回设备侧可达 IP，`BYE` 后会清理播音状态并把广播桥恢复到待机接收态。
- TCP 实时流要区分 `setup:active` 与 `setup:passive` 方向。
- SIP `INVITE` 应答不要被同步媒体建链阻塞。
- 对于 `MESSAGE` 类命令，要先区分“本事务立即返回纯 SIP `200 OK`”和“后续独立发送业务应答 `MESSAGE`”两层语义，不能混用。
- `Keepalive`、控制类应答、查询类应答、报警/目录通知确认都需要分别核对 `MESSAGE/200 OK` 与 XML 业务体的对应关系。
- 广播主动 `INVITE` 的 `200 OK` 若已收到却立刻出现 `parse answer failed` / `BYE`，先检查 SDP 解析结果里是否真正回填了 `payload/rtpmap`，不要只看 `IP/Port`。
- GB 回放音频不能按原始 PCM 直接塞给 RTP PS；应与实时流保持一致，按 SDP/`gb.live.audio_codec` 输出 `G711A` 或 `G711U`。
- GB 回放/下载回调需要带 session 身份；否则旧存储线程收尾时的 EOS 可能误伤新回放 session，表现为 ACK 已到但媒体立即 `eos video=0 audio=0`。
- GB 回放依赖 MP4 demux 连续读到有效 ES；对“packet 已读到但当前未产出有效音视频载荷”的场景不能直接当 EOF，否则会在首包前提前结束回放。
- 当 GB 回放仍在 `play-start` 后立即 `eos` 时，要同时观察存储层日志：先确认索引是否装载成功、是否选中目标录像文件，再区分 open/read 失败还是协议发送失败。
- GB 回放选择首个录像文件时，不能依赖 `index` 原始顺序和“包含开始时间”的单一条件；应先对有效记录按开始/结束时间排序，再按与请求区间重叠或紧随其后的文件进行匹配。
- GB28181 远程抓拍目前仍未形成完整业务能力，`kIFameControl` 在业务层仍是空执行；目录/报警/移动位置订阅也还停留在“保存句柄 + 首次静态通知”的阶段，后续待办见 [`gb28181-snapshot-subscribe-review.md`](gb28181-snapshot-subscribe-review.md)。

## 对讲现状
- 当前被动 `Talk INVITE` 会进入 `HandleGbAudioStreamRequest()`，并落到独立的 `m_gb_talk_session` / `m_talk_broadcast`。
- `Talk` 现在使用独立的 `gb_talk` 配置，`200 OK` 走 `gb_talk.recv_port`，`ACK` 后再应用媒体协商参数。
- 对 `UDP` 和设备主动 `TCP` 场景，当前已能建立基础双向音频闭环；`TCP passive` 复用同一连接回传音频仍是后续待办。
- 如需继续处理对讲问题，优先参考 [`gb28181-talk-review.md`](gb28181-talk-review.md) 中的审查结论和待办顺序。
