#ifndef __LOCAL_CONFIG_PROVIDER_H__
#define __LOCAL_CONFIG_PROVIDER_H__

#include <string>

#include "IExternalConfigProvider.h"

namespace protocol
{

class LocalConfigProvider : public IExternalConfigProvider
{
public:
    explicit LocalConfigProvider(const std::string& sourceTag);
    virtual ~LocalConfigProvider();

    int PullLatest(ProtocolExternalConfig& out);
    int PushApply(const ProtocolExternalConfig& cfg);
    int Validate(const ProtocolExternalConfig& cfg);
    int QueryCapabilities(std::string& outJson);
    void SubscribeChange();

    void SetMockConfig(const ProtocolExternalConfig& cfg);

private:
    void InitDefaultConfig();

private:
    std::string m_source_tag;
    ProtocolExternalConfig m_cached_cfg;
};

}

#endif
