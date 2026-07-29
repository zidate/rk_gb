# 任务清单: OTA 直接镜像解析与移动回调升级

目录: `helloagents/plan/202607291546_ota_direct_images/`

## 1. 应用解析与调用
- [√] 1.1 将 OtaPackage 改为事务性解析三镜像
- [√] 1.2 将 AbUpdate 改为 `--save_dir=/tmp` 并增加清理和并发保护

## 2. Vendor updater
- [√] 2.1 修改 rk_ota 支持已准备镜像目录
- [√] 2.2 同步 vendor 补丁与源码快照

## 3. 中国移动回调
- [√] 3.1 增加安全下载与 MD5 校验 helper
- [√] 3.2 将 demo_upgrade_callback 改为异步完整升级流程
- [√] 3.3 根据板端运行时约束将下载从外部 `curl` 命令改为静态 libcurl easy API

## 4. 测试和文档
- [√] 4.1 更新 OTA、wrapper、rk_ota 和 callback 回归测试
- [√] 4.2 更新知识库、CHANGELOG 和历史索引
- [√] 4.3 完成 host/ARM 编译与全量回归
