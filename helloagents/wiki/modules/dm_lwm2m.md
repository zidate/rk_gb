# DM LwM2M 接入

## 概览

本项目新增独立 DM 客户端，不接入 `ProtocolManager`。主程序在网络初始化后的协议启动段调用 `dm::DmClientService::Instance().Start()`，析构、升级释放资源时调用 `Stop()`，网络链路或 IP 变化时调用 `dm::RestartDmClientService()`。

## 配置

DM 参数文件为 `/userdata/conf/Config/DM/dm.ini`。首次启动缺失该文件时会生成默认配置，默认 `enabled=0`，避免未配置真实平台参数时误连 DM 平台。

关键字段：
- `server_uri`: LwM2M/CoAP 地址，默认 `coap://m.fxltsbl.com:5683`
- `brand`, `model`, `app_key`, `template_id`, `imei1`, `secret`: 启用后必填
- `sdk_version`: 非 SDK 接入默认 `***`
- `api_version`: 默认 `4.0.1`
- `api_type`: 非 Android 默认 `I`
- `device_*`: 669/0/1 上报字段来源，缺失时按 DM 文档使用 `***`

对外模块可通过 `App/DM/DmConfig.h` 读写完整 DM 参数：
- `dm::GetDmConfig(DmConfig& out, path)`: 读取 `dm.ini`，文件缺失时按默认值创建。
- `dm::SetDmConfig(const DmConfig& cfg, path)`: 保存完整 DM 参数；启用状态下会复用 `ValidateDmConfig` 校验，非法启用配置不落盘。
- `DmConfig::device_values` 使用不带 `device_` 前缀的字段名，保存时写成 `device_<field>`；已覆盖 669/0/1 支持的标准字段，并保留额外自定义字段。

## 实现边界

Wakaama 源码 vendoring 到 `third_party/wakaama`，以 `wakaama_static` 静态库编译。工程没有使用 Wakaama 自带 CMake 入口，因为当前项目 CMake 最低版本为 3.0，而 Wakaama 自带入口要求更高版本。

注册使用 Wakaama 生成 `lwm2m=1.1` 和 `</>;rt="oma.lwm2m"...` ObjectLinks payload；心跳使用带 ObjectLinks payload 的 registration update，满足 DM 文档对 Update 序列化资源列表的要求。

DM 自定义对象：
- `668/0/1 fieldConfig`: 平台写入字段开关
- `668/0/2 ruleConfig`: 平台写入心跳、重试、限频规则
- `668/0/3 addressConfig`: 平台写入新上报地址，变化后持久化到 `dm.ini`，仅更新 `server_uri`，保留 `enabled` 等既有配置
- `669/0/1 deviceInfo`: 按字段开关生成 JSON，并对每个值执行 `SHA256(secret) + AES-256-CBC + PKCS7 + Base64`

## 验证

本地回归：
- `python3 tools/tests/dm_lwm2m_regression.py`
- `python3 tools/tests/dm_config_regression.py`
- `python3 tools/tests/dm_crypto_compile_regression.py`

构建验证：
- `wakaama_static` 可用当前工具链编译通过。
- 主程序源码和 DM 对象编译通过，最终链接在当前环境缺少 Rockchip `-lmpp` 时停止。
