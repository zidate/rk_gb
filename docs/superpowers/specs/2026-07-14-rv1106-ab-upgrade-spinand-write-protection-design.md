# RV1106 A/B 升级与 SPI NAND 写保护设计

状态：方案已确认，尚未实施

日期：2026-07-14

平台：Rockchip RV1106G / XWR60440 / RC0240

存储器：CHUCUN C5F1GM7RE，128 MiB SPI NAND，2 KiB 页，128 KiB 擦除块

## 1. 目标

1. `boot/rootfs/oem` 使用 A/B 双分区。
2. 网络 OTA 由 Linux 写非活动槽，不更新 U-Boot。
3. SD 卡升级由 SDK U-Boot 内置升级器执行，可更新单一 U-Boot，并动态选择 A/B 目标槽。
4. 使用 SPI NAND A0 Block Lock 保护前 96 MiB 固件区域。
5. 升级非活动槽期间断电，不影响当前槽继续启动。
6. 复用 Rockchip SDK 的 `AvbABData`、7 次启动尝试和槽切换逻辑。

## 2. 范围边界

本设计明确排除：

- 项目 `driver/uboot/` 目录下的覆盖代码。
- `DG_sdupdate.c`。
- `sd_update.txt` 脚本升级路径。
- 旧 U-Boot 的 16 MiB 包大小限制和旧升级包兼容。
- 双文件迁移包设计。
- 按文件偏移断点续写；断电后重新从头写目标槽。
- 自定义联网健康检测和额外启动观察时间。
- 网络 OTA 更新 `uboot/env/idblock`。

U-Boot 相关修改以 `/home/jerry/lhy` 中参考 SDK 的正式 U-Boot 源码为基础。首次从旧 U-Boot 获得新 U-Boot 的兼容过程不在本设计范围内；新 U-Boot 运行后，`misc` 无效时默认初始化 A 槽。

## 3. 分区布局

| 分区 | 起始地址 | 大小 | 结束地址 | A0 保护 |
|---|---:|---:|---:|---|
| env | `0x0000000` | 256 KiB | `0x0040000` | 是 |
| idblock | `0x0040000` | 1 MiB | `0x0140000` | 是 |
| uboot | `0x0140000` | 1 MiB | `0x0240000` | 是 |
| boot_a | `0x0240000` | 4 MiB | `0x0640000` | 是 |
| boot_b | `0x0640000` | 4 MiB | `0x0A40000` | 是 |
| rootfs_a | `0x0A40000` | 10 MiB | `0x1440000` | 是 |
| rootfs_b | `0x1440000` | 10 MiB | `0x1E40000` | 是 |
| oem_a | `0x1E40000` | 32 MiB | `0x3E40000` | 是 |
| oem_b | `0x3E40000` | 32 MiB | `0x5E40000` | 是 |
| 对齐保留区 | `0x5E40000` | 1.75 MiB | `0x6000000` | 是 |
| misc | `0x6000000` | 256 KiB | `0x6040000` | 否 |
| userdata | `0x6040000` | 31.75 MiB | `0x8000000` | 否 |

约束：

- 所有分区按 128 KiB 擦除块对齐。
- `uboot` 只有一个，地址保持 `0x140000`。
- `rootfs` 每槽 10 MiB。
- `oem` 每槽 32 MiB。
- U-Boot 默认分区表、Linux `mtdparts`、`/proc/mtd` 和打包配置必须一致。

## 4. A/B 元数据

沿用 SDK `AvbABData`：

- 位置：`misc + 2 KiB`。
- 最大尝试次数：7。
- 字段：priority、tries_remaining、successful_boot、last_boot、CRC32。

首次初始化或 `misc` CRC 无效时：

| 槽 | priority | tries_remaining | successful_boot |
|---|---:|---:|---:|
| A | 15 | 7 | 0 |
| B | 0 | 0 | 0 |

这需要调整 SDK 的无效元数据默认初始化行为。参考 SDK 当前 `avb_ab_data_init()` 会把 B 也初始化为可启动，不能满足 B 为空或元数据损坏时只启动 A 的要求。

普通槽切换继续使用 SDK 语义：

- 当前槽 priority 调整为 14。
- 目标槽 priority 设置为 15。
- 目标槽 tries_remaining 设置为 7。
- 目标槽 successful_boot 设置为 0。

Linux 正常启动后调用 SDK 的 `rk_ota --misc=now`，沿用 `RETRY_BOOT` 行为。

## 5. 网络 OTA

网络 OTA 在 Linux 用户态执行：

1. 校验升级包并确认包含完整的 `boot.img/rootfs.img/oem.img`。
2. 读取 `misc`，确定当前槽和非活动槽。
3. 使用 Linux SPI NAND 驱动解除 A0 Block Lock。
4. 通过 MTD 坏块感知路径写非活动槽。
5. 检查镜像大小：boot 不超过 4 MiB，rootfs 不超过 10 MiB，oem 不超过 32 MiB。
6. 回读检查写入结果。
7. 恢复并读回确认 `A0=0x2A`。
8. 最后更新 `misc`，把目标槽设置为活动槽。
9. 重启。

网络 OTA 包不包含并且升级器拒绝写入：

```text
uboot
env
idblock
misc 镜像
userdata
```

## 6. SD 卡升级入口

SD 卡升级由新 SDK U-Boot 内置升级器执行，不使用脚本或单一容器文件。U-Boot 在 SD 卡 FAT 分区根目录扫描固定文件名：

```text
uboot.img
boot.img
rootfs.img
oem.img
```

每个文件均可不存在。存在的文件必须先检查长度是否适合目标分区。

目标选择：

- 当前槽为 A：`boot/rootfs/oem` 写 B。
- 当前槽为 B：`boot/rootfs/oem` 写 A。
- `misc` 无效或未初始化：目标为 A。
- `uboot.img` 始终写唯一的 `uboot` 分区，不带槽后缀。

## 7. SD 文件组合规则

### 7.1 只有 `uboot.img`

- 解除保护。
- 写固定 U-Boot 分区并回读校验。
- 恢复 `A0=0x2A`。
- 不修改 `misc`。
- 重启。

### 7.2 只有部分槽镜像

例如只有 `rootfs.img` 或只有 `oem.img`：

- 写对应的非活动分区。
- 写入成功后恢复保护。
- 不修改 `misc`，不切换槽。
- 后续完整升级将重新覆盖目标槽的三个组件。

### 7.3 完整槽镜像

只有以下三个文件全部存在时，才允许切换槽：

```text
boot.img
rootfs.img
oem.img
```

全部写入并校验成功、写保护恢复成功后，最后更新 `misc`。

### 7.4 同时包含 U-Boot 和完整槽镜像

顺序为：

1. 写目标槽的 boot/rootfs/oem。
2. 写单一 U-Boot 分区。
3. 回读校验。
4. 恢复 `A0=0x2A`。
5. 最后更新 `misc`。
6. 重启。

U-Boot 在 RAM 中运行，可以写自己的 Flash 分区；但单副本 U-Boot 在擦写过程中断电仍可能导致设备无法启动，此风险无法由 boot/rootfs/oem A/B 机制消除。

## 8. 断电行为

### 8.1 写非活动槽时断电

以 A 当前运行、写 B 为例：

- 写 B 期间不修改 A。
- B 全部完成前不修改 `misc`。
- 断电后 `misc` 仍选择 A。
- 下次启动继续运行 A。
- 再次升级时从头擦写 B，不从上次偏移续写。

### 8.2 恢复保护前断电

- `misc` 尚未切换，仍启动当前槽。
- Flash 上电后的保护寄存器恢复芯片缺省锁定状态。
- U-Boot 每次启动主动设置并确认正常策略 `A0=0x2A`。

### 8.3 更新 `misc` 时断电

`misc` 只会在目标槽三个镜像完整且校验成功后写入。如果 CRC 无效，U-Boot 使用 A-only 默认值：

- A 当前、B 为目标时，回到未修改的 A。
- B 当前、A 为目标时，A 已在写 `misc` 前完整写入，因此选择 A 仍可启动。

### 8.4 切换完成后断电

目标槽已完整，正常启动目标槽。若目标槽无法正常启动，沿用 SDK 7 次尝试机制选择另一可启动槽。

### 8.5 单 U-Boot 风险

在 `uboot.img` 擦除或写入过程中断电，可能损坏唯一 U-Boot。设计仅通过“最后写 U-Boot、回读校验”缩短风险窗口，不承诺单 U-Boot 的断电安全。

## 9. SPI NAND 写保护

Flash 数据手册的 A0 Protection Feature Register 位定义为 BRWD、BP2:0、INV、CMP。使用：

```text
升级解锁：A0 = 0x00
正常保护：A0 = 0x2A
```

`0x2A` 对应 Lower 3/4 locked，保护范围：

```text
[0x0000000, 0x6000000)
```

该范围覆盖 env、idblock、uboot、两套 boot/rootfs/oem 和对齐保留区。`misc/userdata` 位于 96 MiB 之后，可正常写入。

保护要求：

- U-Boot 和 Linux SPI NAND 驱动都提供 A0 读取、设置和验证接口。
- U-Boot 每次正常启动设置 `A0=0x2A`。
- 网络 OTA 和 SD 升级仅在写固件期间使用 `A0=0x00`。
- 固件写入结束后先恢复保护，再修改 `misc`。
- 不设置 B0[3] BPL。
- 不改变 B0[0] QE，不启用外部 WP# 模式。
- 不修改硬件。

## 10. 代码范围

### SDK U-Boot

- 新分区表和 A/B 配置。
- A/B 元数据无效时的 A-only 默认策略。
- 内置 SD 文件扫描及升级器。
- SPI NAND 坏块感知写入和回读。
- CHUCUN A0 解锁、保护和验证。
- 每次启动恢复 `A0=0x2A`。

### Linux SDK

- CHUCUN SPI NAND A0 lock/unlock/is_locked 支持。
- `rk_ota` 仅写非活动 boot/rootfs/oem。
- `rk_ota` 写入前解锁，写入后恢复保护，最后更新 `misc`。

### 打包

- 分区表改为 rootfs 10 MiB、oem 32 MiB。
- 网络 OTA 输出完整 boot/rootfs/oem 包。
- SD 卡输出独立的可选镜像文件。
- 镜像大小在打包阶段检查。

### 不修改

- 项目 `driver/uboot/`。
- `DG_sdupdate.c`。
- `sd_update.txt`。

## 11. 验证矩阵

### 分区与启动

- U-Boot、kernel cmdline 和 `/proc/mtd` 地址一致。
- U-Boot 仍位于 `0x140000`，只有一个分区。
- rootfs 每槽 10 MiB，oem 每槽 32 MiB。
- `misc` 无效时只启动 A，B 不可启动。

### 网络 OTA

- A 运行时完整升级 B 并切换。
- B 运行时完整升级 A 并切换。
- 网络包包含 U-Boot 时拒绝升级。
- 写目标槽期间断电后仍启动当前槽。

### SD 升级

- 只放 `uboot.img`：只更新 U-Boot，不修改 `misc`。
- 只放一个或两个槽镜像：写非活动槽但不切换。
- 三个槽镜像齐全：完整写入后切换。
- 同时包含 U-Boot 和三个槽镜像：按规定顺序写入并切换。
- 不依赖 `sd_update.txt`。

### 写保护

- 正常状态 A0 读回 `0x2A`。
- 正常状态前 96 MiB PROGRAM/ERASE 失败。
- `misc/userdata` 保持可写。
- 网络 OTA 和 SD 升级能够临时解锁并完成写入。
- 升级结束后 A0 恢复 `0x2A`。
- Quad-SPI 工作不受影响。

### A/B 回退

- 新槽初始 tries_remaining 为 7。
- Linux 正常启动后刷新当前槽状态。
- 新槽连续启动失败后选择另一可启动槽。

## 12. 验收标准

1. 分区布局与第 3 节完全一致。
2. 网络 OTA 不更新 U-Boot，且只能在完整三件套成功后切槽。
3. SD 卡使用独立镜像文件，由 U-Boot 动态选择目标槽。
4. 部分 SD 镜像不触发槽切换。
5. 非活动槽升级断电后当前槽仍能启动。
6. `misc` 无效时只启动 A。
7. A0 正常值为 `0x2A`，前 96 MiB 被硬件保护。
8. `misc/userdata` 可写，BPL 未设置，硬件未修改。
9. 项目 `driver/uboot/`、`DG_sdupdate.c` 和 `sd_update.txt` 未被纳入实现。
