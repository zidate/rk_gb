# 背景

- 用户用 `246.pcap` 和 `debug.log` 反馈 issue 44 仍未修复。
- 抓包已确认同一会话里：
  - SIP `From/Contact`、`Keepalive`、报警订阅 `SUBSCRIBE` 查询体都使用真实连接 SIP 本端 ID `35010101001320124935`
  - 实际报警 `NOTIFY` XML `<DeviceID>` 仍是旧值 `C0440326101000000010`
- 代码追踪显示最终报警 XML 由 `CGB28181XmlParser::PackAlarmNotify()` 负责组包，而这里直接使用上层传入的 `AlarmNotifyInfo::DeviceID`，没有像 `PackKeepalive()` 一样优先使用 SDK 当前运行态 `m_local_code`。

# 目标

- 将报警 `NOTIFY` XML 的 `<DeviceID>` 与 SDK 当前真实连接的 SIP 本端 ID 强绑定。
- 修正点尽量下沉到最终组包处，避免继续依赖上层调用方是否正确回填。

# 非目标

- 不重做 `ProtocolManager::NotifyGbAlarm()` 的整体流程。
- 不扩展新的报警结构体或额外配置项。
- 不处理与本次 `DeviceID` 问题无关的报警字段。
