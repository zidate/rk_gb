# 技术设计: GB 画面反转对齐平台 `FrameMirror` 并下沉媒体边界

## 技术方案
### 核心技术
- 在共享结构层新增 `ConfigType=FrameMirror`
- `GB28181XmlParser` 通过手工 XML 注入 / 解析补齐顶层 `<FrameMirror>`
- 新增 `App/Media/VideoImageControl.*` 统一承接设备侧画面反转 `get/set`
- `ProtocolManager` 只负责 `FrameMirror(0/1/2/3)` 和设备侧字符串模式之间的映射

## 实现要点
- `GB28181Defs.h`
  - 新增 `kFrameMirrorConfig`
  - 新增 `CfgFrameMirror`
  - 扩展 `UnionConfigParam`
- `GB28181XmlParser.cpp`
  - `PackConfigDownloadQuery()` / `UnPackConfigDownloadQuery()` 支持 `FrameMirror`
  - `PackConfigDownloadResponse()` 在 `<Response>` 中注入 `<FrameMirror>`
  - `UnPackConfigControl()` 优先解析顶层 `<FrameMirror>`，旧 `VideoParamOpt.ImageFlip` 作为兼容回退
- `VideoImageControl.*`
  - 统一暴露 `QueryVideoImageFlipMode()` / `ApplyVideoImageFlipMode()`
  - 默认适配 `rk_isp_get_image_flip()` / `rk_isp_set_image_flip()`
- `ProtocolManager.cpp`
  - 启动、热加载、远程配置三条链路统一改走 `media::ApplyVideoImageFlipMode()`
  - 查询侧支持 `FrameMirror` 回包，并保留 `ImageFlip` 兼容

## 协议映射
- `FrameMirror=0` -> `close`
- `FrameMirror=1` -> `mirror`
- `FrameMirror=2` -> `flip`
- `FrameMirror=3` -> `centrosymmetric`

## 架构决策 ADR
### ADR-20260328-01: `FrameMirror` 作为正式平台联调口径，`ImageFlip` 仅保留兼容
**上下文:** 平台抓包与旧代码口径不一致；继续只维护 `ImageFlip` 会导致查询 / 设置都对不上平台。
**决策:** 新增正式 `FrameMirror` 查询 / 设置链路，旧 `VideoParamOpt/ImageFlip` 仅作为兼容保留。
**理由:** 不影响历史兼容，同时让平台当前测试口径和代码对齐。

### ADR-20260328-02: 设备侧画面反转统一收口到 `VideoImageControl`
**上下文:** `ProtocolManager` 之前直接读写翻转配置，导致协议层与设备能力耦合。
**决策:** 新建 `App/Media/VideoImageControl.*`，GB 模块只做协议映射与配置同步。
**理由:** 与 OSD、编码参数边界保持一致，方便后续由编码 / ISP 同事接管底层实现。

## 测试与部署
- 使用 `tcpdump -A -nn -s 0 -r '画面反转，依次执行上下、左右、中心、还原.pcap'` 对照平台抓包，确认控制值顺序为 `2/1/3/0`
- 使用 `git diff --check` 检查补丁格式
- 使用 `g++ -fsyntax-only -std=gnu++11` 校验：
  - `App/Media/VideoImageControl.cpp`
  - `App/Protocol/ProtocolManager.cpp`
  - `third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp`
