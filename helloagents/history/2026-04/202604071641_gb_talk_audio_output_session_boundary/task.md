# 轻量迭代任务 - gb_talk_audio_output_session_boundary

- [√] 复盘第二次对讲无声的设备侧行为，确认平台第二次码流本身正常，问题收敛到设备侧播放恢复时机
- [√] 将 `GB28181BroadcastBridge` 的扬声器控制改为按会话边界处理：旧连接关闭时清掉 `AUDIO_TALK_TYPE`，新的 `UDP apply` 或 `TCP connect/accept` 成功后再明确打开
- [√] 保留现有解码前原始码流落盘与 G711 对齐修正，不再使用“每帧都重开扬声器”的兜底方式
- [√] 执行 `git -c core.whitespace=cr-at-eol diff --check`
