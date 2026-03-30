# 任务清单: GB28181 实时流 TCP 发送回压容错

目录: `helloagents/history/2026-03/202603301757_gb_live_tcp_send_backpressure/`

---

## 1. 问题复核
- [√] 1.1 复核 `GB28181RtpPsSender` 的 TCP socket 配置、`SO_SNDTIMEO` 与 `SendAll()` 错误处理，确认 `errno=11` 的本地触发条件
- [√] 1.2 确认上层 `ProtocolManager` 在 live sender 返回失败后会立即停流，修复点应落在 sender 自身回压容错

## 2. 代码实现
- [√] 2.1 调整 `rk_gb/App/Protocol/gb28181/GB28181RtpPsSender.cpp`，为 TCP `send()` 增加 `EAGAIN/EWOULDBLOCK` 的限时等待与重试
- [√] 2.2 保持当前会话关闭语义不变，仅在超过总等待窗口或遇到非重试型错误时返回失败

## 3. 验证与知识库同步
- [√] 3.1 执行最小静态验证，确认 `GB28181RtpPsSender.cpp` 语法正确且日志/返回码链路自洽
- [√] 3.2 更新 `rk_gb/helloagents/wiki/modules/gb28181.md`、`rk_gb/helloagents/CHANGELOG.md`、`rk_gb/helloagents/history/index.md`

## 4. 收尾
- [√] 4.1 更新任务状态与执行结果
- [√] 4.2 将方案包迁移到 `rk_gb/helloagents/history/2026-03/`

## 执行结果
- `GB28181RtpPsSender::SendAll()` 现为 TCP 发送增加 `3s` 总等待窗口；当 `send()` 返回 `EAGAIN/EWOULDBLOCK` 时，会等待 socket 再次可写后继续发送，不再因为单次 `50ms` `SO_SNDTIMEO` 立即上抛失败。
- `WaitForSocketWritable()` 现改用单调时钟控制剩余等待时间，并处理 `EINTR`，避免短时中断或系统时间调整把发送超时窗口算歪。
- `ProtocolManager` 的上层停流语义保持不变；只有 sender 超过总窗口或遇到非重试型错误时，才继续返回失败并触发 stop session。
- 已完成 `g++ -fsyntax-only` 定向语法检查和 `git diff --check`，并同步更新知识库与历史索引。
