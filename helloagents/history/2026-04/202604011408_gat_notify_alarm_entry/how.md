# 技术设计: 新增 ProtocolManager GAT 业务通知入口

## 技术方案
- 在 `ProtocolManager` 中新增 `NotifyGatAlarm(const media::GAT1400CaptureEvent* event)`。
- 在 `GAT1400ClientService` 中新增 `NotifyCaptureEvent(const media::GAT1400CaptureEvent& event)`，供 `ProtocolManager` 转发使用。
- `NotifyCaptureEvent()` 的处理策略：
  - 当前 1400 已启动且已注册: 先直接调用现有 `PostCaptureEvent()` 尝试上传
  - 直接上传失败，或当前未就绪: 回退到 `GAT1400CaptureControl` 内存队列
- 保留现有注册成功/保活成功后的 `DrainPendingCaptureEvents()` 逻辑，作为补偿路径。

## 实现要点
- 不改 `PostFaces/PostMotorVehicles/PostImages/PostVideoSlices/PostFiles` 的顺序。
- 不改 `/Data` Base64 上传逻辑。
- 业务侧推荐调用面改为 `ProtocolManager::NotifyGatAlarm()`；`GAT1400CaptureControl` 继续保留为内部桥接/排队组件。

## 风险与缓解
- **风险:** 新入口若只做直接上传，上传失败时可能丢失事件。
- **缓解:** 直接上传失败后立即回退到 `GAT1400CaptureControl` 队列，保持现有注册/保活补偿路径。
