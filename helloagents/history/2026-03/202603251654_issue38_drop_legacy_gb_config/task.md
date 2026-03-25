# 轻量迭代任务 - issue38_drop_legacy_gb_config

> 类型: 轻量迭代
> 状态: 已完成
> 目标: 按 issue38 于 2026-03-25 的最新评论，移除旧 `gb28181.ini` 路径兼容，只保留 `/userdata/conf/Config/GB/` 下的单份本地协议配置。

## 任务清单

- [√] 移除 `LocalConfigProvider` 中对 `kLegacyLocalGbConfigFile` 的依赖和旧路径迁移逻辑
- [√] 保持 `gb28181.ini` / `gat1400.ini` 仅从 `/userdata/conf/Config/GB/` 读写
- [√] 更新受影响知识库说明与变更记录
- [√] 完成最小语法/差异验证并迁移方案包到 `history/`
