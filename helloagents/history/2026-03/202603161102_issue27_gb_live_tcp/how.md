# 技术设计

## 方案
1. 将 GB 实时流媒体建链从 `INVITE` 处理阶段延后到 `ACK` 后。
2. 修正 GB 实时流应答 SDP 中的 TCP `setup` 方向。
3. 补充关键日志，便于后续定位 SIP/媒体时序问题。

## 影响范围
- `App/Protocol/ProtocolManager.cpp`
- `App/Protocol/ProtocolManager.h`
- 可能涉及 `App/Protocol/gb28181/GB28181RtpPsSender.*`

## 风险与规避
- 风险: 改动 live path 可能影响回放/下载。
- 规避: 仅对实时流路径延后 TCP 打开时机，不动回放/下载逻辑。

## ADR
### ADR-20260316-issue27
- 决策: 实时流 TCP 媒体连接在 ACK 后启动，而不是在 SIP `200 OK` 之前同步建立。
- 原因: 避免媒体建链阻塞 SIP 应答，提升平台兼容性。
