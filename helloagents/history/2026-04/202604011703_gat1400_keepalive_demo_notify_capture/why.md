# 变更提案: 按 issue 41 最新评论调整 1400 保活 demo

## 需求背景
issue 41 在 2026-04-01 16:57:35（北京时间）的最新评论里，要求继续调整当前 GAT1400 保活 demo 的实现方式。

最新评论的核心要求有两点：
1. 去掉原来 `SendKeepaliveDemoUploadOnce()` 那种直接 `PostFaces/PostImages` 的一次性 demo。
2. 改成在保活成功路径里，构造 `media::GAT1400CaptureEvent` 并调用 `NotifyCaptureEvent(const media::GAT1400CaptureEvent& event)`；事件类型为 `FACE_DETECT_EVENT`，内容同时带图片和视频，总共发送 5 次。

## 变更目标
1. 删除旧的 `SendKeepaliveDemoUploadOnce()` 及其“一次性”状态位。
2. 新增基于 `NotifyCaptureEvent()` 的保活 demo 构造函数，在每次保活成功后触发一次。
3. demo 内容改为人脸检测事件，关联本地图片 `/mnt/sdcard/test.jpeg` 和视频 `/mnt/sdcard/DCIM/2026/03/24/20260324174542-20260324174845.mp4`。
4. demo 总触发次数限制为 5 次。

## 影响范围
- `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*`
- `helloagents/wiki/modules/gat1400.md`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
