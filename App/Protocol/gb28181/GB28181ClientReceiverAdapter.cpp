#include "GB28181ClientReceiverAdapter.h"
#include "ProtocolLog.h"

#include <stdio.h>

#include "ProtocolManager.h"

#define printf protocol::ProtocolPrintf

namespace protocol
{

namespace
{

const char* SubscribeTypeToString(SubscribeType type)
{
    switch (type) {
    case kCatalogSubscribe:
        return "catalog";
    case kAlarmSubscribe:
        return "alarm";
    case kMobilePositionSubscribe:
        return "mobile_position";
    case kCatalogSubscribeExpire:
        return "catalog_expire";
    case kAlarmSubscribeExpire:
        return "alarm_expire";
    case kMobilePositionSubscribeExpire:
        return "mobile_position_expire";
    default:
        return "unknown";
    }
}

}

GB28181ClientReceiverAdapter::GB28181ClientReceiverAdapter(ProtocolManager* manager)
    : m_manager(manager)
{
}

GB28181ClientReceiverAdapter::~GB28181ClientReceiverAdapter()
{
}

void GB28181ClientReceiverAdapter::BindManager(ProtocolManager* manager)
{
    m_manager = manager;
}

bool GB28181ClientReceiverAdapter::OnNotify(NotifyType type, const char* gb_code, void* info)
{
    if (type != kBroadcastNotify || info == NULL) {
        return true;
    }

    const BroadcastInfo* broadcastInfo = (const BroadcastInfo*)info;
    const std::string sourceId = SafeStr(broadcastInfo->SourceID, sizeof(broadcastInfo->SourceID));
    const std::string targetId = SafeStr(broadcastInfo->TargetID, sizeof(broadcastInfo->TargetID));

    printf("[GB28181][ReceiverAdapter] notify broadcast gb=%s source=%s target=%s\n",
           gb_code != NULL ? gb_code : "",
           sourceId.c_str(),
           targetId.c_str());

    if (m_manager == NULL) {
        return false;
    }

    return (m_manager->HandleGbBroadcastNotify(gb_code, broadcastInfo) == 0);
}


bool GB28181ClientReceiverAdapter::OnQuery(ResponseHandle handle, const QueryParam* param)
{
    if (m_manager == NULL) {
        return false;
    }

    return (m_manager->HandleGbQueryRequest(handle, param) == 0);
}

bool GB28181ClientReceiverAdapter::OnDeviceControl(ResponseHandle handle, const DevControlCmd* cmd)
{
    if (m_manager == NULL) {
        return false;
    }

    return (m_manager->HandleGbDeviceControl(handle, cmd) == 0);
}

bool GB28181ClientReceiverAdapter::OnSubscribe(SubscribeHandle handle, SubscribeType type, const char* gb_code, void* info)
{
    const char* deviceId = (gb_code != NULL) ? gb_code : "";
    const char* subscribeType = SubscribeTypeToString(type);

    if (m_manager == NULL) {
        printf("[GB28181][ReceiverAdapter] module=gb28181 event=subscribe_callback_result device=%s session=%p trace=adapter error=no_manager type=%s ret=-1\n",
               deviceId,
               handle,
               subscribeType);
        return false;
    }

    printf("[GB28181][ReceiverAdapter] module=gb28181 event=subscribe_callback device=%s session=%p trace=adapter error=0 type=%s info=%p\n",
           deviceId,
           handle,
           subscribeType,
           info);

    const int ret = m_manager->HandleGbSubscribe(handle, type, gb_code, info);
    printf("[GB28181][ReceiverAdapter] module=gb28181 event=subscribe_callback_result device=%s session=%p trace=adapter error=%d type=%s ret=%d\n",
           deviceId,
           handle,
           (ret == 0) ? 0 : ret,
           subscribeType,
           ret);
    return (ret == 0);
}

bool GB28181ClientReceiverAdapter::OnStreamRequest(StreamHandle handle,
                                                   const char* gb_code,
                                                   StreamRequestType type,
                                                   const MediaInfo* input)
{
    if (m_manager == NULL) {
        printf("[GB28181][ReceiverAdapter] stream request dropped no_manager device=%s handle=%p type=%d\n",
               gb_code != NULL ? gb_code : "",
               handle,
               (int)type);
        return false;
    }

    if (type == kStopStream) {
        printf("[GB28181][ReceiverAdapter] stream stop request device=%s handle=%p\n",
               gb_code != NULL ? gb_code : "",
               handle);
        m_manager->HandleGbStopStreamRequest(handle, gb_code);
        return true;
    }

    printf("[GB28181][ReceiverAdapter] stream request device=%s handle=%p type=%d transport=%d remote=%s:%u\n",
           gb_code != NULL ? gb_code : "",
           handle,
           (int)type,
           input != NULL ? (int)input->RtpType : -1,
           input != NULL ? input->IP : "",
           input != NULL ? input->Port : 0U);

    int ret = 0;
    switch (type) {
    case kAudioStream:
        ret = m_manager->HandleGbAudioStreamRequest(handle, gb_code, input);
        break;
    case kLiveStream:
        ret = m_manager->HandleGbLiveStreamRequest(handle, gb_code, input);
        break;
    case kPlayback:
        ret = m_manager->HandleGbPlaybackRequest(handle, gb_code, input);
        break;
    case kDownload:
        ret = m_manager->HandleGbDownloadRequest(handle, gb_code, input);
        break;
    default:
        return true;
    }

    printf("[GB28181][ReceiverAdapter] stream request result device=%s handle=%p type=%d ret=%d\n",
           gb_code != NULL ? gb_code : "",
           handle,
           (int)type,
           ret);
    return (ret == 0);
}

bool GB28181ClientReceiverAdapter::OnStreamAck(StreamHandle handle)
{
    if (m_manager == NULL) {
        return false;
    }

    printf("[GB28181][ReceiverAdapter] stream ack handle=%p\n", handle);
    return (m_manager->HandleGbStreamAck(handle) == 0);
}

bool GB28181ClientReceiverAdapter::OnPlayControl(StreamHandle handle, const PlayCtrlCmd* cmd)
{
    if (m_manager == NULL) {
        return false;
    }

    return (m_manager->HandleGbPlayControl(handle, cmd) == 0);
}

std::string GB28181ClientReceiverAdapter::SafeStr(const char* s, size_t maxLen)
{
    if (s == NULL || maxLen == 0) {
        return "";
    }

    size_t len = 0;
    while (len < maxLen && s[len] != '\0') {
        ++len;
    }

    return std::string(s, len);
}

}






