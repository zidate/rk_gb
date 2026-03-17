#ifndef __GB28181_RTP_PS_SENDER_H__
#define __GB28181_RTP_PS_SENDER_H__

#include <stddef.h>
#include <stdint.h>

#include "ProtocolExternalConfig.h"

namespace protocol
{

class GB28181RtpPsSender
{
public:
    GB28181RtpPsSender();
    ~GB28181RtpPsSender();

    int Init(const GbLiveParam& param);
    int OpenSession();
    void CloseSession();

    int SendVideoFrame(const uint8_t* data, size_t size, uint64_t pts90k, bool keyFrame);
    int SendAudioFrame(const uint8_t* data, size_t size, uint64_t pts90k);

    bool IsOpened() const;
    int GetLocalPort() const;

private:
    int EnsureLibrariesLoaded();
    int OpenTransportSocket();

    int ResolveVideoCodecId() const;
    int ResolveAudioCodecId() const;
    int EnsureStreamAdded(int codecId, int* streamId);
    int SendEsFrameByCodec(int codecId, const uint8_t* data, size_t size, uint64_t pts90k, bool keyFrame);

    int OnPsPacket(int stream, void* packet, size_t bytes);
    int OnRtpPacket(const void* packet, int bytes, uint32_t timestamp, int flags);

    static void* OnPsAlloc(void* param, size_t bytes);
    static void OnPsFree(void* param, void* packet);
    static int OnPsWrite(void* param, int stream, void* packet, size_t bytes);
    static void* OnRtpAlloc(void* param, int bytes);
    static void OnRtpFree(void* param, void* packet);
    static int OnRtpPacketWrite(void* param, const void* packet, int bytes, uint32_t timestamp, int flags);

private:
    struct RuntimeState;

    GbLiveParam m_param;
    RuntimeState* m_state;
    bool m_inited;
};

}

#endif
