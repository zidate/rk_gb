# 任务清单: RV1106 U-Boot UBI 分区映射修复

目录: `helloagents/plan/202607231556_rv1106_mtdids_default/`

---

## 1. 配置修复
- [√] 1.1 在 `0006-uboot-sd-ubi-init.patch` 和交付源码 defconfig 中增加 `CONFIG_MTDIDS_DEFAULT="spi-nand0=spi-nand0"`

## 2. 测试
- [√] 2.1 补充并运行 RV1106 SD/补丁集回归，验证 UBI 命令具备 SPI NAND 设备映射

## 3. 文档与交付
- [√] 3.1 更新 RV1106 A/B 升级知识记录和交付说明，明确新 U-Boot 必须先单独升级
- [√] 3.2 同步交付补丁并生成不含旧二进制的源码修复包

## 4. 安全检查
- [√] 4.1 确认未改变前 96 MiB 常态保护、临时解锁范围和槽激活失败语义
