# 轻量迭代任务 - sip_response_peer_match_order

> 类型: 轻量迭代
> 状态: 已完成
> 目标: 调整 SIP 响应事件的 client 匹配顺序，优先使用 `event->response`，避免把本机请求头当成对端导致误打 `sip peer match failed`。

## 任务清单

- [√] 修改 `SipEventManager::GetClientInfo()`，在响应场景优先用 `event->response` 匹配 client
- [√] 保留 request 侧回退逻辑，避免 response 缺失时破坏兼容性
- [√] 更新最小知识库与变更记录
- [√] 完成语法验证、提交并推送到新分支
