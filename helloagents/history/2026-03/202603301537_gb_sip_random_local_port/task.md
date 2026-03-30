# 任务清单: GB28181 SIP 本地监听端口随机化

目录: `helloagents/history/2026-03/202603301537_gb_sip_random_local_port/`

---

## 1. 链路确认
- [√] 1.1 复核 `ProtocolManager -> GB28181ClientSDK -> SipSDK -> eXosip` 的 SIP 监听与 `REGISTER` 构包链路，确认当前本地端口复用点
- [√] 1.2 确认随机本地端口场景下，`ClientInfo::from/contact` 需要依赖运行态实际监听端口回写

## 2. 代码实现
- [√] 2.1 在 `rk_gb/App/Protocol/ProtocolManager.cpp` 调整 GB28181 SIP 客户端启动逻辑，本地监听改为随机端口，不再复用 `gb_register.server_port`
- [√] 2.2 在 `rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/common/SipEventManager.*` 补齐随机监听端口的实际端口获取与缓存逻辑
- [√] 2.3 在 `rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/client/SipClientImpl.cpp` 同步运行态本地端口，确保 `REGISTER` / 其他出站请求的 `from/contact` 使用真实监听端口

## 3. 验证与知识库同步
- [√] 3.1 执行最小静态检查与定向搜索，确认 `gb_register.server_port` 只保留远端平台端口语义，随机本地端口路径无 `:0` 残留
- [√] 3.2 更新 `rk_gb/helloagents/wiki/modules/protocol.md`、`rk_gb/helloagents/wiki/modules/gb28181.md`、`rk_gb/helloagents/CHANGELOG.md`、`rk_gb/helloagents/history/index.md`

## 4. 收尾
- [√] 4.1 更新任务状态与验证结果
- [√] 4.2 将方案包迁移到 `rk_gb/helloagents/history/2026-03/`

## 执行结果
- `ProtocolManager::StartGbClientLifecycle()` 已改为固定传本地端口 `0`，GB28181 SIP 客户端不再复用 `gb_register.server_port` 作为本地监听端口。
- `CSipEventManager::StartSipListen()` 在本地端口为 `0` 时，会先临时 `bind(0)` 预取一个可用端口，再沿用现有 `eXosip_listen_addr()` 建立正式监听，并缓存到 `m_local_port`。
- `CSipClientImpl::Start/Register()` 与 `CSipEventManager::Register()` 已同步回写实际本地端口，确保 `REGISTER` / 其他出站请求的 `From/Contact` 使用真实监听端口。
- 已完成 `git diff --check` 与三处改动源文件的交叉编译前端 `-fsyntax-only` 验证，并同步更新知识库与历史索引。
