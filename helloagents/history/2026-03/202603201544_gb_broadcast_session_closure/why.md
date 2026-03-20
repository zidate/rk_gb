# 变更提案

## 背景
- 现有 GB28181 broadcast 已能基本完成平台向设备喊话，但代码里仍有几处业务闭环缺失。
- 主要问题是广播通知只记日志、不进入管理器状态；音频 `INVITE` 的应答地址可能退化为 `0.0.0.0`；收到 `BYE` 后广播桥不会恢复到下一次可继续接收的待机态。

## 根因判断
- `GB28181ClientReceiverAdapter::OnNotify()` 仅打印 `source/target`，没有把广播通知交给 `ProtocolManager` 建立会话上下文。
- `BuildGbResponseMediaInfo()` 对 `kAudioStream` 直接调用 `BuildGbBroadcastMediaInfo("", ...)`，导致设备地址无法复用现有的“按对端解析本机出口 IP”逻辑。
- `HandleGbStopStreamRequest()` 只处理 live/replay，没有处理 broadcast，会造成 `BYE` 后广播播音状态和接收桥资源没有按会话维度复位。

## 成功标准
- 广播通知进入 `ProtocolManager`，至少保存 `SourceID/TargetID`，并校验 `TargetID` 与本机设备 ID。
- 广播音频 `INVITE` 的 `200 OK` 优先返回设备对平台实际可达的本机 IP，而不是 `0.0.0.0`。
- 广播 `BYE` 到来时，设备能清理当前播音状态并恢复广播桥待机接收能力，便于后续继续广播。
