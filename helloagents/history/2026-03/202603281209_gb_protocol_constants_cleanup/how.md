# 技术设计: 收口 GB28181 协议散落硬编码

## 技术方案
### 核心技术
- 新增 `App/Protocol/gb28181/GB28181ProtocolConstants.h` 作为 GB28181 协议共享常量头
- 在共享头中集中默认设备 ID、注册/保活缺省值、分辨率摘要、音频 payload/mime、`ConfigType/CmdType` 字符串、`FrameMirror/ImageFlip` 映射 helper
- `ProtocolManager`、`GB28181XmlParser`、`GB28181BroadcastBridge` 改为复用共享常量/映射，不再各自维护一份

## 实现要点
- `GB28181ProtocolConstants.h`
  - 新增默认设备 ID、保活/注册缺省值、OSD 画布缺省尺寸、默认分辨率摘要
  - 新增音频 codec/payload/mime 归一化 helper
  - 新增 `ConfigType` 双向映射 helper
  - 新增 `FrameMirror/ImageFlip` 统一映射 helper
- `ProtocolManager.cpp`
  - 查询应答统一使用 `ResolveGbDeviceIdOrDefault()`
  - 配置查询统一使用共享缺省值与音频 helper
  - `FrameMirror/ImageFlip` 映射改为复用共享 helper
- `GB28181XmlParser.cpp`
  - `OSDConfig/VideoParamAttribute/FrameMirror` 元素名改为共享常量
  - `ConfigDownload` 的 `ConfigType` 编解码改为共享 helper
  - 常用 `CmdType` 改为共享常量
- `GB28181BroadcastBridge.cpp`
  - 音频 codec/payload/mime 映射改为复用共享 helper
  - SDP `m=audio`、`a=setup:`、`TCP/RTP/AVP`、`RTP/AVP` 字符串改为共享常量

## 架构决策 ADR
### ADR-20260328-01: 协议常量集中在 GB 模块内单一头文件
**上下文:** 相同协议默认值和字符串分散在主流程、XML 解析层、广播桥三个位置，后续修改容易遗漏。
**决策:** 引入 `GB28181ProtocolConstants.h` 作为三处共享的最小公共面。
**理由:** 不改业务边界，不引入新模块依赖，只解决协议值漂移和重复代码问题。

### ADR-20260328-02: 本次只收口“跨文件重复”的硬编码
**上下文:** GB 模块内部仍存在不少局部常量，但并非都形成跨文件漂移风险。
**决策:** 优先处理默认设备 ID、基础默认值、音频映射、`ConfigType/CmdType`、`FrameMirror` 这类跨文件重复项。
**理由:** 控制改动面，避免一次性把所有局部 magic number 全部展开成大重构。

## 测试与部署
- 执行 `git diff --check`
- 执行 `g++ -fsyntax-only -std=gnu++11` 校验：
  - `App/Protocol/ProtocolManager.cpp`
  - `App/Protocol/gb28181/GB28181BroadcastBridge.cpp`
  - `third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp`
- 已知 `GB28181XmlParser.cpp` 原文件仍存在空字符字面量告警，本次未顺带处理该历史遗留问题
