# 任务清单

- [√] 在 `ProtocolManager.cpp` 增加 `f=` 视频参数解析 helper。
- [√] 将 live 请求、sender 初始化逻辑收口为“先按 `streamnumber/default` 选流，再按 `f=` 调用编码参数接口”。
- [√] 补充必要日志，便于抓包和日志联调。
- [√] 更新 `helloagents/wiki/modules/gb28181.md`、`helloagents/CHANGELOG.md`、`helloagents/history/index.md`。
- [√] 做编译级验证并迁移方案包到 `history/`。

> 验证: 使用 `g++ -std=c++11 -fsyntax-only` + 现有 `flags.make` include/define 对 `App/Protocol/ProtocolManager.cpp` 做语法校验；当前仅验证协议层改动，不扩展底层编码器实现。
