# 任务清单: ProtocolFeatureSwitch 精简收口

目录: `helloagents/history/2026-04/202604111448_protocol_feature_switch_trim/`

---

## 1. 代码调整
- [√] 1.1 删除 `third_party/platform_sdk_port/CommonFile/CommonLib/ProtocolFeatureSwitch.h` 空壳头文件，并移除 `GBClientImpl.cpp` 中对应死 include
- [√] 1.2 删除 `App/CMakeLists.txt` 与 `App/Protocol/gb28181/sdk_port/CMakeLists.txt` 中重复的 `-DPROTOCOL_HAS_GB28181_CLIENT_SDK=1`
- [√] 1.3 收口 `App/Protocol/ProtocolManager.cpp` 中围绕 `PROTOCOL_HAS_GB28181_CLIENT_SDK` 的恒真条件编译，保留当前唯一真实执行路径
- [√] 1.4 清理 `GB28181XmlParser.cpp` 中重复 include，继续压缩协议层历史冗余

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 2.2 更新 `helloagents/CHANGELOG.md`
- [√] 2.3 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 执行 `rg -n "PROTOCOL_HAS_GB28181_CLIENT_SDK|ProtocolFeatureSwitch\.h" /home/jerry/silver/rk_gb/App /home/jerry/silver/rk_gb/third_party/platform_sdk_port -g '!*.md'`
- [√] 3.2 执行 `git -C /home/jerry/silver/rk_gb diff --check`
- [√] 3.3 尝试执行 `cmake --build /home/jerry/silver/rk_gb/cmake-build --target dgiot`

## 执行结果
- `ProtocolFeatureSwitch.h` 已删除，GB28181 Client SDK 当前按产品线默认能力直接构建，不再额外保留一个固定恒真的编译期开关头。
- `ProtocolManager.cpp` 中围绕 `PROTOCOL_HAS_GB28181_CLIENT_SDK` 的历史 `#if/#else` stub 分支已收口，只保留实际运行路径，避免后续继续维护死代码。
- `App/CMakeLists.txt` 与 `App/Protocol/gb28181/sdk_port/CMakeLists.txt` 中重复宏定义已去掉，GB28181 构建入口更直接。
- `GB28181XmlParser.cpp` 的重复 include 和 `GBClientImpl.cpp` 的死 include 已清掉，周边冗余一并收口。
- `rg` 与 `git diff --check` 已通过；`cmake --build /home/jerry/silver/rk_gb/cmake-build --target dgiot` 在当前环境失败，原因是 `cmake-build/CMakeCache.txt` 仍指向旧路径 `/home/lhy/share/ipc/RK/temp/lhy/0310/rk/rc0240-yuanshi/cmake-build`，且 Makefile 里引用的 `/opt/cmake/cmake-3.16.3-Linux-x86_64/bin/cmake` 不存在。
