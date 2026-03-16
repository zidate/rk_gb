# 变更提案

## 背景
- `silver` 分支在 issue #27 中出现“平台获取实时流失败”。
- 附件抓包显示注册成功后平台下发 `INVITE`，设备仅回 `100 Trying`，未回 `200 OK + SDP`。

## 根因判断
- 实时流 TCP 协商路径在 SIP 应答前同步建立媒体连接，可能阻塞 `INVITE` 处理。
- SDP answer 对 `setup:active/passive` 的方向未按对向关系修正。

## 成功标准
- 实时流 `INVITE` 能稳定返回 `200 OK`。
- TCP `setup` 方向正确，媒体连接建立时序与 SIP 协商解耦。
- 日志能明确标识请求进入、应答发送与 ACK 后媒体启动。
