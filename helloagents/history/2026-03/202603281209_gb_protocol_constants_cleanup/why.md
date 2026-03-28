# 变更提案: 收口 GB28181 协议散落硬编码

## 需求背景
当前 GB 模块在 `ProtocolManager`、`GB28181XmlParser`、`GB28181BroadcastBridge` 中重复维护了多套协议默认值和字符串常量，例如默认设备 ID、保活/注册缺省值、`ConfigType/CmdType` 名称、音频 payload/mime，以及 `FrameMirror/ImageFlip` 映射。相同协议值散落多处后，后续再改任何一个口径都容易漏改并引入行为漂移。

## 变更目标
1. 将跨文件重复的 GB28181 协议常量和映射集中到单一头文件。
2. 让 `ProtocolManager`、XML 解析层、广播桥复用同一套协议字符串和值。
3. 不改变当前业务行为，只减少散落硬编码和重复实现。

## 影响范围
- **模块:** `ProtocolManager`、`GB28181XmlParser`、`GB28181BroadcastBridge`
- **文件:** `App/Protocol/gb28181/GB28181ProtocolConstants.h`、`App/Protocol/ProtocolManager.cpp`、`third_party/.../GB28181XmlParser.cpp`、`App/Protocol/gb28181/GB28181BroadcastBridge.cpp`
- **行为边界:** 注册/查询/广播/图像翻转/音频协商行为保持不变，本次只做协议常量与映射收口

## 核心场景

### 需求: 统一 GB 查询应答的缺省值来源
**模块:** `ProtocolManager`
设备在应答 `DeviceInfo/Catalog/ConfigDownload/PresetQuery` 时，应从统一 helper 取默认设备 ID、保活与注册缺省值，而不是每个应答分支各自写一份硬编码。

#### 场景: 配置为空时回落统一默认值
- 查询入参 `GBCode` 为空时，统一从 `m_cfg.gb_register.device_id` 回退
- 若本地配置同样为空，统一回退到共享默认设备 ID
- `BasicParam` 的 `Expiration/HeartBeatInterval/HeartBeatcount` 统一使用共享缺省值

### 需求: 统一 XML/广播协议字符串来源
**模块:** `GB28181XmlParser` / `GB28181BroadcastBridge`
XML 解析与广播 SDP 构造应复用同一套 `ConfigType/CmdType`、音频 payload/mime 和 `FrameMirror` 映射，避免多处实现逐步偏离。

#### 场景: XML 与广播桥复用同一套协议定义
- `ConfigDownload` 的 `ConfigType` 编解码统一走共享 helper
- `CmdType` 统一使用共享常量
- 广播桥的 `payload/mime/SDP transport` 统一使用共享常量与 helper
