# RV1106 A/B、SD 升级与 SPI NAND 写保护板端验收

## 状态与范围

- 当前状态：SDK 实编和 68 项主机测试已通过，以下项目待 XWR60440 实板执行并回填证据。
- 平台：RV1106G / XWR60440 / CHUCUN C5F1GM7RE 128 MiB SPI NAND。
- 只验收既定行为，不增加断点续写、额外回退或其他异常恢复设计。
- 不修改或验证项目 `driver/uboot/`、`DG_sdupdate.c`，不使用 `sd_update.txt`。
- 不修改坏块检测、BBT、OOB 标记，不调用 `mtd_block_markbad`。
- 网络 OTA 不更新 U-Boot；SD 卡允许更新单副本 U-Boot。
- 不对单副本 U-Boot 执行升级中断电测试。

## 测试准备

1. 保留串口完整日志，记录固件版本、测试时间、当前槽和每次上电原因。
2. 准备只包含 `boot.img/rootfs.img/oem.img` 的 `ota_ab.tar`。
3. 准备 FAT 格式 SD 卡，根目录按用例放置 `env.img/idblock.img/uboot.img/boot.img/rootfs.img/oem.img`，不放 `sd_update.txt`；部分镜像用例按测试目标删除对应文件。
4. 确认板端有 `rk_ota`、`sha256sum`、`flash_lock`、`flash_erase`；涉及 U-Boot 正常升级前准备 Maskrom/USB 恢复条件和已验证的原始镜像。
5. 每条用例开始前执行 `sync`，记录 `rk_ota --misc=display`，确认当前槽处于可启动状态。

## 1. 分区、槽位和挂载一致性

在 U-Boot 记录：

```text
mtd list
printenv mtdparts
```

在 Linux 记录：

```sh
cat /proc/mtd
cat /proc/cmdline
ls -l /dev/block/by-name
mount
rk_ota --misc=display
```

预期 `/proc/mtd` 顺序和大小如下；起始地址同时由 U-Boot `mtdparts` 和 Linux cmdline 核对：

| MTD | 分区 | 起始地址 | 大小 |
|---|---|---:|---:|
| mtd0 | env | `0x0000000` | `0x0040000` |
| mtd1 | idblock | `0x0040000` | `0x0100000` |
| mtd2 | uboot | `0x0140000` | `0x0100000` |
| mtd3 | boot_a | `0x0240000` | `0x0400000` |
| mtd4 | boot_b | `0x0640000` | `0x0400000` |
| mtd5 | rootfs_a | `0x0A40000` | `0x0A00000` |
| mtd6 | rootfs_b | `0x1440000` | `0x0A00000` |
| mtd7 | oem_a | `0x1E40000` | `0x2000000` |
| mtd8 | oem_b | `0x3E40000` | `0x2000000` |
| mtd9 | reserved | `0x5E40000` | `0x01C0000` |
| mtd10 | misc | `0x6000000` | `0x0040000` |
| mtd11 | userdata | `0x6040000` | `0x1FC0000` |

一致性判定：

- cmdline 只有一个合法的 `androidboot.slot_suffix=_a` 或 `_b`。
- `_a` 时 `/dev/block/by-name/{boot,rootfs,oem}` 分别指向 A 槽；`_b` 时全部指向 B 槽。
- `/` 对应当前槽 rootfs，`/oem` 对应同一槽 oem；不得出现 rootfs A、oem B 的混用。
- `/userdata` 可挂载且不随槽位改变。

## 2. 正常写保护与 userdata 可写

正常启动后，对任一固件 MTD 执行只读锁状态检查：

```sh
flash_lock -i /dev/mtd3 0 -1
```

预期输出 `Lock status: locked`、`Return code: 1`。本项目 kernel 的 `MEMISLOCKED` 只有在 A0 精确等于 `0x2A` 时才返回 1，因此该结果同时验证正常保护值。A0=`0x2A` 按 Flash 数据手册表示 Lower 3/4 locked，即 `0x0000000`～`0x5FFFFFF` 的前 96 MiB。

在确认测试目标为非活动槽后，备份其首个擦除块，再尝试擦除。例如当前槽为 A 时使用 `boot_b=/dev/mtd4`：

```sh
dd if=/dev/mtd4 of=/tmp/boot_b.block0.before bs=131072 count=1
sha256sum /tmp/boot_b.block0.before
flash_erase /dev/mtd4 0 1
dd if=/dev/mtd4 of=/tmp/boot_b.block0.after bs=131072 count=1
sha256sum /tmp/boot_b.block0.after
```

预期 `flash_erase` 非零返回，前后 SHA-256 相同。若当前槽为 B，改用 `boot_a=/dev/mtd3`。若首块是已有坏块，本次结果无效，应改选同一非活动分区内由现有只读检测确认的好块；不得为测试修改坏块标记。严禁对当前启动槽、env、idblock 或 U-Boot 做该破坏性探测。

验证 userdata 经 UBI 正常持久化：

```sh
printf 'rv1106-write-protection-acceptance\n' > /userdata/.wp_acceptance
sync
sha256sum /userdata/.wp_acceptance
reboot
sha256sum /userdata/.wp_acceptance
rm -f /userdata/.wp_acceptance
sync
```

预期重启前后哈希相同，删除成功。`misc` 的可写性由后续 A/B 元数据切换用例验证，不对其做额外裸擦写。

## 3. SD 卡文件组合

每次测试先记录当前槽、`misc` 和目标分区哈希；SD 根目录只保留该行列出的文件，然后上电并保存 `ab_sd_update` 串口输出。文件组合和预期如下：

| SD 文件 | 写入行为 | 是否切槽 |
|---|---|---|
| 无 | 不写 Flash | 否 |
| uboot | 固定写单一 U-Boot | 否 |
| boot / rootfs / oem | 只写列出的非活动槽分区 | 否 |
| boot+rootfs / boot+oem / rootfs+oem | 只写列出的非活动槽分区 | 否 |
| uboot 加上述任一单件或两件组合 | 写列出分区和固定 U-Boot | 否 |
| boot+rootfs+oem | 写完整非活动槽 | 是 |
| uboot+boot+rootfs+oem | 写完整非活动槽和固定 U-Boot | 是 |

共覆盖 16 种 presence mask。每条用例都应满足：

- 目标槽由运行时 `misc` 决定，当前 A 写 B，当前 B 写 A。
- 部分槽镜像可以写入，但 `misc` 前后完全不变。
- 只有 boot/rootfs/oem 三件套齐全且全部写入、回读成功后才激活目标槽。
- 正常结束后 `flash_lock -i` 返回 locked；不出现 BBT/OOB/markbad 行为变化。
- U-Boot 文件始终写唯一 `uboot` 分区，不生成 `uboot_a/uboot_b`。

## 4. 网络 OTA A→B 和 B→A

先在 A 槽执行：

```sh
rk_ota --misc=now
rk_ota --misc=display
rk_ota --misc=update --tar_path=/tmp/ota_ab.tar --save_dir=/tmp/ota_ab --partition=all
rk_ota --misc=display
flash_lock -i /dev/mtd4 0 -1
sync
reboot
```

预期升级只写 `boot_b/rootfs_b/oem_b`，不接受 tar 中的 `uboot.img`；切换后 B priority=15、tries_remaining=7、successful_boot=0，A 保持可启动。重启后 cmdline、rootfs 和 oem 全部为 B；系统确认正常后执行 `rk_ota --misc=now` 并保存状态。

在 B 槽使用同一流程再次升级，预期只写 `boot_a/rootfs_a/oem_a`，重启后 cmdline、rootfs 和 oem 全部为 A。两个方向都需记录：

- 升级前后 `rk_ota --misc=display`。
- 三个目标分区写前/写后哈希及源镜像哈希。
- 升级结束和重启后的 lock status。
- 完整串口启动日志与当前槽挂载证据。

## 5. 写非活动槽期间断电

该项只验证既定语义，不实现断点续写：当前 A 写 B、当前 B 写 A；分别在 boot、rootfs、oem 写入期间切断电源。每次重新上电应满足：

- `misc` 尚未切换，仍启动原当前槽。
- cmdline、rootfs、oem 仍一致指向原槽。
- U-Boot/driver 初始化后 `flash_lock -i` 为 locked，即正常保护恢复为 A0=`0x2A`。
- 再次发起升级时从目标分区开头重新写，不从断点续写。
- 完整三件套重新写入并验证成功后才切换目标槽。

不在唯一 U-Boot 的擦除或写入期间断电。单副本 U-Boot 的断电安全不在本方案承诺范围内；只有具备独立可恢复夹具时，才可另行安排破坏性验证。

## 6. 结果记录

| 项目 | 结果 | 证据文件/摘要 |
|---|---|---|
| 分区表与地址 | 待测 | |
| A 槽 cmdline/rootfs/oem 一致 | 待测 | |
| B 槽 cmdline/rootfs/oem 一致 | 待测 | |
| 正常 A0=`0x2A` / 前 96 MiB 受保护 | 待测 | |
| userdata 持久可写 | 待测 | |
| SD 16 种文件组合 | 待测 | |
| OTA A→B | 待测 | |
| OTA B→A | 待测 | |
| 非活动槽写入断电后仍启动原槽 | 待测 | |
| 单副本 U-Boot 断电测试 | 不执行 | 设计限制，非通过项 |

全部必测项有板端日志、状态和哈希证据后，才能把该文档状态改为“实板通过”。

## 依据

- `docs/superpowers/specs/2026-07-14-rv1106-ab-upgrade-spinand-write-protection-design.md`
- `vendor/rv1106_sdk_patches/0001-ab-layout.patch`
- `vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch`
- `vendor/rv1106_sdk_patches/0003-kernel-protection.patch`
- `vendor/rv1106_sdk_patches/0004-rk-ota.patch`
- `helloagents/wiki/modules/rv1106_ab_upgrade.md`
