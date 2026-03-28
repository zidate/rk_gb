# TerminalRequirements

## 目的
将《中国移动定制版终端需求白皮书（2023 v1.0）》和 `测试用例.xlsx` 沉淀为终端侧产品需求基线，作为 `GB/T 28181-2016` 之外的项目开发、联调、审核和验收依据。

## 模块概述
- **职责:** 维护终端侧产品需求、测试用例、当前实现之间的对齐关系
- **状态:** ✅已建立基线
- **适用范围:** 注册重定向、注册/保活、实时点播、终端控制、语音广播/对讲、视频参数/图像参数、设备信息查询、远程升级等终端侧能力
- **最后更新:** 2026-03-27

## 输入基线
- **产品需求:** `/home/jerry/silver/终端需求白皮书.pdf`
- **测试用例:** `/home/jerry/silver/测试用例.xlsx`
- **实现入口:** `rk_gb/App/Protocol/ProtocolManager.*`、`rk_gb/App/Protocol/config/ProtocolExternalConfig.h`、`rk_gb/App/Protocol/gb28181/GB28181BroadcastBridge.*`、`rk_gb/third_party/platform_sdk_port/CommonFile/CommonLib/GB28181Defs.h`

## 对齐原则
- **国家标准基线:** 以 `GB/T 28181-2016` 为主，解决 SIP/SDP/RTP/MANSCDP 的标准语义。
- **产品定制基线:** 白皮书在国标之上补充中国移动终端接入、注册重定向、设备能力字段和验收口径。
- **实现事实基线:** 以当前仓库代码和 SDK 结构体为准，文档需要明确指出“已实现”“部分实现”“仅测试要求存在”三种状态。
- **验收基线:** `测试用例.xlsx` 是当前交付口径，不能只看代码或国标，不看用例。

## 需求-测试-实现对齐矩阵

| 需求域 | 白皮书依据 | 测试用例 | 当前实现 / 知识库映射 | 对齐结论 |
|--------|------------|----------|------------------------|----------|
| 注册重定向 | `4.7.2`、`A.16` | `2-3` | `ProtocolManager + GBClientImpl + SipEventManager` 已补齐 `StringCode/Mac/Line/CustomProtocolVersion` 扩展头、`401 -> 302 -> 正式平台注册 -> 401 -> 200` 单次事务和 `30s / 3次 / 1min` 外层重试 | ⚠️代码已落地，待专项联调确认 |
| 注册 / 鉴权 / 保活 | `4.7.3`、`4.9.1.4` | `4`、`45` | `GbRegisterParam`、`GbKeepaliveParam`、注册/保活主链已存在，默认心跳参数 `60s / 3次` 与白皮书一致 | ✅基础能力已对齐 |
| 实时视音频点播 | `4.7.4` | `5-8` | `HandleGbLiveStreamRequest -> GB28181RtpPsSender` 已形成点播闭环 | ⚠️能力已具备，但白皮书要求同平台注册平台不少于 `3` 路视频流，当前 `GbMultiStreamParam.stream_count` 默认仅 `2` |
| 终端设备控制 | `4.7.5` | `15-21` | `HandleGbDeviceControl` 覆盖 PTZ、布防/撤防、报警复位、参数配置、升级、重启等入口 | ⚠️审核时需区分“无需应答”和“必须回执执行结果”的控制类型 |
| 语音广播 | `4.7.6.1`、`A.1`、`A.14`、`A.15` | `22-23` | `GB28181BroadcastBridge` 已支持 `UDP / TCP active / TCP passive` 协商；`gb_broadcast.transport` 默认 `tcp`，`BuildGbBroadcastMediaInfo` 回落到 `kRtpOverTcpActive` | ✅媒体协商能力已对齐，配置回显仍有缺口 |
| 语音对讲 | `4.7.6.2` | `24` | 白皮书定义“语音对讲 = 实时点播 + 语音广播”组合流程；当前 App 层仍以广播桥 + 监听桥组合完成，不应简单等同独立双向对讲协议栈 | ⚠️能力模型已说明，联调时需按组合流程验证 |
| 图像 / OSD / 视频参数 / 夜视 | `4.7.7`-`4.7.16`、`A.13`-`A.15` | `25-35`、`39-47` | `gb_video`、`gb_image`、`gb_osd`、配置查询/配置应用链路已存在；平台实测的画面反转口径已按 `FrameMirror(0/1/2/3)` 对齐，并兼容旧 `ImageFlip` | ⚠️部分对齐，`44` 明确写明“暂未做音频配置”，`46` 写明“国标文档没有提到可查询配置” |
| 视频告警 | `4.7.15`、`A.11` | `36-37` | 测试用例已引用白皮书 `A.11`；知识库已补齐字段级审核清单，代码已接通本地视频报警最小闭环，但设备类扩展告警仍未实现 | ⚠️部分对齐，仍需补设备类告警事件源 |
| 设备信息查询 | `4.7.17`、`A.19` | `38` | `ResponseGbQueryDeviceInfo` 已回填基础字段、`StringCode/Mac/Line/CustomProtocolVersion`，并通过 XML 嵌套节点回 `DeviceCapabilityList/ProtocolFunctionList` 的最小真实能力集 | ⚠️结构已补齐，仍需平台核对语义 |
| 多码流 | `4.7.9` | `43` | 白皮书要求三码流或以上；当前配置模型仅有 `GbMultiStreamParam.stream_count=2`，且未见完整对外配置入口 | ❗与产品要求不一致 |
| 远程升级 | `4.6.5`、`A.12` | `48` | `HandleGbDeviceUpgradeControl`、包下载校验、升级结果通知、重启后补报链路已存在 | ✅实现链路完整 |

## 测试覆盖总结

### 已有明确测试覆盖
- 注册重定向、正式注册、实时点播、云台控制、语音广播、语音呼叫
- OSD、码率、分辨率、帧率、视频编码、夜视、画面翻转
- 设备信息查询、多码流地址获取、远程升级

### 已知部分覆盖或空白
- `VoiceFlowMode=1` 的 `TCP active` 广播没有独立显式用例，应在公网联调场景单独验证
- 白皮书 `A.16` 中注册重定向失败后的重试节奏、重新获取重定向地址的闭环，当前用例未覆盖失败重试次数与间隔
- `A.14/A.15` 扩展的 `LocalPort/Domain/SipIp/SipPort/SipId/Password/ChannelList` 没有在当前用例中逐项核验
- `A.19` 扩展字段和能力节点已经代码落地，但现有用例仍缺少逐字段断言与能力字符串语义校验

## 当前明确缺口
- `CfgBasicParam` / `BasicSetting` 只覆盖 `DeviceName`、`Expiration`、`HeartBeatInterval`、`HeartBeatcount`，没有承载白皮书扩展的 `VoiceFlowMode`、`LocalPort`、`Domain`、`SipIp`、`SipPort`、`SipId`、`Password`、`ChannelList`
- `GbMultiStreamParam.stream_count` 默认值为 `2`，与白皮书 `4.7.9` “三码流或以上”不一致；当前 `DeviceInfo` 已如实回 `2/Defect:StreamNum2`
- `A.19` 设备信息查询虽然已经补齐结构与最小能力清单，但平台是否强校验更完整的附录 `G` 子项仍待确认
- 注册重定向与 `DeviceInfo` 扩展目前只做了代码级和翻译单元级编译验证，仍需真实平台抓包核对

## 字段级审核清单

### A.11 终端设备告警类型扩展

| 检查项 | 白皮书要求 | 当前代码事实 | 审核结论 |
|--------|------------|--------------|----------|
| 告警上报方式 | 增加终端设备掉电、固件升级失败、文件上传失败告警，并采用“无应答响应方式”进行交互 | SDK 存在 `NotifyDeviceAlarm/NotifyDeviceAlarmForSub`；`NotifyDeviceAlarmForSub` 走 `Notify(..., 0, NULL)`，没有业务层等待 | ⚠️接近白皮书口径，但仍需平台联调确认 SIP 层是否只要求标准 `200 OK` |
| 告警结构承载能力 | 需携带 `AlarmPriority/AlarmMethod/AlarmType/AlarmTime/AlarmDescription/Info/ExtraInfo` | `AlarmNotifyInfo` 已补齐 `AlarmTypeParam`，XML `Alarm` 已能输出 `Info/AlarmTypeParam` 与 `Info/ExtraInfo`；但 `Info -> AlarmTypeParam -> EventType` 仍不是显式结构化字段 | ⚠️最小承载闭环已具备，设备侧细分语义目前仍通过扩展字符串表达 |
| 告警类型编码 | `A.11.2` 表中设备报警新增 `900/901/902/903/904`；`A.11.3` 注释又写成 `6/7/8/9/10` | `GB28181Defs.h` 的 `AlarmTypeDevice` 枚举只有 `1-5` 基础设备报警码 | ❗白皮书内部存在编号不一致，且当前枚举未覆盖扩展码，联调前必须先确认平台采用哪套编号 |
| 告警源事件接线 | 应能够实际把终端设备告警上报给平台 | 当前 `CAlarm::ThreadProc -> ProtocolManager::SyncLocalVideoAlarm -> NotifyGbAlarm` 已接通本地视频报警最小闭环；但掉电、升级失败、文件上传失败等设备类扩展告警仍未接线 | ⚠️已从“完全未接线”提升到“部分实现”，仍需补设备类真实事件源 |
| 报警复位语义 | 白皮书要求设备报警后平台应执行报警复位；但“升级失败”和“文件上传失败”无需报警复位 | `HandleGbAlarmResetControl` 现在会同时清理报警录像标志和 `ProtocolManager` 内 GB 报警状态缓存，但仍没有按告警类型区分“无需复位”例外 | ⚠️最小复位闭环已补齐，白皮书例外语义仍待细化 |

#### A.11 联调核对点
- 先和平台确认扩展设备报警编码到底采用 `900-904` 还是 `6-10`，不要默认按任一版本实现
- 若平台要求 `Info/AlarmTypeParam/EventType` 结构化输出，仍需确认当前 `AlarmTypeParam/ExtraInfo` 字符串口径是否足够，还是要继续扩展 SDK 结构体
- 若要验收 `903 终端设备断电`、`904 文件上传失败` 等设备类扩展告警，仍需补真实设备事件到 `NotifyGbAlarm` 的上报接线

### A.16 注册重定向

| 检查项 | 白皮书要求 | 当前代码事实 | 审核结论 |
|--------|------------|--------------|----------|
| 首次重定向注册身份 | 注册前无 SIPID，`From/To` 的 SIPID 位置填写 `StringCode` | `ProtocolManager + GBClientImpl + SipClientImpl` 已在宏开启时把首次零配置 `REGISTER` 的本地身份切到 `StringCode`，收到 `302` 后再切到平台返回的 `deviceId` | ⚠️代码已落地，待抓包确认 |
| 首次 REGISTER 扩展头 | 需携带 `Mac/StringCode/Line/Manufacturer/Model/Name/CustomProtocolVersion` | `SipEventManager::SetZeroConfigRegisterHeaders()` 已补齐这些扩展头，只在宏开启且零配置首包时发送 | ⚠️代码已落地，待抓包确认 |
| 鉴权流程 | `REGISTER -> 401 -> Authorization REGISTER -> 302 Moved`，正式平台注册后再次 `401/Authorization/200 OK` | `GBClientImpl::Register()` 已显式实现单次零配置事务：`401 -> 302 -> 正式平台注册 -> 401 -> 200` | ⚠️代码已落地，待联调确认 |
| 重试策略 | 重定向后注册失败睡眠 `30s` 重试；失败 `3` 次后睡眠 `1min` 并重新获取重定向地址；未注册成功不得中断 | `ProtocolManager::GbHeartbeatLoop()` 已实现正式平台注册失败 `30s` 重试、连续 `3` 次失败后 `1min` + `ResetZeroConfigState()` 重新获取重定向地址 | ⚠️代码已落地，待联调确认 |

#### A.16 报文字段最小核对清单
- 第一次重定向 REGISTER：`From/To/Contact/CSeq/Call-ID/Via/Expires`
- 白皮书扩展头：`Mac`、`StringCode`、`Line`、`Manufacturer`、`Model`、`Name`、`CustomProtocolVersion`
- 第二次 Authorization REGISTER：确认鉴权后收到 `302 Moved`
- 对正式平台注册的 REGISTER：确认身份已切换到正式 `deviceId`
- 失败路径：确认 `30s` / `3次` / `1min` / “重新获取重定向地址” 这四个状态点都存在

### A.19 设备信息查询应答与附录 G 能力清单

| 检查项 | 白皮书要求 | 当前代码事实 | 审核结论 |
|--------|------------|--------------|----------|
| 基础设备信息 | `DeviceName/Result/Manufacturer/Model/Firmware/Channel` | `ResponseGbQueryDeviceInfo` 已回填这些基础字段 | ✅基础字段已具备 |
| 扩展身份字段 | `StringCode/Mac/Line/CustomProtocolVersion` 必选 | `DeviceInfo` 结构体、`device_info` XML 和 `ResponseGbQueryDeviceInfo` 已补齐这些字段 | ✅结构已具备 |
| 设备能力清单 | `DeviceCapabilityList` 至少承载 `PositionCapability/AlarmOutCapability/MICCapability/SpeakerCapability/ImagingCapability/StreamPeramCapability/SupplyLightCapability` 等 | 当前已按嵌套 XML 节点回最小设备能力子集，并以 `0/1/2 + Defect:/Type:` 语义编码 | ✅最小闭环已具备 |
| 协议功能清单 | `ProtocolFunctionList` 应能表达 `BroadcastCapability/ROICapability/FrameMirrorCapability/MultiStreamCapability/OSDCapability/DeviceUpgradeCapability/AlarmCapability/BroadcastTcpActiveCapability` 等 | 当前已回最小协议功能子集：广播、图像翻转、多码流、OSD、升级、告警、广播 `TCP active` | ✅最小闭环已具备 |
| 多码流能力声明 | 附录 `G` 明确“码流数少于 3 个”属于缺陷 `StreamNum` | 当前 `GbMultiStreamParam.stream_count=2`，`DeviceInfo` 已回 `2/Defect:StreamNum2` | ⚠️已按真实缺陷回报，产品能力仍不达标 |
| TCP 主动广播能力声明 | 附录 `G` 单独列出 `BroadcastTcpActiveCapability` | 当前广播链路已支持 `TCP active`，`DeviceInfo` 已显式回该能力 | ✅能力回报已具备 |
| 告警能力声明 | 附录 `G` 要求 `AlarmCapability` 能描述 `NotAlarmReset`、`NotIndependentAlarm` 等缺陷 | 当前 `DeviceInfo` 已回 `2/Defect:NotIndependentAlarm`；设备类扩展告警事件源和更细粒度缺陷仍未补齐 | ⚠️最小缺陷表达已具备 |

#### A.19 / 附录 G 当前项目最相关能力项
- `BroadcastCapability`: 应回答语音广播是否支持、是否存在远程不可控缺陷
- `FrameMirrorCapability`: 对应当前图像翻转能力和测试用例 `39-42`
- `MultiStreamCapability`: 对应白皮书三码流要求和测试用例 `43`
- `OSDCapability`: 对应 OSD 测试用例 `25-27`
- `DeviceUpgradeCapability`: 对应远程升级测试用例 `48`
- `AlarmCapability`: 对应 `A.11` 告警扩展及 `36-37`
- `BroadcastTcpActiveCapability`: 对应公网场景下的 `VoiceFlowMode=1`

## 白皮书内部待确认项
- `A.11.2` 的设备报警扩展码写成 `900-904`，而 `A.11.3` 报文注释写成 `6-10`，两处冲突
- `A.15` 中 `VoiceFlowMode` 说明写成 `0=UDP，1=TCP`，而 `A.14` 写成 `0=UDP，1=TCP主动拉流`，联调应以 `A.1` 的“TCP 主动拉流模式语音广播”作为最终口径
- `A.19` 中 `DeviceCapabilityList` 和 `ProtocolFunctionList` 已按最小真实子集落地，但平台是否要求更完整的附录 `G` 子项、以及是否严格校验节点顺序和缺陷语义，仍需联调确认

## 联调优先级
- `P0`: 注册重定向首包是否携带 `Mac/StringCode/CustomProtocolVersion`，以及 `From/To` 是否使用 `StringCode`
- `P0`: 广播公网场景是否按 `TCP active` 协商成功，平台返回的媒体 `IP/PORT` 是否被设备正确采用
- `P0`: 设备信息查询应答是否被平台强校验 `StringCode/Mac/Line/CustomProtocolVersion/DeviceCapabilityList`
- `P1`: 告警扩展编码到底采用 `900-904` 还是 `6-10`
- `P1`: 多码流能力是否以白皮书“三码流及以上”为最终验收口径

## 代码整改清单

- 2026-03-27: `A.19` 设备信息扩展和最小能力清单已完成代码落地，当前剩余重点转为平台抓包确认和更完整附录 `G` 子项的必要性确认。
- 2026-03-26: “注册重定向扩展头与身份切换”和“注册重定向重试状态机”两项已完成代码落地，当前剩余重点转为抓包联调确认和 `A.19` 设备信息扩展补齐。

### P0 | 必须优先整改

| 整改项 | 目标 | 影响文件 | 验证方式 |
|--------|------|----------|----------|
| 注册重定向扩展头与身份切换 | 首次重定向 `REGISTER` 带 `Mac/StringCode/Line/Manufacturer/Model/Name/CustomProtocolVersion`，且 `From/To` 使用 `StringCode`；正式平台注册后再切回 `deviceId` | `rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/GB28181Client/GBClientImpl.cpp`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp`、`rk_gb/App/Protocol/config/ProtocolExternalConfig.h`、`rk_gb/App/Protocol/config/HttpConfigProvider.cpp`、`rk_gb/App/Protocol/config/LocalConfigProvider.cpp` | 抓包比对白皮书 `A.16` 的“请求消息1/2”和正式平台注册报文 |
| 注册重定向重试状态机 | 落实“失败 `30s` 重试，失败 `3` 次后 `1min` 并重新获取重定向地址”的闭环 | 优先定位 `GB28181` 注册生命周期实现，候选入口：`rk_gb/App/Protocol/ProtocolManager.cpp`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/GB28181Client/GBClientImpl.cpp` | 人工构造 401/302/注册失败场景，观察日志与抓包中的重试节奏 |
| 设备信息查询应答扩展 | 已于 `2026-03-27` 完成代码落地：`StringCode/Mac/Line/CustomProtocolVersion` 和最小能力清单已补到 `DeviceInfo` 应答 | `rk_gb/third_party/platform_sdk_port/CommonFile/CommonLib/GB28181Defs.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/device_info.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/device_info.cpp`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp`、`rk_gb/App/Protocol/ProtocolManager.cpp` | 触发 `DeviceInfo` 查询，核对白皮书 `A.19` 必填扩展字段和 XML 结构 |
| 能力清单最小闭环 | 已于 `2026-03-27` 完成代码落地：当前最少回 `BroadcastCapability`、`FrameMirrorCapability`、`MultiStreamCapability`、`OSDCapability`、`DeviceUpgradeCapability`、`AlarmCapability`、`BroadcastTcpActiveCapability` | 同上 `DeviceInfo` 编码链路，外加 `rk_gb/App/Protocol/config/ProtocolExternalConfig.h` 和运行时能力采集点 | 平台查询设备信息后核对 capability 字段；同时对照附录 `G` 能力字符串格式 |
| 基础参数查询/配置扩展 | 把 `VoiceFlowMode/LocalPort/Domain/SipIp/SipPort/SipId/Password/ChannelList` 纳入查询/配置闭环，`VoiceFlowMode` 映射到广播 `UDP/TCP active` | `rk_gb/third_party/platform_sdk_port/CommonFile/CommonLib/GB28181Defs.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/configdownload_response.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/configdownload_response.cpp`、`rk_gb/App/Protocol/ProtocolManager.cpp`、`rk_gb/App/Protocol/config/ProtocolExternalConfig.h`、`rk_gb/App/Protocol/config/HttpConfigProvider.cpp`、`rk_gb/App/Protocol/config/LocalConfigProvider.cpp` | 平台下发 `DeviceConfig`、设备回 `ConfigDownload`，核对 `A.14/A.15` 扩展字段完整性 |

### P1 | 第二阶段整改

| 整改项 | 目标 | 影响文件 | 验证方式 |
|--------|------|----------|----------|
| 告警扩展结构化 | 明确扩展报警编码口径，并补齐 `AlarmTypeParam/EventType/ExtraInfo` 的结构化承载 | `rk_gb/third_party/platform_sdk_port/CommonFile/CommonLib/GB28181Defs.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/alarm_notify.h`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/alarm_notify.cpp`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp` | 触发扩展告警，检查 `Alarm/Info/AlarmTypeParam/EventType/ExtraInfo` 是否满足平台要求 |
| 告警事件源接线 | 把升级失败、文件上传失败、断电等真实设备事件接到 `NotifyGbAlarm` | `rk_gb/App/Protocol/ProtocolManager.cpp` 及对应的升级/上传/设备状态模块 | 真实或模拟设备事件触发后，确认平台能收到告警并按白皮书语义处理 |
| 告警复位例外语义 | “升级失败/文件上传失败无需复位”，其余设备报警仍需复位 | `rk_gb/App/Protocol/ProtocolManager.cpp` | 触发不同类型告警后验证平台复位和设备重新上报行为 |
| 多码流能力补齐 | 将 `GbMultiStreamParam.stream_count`、独立参数设置和能力回报对齐到“三码流及以上” | `rk_gb/App/Protocol/config/ProtocolExternalConfig.h`、`rk_gb/App/Protocol/config/HttpConfigProvider.cpp`、`rk_gb/App/Protocol/config/LocalConfigProvider.cpp`、实时流选择逻辑相关文件 | 对照测试用例 `43` 和附录 `G` 的 `MultiStreamCapability` 字符串 |

### P2 | 收尾与回归

| 整改项 | 目标 | 影响文件 | 验证方式 |
|--------|------|----------|----------|
| 配置来源补齐 | 所有白皮书新增字段都能从本地配置/HTTP 配置拿到 | `ProtocolExternalConfig.h`、`HttpConfigProvider.cpp`、`LocalConfigProvider.cpp` | 查询配置接口和本地配置文件回显一致 |
| 联调抓包模板 | 为 `A.11/A.16/A.19` 固化最小抓包检查点和日志关键字 | 知识库文档优先，必要时补测试脚本 | 联调时直接按清单逐项勾选 |

## 建议实施顺序
- 第一步: 先和平台确认 `A.11` 扩展报警编码、`A.19` 能力清单是否强校验、`A.16` 注册重定向头字段是否严格校验
- 第二步: 优先改 `DeviceInfo`、`BasicParam`、注册重定向链路，因为这三块决定联调是否能继续推进
- 第三步: 再补告警扩展和多码流能力，这两块更依赖平台口径和真实设备事件源

## 实施前置条件
- 需要平台抓包样例，至少包含：重定向 `REGISTER`、设备信息查询、基础参数查询/配置、扩展告警
- 需要先确认白皮书冲突项最终口径，否则 `AlarmType` 和 `VoiceFlowMode` 容易改错
- `DeviceInfo`、`CfgBasicParam`、`AlarmNotifyInfo` 都是 SDK 公共结构体，改动前要评估对现有 XML 编码/解码链路的兼容性

## 开发 / 审核清单
- 任意 GB28181 改动都要同时对照 `GB/T 28181-2016`、本白皮书和 `测试用例.xlsx`
- 公网广播场景优先核对平台 `INVITE/应答` 返回的媒体 `IP/PORT`，`VoiceFlowMode=1` 时应走 `TCP active`
- 审核 `DeviceConfig` 和 `ConfigDownload` 时，不要只看当前 App 层已支持字段，还要对照白皮书扩展字段是否被忽略
- 审核 `DeviceInfo` 应答时，不能只确认“有回包”，还要确认白皮书扩展字段是否满足平台验收口径
- 多码流相关改动必须核对白皮书要求的码流数量，不要仅以当前默认配置值为准
- 审核告警改动时，必须同时核对“扩展编码是否已和平台对齐”“是否需要报警复位”“是否已接入真实设备事件源”

## 变更历史
- 2026-03-27: 补齐 `DeviceInfo` 的 `A.19` 扩展身份字段与最小能力清单，更新 `A.19/附录 G` 的代码事实和剩余缺口判断
- 2026-03-23: 继续补充代码整改清单，按优先级拆出影响文件、整改目标和验证路径
- 2026-03-23: 继续细化 `A.11`、`A.16`、`A.19` 和附录 `G` 的字段级审核清单，补充白皮书内部不一致项和联调优先级
- 2026-03-23: 基于 `终端需求白皮书.pdf` 和 `测试用例.xlsx` 建立终端需求-测试-实现对齐基线
