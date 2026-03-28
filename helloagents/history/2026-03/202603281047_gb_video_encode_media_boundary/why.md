# 变更提案: GB 编码参数职责下沉到媒体模块

## 需求背景
当前 GB28181 在读取编码参数时，`ProtocolManager` 直接访问 `rk_video_*` 和 `CaptureGetResolution`，把协议字段映射和设备编码能力控制混在了一起。  
OSD 已经按“协议侧只做映射，媒体侧负责设备落地”的方式完成收口，编码参数这里也需要对齐同样的边界。

## 变更目标
1. 新增媒体侧统一接口，承接 `codec / fps / bitrate / gop / resolution` 的查询能力，以及后续设备侧应用入口。
2. `ProtocolManager` 不再直接调用编码底层接口，只通过媒体接口获取运行态并组装 GB 应答。
3. 保持现有 GB 协议模型不变，这次只做职责下沉，不顺手扩 `GB28181Defs/XML` 的字段模型。

## 影响范围
- **模块:** `ProtocolManager`、`Media`
- **文件:** `App/Protocol/ProtocolManager.cpp`、新增 `App/Media/VideoEncodeControl.*`、构建脚本、知识库
- **行为边界:** 不改现有对外 GB 查询/实时流行为，重点调整内部职责归属

## 核心场景

### 需求: GB 查询编码参数
**模块:** `ProtocolManager` / `Media`
平台查询基础配置或设备发流时，需要读取当前主辅码流的编码运行态。

#### 场景: GB 通过媒体接口读取运行态
- `ProtocolManager` 负责 GB 协议字段映射和查询应答组包
- `VideoEncodeControl` 负责访问设备实际编码能力
- 若媒体接口未返回完整运行态，则协议侧继续按已有配置模型回退

### 需求: 后续设备侧应用编码参数
**模块:** `ProtocolManager` / `Media`
当前协议模型虽然没有新增 `codec/fps/bitrate` 的标准设置字段，但设备侧仍需要统一设置入口。

#### 场景: 编码模块统一承接设置
- 媒体模块对外暴露 `ApplyVideoEncodeStreamConfig`
- 后续无论是显式重载还是新增配置控制链路，都只调用这一入口
- `ProtocolManager` 不再重新拼装 `rk_video_*` 设置逻辑
