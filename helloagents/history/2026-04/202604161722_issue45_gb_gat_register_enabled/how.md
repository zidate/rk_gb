# 技术设计: issue45 GB/GAT 注册开关与停服语义收口

## 技术方案
1. 扩展 `ProtocolExternalConfig::GatRegisterParam`，新增 `enabled`，默认值设为 `1`。
2. 扩展 `LocalConfigProvider` 的 GAT 读写与校验逻辑：
   - `gat1400.ini` 新增 `enable`
   - `UpdateGatRegisterConfig()` 只写 flash，但会把 `enabled` 归一化为 `0/1`
   - 当 `enabled=0` 时，允许服务器地址为空，避免“仅为停服而保存禁用配置”被校验拦住
3. 调整 `ProtocolManager` 的 1400 生命周期：
   - `Start()` 中仅在 `gat_register.enabled != 0` 时启动 `m_gat_client`
   - `ReloadExternalConfig()` 将 `gat_register.enabled` 纳入 `reloadGat` 判定
   - `RestartGatRegisterService()` 在运行中先更新缓存，再由 `Reload()` 统一执行“停服 / 保持停服 / 重新启动”
4. 调整 `GAT1400ClientService::Start()/Reload()`：
   - `Start()` 入口先判 `enabled`，关闭时直接返回成功且保持停止态
   - `Reload()` 中把 `enabled` 差异纳入重启判定；若新配置关闭，则 `Stop()` 后不再 `Start()`
5. 保持 GB28181 现有语义不改，只补验证记录和知识库：
   - `RestartGbRegisterService()` 当前已在运行中先 `StopGbClientLifecycle()`，再按 `enabled` 决定是否重启

## 架构决策
- **决策:** 不新增独立的 `StopGatRegisterService()` 接口，继续复用 issue38 已建立的“`Set*` 只写 flash、`Restart*` 显式把 flash 重载到运行态”边界。
- **原因:** issue 45 明确要求调用 `Set*RegisterConfig()` 与 `Restart*RegisterService()` 就能完成注销，沿用现有接口语义改动最小，也能保持 GB/GAT 对称。

## 风险与规避
- **风险:** `ReloadExternalConfig()`、网络变化回调和 `ProtocolManager::Start()` 入口之间若有遗漏，关闭配置后仍可能被重新拉起。
- **规避:** 所有触发 1400 重载的入口最终都走 `ProtocolManager::Start()` 或 `m_gat_client->Reload()`，因此统一在这两层收口 `enabled` 判定。
- **风险:** 禁用后如果仍强制校验 `server_ip/server_port`，会导致“先清空地址再关闭”的配置无法保存。
- **规避:** GAT 校验改为“仅在 enabled 打开时要求地址与端口有效”。

## 测试与验证
- 新增最小回归验证入口，覆盖 `GatRegisterParam.enabled` 的默认值与读写字段存在性。
- 执行 `bash -n build.sh` 之外，再至少对 `ProtocolManager.cpp`、`LocalConfigProvider.cpp`、`GAT1400ClientService.cpp` 做语法级编译验证。
- 代码级复核 `RestartGbRegisterService()` 的停服语义，并把验证结果记入任务清单与知识库。
