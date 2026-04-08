# API 手册

## 概述
本项目对外不是传统 Web 服务工程，但存在四类“接口面”：
- GB28181 配置查询 / 设置接口与 SDK 回调
- GAT1400 作为下级系统发起 / 接收的 HTTP VIID 接口
- GB28181 通过 SDK 回调暴露的媒体与控制接口

## 认证方式
- 本地注册配置接口: 当前代码中未体现额外认证
- GAT1400 对上 HTTP: 支持 Digest / Basic 类认证流程，401 后通过 `http_auth` 重试
- GB28181: 由 SIP 注册参数 `username/password` 驱动

---

## 接口列表

### GB28181 本地注册配置接口

#### `GetGbRegisterConfig()`
**描述:** 从 `/userdata/conf/Config/GB/gb28181.ini` 与 `/userdata/conf/Config/GB/zero_config.ini` 读取 GB28181 本地持久化配置；若文件不存在，会先按默认值补齐后返回，不依赖 `ProtocolManager` 当前缓存。

#### `SetGbRegisterConfig()`
**描述:** 只写入 `gb28181.ini` 中的标准 GB28181 注册配置到 flash，不直接修改运行中的 GB 注册生命周期，也不触碰 `zero_config.ini`。

#### `GetGbZeroConfig()`
**描述:** 从 `/userdata/conf/Config/GB/zero_config.ini` 读取零配置入口参数；文件缺失时回落代码默认值，不依赖 `ProtocolManager` 当前缓存。

#### `SetGbZeroConfig()`
**描述:** 只写入零配置入口参数到 flash，不直接修改运行中的 GB 注册生命周期。

#### `RestartGbRegisterService()`
**描述:** 从 flash 重新读取 GB28181 注册配置，并按当前运行状态与 `enabled` 决定只刷新缓存、停止 GB 服务或重新启动注册生命周期；当 `register_mode=zero_config` 时，运行态会额外套用代码内固定的零配置重定向 `server_id/server_ip/server_port`。

**当前约束:**
- 当前 `gb28181.ini` 仅持久化 `enabled`、`register_mode`、`username`、`server_ip`、`server_port`、`device_id`、`password` 7 个注册字段，其中 `register_mode` 取值为 `standard` 或 `zero_config`
- 零配置文件 `zero_config.ini` 当前只持久化 `string_code`、`mac_address`；`line_id`、`redirect_domain`、`redirect_server_id`、`custom_protocol_version`、`manufacturer`、`model` 统一走代码默认值
- `GetGbRegisterConfig()` 返回的是 flash 中保存的“标准国标配置 + 零配置串码/Mac”；在 `register_mode=zero_config` 下，运行中的 `ProtocolManager` 会另外将零配置重定向 `server_id/server_ip/server_port` 固定到代码默认值，不回写 `gb28181.ini`
- 上述两个 `ini` 只保存零配置首次接入所需的本地入口参数；运行期 `302` 返回的正式平台 `ServerIp/ServerPort/ServerDomain/ServerId/deviceId` 只保存在 SDK 内存，不会通过这些接口回写 flash
- 当 `register_mode=zero_config` 且 `zero_config.ini` 缺失时，flash 读取链路会记录错误日志；其中 `RestartGbRegisterService()` 与 `ProtocolManager::Init()/Start()` 会返回错误，`GetGbRegisterConfig()` 则会保留当前“记录日志后回落默认结构”的接口语义
- `device_name`、`expires_sec`、`gb_talk`、`gb_broadcast`、`gb_upgrade`、`gat_*` 等其他协议项不再写入本地 `ini`
- `SetGbRegisterConfig()` 成功仅表示 flash 落盘成功；运行中如需生效，需再显式调用 `RestartGbRegisterService()`
- `SetGbZeroConfig()` 成功仅表示 flash 落盘成功；运行中如需让新值参与注册，也需再显式调用 `RestartGbRegisterService()`
- `RestartGbRegisterService()` 在 `ProtocolManager` 未启动时只刷新缓存；已启动时会结合当前生命周期状态和 `enabled` 决定是否停/启 GB 服务
- 当 `RestartGbRegisterService()` 触发 GB 生命周期重启时，`GBClientImpl::Stop()` 会清空内存中的正式平台注册目标；后续再次注册会基于 flash 中的入口参数重新向重定向服务器获取
- `ProtocolManager::Start()` 前会先执行一次 `ReloadExternalConfig()`，保证 `Init()` 之后、`Start()` 之前写入 flash 的配置不会被旧缓存覆盖

### GAT1400 本地注册配置接口

#### `GetGatRegisterConfig()`
**描述:** 从 `/userdata/conf/Config/GB/gat1400.ini` 读取 GAT1400 注册配置；若文件不存在，会先按默认值补齐后返回，不依赖 `ProtocolManager` 当前缓存。

#### `SetGatRegisterConfig()`
**描述:** 只写入 GAT1400 注册配置到 flash，不直接修改运行中的 GAT1400 生命周期。

#### `RestartGatRegisterService()`
**描述:** 从 flash 重新读取 GAT1400 注册配置，并在 `ProtocolManager` 已启动时调用 `m_gat_client->Reload(...)` 使新配置生效；未启动时只刷新缓存。

**当前约束:**
- 当前仅持久化 `GatRegisterParam`：`scheme`、`server_ip`、`server_port`、`base_path`、`device_id`、`username`、`password`、`auth_method`、`listen_port`、`expires_sec`、`keepalive_interval_sec`、`max_retry`、`request_timeout_ms`、`retry_backoff_policy`
- `gat_upload`、`gat_capture` 等其他 1400 协议项不写入本地 `ini`
- `SetGatRegisterConfig()` 成功仅表示 flash 落盘成功；运行中如需生效，需再显式调用 `RestartGatRegisterService()`
- `RestartGatRegisterService()` 在 `ProtocolManager` 已启动且 `m_gat_client` 已构造时会复用现有 `Reload()` 逻辑决定是否热更新或重启本地 1400 服务
- 当前本地注册配置只认 `/userdata/conf/Config/GB/gb28181.ini`、`/userdata/conf/Config/GB/zero_config.ini` 与 `/userdata/conf/Config/GB/gat1400.ini` 三个新路径；旧的 `/userdata/conf/Config/gb28181.ini` 不再作为读取或迁移来源

### ProtocolManager 核心回调

#### `HandleGbLiveStreamRequest`
**描述:** 处理 GB28181 实时流请求，解析 SDP、准备媒体参数并返回应答媒体信息。

#### `HandleGbStreamAck`
**描述:** 处理平台对实时流 / 回放 / 下载应答的 `ACK`，驱动后续媒体启动。

#### `RespondGbMediaPlayInfo`
**描述:** 生成并发送实时流 / 回放 / 下载的 SIP `200 OK` SDP。

#### `HandleGbPlaybackRequest` / `HandleGbDownloadRequest`
**描述:** 启动存储回放通道，并在 `ACK` 或控制恢复后继续发流。

#### `HandleGbPlayControl`
**描述:** 处理 `PLAY / PAUSE / TEARDOWN / Scale / Range` 等回放控制。

#### `HandleGbAudioStreamRequest`
**描述:** 处理被动 `Talk INVITE`，使用独立 talk session，并在 `ACK` 后再应用媒体协商参数。

### GAT1400 对上接口

**规范基线:** `GA/T 1400.4-2017`

#### 通用约束
- 资源风格: 所有接口均按 REST 资源建模，使用 `/VIID/*` 或 `/VIAS/*` 层级 URI
- Content-Type: `application/*+JSON`
- 身份头: 请求头应带 `User-Identify`
- 认证: 应支持 `RFC 2617` 摘要认证，宜支持 `RFC 2818` HTTPS
- 响应约束: `GET` 成功时返回资源对象，非 `2XX` 场景返回 `ResponseStatus`

#### 系统类接口

| 方法 | URI | 说明 |
|------|-----|------|
| `POST` | `/VIID/System/Register` | 设备注册 |
| `POST` | `/VIID/System/UnRegister` | 设备注销 |
| `POST` | `/VIID/System/Keepalive` | 心跳保活 |
| `GET` | `/VIID/System/Time` | 校时；当前工程中已禁用为 no-op，调用只返回空时间字符串 |

#### 标准资源上报接口

| 方法 | URI | 说明 |
|------|-----|------|
| `POST` | `/VIID/Faces` | 批量上报人脸对象 |
| `POST` | `/VIID/Persons` | 批量上报人员对象 |
| `POST` | `/VIID/MotorVehicles` | 批量上报机动车对象 |
| `POST` | `/VIID/NonMotorVehicles` | 批量上报非机动车对象 |
| `POST` | `/VIID/Things` | 批量上报物品对象 |
| `POST` | `/VIID/Scenes` | 批量上报场景对象 |
| `POST` | `/VIID/AnalysisRules` | 批量上报视频图像分析规则 |
| `POST` | `/VIID/VideoLabels` | 批量上报视频图像标签 |
| `POST` | `/VIID/Dispositions` | 批量布控 |
| `POST` | `/VIID/DispositionNotifications` | 批量告警通知 |

#### 元数据 + Data 双阶段接口

| 元数据接口 | Data 接口 | 说明 |
|------------|-----------|------|
| `POST /VIID/Images` | `POST /VIID/Images/{ImageID}/Data` | 先上报图像元数据，再上传 Base64 图像数据 |
| `POST /VIID/VideoSlices` | `POST /VIID/VideoSlices/{VideoID}/Data` | 先上报视频片段元数据，再上传 Base64 视频数据 |
| `POST /VIID/Files` | `POST /VIID/Files/{FileID}/Data` | 先上报文件元数据，再上传 Base64 文件数据 |

#### 订阅与通知接口

| 方法 | URI | 说明 |
|------|-----|------|
| `GET/POST/PUT/DELETE` | `/VIID/Subscribes` | 查询 / 新增 / 修改 / 删除订阅任务 |
| `PUT` | `/VIID/Subscribes/{SubscribeID}` | 取消订阅，写入取消单位、取消人、取消时间、取消原因 |
| `POST` | `/VIID/SubscribeNotifications` | 发送订阅通知 |
| `DELETE` | `/VIID/SubscribeNotifications?IDList=a,b,c` | 删除通知记录 |

#### 其他标准资源

| 方法 | URI | 说明 |
|------|-----|------|
| `GET/PUT` | `/VIID/APEs` | 查询采集设备 / 动态口令更新 |
| `GET` | `/VIID/APSs` | 查询采集系统 |
| `GET` | `/VIID/Tollgates` | 查询视频卡口 |
| `GET` | `/VIID/Lanes` | 查询车道 |
| `GET/POST/PUT/DELETE` | `/VIID/AnalysisRules` | 分析规则查询 / 新增 / 更新 / 删除 |
| `GET/POST/PUT/DELETE` | `/VIID/VideoLabels` | 视频图像标签查询 / 新增 / 更新 / 删除 |

#### 当前工程实现备注
- 当前仓库以“设备侧 lower client”为主，重点覆盖注册、保活、结构化对象上报、订阅任务接收和通知发送。
- 当前本地 HTTP 服务面主要处理 `/VIID/Subscribes` 及 `/VIID/Subscribes/{ID}`。
- 当前对上请求目标由 `gat_register.scheme/server_ip/server_port/base_path/request_timeout_ms` 统一拼装；`overrideUrl` 仍可覆盖到完整 URL。
- 上传失败请求会按 `gat_upload.retry_policy` 重试，最终失败后落盘到 `gat_upload.queue_dir`，并在重新注册成功或保活恢复后回放。
- 标准还定义了 `/VIAS/*` 分析系统接口，当前仓库未见完整分析系统服务面；当前 `ExecuteRequest` 会对 `/VIAS/*` 直接返回本地不支持错误，且不会进入失败补传队列。
- 当前代码中额外存在 `POST /VIID/APEs` 的发送入口，这不是 `GA/T 1400.4-2017` 对 `APEs` 的标准定义；现已收口为兼容扩展开关，只有 `gat_upload_enable_apes_post_compat=1` 时才允许发送。
- 配置模型已接受 `https`，但 `ExecuteRequest` 仍只实现 `HTTP/TCP`，若平台要求 HTTPS 仍需继续开发。

### GAT1400 本地下级订阅接口

#### `GET /VIID/Subscribes`
**描述:** 查询本地维护的订阅列表。

#### `POST /VIID/Subscribes`
**描述:** 新增订阅。

#### `PUT /VIID/Subscribes`
**描述:** 更新订阅。

#### `DELETE /VIID/Subscribes?IDList=a,b,c`
**描述:** 批量删除订阅。

#### `PUT /VIID/Subscribes/{SubscribeID}`
**描述:** 取消单个订阅。

### GAT1400 SDK 导出接口

**描述:** 通过 `LowerGAT1400SDK` 将运行中的 `GAT1400ClientService` 暴露给业务模块。

#### 生命周期与状态

| 导出函数 | 说明 |
|----------|------|
| `LOWER_1400_START` | 基于 `REGIST_PARAM_S + CONNECT_PARAM_S + listenPort` 启动或重启当前进程内的 1400 client |
| `LOWER_1400_STOP` | 停止当前进程内 1400 client |
| `LOWER_1400_REGISTER` / `LOWER_1400_UNREGISTER` / `LOWER_1400_KEEPALIVE` | 主动触发注册、注销、保活 |
| `LOWER_1400_GET_TIME` | 读取平台校时结果；当前实现中返回空时间字符串，不再真正访问 `/VIID/System/Time` |

#### 上报与观察者

| 导出函数 | 说明 |
|----------|------|
| `LOWER_1400_POST_FACES/.../LOWER_1400_POST_APES` | 直接进入 `GAT1400ClientService::Post*` 主链路，复用当前重试和补传能力；其中 `LOWER_1400_POST_APES` 受 `gat_upload_enable_apes_post_compat` 开关控制 |
| `LOWER_1400_POST_SUBSCRIBENOTIFICATIONS` | 回发订阅通知 |
| `LOWER_1400_ADD_SUBSCRIBE_OBSERVER` / `LOWER_1400_DEL_SUBSCRIBE_OBSERVER` | 业务层消费 `/VIID/Subscribes` 变更 |
| `LOWER_1400_ADD_REGIST_OBSERVER` / `LOWER_1400_DEL_REGIST_OBSERVER` | 业务层监听注册状态变化 |
| `LOWER_1400_SET_1400INHTTPCMD(url, method, format, content)` | 按 `HTTP_MSG_TYPE_H + HTTP_URI_TYPE_H` 透传自定义 1400 HTTP 命令，内部仍复用现有发送链路；若目标为 `/VIAS/*` 或未开启的 `POST /VIID/APEs` 兼容扩展，会直接返回本地错误 |

### GAT1400 抓拍桥接接口

**描述:** 编码侧 / 算法侧 / 其他业务模块优先通过 `ProtocolManager::NotifyGatAlarm()` 通知 1400 模块；内部仍复用 `App/Protocol/gat1400/GAT1400CaptureControl.*` 作为抓拍队列。若 1400 已注册，服务会直接尝试上传；否则先入队，后续在注册成功或保活成功后 drain。

| 接口 | 说明 |
|------|------|
| `protocol::ProtocolManager::Instance().NotifyGatAlarm(event)` | 业务侧正式入口；提交一条抓拍事件给 1400 模块，内部自动决定“直接上传”或“先入队” |
| `media::GAT1400CaptureControl::Instance()` | 获取进程内唯一抓拍桥接控制器 |
| `SubmitFaceCapture(face, images, videos, files, traceId)` | 提交一条人脸抓拍事件，携带可选关联图片 / 视频 / 文件 |
| `SubmitMotorCapture(motorVehicle, images, videos, files, traceId)` | 提交一条机动车抓拍事件，携带可选关联图片 / 视频 / 文件 |
| `Submit(event)` | 直接提交完整 `GAT1400CaptureEvent`，供后续扩展更多抓拍类型 |
| `PopPending()` / `PendingCount()` | 供消费者按队列方式主动拉取待处理抓拍事件 |

**当前约束:**
- 当前默认只收口“人脸抓拍 / 机动车抓拍 + 图片 / 视频 / 文件”这两类需求。
- 抓拍桥接层当前只做进程内内存排队，不写 flash；设备或进程重启后，未上传的抓拍事件不会自动恢复。
- 若结构化对象里的 `SourceID/DeviceID` 为空，1400 服务会优先从关联的图片 / 视频元数据里补齐最小来源关系。

**接入步骤:**
1. 调用方先准备好 `GAT_1400_Face` 或 `GAT_1400_Motor`。
2. 如果有图片 / 视频 / 文件，同时准备好对应的 `GAT_1400_ImageSet/GAT_1400_VideoSliceSet/GAT_1400_FileSet`，并把 Base64 数据放到 `Data` 字段。
3. 推荐把这些内容组到 `GAT1400CaptureEvent` 后调用 `ProtocolManager::NotifyGatAlarm()`；只有在协议模块内部或特殊场景下，才直接用 `SubmitFaceCapture()/SubmitMotorCapture()/Submit()`。
4. 返回 `0` 表示 1400 模块已接收这条业务通知；若当前链路已就绪，会直接尝试上传；若未就绪或直接上传失败，会保留到内部队列等待后续补偿。

**最小示例:**
```cpp
GAT_1400_Motor motor;
strncpy(motor.MotorVehicleID, "motor-001", sizeof(motor.MotorVehicleID) - 1);
strncpy(motor.DeviceID, "34020000001320000001", sizeof(motor.DeviceID) - 1);

GAT_1400_ImageSet imageSet;
strncpy(imageSet.ImageInfo.ImageID, "img-001", sizeof(imageSet.ImageInfo.ImageID) - 1);
strncpy(imageSet.ImageInfo.DeviceID, "34020000001320000001", sizeof(imageSet.ImageInfo.DeviceID) - 1);
strncpy(imageSet.ImageInfo.FileFormat, "Jpeg", sizeof(imageSet.ImageInfo.FileFormat) - 1);
imageSet.Data = imageBase64;

media::GAT1400CaptureEvent event;
event.trace_id = "capture-trace-001";
event.object_type = media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE;
event.motor_vehicle = motor;
event.image_list.push_back(imageSet);

const int ret = protocol::ProtocolManager::Instance().NotifyGatAlarm(&event);
```

### GB28181 设备能力接口

#### 实时流请求
**描述:** 由 `GB28181ClientReceiverAdapter::OnStreamRequest` 转发到 `ProtocolManager::HandleGbLiveStreamRequest`。

#### 历史回放 / 下载请求
**描述:** 由 `HandleGbPlaybackRequest` / `HandleGbDownloadRequest` 启动存储回放通道。

#### 历史回放控制
**描述:** 由 `HandleGbPlayControl` 处理 `PLAY / PAUSE / TEARDOWN / Scale / Range`，当前已支持暂停、恢复、倍速、拖动定位和停止。

#### 语音广播 / 对讲 / 监听
**描述:** 广播音频通过 `GB28181BroadcastBridge` 接收；被动 `Talk INVITE` 通过独立 talk session 处理；监听音频通过 `GB28181ListenBridge` 回传。

#### 广播链路约束
- 当前广播流程按 `GB/T 28181-2022` 第 9.12 节调整为“`Notify` 先回纯 SIP `200 OK` -> 独立发送 `Broadcast` 应答 `MESSAGE` -> 设备主动发起音频 `INVITE` 建链”。
- `Broadcast Notify` 的业务 XML 应答必须回填平台原始 `SN`，并使用 `TargetID` 作为响应 `DeviceID`；错误回填通常会导致平台继续判定广播失败。
- 广播主动 `INVITE` 收到 `200 OK` 后，要确保 SDP 里的 `payload/rtpmap` 已成功回填，再进入后续音频桥配置。

#### 设备控制
**描述:** 支持 PTZ、布防 / 撤防、告警复位、参数配置、设备升级、回到预置位、远程重启。

#### 终端白皮书扩展接口约束
- 注册重定向: 白皮书 `A.16` 要求设备先向重定向服务器发起 `REGISTER`，收到 `302 Moved` 后再向正式平台注册；请求头 / 设备信息中需要带 `StringCode`、`Mac`、`CustomProtocolVersion` 等扩展信息。
- 语音广播媒体协商: 白皮书 `A.1` 和 `A.14/A.15` 要求支持 `TCP 主动拉流`；当前实现通过 `StartBroadcastStreamRequest`、`BuildGbBroadcastMediaInfo`、`ApplyGbBroadcastTransportHint`、`BuildGbBroadcastAnswerSdp` 协商 `UDP / TCP active / TCP passive`。
- 基础参数查询 / 配置: 当前 `DeviceConfig` / `ConfigDownload` 主链已支持 `DeviceName`、`Expiration`、`HeartBeatInterval`、`HeartBeatCount`、`FrameMirror`、OSD 开关，并兼容旧 `VideoParamOpt/ImageFlip`；白皮书扩展的 `LocalPort`、`Domain`、`SipIp`、`SipPort`、`SipId`、`VoiceFlowMode`、`Password`、`ChannelList` 尚未在当前 App 层结构中完整暴露。
- 设备信息查询应答: 当前 `ResponseDeviceInfo` 已回基础字段、`StringCode/Mac/Line/CustomProtocolVersion`，并按嵌套 XML 节点回 `DeviceCapabilityList/ProtocolFunctionList` 的最小真实能力集；联调时需重点核对白皮书 `A.19/附录 G` 的缺陷编码语义。

---

## 错误码

| 错误码 | 说明 |
|--------|------|
| `<0` | 本工程内大部分协议模块使用负数表示本地错误 |
| `0` | 成功 |
| `GAT1400 STATUS_CODE_H` | GAT1400 JSON 响应状态码，定义见 `CMS1400Struct.h` |
