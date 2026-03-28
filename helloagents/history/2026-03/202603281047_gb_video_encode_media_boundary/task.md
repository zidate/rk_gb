# 任务清单: GB 编码参数职责下沉到媒体模块

目录: `helloagents/history/2026-03/202603281047_gb_video_encode_media_boundary/`

---

## 1. 媒体侧接口抽象
- [√] 1.1 新增 `App/Media/VideoEncodeControl.h/.cpp`，定义编码参数查询/设置接口并承接当前 RK 默认实现，验证 `why.md#变更目标`
- [√] 1.2 更新构建脚本，将 `VideoEncodeControl.cpp` 纳入编译，依赖任务1.1

## 2. GB 模块改造
- [√] 2.1 在 `App/Protocol/ProtocolManager.cpp` 中移除 `rk_video_* / CaptureGetResolution` 的编码参数直连逻辑，改为调用媒体侧接口，验证 `why.md#需求-gb-查询编码参数`
- [√] 2.2 将实时流协商和配置查询中的编码类型/帧率/码率/GOP/分辨率读取统一切到 `VideoEncodeControl`，验证 `why.md#需求-gb-查询编码参数`
- [√] 2.3 保留 `ApplyVideoEncodeStreamConfig` 作为后续设备侧统一设置入口，不在本轮扩 `GB28181Defs/XML` 标准设置字段，验证 `why.md#需求-后续设备侧应用编码参数`

## 3. 知识库更新
- [√] 3.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 3.2 更新 `helloagents/CHANGELOG.md`
- [√] 3.3 更新 `helloagents/history/index.md`

## 4. 验证
- [√] 4.1 执行 `git diff --check`
- [√] 4.2 执行最小语法编译验证，确认新增媒体接口和 `ProtocolManager` 改造无编译错误
> 备注: 已使用 `g++ -fsyntax-only -std=gnu++11` 分别对 `App/Media/VideoEncodeControl.cpp` 与 `App/Protocol/ProtocolManager.cpp` 做语法级编译验证；`ProtocolManager.cpp` 编译时仍存在 `CMS1400Struct.h` 与 `GB28181Defs.h` 对 `GB_ID_LEN` 的既有重定义告警，但本次改动未新增该问题。

---

## 任务状态符号
- `[ ]` 待执行
- `[√]` 已完成
- `[X]` 执行失败
- `[-]` 已跳过
- `[?]` 待确认
