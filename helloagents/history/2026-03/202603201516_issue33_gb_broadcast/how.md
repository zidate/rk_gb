# 技术设计

## 方案
1. 删除 `CGBClientImpl::OnNotify()` 开头的预置 `SendSipMessageWithOk(data)`，避免对同一 `MESSAGE` 事务重复回复。
2. 保留函数尾部统一的 `m_sip_client->Response()` 作为唯一最终响应，并为其补齐 `Method` 与 `content_type`。
3. 去掉对 `message.code` 的错误回写，确保 `UnPackNotifyInfo()` 失败时返回 `400`、接收端拒绝时返回 `403`。
4. 增加 `gb notify unpack failed`、`gb notify response`、`gb notify response send failed` 诊断日志，便于从设备日志快速定位广播通知阶段问题。

## 影响范围
- `third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/GB28181Client/GBClientImpl.cpp`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
- `helloagents/wiki/modules/protocol.md`

## 风险与规避
- 风险: 删除前置 `200 OK` 后，若尾部 `Response()` 未携带正确头域，平台可能仍无法识别广播应答。
- 规避: 明确设置 `message.Method = kSipMessageMethod` 和 `message.content_type = kSipContentMANSCDP_XML`，并做隔离交叉编译验证。
- 风险: `Broadcast` XML 解析失败时 `sn` 未初始化，可能让错误响应内容不稳定。
- 规避: 将 `sn` 初始化为 `0`，保证失败路径至少生成稳定的响应报文和日志。

## ADR
### ADR-20260320-issue33
- 决策: GB 广播通知采用“单事务单最终响应”模式，不再先发空 `200 OK`。
- 原因: 平台需要收到带 `Broadcast` XML 业务体的最终应答；重复回复会触发 eXosip `transaction already answered`，并让平台只能看到无业务体的前一个应答。
