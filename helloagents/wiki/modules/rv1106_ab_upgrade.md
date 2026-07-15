# RV1106 A/B 升级与 SPI NAND 保护

## 平台结论

- 平台：RV1106G / XWR60440 / RC0240。
- Flash：CHUCUN C5F1GM7RE，128 MiB SPI NAND，ID `d8 81 d8`。
- U-Boot 保持单分区，`boot/rootfs/oem` 使用 A/B。
- A/B 元数据沿用 SDK `AvbABData`，位于 `misc + 2 KiB`。

## 分区要点

- boot：每槽 4 MiB。
- rootfs：每槽 10 MiB。
- oem：每槽 32 MiB。
- 固件和对齐保留区占前 96 MiB。
- misc：96 MiB 起，256 KiB。
- userdata：96.25 MiB 起，31.75 MiB。

## 升级职责

- 网络 OTA：Linux 写非活动 boot/rootfs/oem，不更新 U-Boot。
- SD 卡：SDK U-Boot 扫描 `uboot.img/boot.img/rootfs.img/oem.img`，不使用 `sd_update.txt`。
- 单个或部分槽镜像只写非活动槽，不切换。
- boot/rootfs/oem 三件套全部写入并校验成功后才切换。
- `uboot.img` 写固定单一 U-Boot 分区，不参与槽选择。
- 项目 `driver/uboot/` 和 `DG_sdupdate.c` 不属于实现范围。

## 断电语义

- 写非活动槽期间不修改 `misc`；断电后继续启动当前槽。
- 再次升级从头写目标槽，不进行断点续写。
- `misc` CRC 无效时默认 A 可启动、B 不可启动。
- 单副本 U-Boot 擦写期间断电仍有无法启动的固有风险。

## 写保护

- 升级解锁：A0=`0x00`。
- 正常保护：A0=`0x2A`，Lower 3/4 locked，即前 96 MiB。
- `misc/userdata` 位于保护范围外。
- 不设置 BPL，不切换外部 WP# 模式，不修改硬件。

## SDK 实编验证

- 验证日期：2026-07-15。
- 板级配置：`BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`。
- 补丁按 `vendor/rv1106_sdk_patches/series` 中的 0001～0004 顺序应用。
- `project/build.sh uboot` 成功，生成 U-Boot、SPL/TPL、ITB、loader 和 idblock；最终 ELF 中存在 `ab_sd_update`、`spinand_set_block_lock`、`avb_ab_mark_slot_active` 及 libavb user ops。
- `project/build.sh kernel` 成功，生成 `boot.img` 和 `vmlinux`；`vmlinux` 中存在 `spinand_get_block_lock`、`spinand_set_block_lock` 和 MTD lock/unlock 处理函数。
- `make -C sysdrv/tools/board/rk_ota distclean all` 成功，产物为 32-bit ARM EABI5/uClibc 可执行文件。
- 关键产物 SHA-256：`uboot.img`=`9c7188fbd4c08d60e6634c90f663e383d4d0960543fc089d1a2086268f5c2eb8`，`boot.img`=`8585d73c86766d2ee5abf512e4fd8ba5b0a30621004c64c809280491bd48ba85`，`rk_ota`=`82c8ecf642bfad94db5fe6bb22a34e9fc3a32ecd4a5ac896249b439879f428e4`。
- 原始 SDK 归档存在损坏，完整解压最终返回 tar exit 2；本次定向提取的 `project/sysdrv`、全部补丁目标及三个构建目标完整。归档中未参与本功能的其他组件不能据此视为已验证。

## 证据

- 启动日志：`/home/jerry/silver/serial_14.log`。
- 原理图：`/home/jerry/silver/SC1555-B-MAIN_V1.1_20260703.pdf` 第 8 页。
- Flash 数据手册：`/home/jerry/silver/FLASH_CHUCUN-C5F1GMxE-DS(2)(1).pdf`。
- 参考 SDK：`/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz`。
- 详细设计：`docs/superpowers/specs/2026-07-14-rv1106-ab-upgrade-spinand-write-protection-design.md`。
