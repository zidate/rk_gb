# 变更提案: RV1106 A/B 健康确认与 SD 固定镜像交付

## 需求背景
现场日志显示新 U-Boot 已从 A 槽启动，但 `successful=0, tries-remain=2`，应用侧没有按设计调用 `rk_ota --misc=now` 刷新当前槽状态。同时仓库根 `build.sh` 不重建 SDK 固定镜像，SD 发布目录又遗漏 `env.img` 和 `idblock.img`，导致执行构建脚本后仍可能交付旧或不完整的固定镜像。

## 独立品审
- **原始想法:** 修改代码恢复双区升级和默认槽位配置。
- **品审结论:** 修正。A/B 槽位选择已经由 `misc` 正常工作，不应在 ENV 中增加固定槽变量；应修复健康确认与交付链路。
- **证据:** `debug.log` 中 A 槽只剩 2 次尝试；代码中无 `--misc=now` 调用；`packaging/Makefile` 仅发布四个 SD 镜像；根构建脚本不调用 SDK `build_env()`。
- **关键假设:** 完整 SDK 的固定镜像输出路径保持为 `output/image/env.img` 和 `sysdrv/source/uboot/u-boot/{idblock,uboot}.img`。
- **替代方案:** 手工执行 `rk_ota --misc=now` 和复制镜像只能临时规避，不能防止后续构建再次遗漏，故不采用。

## 变更内容
1. 应用正常模式完成音视频初始化并持续存活一段时间后，调用 `rk_ota --misc=now`。
2. 根构建脚本从显式指定的完整 SDK `output/image/` 导入固定镜像，并直接校验 `env.img` 的真实内容。
3. SD 发布改为强制包含 `env/idblock/uboot/boot/rootfs/oem` 六件套。
4. 增加回归测试并同步 A/B 升级知识库。

## 影响范围
- **模块:** 应用 OTA 包装层、应用启动、构建打包、RV1106 A/B 文档。
- **文件:** `App/Update/AbUpdate.*`、`App/Main.cpp`、`build.sh`、`packaging/Makefile`、相关测试与知识库。
- **API:** 新增内部函数 `AbUpdateMarkBootSuccessful()`。
- **数据:** 更新 `misc + 2 KiB` 的 A/B metadata，不改变 ENV 槽位格式。

## 核心场景

### 需求: 健康启动确认
**模块:** 应用 OTA 包装层

#### 场景: 新槽启动后应用保持运行
正常模式完成关键音视频初始化且进程持续存活达到健康延迟。
- 通过无 shell 的 `execv` 调用 `rk_ota --misc=now`。
- 调用失败只记录日志并有限重试，不阻断应用主流程。

### 需求: SD 六件套交付
**模块:** 构建打包

#### 场景: 执行根目录镜像构建
构建者显式提供完整 RV1106 SDK 目录。
- 从 SDK 导入本次生成的三个固定镜像。
- `env.img` 内容不完整或任一镜像缺失时构建失败；不以可能被后续构建目标重写的一行 `.env.txt` 作为最终产物依据。
- `Release/sd` 最终恰好包含六类升级镜像。

## 风险评估
- **风险:** 过早确认启动会削弱回滚；错误 SDK 路径会混入旧镜像；健康确认可能与 OTA 并发。
- **缓解:** 延迟确认、复用进程内升级锁、有限重试、要求显式 SDK 路径并校验 `env.img` 内的关键环境项及镜像大小。
