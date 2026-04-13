#include "GbAlarmIdentity.h"

#include <stdio.h>
#include <string.h>

namespace {

static void CopyBoundedText(char* dest, size_t destSize, const std::string& value) {
    if (dest == NULL || destSize == 0) {
        return;
    }

    memset(dest, 0, destSize);
    if (value.empty()) {
        return;
    }

    snprintf(dest, destSize, "%s", value.c_str());
}

static std::string ResolveGbConfiguredLocalDeviceId(const protocol::ProtocolExternalConfig& cfg) {
    if (protocol::IsGbRegisterModeZeroConfig(cfg.gb_register)) {
        if (!cfg.gb_register.string_code.empty()) {
            return cfg.gb_register.string_code;
        }
        if (!cfg.gb_register.device_id.empty()) {
            return cfg.gb_register.device_id;
        }
        return cfg.gb_register.username;
    }

    if (!cfg.gb_register.device_id.empty()) {
        return cfg.gb_register.device_id;
    }
    return cfg.gb_register.username;
}

}  // namespace

namespace protocol {
namespace gb28181 {

std::string ResolveGbAlarmRuntimeDeviceId(const ProtocolExternalConfig& cfg,
                                          const std::string& runtimeDeviceId) {
    if (!runtimeDeviceId.empty()) {
        return runtimeDeviceId;
    }

    return ResolveGbConfiguredLocalDeviceId(cfg);
}

void NormalizeGbAlarmIdentity(const ProtocolExternalConfig& cfg,
                              const std::string& runtimeDeviceId,
                              AlarmNotifyInfo& info) {
    const std::string resolvedDeviceId =
        ResolveGbAlarmRuntimeDeviceId(cfg, runtimeDeviceId);

    if (resolvedDeviceId.empty()) {
        return;
    }

    CopyBoundedText(info.DeviceID, sizeof(info.DeviceID), resolvedDeviceId);
    if (info.AlarmID[0] == '\0') {
        CopyBoundedText(info.AlarmID, sizeof(info.AlarmID), resolvedDeviceId);
    }
}

}  // namespace gb28181
}  // namespace protocol
