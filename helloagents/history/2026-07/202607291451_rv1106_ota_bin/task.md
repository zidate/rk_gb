# 任务清单: RV1106 ota.bin 升级包

目录: `helloagents/plan/202607291451_rv1106_ota_bin/`

## 1. Host 打包与构建
- [√] 1.1 新增 `packaging/image/tools-sourcecode/packaging-update/`，实现兼容 ota.bin 格式的严格打包工具
- [√] 1.2 修改 `packaging/image/Makefile`、`packaging/Makefile`、`build.sh`，移除 tar OTA 产物并生成 `Release/ota.bin`

## 2. 设备端解析与执行
- [√] 2.1 新增 `App/Update/OtaPackage.*`，校验 ota.bin 并生成固定三成员临时 USTAR
- [√] 2.2 修改 `App/Update/AbUpdate.*`，解析 ota.bin 后通过 `execv` 调用 `rk_ota` 并清理临时文件
- [√] 2.3 修改两个 CMake 入口、GB 下载路径和 `Main.cpp` 调试触发逻辑

## 3. 安全检查
- [√] 3.1 检查整数溢出、越界、临时文件、命令注入和非法镜像组合拒绝路径

## 4. 文档更新
- [√] 4.1 更新 `helloagents/wiki/modules/rv1106_ab_upgrade.md`、架构 ADR、CHANGELOG 和历史索引

## 5. 测试
- [√] 5.1 更新/新增 OTA host 回归测试，覆盖正常包、损坏头/CRC/长度/类型和 wrapper 调用
- [√] 5.2 执行 Python 测试、host 编译、`git diff --check` 和可用的 RV1106 交叉构建验证
