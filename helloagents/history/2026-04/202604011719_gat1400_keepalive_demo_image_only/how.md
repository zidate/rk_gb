# 技术设计: 修正 keepalive demo 仅发送图片

## 技术方案
- 保留 `SendKeepaliveDemoCaptureEvent(const std::string& deviceId)` 入口。
- 删除 demo 内部对本地视频文件的读取、Base64 编码、`GAT_1400_VideoSliceSet` 构造和 `event.video_slice_list.push_back(...)`。
- 继续构造 `media::GAT1400CaptureEvent`，事件类型仍为 `FACE_DETECT_EVENT`，但只带 `image_list`。
- 更新当前知识库和变更历史说明，明确 demo 现已收口为“只发图片”。

## 风险与缓解
- **风险:** 若只删视频上送但残留日志或文档描述，会造成代码和知识库不一致。
- **缓解:** 同步更新 `gat1400` 模块文档、`CHANGELOG` 和历史索引。
