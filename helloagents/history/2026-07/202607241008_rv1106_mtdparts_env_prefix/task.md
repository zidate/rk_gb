# 任务清单: RV1106 U-Boot mtdparts 环境前缀修复

目录: `helloagents/plan/202607241008_rv1106_mtdparts_env_prefix/`

---

## 1. 补丁修复
- [√] 1.1 新增 SDK 补丁，令 SPI NAND `env.img` 中变量 `mtdparts` 的值符合旧式 `cmd/mtdparts.c` 要求，并统一设备名为 `spi-nand0`
- [√] 1.2 根据现场反馈修正补丁入口：正常 `./build.sh env` 修改 `project/build.sh`，同时保留 `sysdrv/Makefile` 直接构建入口

## 2. 回归测试
- [√] 2.1 更新补丁序列测试，强制校验 0008 同时覆盖 `project/build.sh`、`sysdrv/Makefile`、双层 `mtdparts=` 前缀和设备名
- [√] 2.2 在已应用 0001～0007 的 SDK 上检查补丁，并通过 `./build.sh env` 真实生成 `.env.txt` 与 `env.img`

## 3. 文档更新
- [√] 3.1 更新补丁 README、模块知识库、Changelog 和源码交付包，记录真实构建链路及现场投放步骤

## 4. 安全检查
- [√] 4.1 确认前 96 MiB 常态保护、临时解锁窗口和 OTA 镜像策略均未改变

---

## 任务状态符号
- `[ ]` 待执行
- `[√]` 已完成
- `[X]` 执行失败
- `[-]` 已跳过
- `[?]` 待确认
