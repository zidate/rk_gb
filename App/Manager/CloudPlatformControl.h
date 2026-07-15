/* 读取并固定当前进程使用的云平台，平台配置修改后重启生效。 */
#ifndef CLOUD_PLATFORM_CONTROL_H_
#define CLOUD_PLATFORM_CONTROL_H_

enum CloudPlatformType
{
    CLOUD_PLATFORM_GB28181 = 0,
    CLOUD_PLATFORM_CMIOT = 1,
};

CloudPlatformType GetCloudPlatform(void);
const char* GetCloudPlatformName(CloudPlatformType platform);

#endif
