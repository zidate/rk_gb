# 修复任务 - rv1106_mtdparts_double_scale

> 类型: 修复
> 状态: 已完成
> 目标: 修复 RV1106 U-Boot MTD parser 对 `K/M/G` 单位重复换算、导致分区容量失真的问题。

## 任务清单

- [√] 分析 12:00 `debug.log`，确认新 U-Boot 已启动、三件套已识别，但 `env` 被截断为整颗 NAND 且其余分区全部 `out of reach`。
- [√] 对照 `lib/strto.c` 与 `cmd/mtdparts.c`，确认初版 helper 以已换算单位的 `ustrtoull()` 为底层，随后再次移位。
- [√] 增加回归断言，要求 `0005` 明确使用 `simple_strtoull()`，并禁止新增的 helper 使用 `ustrtoull()`。
- [√] 修正 `0005-uboot-mtdparts-unit-suffix.patch` 与真实 SDK 的 `drivers/mtd/mtdpart.c`。
- [√] 重新构建 U-Boot、SPL、idblock 和 download loader，并刷新交付包。
- [√] 运行补丁集、SD policy、A/B layout 和写保护回归测试。

## 验证边界

- 源码、补丁应用、交叉编译和静态回归可在主机完成。
- 仍需使用工厂/USB方式刷入修正版后上板确认 `env` 为 `0x40000`，且不再出现 `out of reach`。
