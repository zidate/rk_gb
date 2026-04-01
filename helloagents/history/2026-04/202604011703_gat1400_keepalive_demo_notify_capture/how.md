# 技术设计: 按 issue 41 最新评论调整 1400 保活 demo

## 技术方案
- 移除 `SendKeepaliveDemoUploadOnce(const std::string& deviceId)` 和 `m_keepalive_demo_upload_attempted`。
- 新增 `SendKeepaliveDemoCaptureEvent(const std::string& deviceId)`：
  - 从本地磁盘读取图片 / 视频文件。
  - 编码为 Base64，填充 `media::GAT1400CaptureEvent`。
  - 构造 `GAT_1400_Face`、`GAT_1400_ImageSet`、`GAT_1400_VideoSliceSet`，事件分类统一设为 `FACE_DETECT_EVENT`。
  - 通过 `NotifyCaptureEvent()` 进入 1400 主链，而不是直接调用 `PostFaces/PostImages/PostVideoSlices`。
- 在 `SendKeepaliveNow()` 的保活成功路径中调用新 demo 函数。
- 用计数器限制 demo 最多成功进入链路 5 次；构造失败或通知失败时回退计数，允许后续保活继续补尝试。

## 实现要点
- 不改 `NotifyCaptureEvent()` 自身“直接上传 / 入队回退”的逻辑。
- 不改 `/Data` Base64 上传实现。
- demo 只在保活成功后触发，不在注册成功时额外直发。

## 风险与缓解
- **风险:** 本地图片或视频文件不存在时，demo 会持续失败。
- **缓解:** 失败时仅记录日志并回退 demo 计数，不影响保活主链路。
- **风险:** demo 若仍直接走 `Post*`，会违背最新评论要求。
- **缓解:** 明确把 demo 入口收口到 `NotifyCaptureEvent()`。
