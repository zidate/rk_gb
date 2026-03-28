# 变更提案: GB 编码参数 XML 按 GB/T 28181-2022 对齐

## 需求背景
当前设备虽然已经把编码参数查询/设置职责下沉到 `App/Media/VideoEncodeControl.*`，但 `ConfigDownload/DeviceConfig` 侧仍沿用旧的 `VideoParamOpt` / `SVACEncodeConfig` 模型，没有按 `GB/T 28181-2022` 的 `VideoParamAttribute` 表达编码参数。

## 变更目标
1. 将 GB 编码参数 XML 对齐到 `GB/T 28181-2022` 的 `VideoParamAttribute`。
2. 补齐 `ConfigDownload` 查询回包和 `DeviceConfig` 设置解析两条链路。
3. 保持“协议侧只做字段映射，媒体侧负责设备编码能力”的边界不变。

## 影响范围
- **模块:** `ProtocolManager`、`GB28181 XML parser`、`Media`
- **文件:** `GB28181Defs.h`、`GB28181XmlParser.cpp`、`ProtocolManager.cpp`、`VideoEncodeControl.*`、知识库
- **行为边界:** 旧 `VideoParamOpt` 查询兼容保留，但标准编码参数的正式口径切到 `VideoParamAttribute`

## 核心场景

### 需求: GB 查询编码参数
**模块:** `ProtocolManager` / `GB28181 XML parser`
平台通过 `ConfigDownload` 查询设备编码参数时，应收到 `GB/T 28181-2022` 定义的 `VideoParamAttribute`。

#### 场景: 查询返回 2022 标准字段
- `VideoFormat` 按附录 G 回 `1~5`
- `Resolution` 按附录 G 回 `1~6` 或 `WxH`
- `BitRateType` 按附录 G 回 `1=固定码率`、`2=可变码率`

### 需求: GB 设置编码参数
**模块:** `ProtocolManager` / `Media`
平台通过 `DeviceConfig + VideoParamAttribute` 设置编码参数后，设备应把配置转发给媒体/编码侧生效。

#### 场景: 设置下发到媒体接口
- `ProtocolManager` 解析 `StreamNumber/VideoFormat/Resolution/FrameRate/BitRateType/VideoBitRate`
- `VideoEncodeControl` 统一承接 `codec / resolution / fps / bitrate / bitrate_type` 设置
- 主辅码流配置同步回写到协议配置缓存
