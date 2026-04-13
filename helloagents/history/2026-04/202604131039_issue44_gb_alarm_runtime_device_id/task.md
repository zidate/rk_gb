# 任务清单: issue 44 GB 报警运行态 DeviceID 对齐

目录: `helloagents/history/2026-04/202604131039_issue44_gb_alarm_runtime_device_id/`

---

## 1. 代码调整
- [√] 1.1 增加一个可独立验证的 GB 报警身份解析小工具，明确“报警通知应优先使用运行态真实 DeviceID，而不是静态配置里的 `gb_register.device_id`”
- [√] 1.2 在 `rk_gb/App/Protocol/ProtocolManager.*` 中缓存当前 GB 运行态 DeviceID，并在订阅回调进入时用 SDK 回传的 `gbCode` 刷新
- [√] 1.3 在 `rk_gb/App/Protocol/ProtocolManager.cpp` 的 `NotifyGbAlarm()` 中改为自动回填运行态 `DeviceID`，并在 `AlarmID` 为空时补默认值

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 2.2 更新 `helloagents/wiki/modules/protocol.md`
- [√] 2.3 更新 `helloagents/CHANGELOG.md`
- [√] 2.4 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 先执行最小回归用例，确认修复前“运行态 DeviceID 优先级”校验失败
- [√] 3.2 修复后重新执行最小回归用例，确认通过
- [√] 3.3 执行 `git -C /home/jerry/silver/rk_gb diff --check`

## 执行结果
- 新增 `App/Protocol/gb28181/GbAlarmIdentity.*`，把“报警应选哪个设备编号”的逻辑从 `ProtocolManager` 中抽出，并补了可独立运行的最小回归用例。
- `ProtocolManager` 现在会在 GB 订阅回调进入时缓存 SDK 回传的运行态 `gbCode`；`NotifyGbAlarm()` 发送报警前优先使用这份运行态值，不再固定写回 `m_cfg.gb_register.device_id`。
- 零配置 `302` 之后，若 SDK 已切到平台正式分配的本端 `deviceId`，报警通知 `DeviceID` 会随订阅回调同步跟进；若调用方未填写 `AlarmID`，当前默认补成同一运行态设备编号。
- 最小回归按 TDD 先验证“实现缺失时链接失败”，再验证 helper 行为转绿；另外用当前 `cmake-build/App/CMakeFiles/dgiot.dir/flags.make` 的 include 列表映射到本工作区后，对 `GbAlarmIdentity.cpp` 和 `ProtocolManager.cpp` 执行了 `g++ -std=gnu++11 -fsyntax-only`，结果通过，仅保留仓库原有 `GB_ID_LEN` 宏重复定义告警。
