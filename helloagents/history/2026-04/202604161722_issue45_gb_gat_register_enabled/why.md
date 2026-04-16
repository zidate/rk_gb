# 变更提案: issue45 GB/GAT 注册开关与停服语义收口

## 需求背景
issue 45 要求在 `feature/dg_ipc_replay_20260415` 分支补齐两条注册配置的启停控制语义：
1. `GatRegisterParam` 增加 `enabled`，开启时允许 1400 注册平台，关闭时不注册；若运行中已连上平台，再调用 `SetGatRegisterConfig()` 与 `RestartGatRegisterService()` 后应能注销并断开 1400 连接。
2. 复核 GB28181 现有 `GbRegisterParam.enabled` 是否满足同样语义：当平台已连上时，关闭 `enabled` 后再调用 `SetGbRegisterConfig()` 与 `RestartGbRegisterService()` 应注销并断开 GB28181 连接。

## 变更内容
1. 为 `GatRegisterParam`、`gat1400.ini` 和相关知识库补齐 `enabled` 字段与默认值。
2. 在 `ProtocolManager` 与 `GAT1400ClientService` 中把 1400 生命周期收口为“禁用时不启动 / 重载时先停服再按 enabled 决定是否重启”。
3. 对 GB28181 当前 `enabled` 关闭后的重载行为做代码级确认，并把该事实补进知识库与任务验证记录。

## 影响范围
- **模块:** `ProtocolManager`、`LocalConfigProvider`、`GAT1400ClientService`、协议配置模型
- **文件:** `App/Protocol/config/ProtocolExternalConfig.h`、`App/Protocol/config/LocalConfigProvider.*`、`App/Protocol/ProtocolManager.cpp`、`App/Protocol/gat1400/GAT1400ClientService.cpp`、知识库文档
- **运行态:** GB28181 / GAT1400 注册启停与重载语义

## 核心场景

### 需求: GAT1400 支持显式开关
**模块:** GAT1400 注册配置 / 运行态生命周期

#### 场景: 关闭 enabled 后注销 1400 平台连接
当 `gat1400.ini` 中 `enabled=0`，并调用 `RestartGatRegisterService()` 时：
- 若 1400 尚未启动，则只刷新缓存，不发起注册
- 若 1400 已启动或已注册，则先注销并停掉本地服务
- 后续 `ProtocolManager::Start()` 也不应再自动启动 1400 注册链路

### 需求: GB28181 关闭 enabled 后注销连接
**模块:** GB28181 注册配置 / 运行态生命周期

#### 场景: 关闭 enabled 后重载 GB28181
当 `gb28181.ini` 中 `enable=0`，并调用 `RestartGbRegisterService()` 时：
- 若 GB 生命周期正在运行，应先执行停服 / 注销
- 停服后不再重新注册
- 知识库需明确这一行为已经在代码中成立

## 风险评估
- **风险:** 给 GAT1400 增加 `enabled` 后，如果 `Start()`、`Reload()`、`ProtocolManager::Start()` 三处判断不一致，可能出现“关闭后仍被网络变化重新拉起”的回退。
- **缓解:** 在配置持久化、显式重载、启动入口三层统一按 `enabled` 判定，并把差异比较也纳入 `reloadGat`。
