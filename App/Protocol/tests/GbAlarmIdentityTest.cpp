#include <assert.h>
#include <string.h>

#include <iostream>
#include <string>

#include "gb28181/GbAlarmIdentity.h"

static void TestResolveDeviceIdUsesStandardConfig() {
    protocol::ProtocolExternalConfig cfg;
    cfg.gb_register.register_mode = protocol::kGbRegisterModeStandard;
    cfg.gb_register.device_id = "34020000001320000001";
    cfg.gb_register.username = "standard_user";

    assert(protocol::gb28181::ResolveGbAlarmRuntimeDeviceId(cfg, "") ==
           "34020000001320000001");
}

static void TestResolveDeviceIdUsesZeroConfigStringCode() {
    protocol::ProtocolExternalConfig cfg;
    cfg.gb_register.register_mode = protocol::kGbRegisterModeZeroConfig;
    cfg.gb_register.string_code = "C04403261010000000101";
    cfg.gb_register.device_id = "34020000001320000001";
    cfg.gb_register.username = "zero_cfg_user";

    assert(protocol::gb28181::ResolveGbAlarmRuntimeDeviceId(cfg, "") ==
           "C04403261010000000101");
}

static void TestResolveDeviceIdPrefersRuntimeFormalId() {
    protocol::ProtocolExternalConfig cfg;
    cfg.gb_register.register_mode = protocol::kGbRegisterModeZeroConfig;
    cfg.gb_register.string_code = "C04403261010000000101";
    cfg.gb_register.device_id = "34020000001320000001";

    assert(protocol::gb28181::ResolveGbAlarmRuntimeDeviceId(
               cfg, "34020000002000000001") ==
           "34020000002000000001");
}

static void TestNormalizeAlarmIdentityFillsRuntimeDeviceAndDefaultAlarmId() {
    protocol::ProtocolExternalConfig cfg;
    cfg.gb_register.register_mode = protocol::kGbRegisterModeZeroConfig;
    cfg.gb_register.string_code = "C04403261010000000101";

    AlarmNotifyInfo info;
    memset(&info, 0, sizeof(info));

    protocol::gb28181::NormalizeGbAlarmIdentity(cfg,
                                                "34020000002000000001",
                                                info);

    assert(std::string(info.DeviceID) == "34020000002000000001");
    assert(std::string(info.AlarmID) == "34020000002000000001");
}

static void TestNormalizeAlarmIdentityKeepsExplicitAlarmId() {
    protocol::ProtocolExternalConfig cfg;
    cfg.gb_register.register_mode = protocol::kGbRegisterModeStandard;
    cfg.gb_register.device_id = "34020000001320000001";

    AlarmNotifyInfo info;
    memset(&info, 0, sizeof(info));
    strncpy(info.AlarmID, "local_motion_alarm", sizeof(info.AlarmID) - 1);

    protocol::gb28181::NormalizeGbAlarmIdentity(cfg, "", info);

    assert(std::string(info.DeviceID) == "34020000001320000001");
    assert(std::string(info.AlarmID) == "local_motion_alarm");
}

int main() {
    TestResolveDeviceIdUsesStandardConfig();
    TestResolveDeviceIdUsesZeroConfigStringCode();
    TestResolveDeviceIdPrefersRuntimeFormalId();
    TestNormalizeAlarmIdentityFillsRuntimeDeviceAndDefaultAlarmId();
    TestNormalizeAlarmIdentityKeepsExplicitAlarmId();
    std::cout << "GbAlarmIdentityTest passed" << std::endl;
    return 0;
}
