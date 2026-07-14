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

## 证据

- 启动日志：`/home/jerry/silver/serial_14.log`。
- 原理图：`/home/jerry/silver/SC1555-B-MAIN_V1.1_20260703.pdf` 第 8 页。
- Flash 数据手册：`/home/jerry/silver/FLASH_CHUCUN-C5F1GMxE-DS(2)(1).pdf`。
- 参考 SDK：`/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz`。
- 详细设计：`docs/superpowers/specs/2026-07-14-rv1106-ab-upgrade-spinand-write-protection-design.md`。
