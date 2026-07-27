# 任务清单: RV1106 SD 固定区升级与 A/B 激活修复

目录: `helloagents/plan/202607241536_rv1106_sd_fixed_raw_write/`

> **类型:** 轻量迭代
> **依据:** `/home/jerry/silver/debug.log` 中固定分区查找失败及 FAT 缓冲区未对齐日志。

## 1. U-Boot SD 升级命令
- [√] 1.1 将 FAT 读写缓冲区改为 `ARCH_DMA_MINALIGN` 对齐分配。
- [√] 1.2 让 `env.img`、`idblock.img`、`uboot.img` 在 `spi-nand0` 上按固定物理区间擦写并校验，槽镜像保持命名 MTD 分区写入。
- [√] 1.3 增加固定镜像写入成功和 A/B 激活成功日志，失败时不得激活目标槽。

## 2. 补丁与回归
- [√] 2.1 新增 `0009-uboot-sd-fixed-raw-offset.patch` 并加入补丁序列。
- [√] 2.2 更新 SD 策略和补丁集测试，覆盖 raw offset 边界、对齐分配和激活时序。
- [√] 2.3 执行补丁回放、35 项单元测试及完整 `project/build.sh uboot` 验证。

## 3. 文档与交付
- [√] 3.1 更新补丁说明、知识库与变更历史。
- [√] 3.2 生成可直接交付同事的源代码与增量补丁包。

## 执行结果

- U-Boot 完整链接及打包成功，`uboot.img` 为 262144 字节。
- 35 项 RV1106 补丁/策略回归通过。
- 交付包：`rv1106-sd-fixed-raw-write-20260724.tar.gz`。
