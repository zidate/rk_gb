# 变更提案: 修正 keepalive demo 仅发送图片

## 需求背景
上一提交把 keepalive demo 调整成了“通过 `NotifyCaptureEvent()` 上送人脸检测事件，并同时带图片和视频”。

用户现在明确指出：上一提交这里不应该发送视频，只发送图片就够了。

## 变更目标
1. 保留 keepalive demo 走 `NotifyCaptureEvent()` 的调用方式。
2. 保留 `FACE_DETECT_EVENT` 与“每次服务启动最多触发 5 次”的约束。
3. 删除 demo 中的视频读取、视频元数据和 `video_slice_list` 上送，只保留图片。

## 影响范围
- `rk_gb/App/Protocol/gat1400/GAT1400ClientService.cpp`
- `helloagents/wiki/modules/gat1400.md`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
