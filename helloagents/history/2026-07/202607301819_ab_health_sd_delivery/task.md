# 任务清单: RV1106 A/B 健康确认与 SD 固定镜像交付

目录: `helloagents/history/2026-07/202607301819_ab_health_sd_delivery/`

---

## 1. 启动健康确认
- [√] 1.1 扩展 `App/Update/AbUpdate.cpp` 和 `App/Update/AbUpdate.h`，以无 shell 方式执行 `rk_ota --misc=now` 并复用 OTA 并发锁。
- [√] 1.2 在 `App/Main.cpp` 正常模式关键初始化完成后增加延迟、有限重试的健康确认线程，依赖任务 1.1。

## 2. 固定镜像导入与 SD 发布
- [√] 2.1 修改 `build.sh` 和 `tools/verify_uboot_env.py`，要求显式 SDK 根目录，校验 env 大小/CRC/内容并导入 `env/idblock/uboot`。
- [√] 2.2 修改 `packaging/Makefile`，统一校验和发布 SD 六件套，依赖任务 2.1。

## 3. 测试
- [√] 3.1 扩展 `tools/tests/test_ab_update_wrapper.py`，验证健康确认命令、退出状态和无 shell约束。
- [√] 3.2 扩展 `tools/tests/test_ota_bin_package.py`，验证 SDK 固定镜像导入和 SD 六件套策略。
- [√] 3.3 运行 OTA 包装、打包、RV1106 A/B、SD 和 SDK patchset 回归测试。

## 4. 安全检查
- [√] 4.1 检查命令参数固定、路径引用、并发锁、失败降级和发布目录清理边界。

## 5. 文档更新
- [√] 5.1 更新 `helloagents/wiki/modules/rv1106_ab_upgrade.md` 和 `helloagents/CHANGELOG.md`。
- [√] 5.2 完成一致性审计并迁移方案包至 `helloagents/history/2026-07/`，更新历史索引。

## 执行总结

- OTA wrapper 4 项、ota.bin/SD/ENV 打包校验 6 项、RV1106 策略 68 项、中国移动 OTA 3 项均通过。
- `Main.cpp` 与 `AbUpdate.cpp` 已由 ARM GNU 8.3.0 成功生成目标文件；整机最终链接因隔离工作树缺少既有 `Manager/Infra/Exchanger/json` 等预编译库而停止，未发现本次源文件编译错误。
- `bash -n build.sh` 与 `git diff --check` 通过。
