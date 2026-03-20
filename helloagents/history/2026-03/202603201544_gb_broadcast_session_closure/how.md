# 技术设计

## 方案
1. 在 `ProtocolManager` 中新增 `GbBroadcastSession`，统一保存广播通知和音频广播会话的 `source/target`、句柄、协商地址、payload、codec 和 ACK 状态。
2. 把 `GB28181ClientReceiverAdapter::OnNotify()` 改为调用 `ProtocolManager::HandleGbBroadcastNotify()`，在这里完成 `TargetID` 与本机设备 ID 的匹配校验以及会话上下文落盘。
3. 在 `HandleGbAudioStreamRequest()` 中接入广播会话状态，遇到新广播覆盖旧会话时先复位广播桥，再重新应用新的 transport hint。
4. 调整 `BuildGbResponseMediaInfo()` 的 `kAudioStream` 分支，先按平台提供的对端地址解析本机出口 IP，再生成广播音频的应答 `MediaInfo`。
5. 扩展 `HandleGbStreamAck()` 和 `HandleGbStopStreamRequest()`，让 `ACK/BYE` 也纳入广播会话生命周期；`BYE` 后自动重启广播桥进入待机接收态。

## 影响范围
- `App/Protocol/ProtocolManager.h`
- `App/Protocol/ProtocolManager.cpp`
- `App/Protocol/gb28181/GB28181ClientReceiverAdapter.cpp`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
- `helloagents/wiki/modules/protocol.md`

## 风险与规避
- 风险: `BuildGbResponseMediaInfo()` 是 `const` 方法，新增读取广播会话状态时需要访问互斥锁。
- 规避: 将广播会话互斥锁改为 `mutable`，只保护只读快照，不改动对外行为。
- 风险: `BYE` 后直接 `StopSession()` 会把后续广播能力一并停掉。
- 规避: 在设备仍处于 `Start()` 生命周期内且属于单次广播 `BYE` 的情况下，立即按当前配置重启广播桥，恢复待机态。
- 风险: 平台不发 `notify` 而直接发 `INVITE` 时，广播仍需可用。
- 规避: 音频 `INVITE` 接收链路不强依赖前置 `notify`；即使缺少 `notify`，也会根据 `INVITE` 建立新的广播会话。

## ADR
### ADR-20260320-broadcast-session
- 决策: 广播业务按“单会话待机桥”模型实现，广播桥常驻，具体一轮广播的 `notify/invite/ack/bye` 由 `ProtocolManager` 维护单独会话状态。
- 原因: 这更符合当前嵌入式单路喊话的实际约束，既能补齐生命周期，又不需要把现有 `GB28181BroadcastBridge` 改造成复杂的多会话管理器。
