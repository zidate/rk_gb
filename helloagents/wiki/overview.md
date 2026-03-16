# rk_gb 项目概述

## 1. 项目概述

### 目标与背景
基于 RK 芯片 IPC 平台实现多协议接入，当前重点包含 GB28181 与 GAT1400。

### 范围
- 范围内: 设备注册、目录查询、保活、实时流/回放/下载、1400 注册与保活。
- 范围外: 云端平台业务逻辑、非本项目协议栈实现。

## 2. 模块索引

| 模块名称 | 职责 | 状态 | 文档 |
|---------|------|------|------|
| ProtocolManager | 协议编排、配置与会话生命周期 | 稳定 | [modules/protocol.md](modules/protocol.md) |
| GB28181 SDK Port | 对接第三方 GB/SIP SDK 与平台差异适配 | 稳定 | [modules/protocol.md](modules/protocol.md) |
| GAT1400 Client | 1400 注册、保活、接口交互 | 开发中 | [modules/protocol.md](modules/protocol.md) |

## 3. 快速链接
- [技术约定](../project.md)
- [架构设计](arch.md)
- [API 手册](api.md)
- [数据模型](data.md)
- [变更历史](../history/index.md)
