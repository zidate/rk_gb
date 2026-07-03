#ifndef __DM_CONFIG_H__
#define __DM_CONFIG_H__

#include <map>
#include <string>

namespace dm
{

static const char* const kDmConfigFile = "/userdata/conf/Config/DM/dm.ini";
static const char* const kDmDefaultAdaptServerUri = "coap://b.fxltsbl.com:5683";
static const char* const kDmDefaultCommercialServerUri = "coap://m.fxltsbl.com:5683";
static const char* const kDmDefaultBrand = "cmiot";
static const char* const kDmDefaultModel = "C4611";
static const char* const kDmDefaultAppkey = "M100000052";
static const char* const kDmDefaultSecret = "n525A97z0M7Vyh91b0508l7j0U5g2g9Y";
static const char* const kDmDefaultTemplateId = "TY000127";
static const char* const kDmDefaultApiVersion = "4.0.1";
static const char* const kDmDefaultApiType = "I";
static const char* const kDmNoValue = "***";

struct DmConfig
{
    int enabled;
    std::string server_uri;
    int local_port;
    int lifetime_sec;
    int short_server_id;
    int startup_retry_interval_sec;

    std::string brand;
    std::string model;
    std::string app_key;
    std::string sdk_version;
    std::string api_version;
    std::string api_type;
    std::string template_id;
    std::string imei1;
    std::string imei2;
    std::string secret;

    std::map<std::string, std::string> device_values;

    DmConfig();
};

int LoadDmConfig(DmConfig& out, const std::string& path = kDmConfigFile);
int GetDmConfig(DmConfig& out, const std::string& path = kDmConfigFile);
int SetDmConfig(const DmConfig& cfg, const std::string& path = kDmConfigFile);
int SaveDmServerUri(const std::string& serverUri,
                    const std::string& path = kDmConfigFile);
std::string BuildDmEndpoint(const DmConfig& cfg);
bool ValidateDmConfig(const DmConfig& cfg, std::string& reason);
std::string GetDmDeviceValue(const DmConfig& cfg, const std::string& field);

}

#endif
