# 数据模型

## 概述
本项目不是典型数据库驱动应用，核心“数据模型”主要体现在协议配置结构、会话状态结构和 GAT1400 / GB28181 协议对象。

---

## 配置模型

### `ProtocolExternalConfig`

**描述:** 协议总配置对象，定义于 `App/Protocol/config/ProtocolExternalConfig.h`。

| 字段名 | 类型 | 约束 | 说明 |
|--------|------|------|------|
| `version` | `std::string` | 非空更佳 | 配置版本 |
| `gb_register` | `GbRegisterParam` | 必填 | GB28181 注册参数 |
| `gb_keepalive` | `GbKeepaliveParam` | 必填 | GB28181 心跳参数 |
| `gb_live` | `GbLiveParam` | 必填 | GB28181 实时流发送参数 |
| `gb_playback` | `GbPlaybackParam` | 可选 | GB28181 回放 / 下载参数 |
| `gb_video` | `GbVideoParam` | 可选 | 主辅码流视频参数 |
| `gb_image` | `GbImageParam` | 可选 | 图像翻转等参数 |
| `gb_osd` | `GbOsdParam` | 可选 | OSD 开关与格式 |
| `gb_upgrade` | `GbUpgradeParam` | 可选 | 升级 URL 白名单、校验策略 |
| `gb_broadcast` | `GbBroadcastParam` | 可选 | 广播接收参数 |
| `gb_talk` | `GbTalkParam` | 可选 | 对讲接收与编码参数 |
| `gb_listen` | `GbListenParam` | 可选 | 监听发送参数 |
| `gat_register` | `GatRegisterParam` | 必填 | GAT1400 注册、本地监听和对上请求地址参数 |
| `gat_upload` | `GatUploadParam` | 可选 | 批量大小、重试策略和失败补传队列参数 |
| `gat_capture` | `GatCaptureParam` | 可选 | 采集侧并发和 profile 参数 |

### `GbRegisterParam` 本地持久化子集

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `enabled` | `int` | GB28181 总开关，`0` 为禁用，`1` 为启用 |
| `register_mode` | `std::string` | 运行时注册模式，取值为 `standard` 或 `zero_config` |
| `server_ip` | `std::string` | GB 平台 SIP 接入地址 |
| `server_port` | `int` | GB 平台 SIP 接入端口 |
| `device_id` | `std::string` | 设备国标编码 |
| `username` | `std::string` | 注册用户名 / 接入编码 |
| `password` | `std::string` | 注册密码 |

**说明:** 当前 `gb28181.ini` 只持久化上述 7 个注册字段；`device_name`、`expires_sec`、`gb_talk`、`gb_broadcast`、`gb_upgrade`、`gb_reboot` 等其余 GB 协议项统一使用代码默认值，不再落本地 `ini`。在 `register_mode=zero_config` 下，这里的 `server_ip/server_port` 仍表示首次重定向接入入口，而不是 `302` 返回的正式平台地址。

### `GbRegisterParam` 零配置持久化子集

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `string_code` | `std::string` | 首次重定向注册使用的设备身份 |
| `mac_address` | `std::string` | 首次重定向注册扩展头 `Mac` |
| `line_id` | `std::string` | 首次重定向注册扩展头 `Line` |
| `redirect_domain` | `std::string` | 重定向接入域 |
| `redirect_server_id` | `std::string` | 重定向接入服务器标识 |
| `custom_protocol_version` | `std::string` | 首次重定向注册扩展头 `CustomProtocolVersion` |
| `manufacturer` | `std::string` | 首次重定向注册扩展头 `Manufacturer` |
| `model` | `std::string` | 首次重定向注册扩展头 `Model` |

**说明:** 当前 `zero_config.ini` 只持久化上述 8 个零配置字段；只有当 `gb28181.ini` 中 `register_mode=zero_config` 时，`LocalConfigProvider` 才要求该文件存在，缺失时会直接记录日志并返回错误，不做兼容迁移或自动补文件。运行期 `302` 返回的正式平台 `ServerIp/ServerPort/ServerDomain/ServerId/deviceId` 不会写入该文件，而是只保存在 SDK 进程内存中。

### `GatRegisterParam` 本地持久化子集

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `scheme` | `std::string` | 对上请求协议，当前配置支持 `http/https`，实现层仅真正支持 `http` |
| `server_ip` / `server_port` | `std::string` / `int` | 平台地址 |
| `base_path` | `std::string` | 统一 URI 前缀，例如平台部署在反向代理子路径下 |
| `device_id` | `std::string` | 注册设备标识 |
| `listen_port` | `int` | 本地订阅 HTTP 服务端口 |
| `request_timeout_ms` | `int` | 对上 HTTP 请求超时 |
| `retry_backoff_policy` | `std::string` | 注册失败后的退避序列，逗号分隔秒数 |

**说明:** 当前 `gat1400.ini` 只持久化 `GatRegisterParam`；`gat_upload`、`gat_capture` 等其余 1400 协议项统一使用代码默认值或 HTTP 配置链路，不落本地 `ini`。

### `GatUploadParam`

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `batch_size` | `int` | 批量上报切片大小 |
| `flush_interval_ms` | `int` | 采集侧刷出节奏 |
| `retry_policy` | `std::string` | 单次上传重试策略，逗号分隔秒数 |
| `queue_dir` | `std::string` | 最终失败请求的持久化目录 |
| `max_pending_count` | `int` | 本地待补传请求上限 |
| `replay_interval_sec` | `int` | 注册恢复后以外的周期性回放间隔 |
| `enable_apes_post_compat` | `int` | 是否允许平台兼容扩展 `POST /VIID/APEs`，`0=关闭(默认)`、`1=开启` |

### `gb28181.ini`

| 键名 | 说明 |
|------|------|
| `enable` | GB28181 开关 |
| `register_mode` | 运行时注册模式，`standard` 或 `zero_config` |
| `username` | 接入编码 |
| `server_ip` | 接入 IP |
| `server_port` | 接入端口 |
| `device_id` | 设备编码 |
| `password` | 设备密码 |

默认路径: `/userdata/conf/Config/GB/gb28181.ini`

### `zero_config.ini`

| 键名 | 说明 |
|------|------|
| `string_code` | 首次重定向注册设备身份 |
| `mac_address` | 设备 MAC |
| `line_id` | 线路标识 |
| `redirect_domain` | 重定向域 |
| `redirect_server_id` | 重定向服务器 ID |
| `custom_protocol_version` | 自定义协议版本 |
| `manufacturer` | 厂商名称 |
| `model` | 设备型号 |

默认路径: `/userdata/conf/Config/GB/zero_config.ini`

### `gat1400.ini`

| 键名 | 说明 |
|------|------|
| `scheme` | 对上请求协议 |
| `server_ip` | 平台地址 |
| `server_port` | 平台端口 |
| `base_path` | 统一 URI 前缀 |
| `device_id` | 设备标识 |
| `username` | 认证用户名 |
| `password` | 认证密码 |
| `auth_method` | 认证方式 |
| `listen_port` | 本地订阅 HTTP 监听端口 |
| `expires_sec` | 注册有效期 |
| `keepalive_interval_sec` | 保活周期 |
| `max_retry` | 最大重试次数 |
| `request_timeout_ms` | HTTP 超时 |
| `retry_backoff_policy` | 注册退避序列 |

默认路径: `/userdata/conf/Config/GB/gat1400.ini`

### 白皮书字段映射

| 白皮书字段 / 要求 | 当前模型 / 结构 | 现状 |
|------------------|-----------------|------|
| `Name` | `GbRegisterParam.device_name` / `CfgBasicParam.DeviceName` | ✅已映射 |
| `Expiration` | `GbRegisterParam.expires_sec` / `CfgBasicParam.Expiration` | ✅已映射 |
| `HeartBeatInterval` | `GbKeepaliveParam.interval_sec` / `CfgBasicParam.HeartBeatInterval` | ✅已映射 |
| `HeartBeatCount` | `GbKeepaliveParam.max_retry` / `CfgBasicParam.HeartBeatcount` | ✅已映射 |
| `VoiceFlowMode` | `GbBroadcastParam.transport`，广播协商阶段映射到 `UDP/TCP active` | ⚠️运行态可映射，但不在 `CfgBasicParam` 中 |
| `LocalPort` / `Domain` / `SipIp` / `SipPort` / `SipId` / `Password` / `ChannelList` | `ProtocolExternalConfig` / `CfgBasicParam` 中未见完整对应字段 | ❗存在结构缺口 |
| 多码流数量 `>=3` | `GbMultiStreamParam.stream_count` | ⚠️当前默认值为 `2` |

---

## GB28181 会话模型

### `MediaInfo`

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `RtpType` | `enum` | UDP / TCP 及 active / passive 模式 |
| `DeviceID` | `char[]` | 设备国标 ID |
| `IP` | `char[]` | 对端媒体地址 |
| `Port` | `uint` | 对端媒体端口 |
| `Ssrc` | `char[]` | 媒体 SSRC |
| `RequestType` | `enum` | 实时流 / 回放 / 下载 / 语音 |

### `GbLiveSession`

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `active` | `bool` | 是否正在推流 |
| `stream_handle` | `StreamHandle` | SDK 会话句柄 |
| `gb_code` | `std::string` | 设备编码 |
| `audio_requested` | `bool` | 平台是否请求音频 |
| `audio_enabled` | `bool` | 当前是否实际发送音频 |
| `prefer_sub_stream` | `bool` | 是否使用辅码流 |
| `acked` | `bool` | 是否已收到流应答 |

### `GbReplaySession`

**描述:** 回放 / 下载会话状态。

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `download` | `bool` | 区分下载或回放 |
| `storage_handle` | `int` | 存储模块句柄 |
| `start_time` | `uint64_t` | 回放起始时间 |
| `end_time` | `uint64_t` | 回放结束时间 |
| `acked` | `bool` | 收到 `INVITE ACK` 或成功处理 `PLAY` 类控制后才继续发送 |

### 语音会话约束

- 被动 `Talk INVITE` 当前使用独立 talk session，不再复用广播状态。
- 广播通知与后续主动音频 `INVITE` 也使用独立广播会话；`ACK` 到达前只记录协商结果，不应提前发流。
- 回放 / 下载 / 实时流会话都依赖 `acked` 状态控制真正的媒体发送时机。

### 白皮书扩展结构缺口

| 白皮书对象 | 当前结构 | 现状 |
|------------|----------|------|
| `A.19 DeviceInfo` 扩展字段 `StringCode` / `Mac` / `Line` / `CustomProtocolVersion` / `DeviceCapabilityList` / `ProtocolFunctionList` | `DeviceInfo` | ✅`GB28181Defs.h` 已补齐扩展身份字段、最小设备能力子集和最小协议功能子集；当前仍按真实实现保留 `StreamNum2`、图像翻转、升级回滚、独立告警等缺陷表达 |
| `A.14/A.15 BasicParam` 扩展字段 `VoiceFlowMode` / `LocalPort` / `Domain` / `SipIp` / `SipPort` / `SipId` / `Password` / `ChannelList` | `CfgBasicParam` / `BasicSetting` | ❗当前结构仅覆盖 `DeviceName`、`Expiration`、`HeartBeatInterval`、`HeartBeatcount` |

---

## GAT1400 协议与状态模型

### 规范定位
- `GA/T 1400.4-2017` 负责定义资源 URI、HTTP 方法、消息配对和交互流程。
- `GA/T 1400.3-2017` 负责定义 `Register`、`ResponseStatus`、`Person`、`Face`、`ImageInfo` 等对象结构。
- `GA/T 1399` 负责定义分析系统任务、规则、标签和能力对象。

### 协议消息骨架

| 消息族 | 典型对象 | 说明 |
|--------|----------|------|
| 系统消息 | `Register` `UnRegister` `Keepalive` `SystemTime` | 注册、注销、保活、校时 |
| 响应消息 | `ResponseStatus` `ResponseStatusList` | 单条或批量结果返回 |
| 订阅消息 | `Subscribe` `SubscribeList` | 订阅任务创建、查询、修改、删除 |
| 通知消息 | `SubscribeNotification` `SubscribeNotificationList` | 订阅通知发送与查询 |
| 业务对象消息 | `PersonList` `FaceList` `ImageList` `VideoSliceList` 等 | 视频图像对象批量 CRUD |

### `ResponseStatus` 语义

| 字段 | 语义 |
|------|------|
| `Id` | 本次响应对应的资源标识；注册场景下通常为 `DeviceID` |
| `StatusCode` | 操作结果码 |
| `StatusString` | 操作结果说明 |
| `LocalTime` | 被请求方本地时间，可用于校时 |

### 资源组织模式

| 模式 | 示例 | 说明 |
|------|------|------|
| 列表资源 | `/VIID/Persons` | 支持批量查询和批量 CRUD |
| 单体资源 | `/VIID/Persons/{ID}` | 支持单对象查询、修改、删除 |
| 信息资源 | `/VIID/Images/{ID}/Info` | 对象描述信息 |
| 数据资源 | `/VIID/Images/{ID}/Data` | 二进制数据，消息体为 Base64 |

### 查询字符串模式

| 模式 | 说明 |
|------|------|
| `属性键=值` | `GET` 查询使用对象属性过滤 |
| `IDList=a,b,c` | `DELETE` 批量删除使用半角逗号分隔的 ID 列表 |

### GAT1400 资源对象族

| 资源族 | 典型对象 |
|--------|----------|
| 系统资源 | `Register` `UnRegister` `Keepalive` `SystemTime` |
| 采集资源 | `APE` `APS` `Tollgate` `Lane` |
| 视频图像资源 | `VideoSlice` `Image` `File` |
| 结构化对象 | `Person` `Face` `MotorVehicle` `NonMotorVehicle` `Thing` `Scene` |
| 业务对象 | `Case` `Disposition` `DispositionNotification` |
| 订阅对象 | `Subscribe` `SubscribeNotification` |
| 分析对象 | `AnalysisRule` `VideoLabel` |

### `m_subscriptions`

**描述:** `GAT1400ClientService` 维护的订阅表，键为 `SubscribeID`。

| 字段名 | 类型 | 约束 | 说明 |
|--------|------|------|------|
| `SubscribeID` | 字符数组 | 唯一 | 订阅标识 |
| `Title` | 字符数组 | 可选 | 订阅标题 |
| `SubscribeDetail` | 字符数组 | 可选 | 订阅内容 |
| `BeginTime` | 字符数组 | `YYYYMMDDHHMMSS` | 生效时间 |
| `EndTime` | 字符数组 | `YYYYMMDDHHMMSS` | 失效时间 |
| `SubscribeStatus` | `int` | 运行态 | 当前订阅状态 |

### `PendingUploadItem`

**描述:** `GAT1400ClientService` 维护的最小失败请求持久化模型，用于重注册后的补传。

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `id` | `std::string` | 本地队列项标识，同时映射持久化文件名 |
| `method` | `std::string` | HTTP 方法，当前主要为 `POST` |
| `path` | `std::string` | 相对 VIID 资源路径或绝对覆盖 URL |
| `content_type` | `std::string` | 请求头内容类型 |
| `response_kind` | `std::string` | 响应解析模式，当前区分 `status` 与 `list` |
| `body` | `std::string` | 原始请求体 |
| `attempt_count` | `int` | 已累计重放尝试次数 |
| `enqueue_time` | `time_t` | 入队时间，用于保序回放 |
| `persist_path` | `std::string` | 持久化文件路径 |

### SDK 运行时桥接模型

| 对象 | 说明 |
|------|------|
| `CONNECT_PARAM_S` | `LowerGAT1400SDK` 启动时传入的对端地址模型，包含 `GBCode/ip/port` |
| `REGIST_PARAM_S` | `LowerGAT1400SDK` 启动时传入的注册认证参数，包含 `expires/username/password/auth` |
| `ProtocolManager::Instance()` | 进程内唯一协议入口；主程序和 `LowerGAT1400SDK` 都通过它访问当前运行中的 `ProtocolManager`，不再经过 `CSofia` 转发。 |
| `LOWER_1400_SET_1400INHTTPCMD(url, method, format, content)` | 运行时透传模型；`url` 可为空、相对路径或绝对 URL，`format` 决定默认资源路径和响应解析方式；当前会拒绝 `/VIAS/*`，并对 `POST /VIID/APEs` 应用兼容开关 |

### `GAT_1400_*`

**描述:** 所有 1400 协议对象结构体统一定义在 `CMS1400Struct.h`。当前工程重点涉及：
- `GAT_1400_Face`
- `GAT_1400_Person`
- `GAT_1400_Motor`
- `GAT_1400_NonMotor`
- `GAT_1400_Thing`
- `GAT_1400_Scene`
- `GAT_1400_ImageSet`
- `GAT_1400_VideoSliceSet`
- `GAT_1400_FileSet`
- `GAT_1400_Disposition`
- `GAT_1400_Disposition_Notification`
- `GAT_1400_Subscribe`
- `GAT_1400_Subscribe_Notification`
- `GAT_1400_AnalysisRule`
- `GAT_1400_VideoLabel`
- `GAT_1400_Ape`

**说明:** `GetTime()` / `LOWER_1400_GET_TIME` 当前已禁用为 no-op，不再真正访问 `/VIID/System/Time`。

---

## 索引与关联

- 配置来源: `HttpConfigProvider -> ProtocolExternalConfig -> ProtocolManager / GAT1400ClientService`
- 本地配置: `/userdata/conf/Config/GB/gb28181.ini + /userdata/conf/Config/GB/zero_config.ini -> LocalConfigProvider -> GetGbRegisterConfig()/SetGbRegisterConfig()/RestartGbRegisterService() -> ProtocolManager::Start()/GB lifecycle`
- 本地配置: `/userdata/conf/Config/GB/gat1400.ini -> LocalConfigProvider -> GetGatRegisterConfig()/SetGatRegisterConfig()/RestartGatRegisterService() -> ProtocolManager::Start()/GAT1400 lifecycle`
- 实时流关联: `GbLiveSession -> GB28181RtpPsSender`
- 回放关联: `GbReplaySession -> Storage_Module_*`
- 订阅关联: `m_subscriptions -> observer 回调 -> 上层业务`
- 1400 基线: `GAT1400.pdf -> helloagents/wiki/modules/gat1400.md -> 本文件 / api.md`
