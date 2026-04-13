# 方案

## 核心思路

把最终 `NOTIFY` 报文 `<DeviceID>` 的归一化下沉到 `CGB28181XmlParser::PackAlarmNotify()`：

- 若 `m_local_code` 非空，优先用它填充报警 XML 的 `DeviceID`
- 否则退回调用方传入的 `AlarmNotifyInfo::DeviceID`

这样能保证：

- 和 `PackKeepalive()` 的设备标识策略一致
- 即使上层还带着旧的 `C044...`，最终报警 XML 也会跟真实 SIP 本端 ID 一致

## 验证

1. 先用最小复现程序验证旧行为失败：
   - `m_local_code=35010101001320124935`
   - 输入报警 `DeviceID=C0440326101000000010`
   - 旧逻辑生成的 XML 仍包含 `C044...`
2. 修改后重新运行复现程序：
   - XML 应包含 `35010101001320124935`
   - XML 不应再包含 `C0440326101000000010`
3. 额外做目标文件级编译校验，确保修改未破坏当前工程编译。

## 风险

- `PackAlarmNotify()` 行为从“信任上层 DeviceID”变成“优先跟随运行态 local_code”。
- 这是本次需求想要的行为，且与 `Keepalive`/SIP 会话一致，风险可控。
