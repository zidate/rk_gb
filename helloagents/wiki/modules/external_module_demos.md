# 外部模块接入 Demo

## 目的
给 Web、算法、编码、业务适配层等外部模块提供最小可复制示例，说明如何通过当前协议入口配置 GB28181 / 零配置 / GAT1400，以及如何查询在线状态和上报 1400 结构化对象。

## 接入原则
- 同进程模块优先通过 `protocol::ProtocolManager::Instance()` 访问协议能力，不再自行创建协议服务实例。
- `SetGbRegisterConfig()`、`SetGbZeroConfig()`、`SetGatRegisterConfig()` 只表示写 flash 成功；如果协议栈已经在运行，需要再显式调用 `RestartGbRegisterService()` 或 `RestartGatRegisterService()` 才会把新值带入运行态。
- `Restart*RegisterService()` 在 `ProtocolManager` 尚未启动时只刷新缓存；后续 `ProtocolManager::Start()` 会再次读取 flash。
- 示例中的 IP、设备编码、用户名和密码都是占位值，实际项目必须由调用方按平台分配参数填入。
- 本文件只提供文档型 demo，不新增编译目标，也不改 Makefile/CMake。

## 1. 设置标准 GB28181 注册参数

适用场景：外部模块提供设备接入平台页面或远程配置接口，需要切回标准国标注册并修改平台注册参数。

```cpp
#include "Protocol/ProtocolManager.h"

int UpdateGbStandardRegisterDemo()
{
    protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();

    protocol::GbRegisterParam cfg = pm.GetGbRegisterConfig();
    cfg.enabled = 1;
    cfg.register_mode = protocol::kGbRegisterModeStandard;
    cfg.username = "34020000002000000001";
    cfg.server_ip = "192.0.2.10";
    cfg.server_port = 5060;
    cfg.device_id = "34020000001320000001";
    cfg.password = "replace-with-platform-password";

    int ret = pm.SetGbRegisterConfig(cfg);
    if (ret != 0) {
        return ret;
    }

    return pm.RestartGbRegisterService();
}
```

注意：
- 这个接口只写 `gb28181.ini` 的标准注册字段，不会修改 `zero_config.ini`。
- `register_mode=standard` 时，`zero_config.ini` 缺失不会阻塞标准国标注册。

## 2. 设置 GB 零配置串码和 MAC

适用场景：装维、产测或 Web 模块需要写入零配置入口参数，并让设备按零配置重定向流程重新注册。

```cpp
#include "Protocol/ProtocolManager.h"

int UpdateGbZeroConfigDemo()
{
    protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();

    protocol::GbRegisterParam gb = pm.GetGbRegisterConfig();
    gb.enabled = 1;
    gb.register_mode = protocol::kGbRegisterModeZeroConfig;

    int ret = pm.SetGbRegisterConfig(gb);
    if (ret != 0) {
        return ret;
    }

    protocol::GbZeroConfigParam zero = pm.GetGbZeroConfig();
    zero.string_code = "C04403261010000000101";
    zero.mac_address = "001122334455";

    ret = pm.SetGbZeroConfig(zero);
    if (ret != 0) {
        return ret;
    }

    return pm.RestartGbRegisterService();
}
```

注意：
- `zero_config.ini` 当前只保存 `string_code/mac_address`。
- 零配置首次重定向使用的 `server_id/server_ip/server_port` 固定走代码默认值，不复用 `gb28181.ini` 的标准平台注册参数。
- 如果设备已经处于 `register_mode=zero_config`，只改串码或 MAC 时可以只调用 `SetGbZeroConfig()`，然后再调用 `RestartGbRegisterService()`。

## 3. 设置 GAT1400 注册参数或停服

适用场景：外部模块需要修改 1400 平台地址、认证参数、本地订阅端口，或显式关闭 1400 注册链路。

```cpp
#include "Protocol/ProtocolManager.h"

int UpdateGat1400RegisterDemo(bool enable)
{
    protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();

    protocol::GatRegisterParam cfg = pm.GetGatRegisterConfig();
    cfg.enabled = enable ? 1 : 0;
    cfg.scheme = "http";
    cfg.server_ip = "192.0.2.20";
    cfg.server_port = 8080;
    cfg.server_ipv6 = "2001:db8::20";
    cfg.server_ipv6_port = 8080;
    cfg.base_path = "";
    cfg.device_id = "34020000001190000001";
    cfg.username = "gat-user";
    cfg.password = "replace-with-gat-password";
    cfg.auth_method = "digest";
    cfg.listen_port = 18080;
    cfg.expires_sec = 3600;
    cfg.keepalive_interval_sec = 60;
    cfg.max_retry = 3;
    cfg.request_timeout_ms = 5000;
    cfg.retry_backoff_policy = "5,10,30";

    int ret = pm.SetGatRegisterConfig(cfg);
    if (ret != 0) {
        return ret;
    }

    return pm.RestartGatRegisterService();
}
```

注意：
- `enabled=0` 会保存停服态配置；协议栈运行中再调用 `RestartGatRegisterService()` 会注销并停止 1400 服务。
- `server_ipv6/server_ipv6_port` 可不填；同时配置 IPv6 和 IPv4 时，GAT1400 对上请求先尝试 IPv6，失败后回退 IPv4。
- 配置模型允许 `scheme=https`，但当前发送实现仍只真正支持 HTTP；平台要求 HTTPS 时需要补 TLS 链路。

## 4. 查询 GB28181 / GAT1400 在线状态

适用场景：外部模块需要在 UI、日志或业务策略里判断协议是否已经注册到平台。

```cpp
#include "Protocol/ProtocolManager.h"

void QueryProtocolOnlineStatusDemo(bool* gbOnline, bool* gatOnline)
{
    protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();

    if (gbOnline != NULL) {
        *gbOnline = pm.GetGbOnlineStatus();
    }

    if (gatOnline != NULL) {
        *gatOnline = pm.GetGatOnlineStatus();
    }
}
```

注意：
- `true` 表示已注册到平台。
- 启动中、重试中、配置关闭、服务停止或未初始化时都会返回 `false`。

## 5. 上报 GAT1400 结构化对象

适用场景：算法或业务模块已经组好人脸、机动车、非机动车对象，只需要交给协议模块异步上报。

```cpp
#include <list>
#include <string.h>

#include "Protocol/ProtocolManager.h"

template <size_t N>
static void CopyField(char (&dst)[N], const char* src)
{
    memset(dst, 0, N);
    if (src != NULL) {
        strncpy(dst, src, N - 1);
    }
}

int NotifyGatMotorVehicleDemo()
{
    GAT_1400_Motor motor;
    CopyField(motor.MotorVehicleID, "motor-20260427-0001");
    CopyField(motor.SourceID, "image-source-20260427-0001");
    CopyField(motor.DeviceID, "34020000001190000001");
    motor.InfoKind = 1;
    motor.LeftTopX = 100;
    motor.LeftTopY = 120;
    motor.RightBtmX = 500;
    motor.RightBtmY = 420;

    std::list<GAT_1400_Motor> motors;
    motors.push_back(motor);

    return protocol::ProtocolManager::Instance().NotifyGatMotorVehicles(motors);
}

int NotifyGatPlateDetectionDemo()
{
    GAT_1400_Motor motor;
    CopyField(motor.MotorVehicleID, "plate-20260706-0001");
    CopyField(motor.SourceID, "image-source-20260706-0001");
    CopyField(motor.DeviceID, "34020000001190000001");
    motor.InfoKind = 1;
    motor.LeftTopX = 140;
    motor.LeftTopY = 180;
    motor.RightBtmX = 460;
    motor.RightBtmY = 360;
    motor.HasPlate = true;
    motor.PlateColor = COLOR_BLUE;
    CopyField(motor.PlateNo, "TEST12345");

    std::list<GAT_1400_Motor> motors;
    motors.push_back(motor);

    return protocol::ProtocolManager::Instance().NotifyGatPlateDetections(motors);
}
```

人脸和非机动车入口类似：

```cpp
int NotifyGatFaceDemo()
{
    GAT_1400_Face face;
    CopyField(face.FaceID, "face-20260427-0001");
    CopyField(face.SourceID, "image-source-20260427-0001");
    CopyField(face.DeviceID, "34020000001190000001");
    face.InfoKind = 1;
    face.LeftTopX = 160;
    face.LeftTopY = 90;
    face.RightBtmX = 260;
    face.RightBtmY = 210;

    std::list<GAT_1400_Face> faces;
    faces.push_back(face);

    return protocol::ProtocolManager::Instance().NotifyGatFaces(faces);
}

int NotifyGatNonMotorDemo()
{
    GAT_1400_NonMotor nonmotor;
    CopyField(nonmotor.NonMotorVehicleID, "nonmotor-20260427-0001");
    CopyField(nonmotor.SourceID, "image-source-20260427-0001");
    CopyField(nonmotor.DeviceID, "34020000001190000001");
    nonmotor.InfoKind = 1;
    nonmotor.LeftTopX = 80;
    nonmotor.LeftTopY = 100;
    nonmotor.RightBtmX = 360;
    nonmotor.RightBtmY = 460;

    std::list<GAT_1400_NonMotor> nonmotors;
    nonmotors.push_back(nonmotor);

    return protocol::ProtocolManager::Instance().NotifyGatNonMotorVehicles(nonmotors);
}
```

注意：
- `NotifyGatFaces()`、`NotifyGatMotorVehicles()`、`NotifyGatPlateDetections()`、`NotifyGatNonMotorVehicles()` 会快速返回；返回 `0` 表示协议模块已接收并完成入队。
- 车牌检测不新增 `GAT_1400_Plate` 对象，直接使用 `GAT_1400_Motor` 的 `HasPlate/PlateNo/PlateColor` 等字段，资源仍是 `/VIID/MotorVehicles`。
- 若 1400 已注册，会立即唤醒后台发送；若未注册，则等待注册恢复后回放。
- 当前这些入口单条最多总发送 2 次，即首次发送加 1 次重发。
- 这些入口只上报结构化对象本体，不会自动补图像、视频或文件。调用方如需上传图片 / 视频 / 文件，需继续走 `LOWER_1400_POST_IMAGES()`、`LOWER_1400_POST_VIDEOSLICES()`、`LOWER_1400_POST_FILES()` 或对应 `Post*` 链路自行编排。

## 6. 使用 LowerGAT1400SDK 兼容导出面

适用场景：外部模块已经依赖 `LowerGAT1400SDK.h`，或者需要调用更多 `LOWER_1400_POST_*` 标准资源接口。

```cpp
#include <list>

#include "LowerGAT1400SDK.h"

int LowerSdkPostMotorVehicleDemo(const std::list<GAT_1400_Motor>& motors)
{
    return LOWER_1400_POST_MOTORVEHICLES(motors);
}

int LowerSdkNotifyPlateDetectionDemo(const std::list<GAT_1400_Motor>& motors)
{
    return LOWER_1400_NOTIFY_PLATEDETECTIONS(motors);
}
```

注意：
- `LOWER_1400_POST_*` 直接进入当前进程内 `GAT1400ClientService::Post*` 主链路。
- 如果只是人脸、机动车、车牌检测、非机动车结构化对象上报，优先使用 `ProtocolManager::NotifyGat*()`，因为它们已经收口为非阻塞异步入队语义。
- 兼容 Lower SDK 调用面新增 `LOWER_1400_NOTIFY_PLATEDETECTIONS()`，语义与 `NotifyGatPlateDetections()` 一致。
- `LOWER_1400_GET_TIME()` 当前为 no-op，不再真正访问 `/VIID/System/Time`。

## 常见错误
- 只调用 `Set*Config()`，但忘记调用 `Restart*RegisterService()`，导致运行中的协议仍使用旧配置。
- 用标准国标的 `server_ip/server_port` 误以为能改零配置首次重定向地址；当前零配置重定向入口固定走代码默认值。
- 在 1400 未注册时同步等待 `NotifyGat*()` 发送完成；当前接口设计是快速入队，未注册时等注册恢复后后台回放。
- 把图片 / 视频 / 文件路径填到结构化对象后就认为协议模块会自动上传本体；当前图片、视频、文件仍需要调用方显式走对应上传接口。
