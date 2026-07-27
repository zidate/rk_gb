# 任务清单: RV1106 升级临时解锁与 96 MiB 常态保护

目录: `helloagents/plan/202607231500_rv1106_temporary_unlock/`

---

## 1. U-Boot SD updater
- [√] 1.1 增加 env/idblock 镜像定义、固定分区映射和安全写入顺序
- [√] 1.2 在 rootfs/oem 写入后、恢复 A0=`0x2A` 前完成 UBI attach/detach
- [√] 1.3 启用最小 U-Boot UBI 配置并同步补丁载荷

## 2. Linux OTA
- [√] 2.1 显式拒绝 env/idblock/uboot，保持 boot/rootfs/oem 白名单
- [√] 2.2 在 MTD 临时解锁窗口内完成目标 rootfs/oem UBI attach/detach

## 3. 安全检查
- [√] 3.1 检查失败重锁、槽激活顺序、镜像大小边界和非 A/B 启动链断电风险

## 4. 文档更新
- [√] 4.1 更新 README、CHANGELOG 和 `wiki/modules/rv1106_ab_upgrade.md`

## 5. 测试
- [√] 5.1 更新 SD/OTA 策略测试并运行全套回归
- [√] 5.2 将补丁应用到真实 SDK，重建 U-Boot 与 rk_ota 并记录产物哈希；kernel 保护代码未变，沿用前次实编产物
