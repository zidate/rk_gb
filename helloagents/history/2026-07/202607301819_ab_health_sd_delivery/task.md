# 任务清单: RV1106 A/B 健康确认与 SD 固定镜像交付

目录: `helloagents/history/2026-07/202607301819_ab_health_sd_delivery/`

---

## 1. 启动健康确认
- [√] 1.1 删除 `App/Main.cpp`、`App/Update/AbUpdate.cpp/.h` 的应用延迟确认线程和包装函数，保留 OTA 升级调用。
- [√] 1.2 在 `S20linkmount` 的 OEM 挂载成功后、userdata 和应用启动前同步执行 `/oem/usr/bin/rk_ota --misc=now`。
- [√] 1.3 修改 `rk_ota`：健康槽写为 `successful_boot=1/tries_remaining=0`，增加 magic/CRC 校验和 metadata 未变化不写回判断。

## 2. 固定镜像与 SD 发布
- [√] 2.1 修改 `build.sh`，从 `packaging.7z` 恢复并校验 `env/idblock/uboot/boot`，仅重新生成 `rootfs/oem`。
- [√] 2.2 修改 `packaging/Makefile`，要求 OEM 内存在可执行 `rk_ota`，统一校验和发布 SD 六件套。
- [√] 2.3 用已验证 A/B 固定镜像和新 ARM/uClibc `rk_ota` 更新 `packaging.7z`，保留原始断链符号链接。

## 3. 测试
- [√] 3.1 更新 OTA 包装、ota.bin/SD 打包、A/B 布局和 rk_ota 策略测试。
- [√] 3.2 验证 patch series 零 fuzz 应用、shell 语法、ENV CRC/A/B 内容、归档选择性解包、镜像哈希和目标 ELF 架构。

## 4. 安全检查
- [√] 4.1 确认状态不变时不写 misc，magic/CRC 损坏时不由 Linux 覆盖；记录逻辑损坏回退默认 A 与物理不可读阻断启动的差异。

## 5. 文档更新
- [√] 5.1 更新 `helloagents/wiki/modules/rv1106_ab_upgrade.md`、`helloagents/CHANGELOG.md` 和本历史方案，使其与最终实现一致。

## 执行总结

- 最终实现以 `env.img` 为发布真实性来源，不依赖 `.env.txt`。
- 固定启动镜像来自受版本控制的 A/B `packaging.7z`；rootfs/oem 由当前源码生成。
- 健康确认在 OEM 挂载成功后完成，正常后续启动不重复擦写 misc。
