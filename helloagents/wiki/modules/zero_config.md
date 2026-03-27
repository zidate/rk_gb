# ZeroConfig

## 目的
将《中国移动定制版终端需求白皮书》中“零配置 / 注册重定向”相关要求抽成独立专题，作为装维、平台联调和设备侧实现的直接检查依据。

## 模块概述
- **职责:** 维护零配置定义、装维流程、出厂预配置、注册重定向协议要求和验收要点
- **状态:** ✅已建立专题
- **适用范围:** 装维扫码绑定、首次上电自动接入、重定向注册、正式平台注册、二维码/串码/CMEI 标识
- **最后更新:** 2026-03-27

## 输入基线
- **产品需求:** `/home/jerry/silver/需求白皮书.pdf`
- **关键章节:** `4.7.1`、`4.7.2`、`4.7.3`、`4.9.1.5`、`4.9.1.6`、`A.16`、`B.3.2`
- **关联文档:** `helloagents/wiki/modules/terminal_requirements.md`、`helloagents/wiki/modules/gb28181.md`

## 定义澄清
- **零配置的真实含义:** 现场装维人员不需要登录设备 Web 页面手动录入平台参数，完成扫码、绑定、上电、联网后，设备即可自动完成平台注册闭环。
- **不等于完全无预配置:** 设备出厂前必须预置重定向接入所需参数和默认能力，否则现场无法实现“零配置”。
- **适用前提:** 白皮书明确要求出厂版本只能使用中国移动视频云定制版平台进行终端配置，不允许改由第三方平台完成配置。

## 参与方

| 参与方 | 职责 |
|--------|------|
| 装维 APP | 获取工单、扫码设备、把工单与设备串码绑定到视频云平台 |
| 终端设备 | 上电后自动发起重定向注册，并在收到正式接入信息后完成平台注册 |
| 视频云平台 | 保存工单与设备绑定关系，对重定向请求做鉴权，并下发正式注册信息 |
| SIP 重定向服务器 | 处理首次注册鉴权，返回 `302 Moved` 和正式平台地址 |
| 正式 SIP 服务器 | 接收设备使用 `deviceId` 发起的正式注册并完成鉴权 |

## 零配置业务闭环
1. 装维人员在装维 APP 中获取装维订单。
2. 装维 APP 扫描终端机身二维码，读取终端设备串码。
3. 装维 APP 将工单信息和扫码得到的串码发送给视频云平台，完成绑定。
4. 装维人员给设备接电、插网线；设备自动向视频云平台发起重定向注册。
5. 视频云平台完成重定向鉴权后，下发真实的视频云平台注册信息。
6. 设备拿到正式平台注册信息后，向正式网关完成注册。

## 出厂前必须具备的能力与预配置

### 默认能力

| 项目 | 白皮书要求 | 落地含义 |
|------|------------|----------|
| 省级中心平台地址 | 终端设备出厂时应默认配置 | 设备至少要有一组启动引导地址，不能依赖现场人工录入 |
| 自动获取 IP | 终端设备出厂时应默认支持 | 设备应支持 DHCP，接网即可发起后续注册流程 |
| 机身二维码 | 终端设备出厂时应默认支持携带二维码 | 装维 APP 能通过扫码拿到设备身份标识 |

### 软件预配置

| 项目 | 来源章节 | 说明 |
|------|----------|------|
| `StringCode` / 串码 | `4.7.2` | 首次重定向注册的设备身份基础字段 |
| 重定向服务器 ID | `4.7.2` | 首次 REGISTER 的目标服务器标识 |
| 重定向服务器域 | `4.7.2` | 重定向接入域信息 |
| 重定向服务器地址 / 端口 | `4.7.2` | 设备首次接入时的网络连接目标 |
| 重定向服务器编码 `SIPID` | `4.7.2` | 注册报文中的目标平台标识 |
| 国标接入口令 | `4.7.2` | 首次重定向鉴权所需的预置口令 |

### 机身标识与二维码
- `4.7.1` 表述二维码包含“串码”。
- `B.3.2` 要求机身二维码需包含 `CMEI` 和 `MAC`。
- `4.9.1.5 / 4.9.1.6` 与附录接口定义说明：对中国移动定制非蜂窝终端，`StringCode` 取值就是 `CMEI`。因此该类设备落地时应保证二维码至少能解析出 `CMEI/StringCode`，并满足机身标签对 `MAC` 的要求。

## 注册重定向协议要求

### 协议主链
1. 设备使用 `StringCode + 预置口令` 向 SIP 重定向服务器发起首次 `REGISTER`。
2. 服务器返回 `401 Unauthorized`，设备携带 `Authorization` 再次发起 `REGISTER`。
3. 鉴权通过后，服务器返回 `302 Moved`，并带上正式服务器的 `Contact`、`ServerDomain`、`ServerId`、`ServerIp`、`ServerPort`、`deviceId`。
4. 设备切换到正式服务器，使用 `deviceId` 再次发起 `REGISTER`。
5. 正式服务器再次走 `401 -> Authorization REGISTER -> 200 OK`，注册完成。

### 报文字段要求
- 重定向过程中，SIP 请求头必须携带 `Mac` 和 `StringCode`。
- 白皮书示例进一步要求同时携带 `Line`、`Manufacturer`、`Model`、`Name`、`CustomProtocolVersion`。
- 在首次重定向注册阶段，设备尚未获得正式 `SIPID`，`From/To/Contact` 中的身份位置应填写 `StringCode`。
- 在收到 `302 Moved` 后，设备应切换为平台返回的 `deviceId`，并以该身份完成正式注册。

### 重试与恢复策略
- 重定向后若正式注册失败，应睡眠 `30 秒` 后重试。
- 若连续失败达到 `3 次`，应睡眠 `1 分钟`，重新发起重定向请求，重新获取最新地址后再注册。
- 设备在未注册成功前不得中断注册尝试。
- 白皮书要求该流程发生在设备首次上电和设备断电重启之后。

### 手工兜底方式
- 默认接入方式是重定向注册。
- 同时允许通过设备 Web 界面登录后，手工录入方式接入平台，作为兜底方案。

## 当前代码落点
- `gb28181.ini` 新增 `register_mode=standard|zero_config` 作为运行时模式开关；`zero_config.ini` 继续只承载 `StringCode/Mac/Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model` 这些零配置专属入口字段，不承担模式判定职责，也不保存 `302` 返回的正式平台 `ServerIp/ServerPort/ServerDomain/ServerId/deviceId`。
- `register_mode=zero_config` 时，`LocalConfigProvider` / `HttpConfigProvider` 会校验 `StringCode`、`redirect_server_id` 等关键字段，且本地 flash 中 `/userdata/conf/Config/GB/zero_config.ini` 缺失时会直接记录日志并返回错误，不做兼容迁移；`register_mode=standard` 时忽略该文件缺失。
- `SipEventManager` 已在首次零配置 `REGISTER` 时补齐 `Mac/StringCode/Line/Manufacturer/Model/Name/CustomProtocolVersion` 扩展头，并解析 `302` 返回的 `Contact/ServerDomain/ServerId/ServerIp/ServerPort/deviceId`。
- `GBClientImpl::Register` 已实现单次零配置事务：`StringCode -> 401 -> 302 -> 正式平台注册 -> 401 -> 200`，且只会在 `GBClientImpl` 进程内缓存正式平台目标，用于同一轮进程存活期间的后续直接重注册，不会写入 flash。
- `CGBClientImpl::Stop()` 会调用 `ResetZeroConfigState()` 清空这份正式平台内存缓存；进程退出或设备重启后同样会失效，下次启动零配置流程时，`ProtocolManager` 仍基于 flash 中保存的入口参数重新发起 `302` 获取正式平台注册信息。
- `ProtocolManager::GbHeartbeatLoop` 已实现外层重试节奏：正式平台注册失败 `30 秒` 重试，连续 `3 次` 失败后等待 `1 分钟` 并通过 `ResetZeroConfigState()` 重新获取重定向地址。
- `ProtocolManager::ResponseGbQueryDeviceInfo` 已补齐 `StringCode/Mac/Line/CustomProtocolVersion`，并按白皮书 `A.19/附录 G` 回最小真实能力节点：`DeviceCapabilityList` 与 `ProtocolFunctionList`。
- `register_mode=standard` 时仍走原有标准国标注册路径，不发送零配置扩展头，也不进入 `302` 重定向流程。

## 当前缺口
- `A.19` 虽已完成代码落地，但当前只回最小真实能力子集；平台若强校验更完整的 `DeviceCapabilityList/ProtocolFunctionList`，仍需按平台口径继续补字段。
- 现阶段只做了代码级和翻译单元级编译验证，仍需要真实平台抓包确认首次 `REGISTER` 扩展头、`302` 返回字段和 `deviceId` 身份切换完全符合白皮书。

## 设备侧实现清单

### P0 必须具备
- 能从本地配置中读取并持久化零配置入口参数；当前设备侧本地落盘拆成 `gb28181.ini` 保存通用注册字段和首次接入 `server_ip/server_port`，`zero_config.ini` 保存 `StringCode/Mac/Line/redirect_domain/redirect_server_id/CustomProtocolVersion/manufacturer/model`，`302` 返回的正式平台参数当前不落盘。
- 首次上电和重启后，自动走重定向注册闭环，无需人工点击。
- 首次 REGISTER 使用 `StringCode` 身份，并带齐 `Mac/StringCode/Line/Manufacturer/Model/Name/CustomProtocolVersion`。
- 能正确解析 `302 Moved` 返回的正式平台信息，并切换到 `deviceId` 完成正式注册；这组正式平台参数当前只允许进程内缓存，不写回 flash。
- 严格实现 `30 秒 / 3 次 / 1 分钟 / 重新获取重定向地址` 的重试状态机。

### P1 建议同步补齐
- 在日志中明确区分“重定向阶段”和“正式注册阶段”，方便联调抓包与定位。
- 对装维失败场景保留明确错误码或日志关键字，例如鉴权失败、未绑定、地址失效、口令不匹配。
- 让 Web 手工接入与自动重定向共用同一套配置模型，避免两套参数不一致。

## 平台与联调验收点

| 检查项 | 验收要点 |
|--------|----------|
| 工单绑定 | 装维 APP 扫码后，平台能完成工单与设备串码绑定 |
| 自动接入 | 设备接电插网后，无需进 Web 页面即可开始注册 |
| 重定向鉴权 | 平台能根据设备串码和预置口令做鉴权，只对已绑定设备下发正式注册信息 |
| 报文完整性 | 首次 REGISTER 中能看到 `Mac/StringCode/Line/Manufacturer/Model/Name/CustomProtocolVersion` |
| 身份切换 | `302` 前使用 `StringCode`，`302` 后使用 `deviceId` |
| 重试策略 | 注册失败时能看到 `30 秒重试`、`3 次失败后 1 分钟重取地址` |
| 重启恢复 | 设备断电重启后，会基于 flash 中保留的入口参数重新发起 `302`，并自动重走零配置闭环 |

## 容易混淆的点
- **“零配置”不是零预置。** 真正减少的是现场人工配置，而不是厂家出厂配置。
- **`StringCode` 与 `CMEI` 不总是同义。** 但对白皮书覆盖的中国移动定制非蜂窝终端，附录已明确 `StringCode = CMEI`。
- **二维码字段存在两处表述。** `4.7.1` 强调“二维码包含串码”，`B.3.2` 强调“二维码需包含 CMEI、MAC”；结合附录接口定义，项目实现时应覆盖 `CMEI/StringCode + MAC`。
- **`302` 返回的正式平台信息当前不会持久化。** 这部分数据只缓存在 SDK 进程内存中；`Stop`、进程退出或设备重启后会清空，下次仍需重新通过重定向获取。

## 开发 / 审核建议
- 任何“零配置”联调都不要只看注册是否成功，还要核对首次 REGISTER 的扩展头和身份切换。
- 若平台抓包里没有 `302 Moved`、`deviceId` 返回和正式二次注册，不能判定为满足白皮书要求。
- 若设备需要现场人工输入平台地址、SIPID 或密码，就不应再宣称符合该白皮书的“零配置”。
- 若后续要改动注册链路，应同时回看 `helloagents/wiki/modules/terminal_requirements.md` 中的整体需求矩阵与 `helloagents/wiki/modules/gb28181.md` 中的实现映射。

## 变更历史
- 2026-03-27: 明确零配置 `302` 返回的正式平台参数只保存在 SDK 进程内存，不写入 flash；`Stop` / 进程重启 / 设备重启后清空，下次启动重新通过重定向获取
- 2026-03-27: 将“标准国标 / 零配置”切换方式从编译期开关改为 `gb28181.ini::register_mode` 运行时控制，`zero_config.ini` 只保留零配置扩展字段
- 2026-03-27: 将零配置字段从 `gb28181.ini` 拆到独立 `zero_config.ini`；宏开启且文件缺失时直接记录日志并返回错误，不做兼容迁移
- 2026-03-27: 补齐 `DeviceInfo` 的 `A.19` 扩展身份字段和最小能力清单节点，按真实实现回报 `FrameMirror/MultiStream/Upgrade/Alarm` 等缺陷
- 2026-03-26: 补齐 `PROTOCOL_ENABLE_GB_ZERO_CONFIG` 编译期开关、零配置配置模型、`302` 重定向注册事务和 `30s / 3次 / 1min` 外层重试状态机，默认保持标准国标流程不变
- 2026-03-25: 新增零配置专题文档，沉淀定义澄清、装维闭环、预配置项、注册重定向协议要求和设备 / 平台验收清单
