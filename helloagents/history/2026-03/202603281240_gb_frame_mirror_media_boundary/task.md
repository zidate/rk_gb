# 任务清单: GB 画面反转对齐平台 `FrameMirror` 并下沉媒体边界

目录: `helloagents/history/2026-03/202603281240_gb_frame_mirror_media_boundary/`

---

## 1. 抓包与协议现状确认
- [√] 1.1 分析 `画面反转，依次执行上下、左右、中心、还原.pcap`，确认平台使用的是顶层 `FrameMirror`
- [√] 1.2 确认平台抓包中的控制顺序和值映射为 `2=上下`、`1=左右`、`3=中心`、`0=还原`
- [√] 1.3 确认当前代码仍停留在 `VideoParamOpt/ImageFlip` 和 `ProtocolManager` 直连设备侧的旧链路

## 2. 协议结构与 XML 编解码补齐
- [√] 2.1 在 `GB28181Defs.h` 中新增 `kFrameMirrorConfig` 与 `CfgFrameMirror`
- [√] 2.2 在 `GB28181XmlParser.cpp` 中补齐 `ConfigDownload + FrameMirror` 查询识别与 `<FrameMirror>` 应答编码
- [√] 2.3 在 `GB28181XmlParser.cpp` 中补齐 `DeviceConfig + FrameMirror` 解析，并保留旧 `ImageFlip` 兼容回退

## 3. 媒体边界收口
- [√] 3.1 新增 `App/Media/VideoImageControl.*`，统一承接画面反转 `get/set`
- [√] 3.2 在 `ProtocolManager::ResponseGbQueryConfig()` 中改为通过媒体侧读取当前画面反转运行态
- [√] 3.3 在 `ProtocolManager::HandleGbConfigControl()`、启动、热加载路径中改为通过媒体侧应用画面反转

## 4. 知识库更新
- [√] 4.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 4.2 更新 `helloagents/wiki/api.md`
- [√] 4.3 更新 `helloagents/wiki/modules/terminal_requirements.md`
- [√] 4.4 更新 `helloagents/CHANGELOG.md`
- [√] 4.5 更新 `helloagents/history/index.md`

## 5. 验证
- [√] 5.1 执行 `git diff --check`
- [√] 5.2 执行最小语法编译验证

---

## 任务状态符号
- `[ ]` 待执行
- `[√]` 已完成
- `[X]` 执行失败
- `[-]` 已跳过
- `[?]` 待确认
