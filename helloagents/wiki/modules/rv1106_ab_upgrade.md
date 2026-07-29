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

- 网络 OTA：Linux 只写非活动 boot/rootfs/oem；其中 kernel 与 DTB 的镜像名是 `boot.img`。发现 `env.img/idblock.img/uboot.img` 任一文件即拒绝事务。
- SD 卡：SDK U-Boot 扫描 `env.img/idblock.img/uboot.img/boot.img/rootfs.img/oem.img`，不使用 `sd_update.txt`。
- SD 扫描沿用厂商兼容口径：访问 `mmc 1` 前临时把块设备描述符设为 `PART_TYPE_DOS`，使 MBR/FAT 卡不会被 Rockchip 私有分区类型误判；扫描、读取或升级结束后恢复原值。
- 单个或部分槽镜像只写非活动槽，不切换。
- boot/rootfs/oem 三件套全部写入并校验成功后才切换。
- `uboot.img` 写固定单一 U-Boot 区，不参与槽选择；U-Boot SD updater 通过 `spi-nand0` 主设备将其限制在 `0x140000+0x100000` 物理区间。
- `env.img/idblock.img` 分别限制在 `0x000000+0x040000`、`0x040000+0x100000` 物理区间，不依赖 U-Boot 是否创建同名 MTD 子设备；它们不参与槽选择，并与 `uboot.img` 一样禁止进入网络 OTA 包。
- `download.bin` 是 SDK 将 `rv1106_download_v1.15.108.bin` 重命名后的 Rockchip 下载 loader，内含 DDR 初始化、USB plug 和 SPL loader，供 MaskROM/USB 工厂烧录工具（如 `rkdeveloptool db`）加载；当前 SD A/B updater 不读取它，放入 SD 卡也不会触发升级。
- 项目 `driver/uboot/` 和 `DG_sdupdate.c` 不属于实现范围。

## 2026-07-22 SD 升级故障

- `debug.log` 中新的 SPL、U-Boot、kernel 和 12 个 A/B 分区均已启动，说明新固件与分区表已经生效。
- 升级在读取首个镜像前报 `Unrecognized filesystem type` 和 `Failed to select SD FAT before probing uboot.img`；根因是新命令没有继承厂商旧 SD updater 临时强制 `PART_TYPE_DOS` 的兼容处理，不是 `uboot.img` 内容或镜像组合问题。
- FAT 选择失败后没有写入 `rootfs_a`，因此后续 `ubi_read_volume_table: the layout volume was not found` 是升级未执行的结果。
- 修复后所有 FAT 探测和分块读取都在 DOS 分区解析窗口内完成，所有失败与成功路径均恢复原块设备类型。

## 2026-07-23 MTD 后缀解析故障

- 最新 `debug.log` 已显示四个 SD 镜像均校验成功，说明 FAT/DOS 修复已生效；剩余错误为 `Unexpected character 'K' in mtdparts` 和 `MTD partition boot_b not found`。
- 根因是 `drivers/mtd/mtdpart.c` 原先用 `ustrtoull()` 解析 size/offset，只消费数字和十六进制前缀，不消费 `K/M` 后缀。
- `0005-uboot-mtdparts-unit-suffix.patch` 现在在 parser 内增加与 `cmd/mtdparts.c` 一致的二进制单位转换，从 `simple_strtoull()` 开始并支持大小写 `K/k/M/m/G/g`；不再修改 `project/build.sh` 或分区布局。
- 12:00 上板日志进一步暴露初版 helper 的实现错误：它以 `ustrtoull()` 为底层，而本 SDK `lib/strto.c::ustrtoull()` 已经换算 `K/M/G`、只是没有推进单字符后缀；helper 再次移位导致 `256K` 被重复换算成 256 MiB，`env` 被截断为整颗 128 MiB NAND，后续分区全部 `out of reach`。修正版已像 `cmd/mtdparts.c::memsize_parse()` 一样改从 `simple_strtoull()` 开始，再由 helper 统一换算和推进后缀。
- 14:23 复测日志仍显示 SPL/U-Boot 编译时间 `Jul 23 2026 - 10:38:11` 和旧 FIT hash `bdd5e3fc26...`，而重复换算修正版构建于 12:08、FIT hash 为 `cb0cc405...`，说明修正版尚未刷入。该日志的 `present mask 0x7` 也表明 SD 仅识别 `boot/rootfs/oem` 三件套，未识别 `uboot.img`；即使补放文件，当前旧 parser 也无法找到有效 `uboot` 分区完成自更新，仍需通过工厂/USB烧录路径替换。
- 14:40 复测已启动 12:08 修正版，三件套写入成功并激活 `_b`；随后 Linux 在 `ubi.mtd=6` 挂载 `rootfs_b` 时执行 autoresize，擦除 PEB 69 返回 `-EIO`。该 PEB 对应约 `0x1CE0000`，位于 A0=`0x2A` 锁定的前 96 MiB 内；C5F1GM7RE 数据手册明确说明锁定块的 ERASE 会置 `E_FAIL`。因此这不是后缀解析或 SD 写入问题，而是升级在 UBI 首次 attach 前过早恢复 Lower 3/4 硬件保护。
- 保留前 96 MiB 常态保护的修复路径是把解锁窗口延长至 UBI 初始化结束：SD 在 U-Boot 中对刚写入的 rootfs/oem 执行 `ubi part`/`ubi detach`，OTA 在 Linux 中执行 `UBI_IOCATT`/`UBI_IOCDET`，由 attach 同步消费 autoresize 标志，然后才恢复 A0=`0x2A` 并激活目标槽。解锁状态不跨重启。
- 15:49 复测中三件套已写入 `boot_a/rootfs_a/oem_a`，但 `ubi part rootfs_a` 报 `Error initializing mtdparts`、`incorrect device type in rootfs_a`。旧版 U-Boot 的 `cmd/mtdparts.c` 除 `mtdparts` 外还要求 `mtdids` 设备映射；板级 defconfig 只有 `CONFIG_CMD_UBI=y`，默认环境又没有 `mtdids`，分区列表为空后 `find_dev_and_part()` 才把分区名误当设备 ID。修复为 `CONFIG_MTDIDS_DEFAULT="spi-nand0=spi-nand0"`，不修改写保护或 C 事务逻辑。
- 2026-07-24 复测中新 U-Boot、三件套和 12 个 Linux 分区均已识别，但 `ubi part rootfs_b` 先报 `mtdparts variable doesn't start with 'mtdparts='`。正常 `./build.sh env` 的真实链路是 `project/build.sh::parse_partition_file()` 先写 `output/image/.env.txt`，再由 `build_env()` 调 `mkenvimage`；它不会进入 `sysdrv/Makefile:625`。旧主入口只生成单层 `mtdparts=spi-nand0:...`，环境导入后的变量值缺少旧式 `cmd/mtdparts.c` 要求的 `mtdparts=` 前缀。`0008-uboot-env-mtdparts-prefix.patch` 现同时修正主入口和 `make -C sysdrv env` 兼容入口，生成 `mtdparts=mtdparts=spi-nand0:...`；第一层是 env 键名，第二层是 parser 前缀。
- 该错误发生在 U-Boot 写入阶段的 UBI 初始化，故本轮 `boot/rootfs/oem` 没有完成升级；Linux 后续 `ubi.mtd=5` 的 autoresize 擦除 `PEB 69` 返回 `-EIO` 是失败后的连锁现象。应用 0008 后必须重新生成并投放 `env.img`，不能只替换 `uboot.img`。由于 updater 的固定分区写入排在 UBI 初始化之后，当前板子应先用只含新 `env.img` 的 SD 卡更新并重启，再放 `boot/rootfs/oem` 三件套；若仍是更旧且不识别 `env.img` 的 U-Boot，则先增加只含 `uboot.img` 的第一阶段。
- 10:49 复测日志显示新 U-Boot 编译时间为 `Jul 24 2026 - 10:42:58`，但 SD 同时识别 `env.img` 与三件套，`present mask 0x39`。updater 仍先写三件套并在固定镜像写入前执行 `ubi part rootfs_b`，因此 Flash 内旧 `mtdparts` 再次触发同一错误，`env.img` 实际没有落盘。恢复步骤不是继续改代码，而是清空 SD 目录后只保留已确认内容为 `mtdparts=mtdparts=spi-nand0:...` 的新 `env.img`，成功写入并断电重启后，第二阶段才放三件套。
- 第二阶段即使再次放入最新 `env.img`，也不会刷新当前 U-Boot 已加载的内存环境；它只会在 UBI 初始化成功后作为固定分区镜像再次写入。判断第一阶段是否真正完成，必须看重启后的 U-Boot 是否不再报 `mtdparts variable doesn't start with 'mtdparts='`，不能只看 SD 卡上的文件版本。
- 现场更新必须分两次：先让 SD 只保留修正版 `uboot.img` 完成自更新并重启，再放 boot/rootfs/oem 三件套。旧 U-Boot 若同时看到三件套，会先在 UBI 初始化处失败并跳到重锁，固定分区写入阶段尚未执行，因此同卡的新 `uboot.img` 也不会被刷入。

## 2026-07-24 固定区写入与槽切换故障

- 最新 `debug.log` 两次启动都显示六个镜像完成校验、目标槽为 `_b`、rootfs/oem UBI 初始化成功，随后固定写入报 `MTD partition env not found (err = -19)`；kernel cmdline 两次仍为 `androidboot.slot_suffix=_a`。这证明 SD 文件识别和 A/B 目标计算正常，事务是在固定镜像阶段失败。
- `ab_sd_activate_slot()` 位于全部写入、UBI 初始化和恢复前 96 MiB 保护之后。固定镜像失败会直接返回，因此“不升级 env/idblock/uboot”和“不切换 A/B”是同一故障链，不应在失败时强行切槽。
- 修复后固定镜像改用 `spi-nand0` 主设备加严格 raw range，坏块容量统计、擦除、写入和校验都只遍历镜像所属区间；`boot/rootfs/oem` 仍按非活动槽命名分区写，避免扩大擦除范围。
- 日志中的 `FAT: Misaligned buffer address (07012710)` 来自普通 `malloc()` 缓冲区未满足 FAT/DMA 对齐要求；两个事务缓存现由 `memalign(ARCH_DMA_MINALIGN, ...)` 分配。
- 成功时依次可见 `Wrote and verified ...` 与 `Activated A/B slot _b for next boot`。后者修改 `misc` 中的 AVB A/B 元数据，当前已选定的本次 FIT 不会中途换槽，下一次重启才从 `_b` 启动。
- 当前仍按此前决定不写持久完成标记；成功日志出现后应移除 SD 卡或升级文件，否则后续启动会再次把当时的非活动槽作为目标。
- 0010 起，切槽触发条件改为本次升级存在 `oem.img`。因此 OEM-only 升级成功也会激活目标槽；仅有 env/idblock/uboot 或 boot/rootfs 时不会切槽。切槽仍位于事务完全成功、恢复保护之后，任何失败都保持当前槽。

## 断电语义

- 写非活动槽期间不修改 `misc`；断电后继续启动当前槽。
- 再次升级从头写目标槽，不进行断点续写。
- `misc` CRC 无效时默认 A 可启动、B 不可启动。
- 单副本 U-Boot 擦写期间断电仍有无法启动的固有风险。

## 2026-07-29 ota.bin 网络 OTA 容器

- 网络 OTA 交付物统一为 `Release/ota.bin`，构建链路不再生成 `upgrade.tar.gz` 或 `ota_ab.tar`；SD 工厂镜像目录仍保留独立的 `uboot.img/boot.img/rootfs.img/oem.img`。
- 容器兼容历史 `packaging-update` 格式：32 字节包头包含 20 字节平台名 `rv1106`、网络字节序 magic `0xABCD1234`、payload CRC32 和 payload 长度；每个镜像前有 12 字节网络字节序的类型、对齐后大小和起始地址。
- 容器类型固定为 boot=4、rootfs=5、oem=6；起始地址固定为 `0x0240000/0x0A40000/0x1E40000`，镜像上限分别为 4/10/32 MiB。`upgrade.ini` 中的物理分区 `type` 不作为容器类型使用。
- Host `packaging-update` 只接受 `[boot]/[rootfs]/[oem]` 三件套，镜像按 4 字节对齐并补 `0xFF`，对整个镜像头和数据 payload 计算标准 CRC32（多项式 `0xEDB88320`）。
- 应用 `OtaPackage` 在调用 `rk_ota` 前校验平台、magic、文件总长、CRC、类型唯一性、镜像地址和大小，再把三镜像分别写入 `/tmp` 同目录隐藏临时文件；三者全部写入并 `fsync` 成功后，才发布为 `/tmp/boot.img`、`/tmp/rootfs.img`、`/tmp/oem.img`。任一步失败都会清理临时文件和整套固定镜像，旧文件或半套新文件不会进入升级。
- `AbUpdateApply()` 串行化应用升级事务，并通过 `rk_ota --misc=update --save_dir=/tmp --partition=all` 直接读取已准备镜像；`rk_ota` 仍保留 `--tar_path` 兼容模式，但目录模式不再执行 tar 解包。`rk_ota` 返回后应用清理三镜像。
- GB 下载路径为 `/tmp/ota.bin.download`，完整性检查通过后原子改名为 `/tmp/ota.bin`。本地 demo 删除 `/tmp/test_ota` 触发文件后调用 `/mnt/sdcard/ota.bin`，避免每秒重复触发。
- 中国移动 `demo_upgrade_callback` 对 FW/APP 命令统一按整包 `ota.bin` 处理：回调只校验和复制参数、拒绝并发任务，后台线程使用无 shell 参数拼接的 curl 下载，校验 SDK 下发的 MD5，依次上报下载/安装状态后调用 `AbUpdateApply()`。
- 直接镜像模式取消了临时 USTAR 和 `rk_ota` 解包副本，但下载文件与三镜像准备期间仍会同时占用 `/tmp`；量产镜像需按实际包和镜像大小验证空间。
- 2026-07-29 验证：84 项 `tools/tests` 回归通过；OtaPackage/AbUpdate/OtaDownload host 严格编译、ChinaMobile 整文件语法检查、rk_ota ARM 语法检查以及 ARM GNU 8.3.0 整机交叉编译均成功。

## 写保护

- 升级解锁：A0=`0x00`。
- 正常保护：A0=`0x2A`，Lower 3/4 locked，即前 96 MiB。
- 升级解锁窗口覆盖原始写入、逐页回读校验和 rootfs/oem UBI attach/detach；任何失败均不激活目标槽，并尝试恢复 `0x2A`。
- `misc/userdata` 位于保护范围外。
- 不设置 BPL，不切换外部 WP# 模式，不修改硬件。
- 限制：UBI 后续若因 bitflip scrub、坏块替换或磨损均衡再次需要写保护区，仍可能触发只读降级；当前实现解决的是新镜像首次 autoresize 与重锁时序冲突，常态 96 MiB 保护是产品约束而非 UBI 的通用推荐配置。

## SDK 实编验证

- 验证日期：2026-07-15；Rockchip 实际链接 A/B 初始化器修正于 2026-07-16 重新验证；SD FAT/DOS 兼容修复于 2026-07-22 重新实编；U-Boot MTD 后缀解析修复于 2026-07-23 重新实编。
- 板级配置：`BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`。
- 补丁按 `vendor/rv1106_sdk_patches/series` 中的 0001～0009 顺序应用。
- 当前补丁序列新增 0005：在 U-Boot `drivers/mtd/mtdpart.c` 中复用 `K/M/G` 二进制单位语义，解析分区 size 和 offset 的大小写后缀；`project/build.sh` 保持原样，运行时 `mtdparts` 继续使用 `256K/1M` 可读格式。
- `CONFIG_AVB_LIBAVB_AB` 在此 SDK 实际链接 `lib/avb/rk_avb_user/rk_ab_ops_user.o`；无效 misc 初始化已在该对象中设为 A priority=15/tries=7、B priority=0/tries=0。
- `project/build.sh uboot` 成功，生成 U-Boot、SPL/TPL、ITB、loader 和 idblock；最终 ELF 中存在 `ab_sd_update`、`spinand_set_block_lock`、`avb_ab_data_init`、`avb_ab_mark_slot_active` 及 libavb user ops。
- `project/build.sh kernel` 成功，生成 `boot.img` 和 `vmlinux`；`vmlinux` 中存在 `spinand_get_block_lock`、`spinand_set_block_lock` 和 MTD lock/unlock 处理函数。
- `make -C sysdrv/tools/board/rk_ota distclean all` 成功，产物为 32-bit ARM EABI5/uClibc 可执行文件。
- 2026-07-22 关键产物 SHA-256：`uboot.img`=`375414f9091cf0697b91c180d1455a2183acf13e1f4ebbbfef7729616d0bcc8d`，`idblock.img`=`40d61517e7129dff2b5bbce16dc5a42e1a479a2261970f57f98c5becbe0c20b1`；未重建的 `boot.img` 与 `rk_ota` 沿用前次验证结果。
- 2026-07-23 重复换算修复后重新构建：`uboot.img`=`9e5dfe869aaa83655187b17fcf040953c4b9986b7735c14566325b5c939ad451`，`idblock.img`=`dfa016a548a894430681833234f9760da73504046aa9e8774fac1e76727b66f0`，`download.bin`=`1d49967e375743631613ed3d06bb20b985be7db2136cec07b362eb66d8a6cfb2`。当时的 `env.img`=`40f4abc72e78088e237b168c3cc308e59b58f07ae5b2e371fd1b188a55dcbfe1` 含单层 `mtdparts=spi-nand0:...`，已被 0008 判定为不可继续使用；`boot.img`=`eaddad612f01b9346daa713127658e127f1f3f924e3a368147cc4b723615083a`。
- 2026-07-23 临时解锁/UBI 初始化修复后重新构建：`uboot.img`=`1c783213ba3807597d7df9d66d9869ab1b3e516a88b876ef2c450121de274e81`，`idblock.img`=`40463c554c5a14ae612307518cec71e1a69a611ba059896a8a133039ca8e0c20`，`rk_ota`=`497d715349358f7992bdfd1fdf5681c3f3defb10509e0578316349ea40635c49`；U-Boot FIT 中压缩后的 U-Boot 数据为 171610 字节，仍低于 1 MiB 分区上限。63 项 RV1106 回归通过。
- 2026-07-24 修正真实入口后，0008 在 SDK 树上同时通过 `project/build.sh` 与 `sysdrv/Makefile` 的干净反向检查，34 项 RV1106 策略回归通过。当前 SDK 归档缺 ARM toolchain，因此仅为 env-only 路径提供 `gcc --version` 占位以绕过脚本的全局门禁；`./build.sh env` 随后真实执行 `parse_partition_file()`、`build_env()` 和 SDK host `mkenvimage`，生成的 `.env.txt` 首行及 262144 字节 `env.img` 均含 `mtdparts=mtdparts=spi-nand0:...`，镜像 SHA-256=`dfdcd3d0ec875806f7ee489e2cd933249c83064585974dabbe4d15a75ca5ad3f`。
- 2026-07-24 固定 raw range 与 FAT 对齐修复后，35 项 RV1106 补丁/策略回归通过，`project/build.sh uboot` 完整成功；`uboot.img`=262144 字节、SHA-256=`32790494096bb2beb49705b457ca8cc45471bb0c9212e3e63d8bae40ac11afa3`，`idblock.img`=176128 字节、SHA-256=`96ddec15feb836652771515bd7ffc0fbdaac22d6533853a52a140dd6a8d26abe`。最终 ELF 含固定镜像写入成功与下一次启动槽激活日志，FIT 中压缩 U-Boot 数据为 172671 字节。
- 2026-07-24 0010 验证 OEM 触发切槽逻辑：36 项 RV1106 补丁/策略回归通过，`ab_sd_should_switch()` 仅在 `AB_SD_OEM` 置位时返回真；扫描日志改为明确输出 `SD update contains oem` 或 `without oem`。
- 原始 SDK 归档存在损坏，完整解压最终返回 tar exit 2；本次定向提取的 `project/sysdrv`、全部补丁目标及三个构建目标完整。归档中未参与本功能的其他组件不能据此视为已验证。

## 证据

- Git 交付目录：`vendor/rv1106_sdk_patches/`；`series`/`apply.sh` 提供 0001～0010 顺序应用，`source/` 提供全部 18 个最终源码快照用于补丁上下文不一致时逐文件比对或覆盖。

- 启动日志：`/home/jerry/silver/serial_14.log`。
- 原理图：`/home/jerry/silver/SC1555-B-MAIN_V1.1_20260703.pdf` 第 8 页。
- Flash 数据手册：`/home/jerry/silver/FLASH_CHUCUN-C5F1GMxE-DS(2)(1).pdf`。
- 参考 SDK：`/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz`。
- 详细设计：`docs/superpowers/specs/2026-07-14-rv1106-ab-upgrade-spinand-write-protection-design.md`。
