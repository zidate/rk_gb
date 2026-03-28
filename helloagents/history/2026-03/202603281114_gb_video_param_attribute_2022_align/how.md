# 技术设计: GB 编码参数 XML 按 GB/T 28181-2022 对齐

## 技术方案
### 核心技术
- 在共享结构层新增 `ConfigType=VideoParamAttribute` 和对应查询/设置结构
- `GB28181XmlParser` 用手工 XML 注入/解析方式补齐 `<VideoParamAttribute>`
- `ProtocolManager` 通过媒体侧接口生成和应用编码参数
- `VideoEncodeControl` 新增码率控制模式查询/设置，承接 `BitRateType`

### 实现要点
- `GB28181Defs.h`
  - 新增 `CfgVideoParamAttribute`
  - 新增 `VideoParamAttributeSetting`
  - 扩展 `ConfigType`、`SettingType`、联合体
- `GB28181XmlParser.cpp`
  - `PackConfigDownloadQuery()` / `UnPackConfigDownloadQuery()` 支持 `VideoParamAttribute`
  - `PackConfigDownloadResponse()` 在 `<Response>` 中注入一个或多个 `<VideoParamAttribute>`
  - `UnPackConfigControl()` 解析 `DeviceConfig` 里的 `<VideoParamAttribute>`
- `ProtocolManager.cpp`
  - 查询侧把主/辅码流运行态映射到 `VideoParamAttribute`
  - 设置侧把 `VideoParamAttribute` 转成 `media::VideoEncodeStreamConfig`
- `VideoEncodeControl.*`
  - 新增 `bitrate_type` 读写能力
  - 默认适配 `rk_video_get_RC_mode()` / `rk_video_set_RC_mode()`

## 标准映射
- `VideoFormat`: `1=MPEG4`、`2=H.264`、`3=AVS`、`4=SVAC`、`5=H.265`
- `Resolution`: `1=QCIF`、`2=CIF`、`3=4CIF`、`4=D1`、`5=720P`、`6=1080P/I`，其他分辨率用 `WxH`
- `BitRateType`: `1=固定码率(CBR)`、`2=可变码率(VBR)`

## 架构决策 ADR
### ADR-20260328-01: 不改 slothxml 生成模型，改为手工补 `VideoParamAttribute`
**上下文:** 现有 `configdownload_response_t` 由历史模板生成，直接扩生成模型会把影响面扩大到模板和生成产物。
**决策:** 保留旧 slothxml 模型，针对 `VideoParamAttribute` 使用手工 XML 注入和手工解析。
**理由:** 这次只需补齐 2022 编码参数 XML，不需要重构整套 XML 生成链。
**替代方案:** 继续手改 `configdownload_response.h/.cpp` 生成文件。  
**拒绝原因:** 后续难维护，且对当前需求来说没有必要。

### ADR-20260328-02: `BitRateType` 继续走媒体接口，不进入 GB 配置文件模型
**上下文:** 现有 `ProtocolExternalConfig::gb_video` 只持久化 `codec/resolution/fps/bitrate`，没有单独的码率模式字段。
**决策:** `BitRateType` 由 `VideoEncodeControl` 直接读写底层编码侧，不额外扩 `gb28181.ini/http config`。
**理由:** 码率模式本质是编码模块运行态能力，当前设备底层已经有独立接口。

## 测试与部署
- 使用 `git diff --check` 检查补丁格式。
- 使用 `g++ -fsyntax-only -std=gnu++11` 校验：
  - `App/Media/VideoEncodeControl.cpp`
  - `App/Protocol/ProtocolManager.cpp`
  - `third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp`
- 接受仓库既有告警：
  - `GB_ID_LEN` 宏重定义
  - `GB28181XmlParser.cpp` 中既有 `'\0'` 字面量告警
