# 变更提案: GB 画面反转对齐平台 `FrameMirror` 并下沉媒体边界

## 需求背景
平台联调抓包显示，画面反转当前实际使用的是顶层 `DeviceConfig + FrameMirror` / `ConfigDownload + FrameMirror`，且控制顺序为“上下、左右、中心、还原”，对应值分别是 `2/1/3/0`。现有代码仍主要按旧 `VideoParamOpt/ImageFlip` 处理，并且设备落地放在 `ProtocolManager` 内部，不符合“协议侧只做映射，编码/媒体侧负责 get/set”的边界要求。

## 变更目标
1. 将 GB28181 画面反转协议口径对齐到平台实测的 `FrameMirror`。
2. 保留旧 `VideoParamOpt/ImageFlip` 的兼容解析 / 查询回包，避免回归历史链路。
3. 将设备侧画面反转 `get/set` 下沉到 `App/Media/VideoImageControl.*`，GB 模块只保留协议映射和配置持久化。

## 影响范围
- **模块:** `ProtocolManager`、`GB28181 XML parser`、`Media`
- **文件:** `GB28181Defs.h`、`GB28181XmlParser.cpp`、`ProtocolManager.cpp`、`VideoImageControl.*`、知识库
- **行为边界:** `FrameMirror` 数值和设备侧 `close/mirror/flip/centrosymmetric` 模式之间的换算保留在协议侧；底层实际读写能力收口到媒体侧

## 核心场景

### 需求: 平台通过 `FrameMirror` 设置画面反转
**模块:** `GB28181XmlParser` / `ProtocolManager` / `Media`
平台下发 `DeviceConfig + FrameMirror` 后，设备应能识别 `0/1/2/3` 并落到对应画面反转状态。

#### 场景: 新协议口径优先
- 顶层 `<FrameMirror>` 优先于旧 `VideoParamOpt/ImageFlip`
- `0=还原(close)`
- `1=左右(mirror)`
- `2=上下(flip)`
- `3=中心(centrosymmetric)`

### 需求: 平台通过 `FrameMirror` 查询画面反转
**模块:** `ProtocolManager` / `GB28181XmlParser`
平台发送 `ConfigDownload + ConfigType=FrameMirror` 后，设备应返回顶层 `<FrameMirror>`，且值来自设备当前运行态。

#### 场景: 查询从媒体侧取运行态
- GB 模块通过 `media::QueryVideoImageFlipMode()` 读取当前实际翻转模式
- `ConfigDownload` 应答返回与平台抓包一致的 `FrameMirror` 值
- 旧 `VideoParamOpt/ImageFlip` 查询仍兼容保留
