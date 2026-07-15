/* 从持久化配置读取云平台，并在进程生命周期内保持选择不变。 */
#include "CloudPlatformControl.h"

#include "Common.h"

namespace
{

CloudPlatformType ReadCloudPlatform(void)
{
    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_CLOUD_PLATFORM), table)) {
        AppErr("get cloud platform config failed, fallback GB28181\n");
        return CLOUD_PLATFORM_GB28181;
    }

    const int value = table["platform"].asInt();
    if (value == CLOUD_PLATFORM_CMIOT) {
        return CLOUD_PLATFORM_CMIOT;
    }
    if (value != CLOUD_PLATFORM_GB28181) {
        AppErr("invalid cloud platform %d, fallback GB28181\n", value);
    }
    return CLOUD_PLATFORM_GB28181;
}

} // namespace

CloudPlatformType GetCloudPlatform(void)
{
    static const CloudPlatformType platform = ReadCloudPlatform();
    return platform;
}

const char* GetCloudPlatformName(CloudPlatformType platform)
{
    return platform == CLOUD_PLATFORM_CMIOT ? "CMIOT" : "GB28181";
}
