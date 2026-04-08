#ifndef __LOCAL_CONFIG_PROVIDER_H__
#define __LOCAL_CONFIG_PROVIDER_H__

#include <string>
#include "ProtocolExternalConfig.h"

namespace protocol
{

class LocalConfigProvider
{
public:
    explicit LocalConfigProvider(const std::string& sourceTag);
    ~LocalConfigProvider();

    int PullLatest(ProtocolExternalConfig& out);
    int PushApply(const ProtocolExternalConfig& cfg);
    int Validate(const ProtocolExternalConfig& cfg);

    static GbRegisterParam BuildDefaultGbRegisterConfig();
    static GbZeroConfigParam BuildDefaultGbZeroConfig();
    static int LoadGbZeroConfig(GbZeroConfigParam& out);
    static int UpdateGbZeroConfig(const GbZeroConfigParam& param);
    static int LoadOrCreateGbRegisterConfig(GbRegisterParam& out);
    static int LoadOrCreateGbRuntimeRegisterConfig(GbRegisterParam& out);
    static int UpdateGbRegisterConfig(const GbRegisterParam& param);
    static GatRegisterParam BuildDefaultGatRegisterConfig();
    static int LoadOrCreateGatRegisterConfig(GatRegisterParam& out);
    static int UpdateGatRegisterConfig(const GatRegisterParam& param);

private:
    void InitDefaultConfig();

private:
    std::string m_source_tag;
    ProtocolExternalConfig m_cached_cfg;
};

}

#endif
