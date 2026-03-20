# 变更提案

## 背景
- issue #33 反馈平台向设备发起广播后，平台界面显示设备无响应。
- 附件抓包 `164.pcap` 和日志 `debug.log` 显示设备侧已经收到 `Broadcast` 类型的 SIP `MESSAGE`，但广播后续媒体链路没有建立。

## 根因判断
- `CGBClientImpl::OnNotify()` 进入函数后先调用 `SendSipMessageWithOk(data)`，提前对同一事务返回了一个空的 `200 OK`。
- 函数结尾又调用 `m_sip_client->Response(&(data->Dialog), &message)` 发送带 XML 业务体的最终响应，导致 eXosip 报错 `transaction already answered`。
- 同一函数里还存在把 `kForbidden` 覆盖回 `kSuccessRequest` 的逻辑，导致广播拒绝路径被静默吞掉。

## 成功标准
- 广播通知只对同一 SIP 事务应答一次。
- 最终响应携带 `Broadcast` XML 业务体，并保留 `200/400/403` 语义。
- 日志能够直接体现广播通知响应的 `sip_code/result/sn`，便于后续排查。
