#ifndef __DM_LWM2M_OBJECTS_H__
#define __DM_LWM2M_OBJECTS_H__

#include "DmConfig.h"

#include <stdint.h>
#include <time.h>

#include <string>

extern "C" {
#include "liblwm2m.h"
}

namespace dm
{

struct DmRuleConfig
{
    int report_time_min;
    int report_num;
    int heartbeat_time_min;
    int retry_interval_min;
    int retry_num;

    DmRuleConfig();
};

struct DmObjectState
{
    DmConfig config;
    std::string fieldConfig;
    std::string ruleConfig;
    std::string addressConfig;
    bool addressChanged;
    time_t lastReportTime;
    int reportsInWindow;
    time_t reportWindowStart;
    int retryCount;

    DmObjectState();
};

lwm2m_object_t* CreateDmDeviceObject(DmObjectState* state);
lwm2m_object_t* CreateDmLocationObject(DmObjectState* state);
lwm2m_object_t* CreateDmConfigObject(DmObjectState* state);
lwm2m_object_t* CreateDmInfoObject(DmObjectState* state);
void FreeDmObject(lwm2m_object_t* objectP);

DmRuleConfig ParseDmRuleConfig(const std::string& ruleConfig);
std::string BuildDmDeviceInfoJson(DmObjectState& state);
bool DmReportAllowed(DmObjectState& state, time_t now);

}

#endif
