# 任务清单: issue45 GB/GAT 注册开关与停服语义收口

## 1. 配置模型与最小回归
- [√] 1.1 新增最小回归验证入口，先覆盖 GAT `enabled` 默认值 / 持久化字段 / 禁用校验边界

## 2. GAT1400 配置与生命周期收口
- [√] 2.1 在 `ProtocolExternalConfig` 与 `LocalConfigProvider` 中补齐 `GatRegisterParam.enabled`、`gat1400.ini` 读写和校验逻辑
- [√] 2.2 在 `ProtocolManager` 与 `GAT1400ClientService` 中实现“禁用时不启动 / 重载时注销并停服”的运行态语义

## 3. GB28181 语义复核
- [√] 3.1 复核 `RestartGbRegisterService()` 对 `enabled=0` 的停服行为，必要时补齐遗漏；若代码已满足，仅更新验证记录与知识库

## 4. 知识库与验证
- [√] 4.1 更新 `helloagents/wiki`、`CHANGELOG.md`、`history/index.md` 等知识库文档
- [√] 4.2 执行最小验证并记录结果

## 执行结果

- 已为 `GatRegisterParam` 补齐 `enabled` 默认值与 `gat1400.ini::enable` 持久化字段；`enabled=0` 时允许保存停服态配置，不再强制要求 `server_ip/server_port` 有效。
- `ProtocolManager::Start()`、`ReloadExternalConfig()`、`RestartGatRegisterService()` 与 `GAT1400ClientService::Start()/Reload()` 已统一按 `gat_register.enabled` 收口 1400 生命周期：关闭时不启动，运行中重载会先停服并保持停止，重新启用后可从停止态重新拉起。
- 已复核 `ProtocolManager::RestartGbRegisterService()` 现有实现，确认其在 `gb_register.enabled=0` 时会先停掉 GB 生命周期并保持停服，不需要额外代码修改。
- 最小回归脚本 `python3 tools/tests/issue45_gat_enabled_regression.py` 已通过；`bash -n build.sh`、`g++ -std=gnu++11 -fsyntax-only ... LocalConfigProvider.cpp ProtocolManager.cpp GAT1400ClientService.cpp ...` 与 `git diff --check` 也已通过。
