# 修复任务 - rv1106_mtdparts_byte_layout

> 类型: 修复
> 状态: 已完成（方案已被 2026-07-23 的 parser 后缀方案替代）
> 目标: 修复 RV1106 A/B SD updater 已扫描到四个镜像但因 U-Boot MTD 分区字符串解析失败而无法写入的问题。

## 任务清单

- [√] 根据最新 `debug.log` 确认 18:08 新 U-Boot 已生效，FAT 扫描和四个镜像大小校验均成功
- [√] 定位失败点为 `drivers/mtd/mtdpart.c` 拒绝 `mtdparts` 中的 `256K` 后缀，导致 `boot_b` 等分区未创建
- [√] 新增过渡性 `0005-spinand-mtdparts-byte-layout.patch`，让 SPI NAND 的运行时 `mtdparts` 使用 `GLOBAL_PARTITIONS` 生成的十六进制字节/偏移格式（后续已废弃）
- [√] 重新生成过 `.env.txt` 和 `env.img`，确认不再包含 `256K` 分区后缀（后续恢复为 K/M 格式）
- [√] 重新构建 U-Boot 和 kernel，确认 `uboot.img/env.img/boot.img/idblock.img` 产物生成成功
- [√] 运行补丁集、SD policy、A/B layout、C5F1GM7RE protection 共 61 项回归测试
