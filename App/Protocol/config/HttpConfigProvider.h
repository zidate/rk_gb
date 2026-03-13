#ifndef __HTTP_CONFIG_PROVIDER_H__
#define __HTTP_CONFIG_PROVIDER_H__

#include <string>
#include "IExternalConfigProvider.h"

namespace protocol
{

class HttpConfigProvider : public IExternalConfigProvider
{
public:
    explicit HttpConfigProvider(const std::string& endpoint);
    virtual ~HttpConfigProvider();

    int PullLatest(ProtocolExternalConfig& out);
    int PushApply(const ProtocolExternalConfig& cfg);
    int Validate(const ProtocolExternalConfig& cfg);
    int QueryCapabilities(std::string& outJson);
    void SubscribeChange();

    void SetMockConfig(const ProtocolExternalConfig& cfg);

private:
    void InitDefaultConfig();
    std::string BuildUrl(const std::string& suffix) const;
    int HttpGet(const std::string& url, std::string& outBody) const;
    int HttpPostJson(const std::string& url, const std::string& jsonBody, std::string& outBody) const;
    std::string ToMinimalJson(const ProtocolExternalConfig& cfg) const;

private:
    std::string m_endpoint;
    ProtocolExternalConfig m_cached_cfg;
};

}

#endif
