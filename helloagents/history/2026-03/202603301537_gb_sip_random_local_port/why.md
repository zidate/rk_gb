# 需求说明: GB28181 SIP 本地监听端口随机化

## 背景
- 用户要求把设备侧 GB28181 SIP 客户端的本地监听端口改为随机端口，原因是前置 NAT 设备对外映射行为异常，继续固定复用单一端口容易与既有映射策略冲突。
- 当前代码把 `gb_register.server_port` 同时当作“远端平台 SIP 端口”和“本地 SIP 监听端口”使用：
  - `ProtocolManager` 启动 GB28181 SDK 时把 `m_cfg.gb_register.server_port` 传给 `GB28181ClientSDK::Start(...)` 作为本地绑定端口。
  - `SipSDK` / `eXosip` 注册报文中的 `From/Contact` 也沿用这一路本地端口字段。
- 直接把本地绑定端口改成 `0` 还不够。当前 `ClientInfo::SetFromeAndTo()` 会把缓存的 `LocalPort` 写进 `From/Contact`，如果不把实际随机监听端口回写，注册消息可能带出 `:0` 或错误端口。

## 目标
- 让设备侧 SIP 客户端启动时不再固定绑定 `5060` / `gb_register.server_port`，而是使用内核分配的随机本地端口。
- 保持远端平台注册目标 `server_ip/server_port` 语义不变，只拆开“远端连接端口”和“本地监听端口”的运行时行为。
- 确保 `REGISTER` / `MESSAGE` / `INVITE` 等请求中的 `From/Contact` 使用真实监听到的本地端口，避免随机监听后报文仍携带旧端口或 `0`。

## 变更内容
1. 调整 `ProtocolManager` 启动 GB28181 SDK 时的本地 SIP 绑定端口策略，取消对 `gb_register.server_port` 的复用，改为请求随机本地端口。
2. 在 `SipEventManager` 中补齐随机监听端口场景下的真实端口获取与缓存逻辑，保证 `m_local_port` 为实际绑定结果。
3. 在 `SipClientImpl/Register` 等构包入口同步运行态本地端口，确保 `ClientInfo::from/contact` 与实际监听端口一致。

## 影响范围
- **模块:** `ProtocolManager`、`SipSDK`、GB28181 注册链路
- **文件:** `rk_gb/App/Protocol/ProtocolManager.cpp`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/common/SipEventManager.*`、`rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/client/SipClientImpl.cpp`
- **配置语义:** `gb_register.server_port` 继续表示远端平台 SIP 端口；本次不新增新的本地 SIP 端口配置字段
- **协议行为:** 注册、保活、请求发起逻辑不变，但本地 SIP socket 的监听端口改为随机分配

## 核心场景

### 需求: 设备注册时使用随机本地 SIP 端口
**模块:** GB28181 / SIP
设备启动 GB28181 客户端时，本地 SIP socket 不再固定绑定平台端口或 `5060`，而应由系统分配随机可用端口。

#### 场景: 启动 SIP 监听
前置条件:
- `ProtocolManager::StartGbClientLifecycle()` 启动 GB28181 SIP 客户端
- `gb_register.server_ip/server_port` 已配置为平台地址
- 预期结果1: SIP 监听 socket 绑定到随机本地端口
- 预期结果2: 远端平台注册仍发往 `gb_register.server_ip:gb_register.server_port`

### 需求: 注册报文回填真实本地端口
**模块:** SipSDK
随机监听端口启用后，后续 `REGISTER` / 出站 SIP 报文中的 `From/Contact` 必须携带真实本地监听端口，而不是 `0` 或旧配置值。

#### 场景: 生成初始 REGISTER
前置条件:
- SIP 监听已启动并拿到实际本地端口
- `ClientInfo::SetFromeAndTo()` 将被用于生成 `from/contact`
- 预期结果1: `from/contact` 中端口与监听 socket 实际端口一致
- 预期结果2: 不影响鉴权、零配置重定向和后续保活重注册

## 风险评估
- **风险:** 仅把绑定端口改成 `0` 会导致 `ClientInfo` 继续携带旧端口或 `0`，从而生成错误的 `Contact`
- **缓解:** 在 `SipEventManager` 成功启动监听后立即同步实际本地端口，并在 `Register` 入口再次覆盖 `ClientInfo.LocalPort`
- **风险:** 随机端口监听若引入“先探测再绑定”的两阶段逻辑，存在极小的端口竞争窗口
- **缓解:** 当前实现采用“临时 socket `bind(0)` 预取可用端口，再立即交给 `eXosip_listen_addr(actualLocalPort)` 建立正式监听”的最小侵入方案，并同步运行态回写实际本地端口；虽然仍保留一个很小的竞争窗口，但比直接改造 `eXosip` 已有监听链路更稳妥
