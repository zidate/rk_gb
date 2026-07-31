# 变更提案: RV1106 A/B 健康确认与 SD 固定镜像交付

## 需求背景
现场日志显示新 U-Boot 已从 A 槽启动，但 `successful=0, tries-remain=2`，健康槽没有按设计执行 `rk_ota --misc=now`。同时根 `build.sh` 若强制取完整 SDK 最新输出，会把未经本产品验证的 U-Boot 固定镜像混入发布；实际交付要求以仓库 `packaging.7z` 中的 A/B 固定镜像为准，只重新生成 `rootfs.img/oem.img`。

## 独立品审
- **原始想法:** 在 ENV 中补默认启动槽，并由应用启动后延迟确认健康状态。
- **品审结论:** 修正。A/B 槽位状态属于 `misc + 2 KiB`，不应写入 ENV；OEM 已成功挂载即可证明 boot/rootfs/oem 组合可用，健康确认应在 `S20linkmount` 内、应用启动前完成。
- **证据:** U-Boot A/B 实现对无效 metadata 重建默认 A 槽；现场 `_a` 已挂载但尝试次数持续下降；`.env.txt` 不参与当前发布，真实输入是带 CRC 的 `env.img`；`packaging.7z` 可固定一组已验证的 A/B 启动镜像。
- **关键假设:** `packaging.7z` 作为受版本控制的固定镜像来源，并包含目标 ARM/uClibc `rk_ota`；根构建只刷新应用相关 rootfs/oem。
- **替代方案:** 每次从 SDK 最新输出复制 env/idblock/uboot/boot 会扩大启动链变更面；应用延迟确认会晚于 OEM 已可用的明确健康点，均不采用。

## 变更内容
1. `S20linkmount` 成功挂载 OEM 后立即执行 `/oem/usr/bin/rk_ota --misc=now`，成功后才挂载 userdata，随后由 `S21appinit` 启动应用。
2. `rk_ota` 先校验 A/B magic/CRC，将健康槽持久化为 `successful_boot=1、tries_remaining=0`；metadata 未变化时直接返回，避免重复擦写 misc。
3. 根构建从新版 `packaging.7z` 恢复 `env/idblock/uboot/boot`，校验 A/B ENV、大小和 CRC，只重新生成 `rootfs/oem`。
4. `packaging.7z` 保留原 rootfs 断链符号链接，并包含新版 ARM/uClibc `rk_ota`。

## 影响范围
- **模块:** rootfs 启动脚本、RK OTA 工具、构建打包、RV1106 A/B 文档。
- **文件:** `S20linkmount`、`rk_ota/bootloader.cpp`、`build.sh`、`packaging.7z`、`packaging/Makefile`、相关测试与知识库。
- **API:** 不新增应用层接口；删除原应用健康确认线程和包装函数。
- **数据:** 仅在状态确有变化时更新 `misc + 2 KiB` 的 32 字节 A/B metadata；不以 `.env.txt` 决定发布内容。

## 核心场景

### 需求: 健康启动确认
**模块:** rootfs 启动脚本

#### 场景: 新槽 OEM 成功挂载
- 调用 OEM 内的 `rk_ota --misc=now`，成功后继续 userdata 挂载和应用启动。
- 工具缺失、misc 读取失败或 magic/CRC 无效时启动脚本返回失败，不覆盖损坏 metadata。
- 健康状态已持久化时不再写 misc。

### 需求: SD 六件套交付
**模块:** 构建打包

#### 场景: 执行根目录镜像构建
- 固定镜像只从受版本控制的新版 A/B `packaging.7z` 恢复。
- `env.img` 内容不完整、CRC 错误或任一固定镜像缺失时构建失败。
- `Release/sd` 最终包含 `env/idblock/uboot/boot/rootfs/oem` 六件套，其中 rootfs/oem 为当前源码产物。

## 风险评估
- **风险:** 逻辑 misc 损坏会重置到默认 A 槽并丢失切槽状态；物理不可读可能阻断启动；OEM 挂载成功但应用后续失败时不再自动回滚。
- **缓解:** U-Boot 保留 CRC 失效重建默认 A 的恢复逻辑；Linux 健康确认增加 magic/CRC 门禁和幂等判断；物理坏块/ECC 问题需靠板端 NAND 诊断和量产老化处理，不能依赖重复写 misc 修复。
