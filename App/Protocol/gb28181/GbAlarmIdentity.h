#ifndef __GB_ALARM_IDENTITY_H__
#define __GB_ALARM_IDENTITY_H__

#include <string>

#include "config/ProtocolExternalConfig.h"
#include "GB28181Defs.h"

namespace protocol {
namespace gb28181 {

std::string ResolveGbAlarmRuntimeDeviceId(const ProtocolExternalConfig& cfg,
                                          const std::string& runtimeDeviceId);

void NormalizeGbAlarmIdentity(const ProtocolExternalConfig& cfg,
                              const std::string& runtimeDeviceId,
                              AlarmNotifyInfo& info);

}  // namespace gb28181
}  // namespace protocol

#endif
