# 变更提案: RV1106 升级临时解锁与 96 MiB 常态保护

## 需求背景
RV1106 的 C5F1GM7RE SPI NAND 正常运行时需要保持 A0=`0x2A`，即保护前 96 MiB。当前 SD/OTA 写镜像时虽会临时解锁，但写完立即重锁，导致新写入的 UBI rootfs/oem 尚未处理 autoresize，Linux 首启擦除 PEB 时返回 `-EIO`。

## 独立品审
- **原始想法:** 保持前 96 MiB 保护，仅在升级时临时解锁；SD 增加 env/idblock，OTA 只允许 kernel/rootfs/oem。
- **品审结论:** 修正。保护范围与升级白名单合理，但临时解锁窗口必须覆盖 UBI attach/autoresize，不能只覆盖原始镜像写入。
- **证据:** `debug.log` 显示 `rootfs_b` 首次 attach 时 `autoresize` 擦除 PEB 69 返回 `-5`；源码确认 U-Boot 和 kernel 都恢复 A0=`0x2A`。
- **关键假设:** U-Boot 启用 UBI 命令后可以对新写入的 rootfs/oem 分区 attach 并完成 autoresize；Linux OTA 可通过 `/dev/ubi_ctrl` 完成同一初始化。
- **替代方案:** 跨重启保持 NAND 解锁会扩大故障窗口并引入状态协调，暂不采用。

## 变更内容
1. SD updater 扫描并写入 `env.img`、`idblock.img`，保留现有六类镜像的独立可选语义。
2. SD/OTA 在解锁窗口内对新写入的 rootfs/oem 执行 UBI attach/detach，成功后恢复 A0=`0x2A`。
3. OTA 显式拒绝 `env.img`、`idblock.img`、`uboot.img`，仅允许 boot(kernel)/rootfs/oem。

## 影响范围
- **模块:** U-Boot SD updater、rk_ota、SPI NAND 保护策略、补丁回归测试。
- **文件:** `vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch`、`0004-rk-ota.patch`、测试与文档。
- **API:** 无外部 API 变更。
- **数据:** SD 镜像白名单增加 env/idblock；OTA 白名单不变并加强拒绝检查。

## 核心场景

### 需求: SD 升级临时解锁
**模块:** RV1106 A/B 升级

#### 场景: SD 含启动链与槽镜像
所有镜像先校验，再解锁、写入、校验并完成 UBI 初始化。
- 所有退出路径尝试恢复 A0=`0x2A`。
- boot/rootfs/oem 三件套成功后才激活非活动槽。

### 需求: OTA 最小升级白名单
**模块:** RV1106 A/B 升级

#### 场景: 网络 OTA 包升级非活动槽
- 仅处理 boot/rootfs/oem。
- 发现 env/idblock/uboot 任一镜像即拒绝事务。
- rootfs/oem 完成 UBI 初始化后才重锁并激活槽。

## 风险评估
- **风险:** env/idblock/uboot 均非 A/B，写入期间断电可能导致设备无法启动。
- **缓解:** 启动链镜像最后写入、逐页回读校验、失败不激活目标槽，并在交付说明中明确需要 MaskROM 恢复条件。
