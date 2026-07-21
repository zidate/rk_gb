# 任务清单: Text OSD 恢复每秒刷新

目录: `helloagents/history/2026-07/202607211005_text_osd_one_second_refresh/`

> **模式:** 轻量迭代

---

## 1. 刷新策略
- [√] 1.1 删除 text OSD 局部亮度变化门控和附加变化 map
- [√] 1.2 恢复 auto 模式下可见、非空 text OSD 每秒重绘

## 2. 透明背景
- [√] 2.1 保留 FreeType 全透明背景与 GB OSD RGN alpha 配置

## 3. 验证与文档
- [√] 3.1 更新自动反色回归测试，覆盖每秒刷新和透明背景
- [√] 3.2 运行 OSD 回归测试并使用 RV1106 工具链交叉编译
- [√] 3.3 更新知识库、CHANGELOG 和历史索引
