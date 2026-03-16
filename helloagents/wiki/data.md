# 数据模型

## 概述
协议协商核心数据围绕 GB28181 `MediaInfo`、SIP 对话标识和运行态会话结构展开。

## 关键结构

### MediaInfo

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `RtpType` | enum | UDP/TCP 及 active/passive 模式 |
| `DeviceID` | char[] | 设备国标 ID |
| `IP` | char[] | 对端媒体地址 |
| `Port` | uint | 对端媒体端口 |
| `Ssrc` | char[] | 媒体 SSRC |
| `RequestType` | enum | 实时流/回放/下载/语音 |

### GbLiveSession

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `active` | bool | 实时流会话是否有效 |
| `stream_handle` | 指针 | SIP/GB 流会话句柄 |
| `acked` | bool | 是否已收到 ACK |
| `audio_requested` | bool | 请求是否携带音频 |
