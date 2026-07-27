# 技术设计: RV1106 升级临时解锁与 96 MiB 常态保护

## 技术方案

### 方案取舍
- **采用:** 在升级事务内完成 UBI attach/autoresize，再恢复 A0=`0x2A`。
- **修正:** 将“升级时临时解锁”扩展为“镜像写入、回读校验和 UBI 初始化期间解锁”。
- **拒绝/暂缓:** 不把解锁状态跨重启传递给 kernel，避免断电状态机和长期未保护窗口。

### 核心技术
- U-Boot 2017.09 MTD/SPI NAND/UBI。
- Linux `MEMUNLOCK`/`MEMLOCK` 与 `UBI_IOCATT`/`UBI_IOCDET`。

### 实现要点
- U-Boot 增加 `CONFIG_CMD_UBI=y`、关闭不需要的 UBIFS 命令；rootfs/oem 原始写入后执行 `ubi part` 和 `ubi detach`。
- SD 表新增 env/idblock，槽镜像仍映射到非活动槽；非槽镜像写固定分区。
- rk_ota 在 MTD 事务中 attach/detach 新写入的 rootfs/oem，失败则不切槽；显式拒绝三种启动链镜像。
- U-Boot 和 kernel probe 的常态保护值继续使用 `BL_LOWER_3_4_LOCKED` (`0x2A`)。

## 架构决策 ADR

### ADR-20260723-01: 在升级进程内消费 UBI autoresize 标志
**上下文:** 新 UBI 镜像首次 attach 必须修改布局卷，而恢复 96 MiB 硬件保护后无法擦除相关 PEB。
**决策:** SD 在 U-Boot 内、OTA 在 Linux updater 内完成目标 rootfs/oem 的首次 attach/detach。
**理由:** 解锁窗口不跨重启，成功后立即恢复既有硬件保护策略。
**替代方案:** 跨重启解锁并由用户空间重锁 → 拒绝原因: 状态和断电恢复复杂，保护窗口不可控。
**影响:** U-Boot 体积增加 UBI 支持；升级耗时增加一次 UBI 扫描。

## 安全与性能
- **安全:** 先校验全部镜像；失败不激活槽；所有可恢复路径重锁并读取验证；启动链升级明确不具备断电原子性。
- **性能:** UBI attach 仅在 rootfs/oem 被更新时执行，增加的扫描时间仅发生在升级流程。

## 测试与部署
- **测试:** 补丁精确应用、SD 64 种 present mask、OTA 白名单/调用顺序、真实 SDK U-Boot/kernel/rk_ota 构建。
- **部署:** 生成新的补丁包与 U-Boot/rk_ota 产物，SD 实机日志应在重锁前出现 UBI autoresize 成功信息。
