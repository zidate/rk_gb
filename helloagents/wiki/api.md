# API 手册

## 概述
本文档记录项目内部协议模块的关键回调与配置接口，不覆盖外部平台完整协议文档。

## 接口列表

### ProtocolManager

#### `HandleGbLiveStreamRequest`
**描述:** 处理 GB28181 实时流请求，解析 SDP、准备媒体参数并返回应答媒体信息。

#### `HandleGbStreamAck`
**描述:** 处理平台对实时流/回放应答的 ACK，驱动后续媒体启动。

#### `RespondGbMediaPlayInfo`
**描述:** 生成并发送实时流/回放/下载的 SIP 200 OK SDP。
