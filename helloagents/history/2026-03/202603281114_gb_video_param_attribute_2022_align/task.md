# 任务清单: GB 编码参数 XML 按 GB/T 28181-2022 对齐

目录: `helloagents/history/2026-03/202603281114_gb_video_param_attribute_2022_align/`

---

## 1. 共享结构补齐
- [√] 1.1 在 `GB28181Defs.h` 中新增 `VideoParamAttribute` 查询/设置结构与枚举，验证 `why.md#变更目标`
- [√] 1.2 扩展配置查询类型数组容量，避免新增 `VideoParamAttribute` 后被旧上限截断

## 2. XML 编解码补齐
- [√] 2.1 在 `GB28181XmlParser.cpp` 中补齐 `ConfigDownload + VideoParamAttribute` 查询识别
- [√] 2.2 在 `PackConfigDownloadResponse()` 中补齐 `<VideoParamAttribute>` 编码
- [√] 2.3 在 `UnPackConfigControl()` 中补齐 `DeviceConfig + VideoParamAttribute` 解析

## 3. 协议与媒体映射
- [√] 3.1 在 `ProtocolManager::ResponseGbQueryConfig()` 中按 2022 标准生成主辅码流 `VideoParamAttribute`
- [√] 3.2 在 `ProtocolManager::HandleGbConfigControl()` 中支持 `VideoParamAttribute` 设置，并下发到 `media::ApplyVideoEncodeStreamConfig()`
- [√] 3.3 在 `VideoEncodeControl.*` 中补齐 `bitrate_type` 查询/设置，承接 `BitRateType`

## 4. 知识库更新
- [√] 4.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 4.2 更新 `helloagents/CHANGELOG.md`
- [√] 4.3 更新 `helloagents/history/index.md`

## 5. 验证
- [√] 5.1 执行 `git diff --check`
- [√] 5.2 执行最小语法编译验证
> 备注:
> - `ProtocolManager.cpp` 语法校验仍有既有 `GB_ID_LEN` 宏重定义告警
> - `GB28181XmlParser.cpp` 语法校验仍有既有 `'\0'` 字面量告警
> - 两类告警都不是本次新增问题

---

## 任务状态符号
- `[ ]` 待执行
- `[√]` 已完成
- `[X]` 执行失败
- `[-]` 已跳过
- `[?]` 待确认
