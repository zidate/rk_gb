# 技术设计: GB 编码参数职责下沉到媒体模块

## 技术方案
### 核心技术
- 新增媒体侧编码参数控制接口 `VideoEncodeControl`
- `ProtocolManager` 改为通过媒体接口读取编码运行态
- 清理协议模块中的 `rk_video_* / CaptureGetResolution` 直连逻辑

### 实现要点
- 在 `App/Media/VideoEncodeControl.h/.cpp` 中定义并实现媒体侧接口：
  - `QueryVideoEncodeStreamState`
  - `QueryVideoEncodeState`
  - `ApplyVideoEncodeStreamConfig`
- `VideoEncodeControl` 内部先承接当前 RK 默认实现，统一封装：
  - `rk_video_get_output_data_type`
  - `rk_video_get_frame_rate`
  - `rk_video_get_max_rate`
  - `rk_video_get_gop`
  - `CaptureGetResolution`
  - `rk_video_set_output_data_type`
  - `rk_video_set_frame_rate`
  - `rk_video_set_max_rate`
  - `rk_video_set_resolution`
- `ProtocolManager` 继续负责：
  - GB 查询应答组包
  - 实时流协商时的 `video_codec` 选择
  - OSD 画布和码流列表这类协议侧逻辑
- `ProtocolManager` 不再直接声明或调用编码底层查询接口。

## 架构决策 ADR
### ADR-20260328-01: 编码参数设备能力从 ProtocolManager 下沉到 Media 接口
**上下文:** OSD 已完成职责收口，但编码参数查询仍残留在协议模块内。
**决策:** 新增 `App/Media/VideoEncodeControl.*` 作为编码/媒体侧统一接口面，GB 模块只调用接口。
**理由:** 编码参数本质上属于编码模块能力，不应由 GB 协议模块直接管理底层实现。
**替代方案:** 继续在 `ProtocolManager` 内保留 `rk_video_*` / `CaptureGetResolution` 调用。  
**拒绝原因:** 后续编码模块维护者仍需跨模块修改协议代码，边界不清晰。

### ADR-20260328-02: 本轮不扩 GB 标准配置模型
**上下文:** 当前 SDK / XML 模型对 `codec/fps/bitrate` 没有现成标准设置字段。
**决策:** 本轮只做访问链路下沉，不扩 `GB28181Defs/XML`。
**理由:** 用户当前诉求是职责收口，不是新增一套协议字段模型；扩协议会显著扩大影响面。
**影响:** `ApplyVideoEncodeStreamConfig` 先作为统一设置入口预留，供后续显式重载或新增控制链路直接复用。

## 数据模型
```text
media::VideoEncodeStreamConfig
  - codec
  - resolution
  - fps
  - bitrate_kbps

media::VideoEncodeStreamState
  - codec
  - width/height/resolution
  - frame_rate
  - bitrate_kbps
  - gop
  - has_* validity flags

media::VideoEncodeState
  - main_stream
  - sub_stream
  - resolution_summary
```

## 测试与部署
- 使用 `git diff --check` 检查补丁格式。
- 使用 `g++ -fsyntax-only -std=gnu++11` 分别验证 `App/Media/VideoEncodeControl.cpp` 与 `App/Protocol/ProtocolManager.cpp`。
- 接受仓库既有的 `GB_ID_LEN` 宏重定义告警，不把它误判为本次新增问题。
