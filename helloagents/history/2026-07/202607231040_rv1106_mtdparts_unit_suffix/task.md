# 修复任务 - rv1106_mtdparts_unit_suffix

> 类型: 修复
> 状态: 已完成
> 目标: 让 U-Boot MTD parser 原生识别 SDK `mtdparts` 中的二进制 `K/M/G` 后缀，避免通过构建脚本改写分区布局。

## 任务清单

- [√] 根据 `debug.log` 确认 FAT/DOS 兼容修复已生效，剩余失败为 `drivers/mtd/mtdpart.c` 在 `K` 处报 `Unexpected character`。
- [√] 新增 `0005-uboot-mtdparts-unit-suffix.patch`，在 size 和 offset 两个解析点复用支持大小写 `K/k/M/m/G/g` 的 helper。
- [√] 保持 `project/build.sh` 的 `RK_PARTITION_ARGS` 使用 `RK_PARTITION_CMD_IN_ENV`，不再生成十六进制字节布局。
- [√] 重新构建 U-Boot、env、kernel；`.env.txt/env.img` 保留 `mtdparts=spi-nand0:256K(env),1M@256K(idblock),...`。
- [√] 运行 `tools.tests.test_rv1106_sdk_patchset`，并记录 U-Boot、env、boot、idblock 产物哈希。

## 验证证据

- SDK: `/tmp/rv1106-task10-sdk/RV1106_IPC_SDK`
- U-Boot 构建: `project/build.sh uboot` 成功，编译了 `drivers/mtd/mtdpart.o`。
- 环境构建: `project/build.sh env` 成功，`output/image/.env.txt` 包含 `256K/1M`。
- Kernel 构建: `project/build.sh kernel` 成功。
- 产物哈希见 `vendor/rv1106_sdk_patches/README.md` 和 `wiki/modules/rv1106_ab_upgrade.md`。
