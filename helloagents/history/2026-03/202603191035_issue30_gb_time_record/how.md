# 技术设计

## 方案
1. 在 `ProtocolManager.cpp` 中补充 GB 查询时间解析与录像时间格式化辅助函数。
2. 将注册成功后的时间同步从 dry-run 改为真实执行：调用 `settimeofday()` 并在成功后触发 `Storage_Module_SaveSystemTime(true)`。
3. 在录像查询响应中读取 `QueryParam` 的 `StartTime/EndTime/Type`，调用 `Storage_Module_SearchRecordOnTime()`，映射为 `RecordIndex.record_list` 后返回。
4. 修正 `App/Storage/lg/StorageManager.cpp` 中 LG 存储分支按时间搜索录像时误用外层循环索引的问题，保证返回时间段与报警类型判断一致。

## 影响范围
- `App/Protocol/ProtocolManager.cpp`
- `App/Storage/lg/StorageManager.cpp`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
- `helloagents/wiki/modules/protocol.md`

## 风险与规避
- 风险: 设时动作会直接修改系统时间。
- 规避: 仅在收到并成功解析平台 `Date` 头时执行，且保留 hook 扩展点。
- 风险: 录像查询时间格式可能存在平台差异。
- 规避: 同时兼容 ISO 时间与 `YYYYMMDDHHMMSS` 紧凑时间格式，并在解析失败时打印明确日志。

## ADR
### ADR-20260319-issue30
- 决策: 时间同步默认由协议层直接落本地设时，录像查询直接复用本地存储检索接口。
- 原因: 当前仓库已具备稳定的设时与录像搜索基础能力，直接复用可最小化改动面并尽快满足联调需求。
