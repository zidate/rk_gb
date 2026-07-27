# 轻量迭代任务 - rv1106_sd_fat_dos_restore

> 类型: 轻量迭代
> 状态: 已完成
> 目标: 修复 RV1106 A/B SD 升级无法识别现场 MBR/FAT 卡、尚未读取镜像即退出的问题。

## 任务清单

- [√] 结合 `debug.log` 定位失败发生在 `fs_set_blk_dev()`，确认新 SPL/U-Boot/kernel 与 A/B 分区表已生效，镜像尚未进入读取和写入阶段
- [√] 对比厂商 `DG_sdupdate.c` / `script_update.c`，确认旧路径会在访问 SD 前临时设置 `desc->part_type = PART_TYPE_DOS`
- [√] 修改 `0002-uboot-sd-protection.patch`，在 A/B updater 的 FAT 扫描和分块读取期间强制 DOS 分区解析，并统一恢复原 `part_type`
- [√] 新增回归测试，检查块设备获取、类型保存/强制/恢复以及四条错误退出路径
- [√] 运行 RV1106 SD policy 14 项测试及补丁集、A/B layout、C5F1GM7RE 保护 46 项关联测试
- [√] 在展开的真实 RV1106 SDK 中完成 `project/build.sh uboot`，确认 `ab_sd_update`、`blk_get_devnum_by_type` 和 `spinand_set_block_lock` 已链接
- [√] 同步 RV1106 A/B 升级知识库、补丁 README、CHANGELOG 与历史索引
