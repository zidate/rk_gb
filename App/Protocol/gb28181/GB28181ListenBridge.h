#ifndef __GB28181_LISTEN_BRIDGE_H__
#define __GB28181_LISTEN_BRIDGE_H__

#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>

#include "ProtocolExternalConfig.h"

namespace protocol
{

class GB28181ListenBridge
{
public:
    GB28181ListenBridge();
    ~GB28181ListenBridge();

    int StartSession(const GbListenParam& param);
    void StopSession();

    int PushAudioFrame(const uint8_t* data, size_t size, uint64_t timestamp);

    bool IsRunning() const;

private:
    int OpenTransport();
    void CloseTransport();

    int InitAudioInput();
    void UninitAudioInput();

    int StartCaptureThread();
    void StopCaptureThread();
    int RunCaptureLoop();

    int SendRtpPacket(const uint8_t* payload, size_t size, uint32_t timestamp, bool marker);
    int SendPacketBuffer(const uint8_t* packet, size_t size);

    static void* CaptureThreadEntry(void* arg);
    static int DmcAudioCallback(int media_chn,
                                int media_type,
                                int media_subtype,
                                unsigned long long frame_pts,
                                unsigned char* frame_data,
                                int frame_len,
                                int frame_end_flag);
    int HandleDmcAudioFrame(int media_chn,
                            int media_type,
                            int media_subtype,
                            unsigned long long frame_pts,
                            unsigned char* frame_data,
                            int frame_len,
                            int frame_end_flag);

private:
    GbListenParam m_param;
    bool m_running;

    int m_sock;
    struct sockaddr_storage m_remote_addr;
    socklen_t m_remote_addr_len;

    int m_capture_channel;
    bool m_capture_inited;
    pthread_t m_capture_thread;
    bool m_capture_thread_started;
    bool m_capture_loop;

    uint16_t m_seq;
    uint32_t m_timestamp;
    uint32_t m_ssrc;

    uint32_t m_sent_packets;
    unsigned long long m_sent_bytes;
    uint32_t m_last_log_sec;
};

}

#endif
