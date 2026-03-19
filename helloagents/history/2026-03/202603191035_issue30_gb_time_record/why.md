# 变更提案

## 背景
- `silver` 分支的 issue #30 要求补齐 GB28181 注册后的时间同步能力。
- 同一 issue 还要求补齐平台查询设备录像文件列表的响应能力。

## 根因判断
- 注册成功后虽然已经能取到 SIP `Date` 头，但 `ProtocolManager::PrepareGbRegisterTimeSync()` 仅做解析和 dry-run 日志，没有实际设时。
- `ProtocolManager::ResponseGbQueryRecord()` 固定返回空的 `RecordIndex`，未接入本地存储检索结果。

## 成功标准
- 设备在 GB 注册成功后能解析平台时间并执行本地设时。
- 平台发起 `RecordInfo` 查询时，设备能按查询时间段返回本地录像列表。
- 关键日志能反映时间同步与录像查询的输入、结果和失败原因。
