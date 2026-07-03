#include "GB28181RtpPsSender.h"
#include "ProtocolLog.h"
#include "SocketCompat.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

extern "C"
{
#if defined(__has_include)
#if __has_include("mpeg-ps.h") && __has_include("rtp-payload.h") && __has_include("rtp-profile.h")
#include "mpeg-ps.h"
#include "rtp-payload.h"
#include "rtp-profile.h"
#else
#include "MediaServerCompat.h"
#endif
#else
#include "MediaServerCompat.h"
#endif
}

#define printf protocol::ProtocolPrintf

namespace
{

static std::string ToLowerCopy(const std::string& text)
{
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
        }
    }
    return out;
}

static uint32_t NowSeconds()
{
    return static_cast<uint32_t>(time(NULL));
}

static uint64_t NowMonotonicMilliseconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
}

static bool IsRetryableSendError(int err)
{
    return err == EAGAIN || err == EWOULDBLOCK;
}

static int WaitForSocketWritable(int sockfd, int timeoutMs)
{
    if (sockfd < 0 || timeoutMs <= 0) {
        errno = EINVAL;
        return -1;
    }

    const uint64_t deadlineMs = NowMonotonicMilliseconds() + static_cast<uint64_t>(timeoutMs);

    while (true) {
        const uint64_t nowMs = NowMonotonicMilliseconds();
        if (nowMs >= deadlineMs) {
            errno = ETIMEDOUT;
            return -1;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sockfd, &wfds);

        const int remainMs = static_cast<int>(deadlineMs - nowMs);
        struct timeval tv;
        tv.tv_sec = remainMs / 1000;
        tv.tv_usec = (remainMs % 1000) * 1000;

        const int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
        if (ret > 0) {
            return FD_ISSET(sockfd, &wfds) ? 0 : -1;
        }
        if (ret == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static int WaitForSocketReadable(int sockfd, int timeoutMs)
{
    if (sockfd < 0 || timeoutMs < 0) {
        errno = EINVAL;
        return -1;
    }

    const uint64_t deadlineMs = NowMonotonicMilliseconds() + static_cast<uint64_t>(timeoutMs);

    while (true) {
        const uint64_t nowMs = NowMonotonicMilliseconds();
        if (nowMs > deadlineMs) {
            errno = ETIMEDOUT;
            return -1;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        const int remainMs = static_cast<int>(deadlineMs - nowMs);
        struct timeval tv;
        tv.tv_sec = remainMs / 1000;
        tv.tv_usec = (remainMs % 1000) * 1000;

        const int ret = select(sockfd + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0) {
            return FD_ISSET(sockfd, &rfds) ? 0 : -1;
        }
        if (ret == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static bool IsTcpTransport(const std::string& transport)
{
    return transport == "tcp" ||
           transport == "tcp-active" ||
           transport == "tcp-passive";
}

static bool IsTcpPassiveTransport(const std::string& transport)
{
    return transport == "tcp-passive";
}

static void SetSocketSendTimeout(int sockfd, int timeoutMs)
{
    if (sockfd < 0 || timeoutMs < 0) {
        return;
    }

    struct timeval sndTimeout;
    sndTimeout.tv_sec = timeoutMs / 1000;
    sndTimeout.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &sndTimeout, sizeof(sndTimeout)) != 0) {
        printf("[GB28181][RtpPs] set SO_SNDTIMEO failed errno=%d sock=%d\n", errno, sockfd);
    }
}

static int ConnectTcpWithTimeout(int sockfd,
                                 const struct sockaddr* remoteAddr,
                                 socklen_t remoteAddrLen,
                                 int timeoutMs)
{
    if (sockfd < 0 || remoteAddr == NULL || remoteAddrLen == 0 || timeoutMs <= 0) {
        return -1;
    }

    const int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    int ret = connect(sockfd, remoteAddr, remoteAddrLen);
    int savedErrno = (ret == 0) ? 0 : errno;

    if (ret != 0 && (savedErrno == EINPROGRESS || savedErrno == EALREADY || savedErrno == EWOULDBLOCK)) {
        ret = WaitForSocketWritable(sockfd, timeoutMs);
        if (ret == 0) {
            int soError = 0;
            socklen_t soLen = sizeof(soError);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0) {
                savedErrno = errno;
                ret = -1;
            } else if (soError != 0) {
                savedErrno = soError;
                ret = -1;
            }
        } else if (savedErrno == EINPROGRESS || savedErrno == EALREADY) {
            savedErrno = ETIMEDOUT;
        }
    }

    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags);
    }

    if (ret != 0) {
        errno = savedErrno;
        return -1;
    }

    return 0;
}

static int SendAll(int sockfd, const void* data, size_t bytes)
{
    if (sockfd < 0 || data == NULL || bytes == 0) {
        errno = EINVAL;
        return -1;
    }

    const int totalSendTimeoutMs = 3000;
    const uint64_t deadlineMs = NowMonotonicMilliseconds() + static_cast<uint64_t>(totalSendTimeoutMs);
    const uint8_t* cursor = (const uint8_t*)data;
    size_t sent = 0;
    while (sent < bytes) {
        const int n = send(sockfd, cursor + sent, bytes - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }

        if (n == 0) {
            errno = ECONNRESET;
            return -2;
        }

        const int savedErrno = errno;
        if (savedErrno == EINTR) {
            continue;
        }

        if (IsRetryableSendError(savedErrno)) {
            const uint64_t nowMs = NowMonotonicMilliseconds();
            if (nowMs >= deadlineMs) {
                errno = ETIMEDOUT;
                return -2;
            }

            const int remainMs = static_cast<int>(deadlineMs - nowMs);
            if (WaitForSocketWritable(sockfd, remainMs) == 0) {
                continue;
            }
            return -2;
        }

        errno = savedErrno;
        return -2;
    }

    return 0;
}

} 

namespace protocol
{

struct GB28181RtpPsSender::RuntimeState
{
    typedef struct ps_muxer_t* (*PsMuxerCreateFn)(const struct ps_muxer_func_t* func, void* param);
    typedef int (*PsMuxerDestroyFn)(struct ps_muxer_t* muxer);
    typedef int (*PsMuxerAddStreamFn)(struct ps_muxer_t* muxer, int codecid, const void* extradata, size_t bytes);
    typedef int (*PsMuxerInputFn)(struct ps_muxer_t* muxer,
                                  int stream,
                                  int flags,
                                  int64_t pts,
                                  int64_t dts,
                                  const void* data,
                                  size_t bytes);

    typedef void* (*RtpEncodeCreateFn)(int payload,
                                       const char* name,
                                       uint16_t seq,
                                       uint32_t ssrc,
                                       struct rtp_payload_t* handler,
                                       void* cbparam);
    typedef void (*RtpEncodeDestroyFn)(void* encoder);
    typedef int (*RtpEncodeInputFn)(void* encoder, const void* data, int bytes, uint32_t timestamp);
    typedef void (*RtpEncodeGetInfoFn)(void* encoder, uint16_t* seq, uint32_t* timestamp);
    typedef void (*RtpPacketSetSizeFn)(int bytes);

    struct MediaServerApi
    {
        PsMuxerCreateFn ps_muxer_create;
        PsMuxerDestroyFn ps_muxer_destroy;
        PsMuxerAddStreamFn ps_muxer_add_stream;
        PsMuxerInputFn ps_muxer_input;

        RtpEncodeCreateFn rtp_payload_encode_create;
        RtpEncodeDestroyFn rtp_payload_encode_destroy;
        RtpEncodeInputFn rtp_payload_encode_input;
        RtpEncodeGetInfoFn rtp_payload_encode_getinfo;
        RtpPacketSetSizeFn rtp_packet_setsize;

        MediaServerApi()
            : ps_muxer_create(NULL),
              ps_muxer_destroy(NULL),
              ps_muxer_add_stream(NULL),
              ps_muxer_input(NULL),
              rtp_payload_encode_create(NULL),
              rtp_payload_encode_destroy(NULL),
              rtp_payload_encode_input(NULL),
              rtp_payload_encode_getinfo(NULL),
              rtp_packet_setsize(NULL)
        {
        }
    } api;

    int sockfd;
    int listen_sockfd;
    bool opened;

    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;

    struct ps_muxer_t* ps_muxer;
    void* rtp_encoder;

    std::map<int, int> stream_map;

    uint32_t current_timestamp90k;
    uint16_t seq_seed;
    uint32_t ssrc;

    unsigned int es_video_frames;
    unsigned int es_audio_frames;
    unsigned int ps_packets;
    unsigned long long ps_bytes;
    unsigned long long total_bytes;
    unsigned int total_packets;
    uint32_t last_es_log_sec;
    uint32_t last_ps_log_sec;
    uint32_t last_log_sec;
    int local_port;

    RuntimeState()
        : sockfd(-1),
          listen_sockfd(-1),
          opened(false),
          remote_addr_len(0),
          ps_muxer(NULL),
          rtp_encoder(NULL),
          current_timestamp90k(0),
          seq_seed(static_cast<uint16_t>(rand() & 0xFFFF)),
          ssrc(0),
          es_video_frames(0),
          es_audio_frames(0),
          ps_packets(0),
          ps_bytes(0),
          total_bytes(0),
          total_packets(0),
          last_es_log_sec(0),
          last_ps_log_sec(0),
          last_log_sec(0),
          local_port(0)
    {
        memset(&remote_addr, 0, sizeof(remote_addr));
    }
};

GB28181RtpPsSender::GB28181RtpPsSender()
    : m_state(new RuntimeState()), m_inited(false)
{
}

GB28181RtpPsSender::~GB28181RtpPsSender()
{
    CloseSession();

    if (m_state != NULL) {
        delete m_state;
        m_state = NULL;
    }
}

int GB28181RtpPsSender::Init(const GbLiveParam& param)
{
    m_param = param;
    m_inited = true;
    return 0;
}

int GB28181RtpPsSender::EnsureLibrariesLoaded()
{
    if (m_state == NULL) {
        return -1;
    }

    if (m_state->api.ps_muxer_create != NULL && m_state->api.rtp_payload_encode_create != NULL) {
        return 0;
    }

    m_state->api.ps_muxer_create = &ps_muxer_create;
    m_state->api.ps_muxer_destroy = &ps_muxer_destroy;
    m_state->api.ps_muxer_add_stream = &ps_muxer_add_stream;
    m_state->api.ps_muxer_input = &ps_muxer_input;
    m_state->api.rtp_payload_encode_create = &rtp_payload_encode_create;
    m_state->api.rtp_payload_encode_destroy = &rtp_payload_encode_destroy;
    m_state->api.rtp_payload_encode_input = &rtp_payload_encode_input;
    m_state->api.rtp_payload_encode_getinfo = &rtp_payload_encode_getinfo;
    m_state->api.rtp_packet_setsize = &rtp_packet_setsize;
    return 0;
}

int GB28181RtpPsSender::OpenTransportSocket()
{
    if (m_state == NULL) {
        return -1;
    }

    const std::string transport = ToLowerCopy(m_param.transport);
    const bool isTcp = IsTcpTransport(transport);
    const bool isTcpPassive = IsTcpPassiveTransport(transport);
    if (!isTcp && transport != "udp") {
        printf("[GB28181][RtpPs] unsupported transport=%s\n", m_param.transport.c_str());
        return -2;
    }

    if (!isTcpPassive && (m_param.target_ip.empty() || m_param.target_port <= 0)) {
        printf("[GB28181][RtpPs] invalid target endpoint %s:%d\n", m_param.target_ip.c_str(), m_param.target_port);
        return -3;
    }

    if (m_state->sockfd >= 0) {
        close(m_state->sockfd);
        m_state->sockfd = -1;
    }
    if (m_state->listen_sockfd >= 0) {
        close(m_state->listen_sockfd);
        m_state->listen_sockfd = -1;
    }
    m_state->local_port = 0;

    const int sockType = isTcp ? SOCK_STREAM : SOCK_DGRAM;
    int family = AF_INET;
    protocol::socket_compat::Endpoint remoteEndpoint;

#if RK_ENABLE_GB_IPV6_SOCKET
    if (!m_param.target_ip.empty()) {
        if (!protocol::socket_compat::ResolveEndpoint(m_param.target_ip,
                                                       m_param.target_port,
                                                       sockType,
                                                       &remoteEndpoint)) {
            printf("[GB28181][RtpPs] invalid target ip: %s\n", m_param.target_ip.c_str());
            return -6;
        }
        family = remoteEndpoint.family;
    } else if (isTcpPassive) {
        family = AF_INET6;
    }
    const int sockfd = protocol::socket_compat::CreateSocket(family, sockType, isTcpPassive);
#else
    const int sockfd = socket(AF_INET, sockType, 0);
#endif
    if (sockfd < 0) {
        printf("[GB28181][RtpPs] create %s socket failed errno=%d\n", isTcp ? "tcp" : "udp", errno);
        return -4;
    }

    const int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        printf("[GB28181][RtpPs] set SO_REUSEADDR failed errno=%d\n", errno);
    }

    if (isTcpPassive || m_param.local_port > 0) {
#if RK_ENABLE_GB_IPV6_SOCKET
        protocol::socket_compat::Endpoint localEndpoint;
        if (!protocol::socket_compat::BuildAnyEndpoint(family,
                                                        (m_param.local_port > 0) ? m_param.local_port : 0,
                                                        &localEndpoint) ||
            bind(sockfd,
                 protocol::socket_compat::AsSockaddr(localEndpoint),
                 localEndpoint.len) != 0) {
            printf("[GB28181][RtpPs] bind local port failed errno=%d local_port=%d\n",
                   errno,
                   m_param.local_port);
            close(sockfd);
            return -5;
        }
#else
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(static_cast<uint16_t>((m_param.local_port > 0) ? m_param.local_port : 0));
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sockfd, (const struct sockaddr*)&localAddr, sizeof(localAddr)) != 0) {
            printf("[GB28181][RtpPs] bind local port failed errno=%d local_port=%d\n",
                   errno,
                   m_param.local_port);
            close(sockfd);
            return -5;
        }
#endif
    }

    memset(&m_state->remote_addr, 0, sizeof(m_state->remote_addr));
    m_state->remote_addr_len = 0;
    if (!m_param.target_ip.empty()) {
#if RK_ENABLE_GB_IPV6_SOCKET
        memcpy(&m_state->remote_addr, &remoteEndpoint.addr, sizeof(remoteEndpoint.addr));
        m_state->remote_addr_len = remoteEndpoint.len;
#else
        struct sockaddr_in* remoteAddr = (struct sockaddr_in*)&m_state->remote_addr;
        remoteAddr->sin_family = AF_INET;
        remoteAddr->sin_port = htons(static_cast<uint16_t>(m_param.target_port));
        if (1 != inet_pton(AF_INET, m_param.target_ip.c_str(), &remoteAddr->sin_addr)) {
            printf("[GB28181][RtpPs] invalid target ip: %s\n", m_param.target_ip.c_str());
            close(sockfd);
            return -6;
        }
        m_state->remote_addr_len = sizeof(struct sockaddr_in);
#endif
    }

    if (isTcpPassive) {
        if (listen(sockfd, 1) != 0) {
            printf("[GB28181][RtpPs] tcp passive listen failed errno=%d local_port=%d\n",
                   errno,
                   m_param.local_port);
            close(sockfd);
            return -7;
        }
        m_state->listen_sockfd = sockfd;
    } else {
        m_state->sockfd = sockfd;
    }

    if (isTcp && !isTcpPassive) {
        const int connectTimeoutMs = 3000;
        if (ConnectTcpWithTimeout(m_state->sockfd,
                                  (const struct sockaddr*)&m_state->remote_addr,
                                  m_state->remote_addr_len,
                                  connectTimeoutMs) != 0) {
            printf("[GB28181][RtpPs] tcp connect failed errno=%d target=%s:%d\n",
                   errno,
                   m_param.target_ip.c_str(),
                   m_param.target_port);
            close(m_state->sockfd);
            m_state->sockfd = -1;
            return -7;
        }
    }

    struct sockaddr_storage localAddr;
    socklen_t localAddrLen = sizeof(localAddr);
    memset(&localAddr, 0, sizeof(localAddr));
    const int localSockfd = (m_state->listen_sockfd >= 0) ? m_state->listen_sockfd : m_state->sockfd;
    if (getsockname(localSockfd, (struct sockaddr*)&localAddr, &localAddrLen) == 0) {
        m_state->local_port = protocol::socket_compat::GetPort(localAddr);
    } else {
        printf("[GB28181][RtpPs] getsockname failed errno=%d\n", errno);
    }

    if (m_state->sockfd >= 0) {
        SetSocketSendTimeout(m_state->sockfd, 50);
    }

    return 0;
}
int GB28181RtpPsSender::OpenSession()
{
    if (!m_inited || m_state == NULL) {
        return -1;
    }

    if (m_state->opened) {
        return 0;
    }

    int ret = EnsureLibrariesLoaded();
    if (ret != 0) {
        return ret;
    }

    ret = OpenTransportSocket();
    if (ret != 0) {
        return ret;
    }

    m_state->seq_seed = static_cast<uint16_t>(rand() & 0xFFFF);
    if (m_param.ssrc > 0) {
        m_state->ssrc = static_cast<uint32_t>(m_param.ssrc);
    } else {
        m_state->ssrc = static_cast<uint32_t>((time(NULL) ^ getpid()) & 0x7FFFFFFF);
    }

    if (m_state->api.rtp_packet_setsize != NULL && m_param.mtu > 256) {
        m_state->api.rtp_packet_setsize(m_param.mtu);
    }

    struct rtp_payload_t rtpHandler;
    memset(&rtpHandler, 0, sizeof(rtpHandler));
    rtpHandler.alloc = OnRtpAlloc;
    rtpHandler.free = OnRtpFree;
    rtpHandler.packet = OnRtpPacketWrite;

    const int payloadType = (m_param.payload_type >= 0 && m_param.payload_type <= 127)
                                ? m_param.payload_type
                                : RTP_PAYLOAD_MP2P;

    m_state->rtp_encoder = m_state->api.rtp_payload_encode_create(payloadType,
                                                                  "MP2P",
                                                                  m_state->seq_seed,
                                                                  m_state->ssrc,
                                                                  &rtpHandler,
                                                                  this);
    if (m_state->rtp_encoder == NULL) {
        printf("[GB28181][RtpPs] create rtp encoder failed\n");
        CloseSession();
        return -6;
    }

    struct ps_muxer_func_t psHandler;
    memset(&psHandler, 0, sizeof(psHandler));
    psHandler.alloc = OnPsAlloc;
    psHandler.free = OnPsFree;
    psHandler.write = OnPsWrite;

    m_state->ps_muxer = m_state->api.ps_muxer_create(&psHandler, this);
    if (m_state->ps_muxer == NULL) {
        printf("[GB28181][RtpPs] create ps muxer failed\n");
        CloseSession();
        return -7;
    }

    m_state->stream_map.clear();
    m_state->es_video_frames = 0;
    m_state->es_audio_frames = 0;
    m_state->ps_packets = 0;
    m_state->ps_bytes = 0;
    m_state->total_bytes = 0;
    m_state->total_packets = 0;
    m_state->last_es_log_sec = 0;
    m_state->last_ps_log_sec = 0;
    m_state->last_log_sec = 0;
    m_state->opened = true;

    printf("[GB28181][RtpPs] session opened target=%s:%d local_port=%d payload=%d mtu=%d ssrc=%u\n",
           m_param.target_ip.c_str(),
           m_param.target_port,
           m_state->local_port,
           payloadType,
           m_param.mtu,
           m_state->ssrc);
    return 0;
}

void GB28181RtpPsSender::CloseSession()
{
    if (m_state == NULL) {
        return;
    }

    uint16_t seq = 0;
    uint32_t timestamp = 0;
    if (m_state->rtp_encoder != NULL && m_state->api.rtp_payload_encode_getinfo != NULL) {
        m_state->api.rtp_payload_encode_getinfo(m_state->rtp_encoder, &seq, &timestamp);
    }

    if (m_state->ps_muxer != NULL && m_state->api.ps_muxer_destroy != NULL) {
        m_state->api.ps_muxer_destroy(m_state->ps_muxer);
        m_state->ps_muxer = NULL;
    }

    if (m_state->rtp_encoder != NULL && m_state->api.rtp_payload_encode_destroy != NULL) {
        m_state->api.rtp_payload_encode_destroy(m_state->rtp_encoder);
        m_state->rtp_encoder = NULL;
    }

    if (m_state->sockfd >= 0) {
        close(m_state->sockfd);
        m_state->sockfd = -1;
    }
    if (m_state->listen_sockfd >= 0) {
        close(m_state->listen_sockfd);
        m_state->listen_sockfd = -1;
    }
    m_state->local_port = 0;

    if (m_state->opened) {
        printf("[GB28181][RtpPs] session closed es_video=%u es_audio=%u ps_packets=%u ps_bytes=%llu rtp_packets=%u rtp_bytes=%llu seq=%hu ts=%u\n",
               m_state->es_video_frames,
               m_state->es_audio_frames,
               m_state->ps_packets,
               m_state->ps_bytes,
               m_state->total_packets,
               m_state->total_bytes,
               seq,
               timestamp);
    }

    m_state->stream_map.clear();
    m_state->opened = false;
}

int GB28181RtpPsSender::ResolveVideoCodecId() const
{
    const std::string videoCodec = ToLowerCopy(m_param.video_codec);
    if (videoCodec == "h265" || videoCodec == "hevc") {
        return PSI_STREAM_H265;
    }

    return PSI_STREAM_H264;
}

int GB28181RtpPsSender::ResolveAudioCodecId() const
{
    const std::string audioCodec = ToLowerCopy(m_param.audio_codec);
    if (audioCodec == "aac") {
        return PSI_STREAM_AAC;
    }

    if (audioCodec == "g711u" || audioCodec == "pcmu") {
        return PSI_STREAM_AUDIO_G711U;
    }

    if (audioCodec == "g722") {
        return PSI_STREAM_AUDIO_G722;
    }

    return PSI_STREAM_AUDIO_G711A;
}

int GB28181RtpPsSender::EnsureStreamAdded(int codecId, int* streamId)
{
    if (m_state == NULL || m_state->ps_muxer == NULL || streamId == NULL) {
        return -1;
    }

    std::map<int, int>::iterator it = m_state->stream_map.find(codecId);
    if (it != m_state->stream_map.end()) {
        *streamId = it->second;
        return 0;
    }

    const int id = m_state->api.ps_muxer_add_stream(m_state->ps_muxer, codecId, NULL, 0);
    if (id <= 0) {
        printf("[GB28181][RtpPs] add ps stream failed codec=%d ret=%d\n", codecId, id);
        return -2;
    }

    m_state->stream_map[codecId] = id;
    *streamId = id;
    return 0;
}

int GB28181RtpPsSender::SendEsFrameByCodec(int codecId,
                                           const uint8_t* data,
                                           size_t size,
                                           uint64_t pts90k,
                                           bool keyFrame)
{
    if (m_state == NULL || !m_state->opened) {
        return -1;
    }

    if (data == NULL || size == 0) {
        return -2;
    }

    int streamId = 0;
    int ret = EnsureStreamAdded(codecId, &streamId);
    if (ret != 0) {
        return ret;
    }

    const int flags = keyFrame ? 0x0001 : 0;
    m_state->current_timestamp90k = static_cast<uint32_t>(pts90k & 0xFFFFFFFFu);

    const bool isVideo = (codecId == PSI_STREAM_H264 || codecId == PSI_STREAM_H265);
    if (isVideo) {
        ++m_state->es_video_frames;
    } else {
        ++m_state->es_audio_frames;
    }

    ret = m_state->api.ps_muxer_input(m_state->ps_muxer,
                                      streamId,
                                      flags,
                                      static_cast<int64_t>(pts90k),
                                      static_cast<int64_t>(pts90k),
                                      data,
                                      size);
    if (ret != 0) {
        printf("[GB28181][RtpPs] ps muxer input failed ret=%d size=%lu codec=%d\n",
               ret,
               static_cast<unsigned long>(size),
               codecId);
        return -3;
    }

    return 0;
}

int GB28181RtpPsSender::SendVideoFrame(const uint8_t* data, size_t size, uint64_t pts90k, bool keyFrame)
{
    return SendEsFrameByCodec(ResolveVideoCodecId(), data, size, pts90k, keyFrame);
}

int GB28181RtpPsSender::SendVideoFrameByCodecType(const uint8_t* data,
                                                  size_t size,
                                                  uint64_t pts90k,
                                                  bool keyFrame,
                                                  int codecType)
{
    if (codecType == 2) {
        return SendEsFrameByCodec(PSI_STREAM_H265, data, size, pts90k, keyFrame);
    }

    if (codecType == 1) {
        return SendEsFrameByCodec(PSI_STREAM_H264, data, size, pts90k, keyFrame);
    }

    return SendVideoFrame(data, size, pts90k, keyFrame);
}

int GB28181RtpPsSender::SendAudioFrame(const uint8_t* data, size_t size, uint64_t pts90k)
{
    return SendEsFrameByCodec(ResolveAudioCodecId(), data, size, pts90k, false);
}

bool GB28181RtpPsSender::IsOpened() const
{
    return m_state != NULL && m_state->opened;
}

int GB28181RtpPsSender::GetLocalPort() const
{
    if (m_state == NULL) {
        return 0;
    }

    return m_state->local_port;
}

int GB28181RtpPsSender::OnPsPacket(int stream, void* packet, size_t bytes)
{
    (void)stream;

    if (m_state == NULL || m_state->rtp_encoder == NULL || packet == NULL || bytes == 0) {
        return -1;
    }

    ++m_state->ps_packets;
    m_state->ps_bytes += static_cast<unsigned long long>(bytes);

    const int ret = m_state->api.rtp_payload_encode_input(m_state->rtp_encoder,
                                                           packet,
                                                           static_cast<int>(bytes),
                                                           m_state->current_timestamp90k);
    if (ret != 0) {
        printf("[GB28181][RtpPs] rtp payload encode failed ret=%d ps_bytes=%lu ts90k=%u\n",
               ret,
               static_cast<unsigned long>(bytes),
               m_state->current_timestamp90k);
    }
    return ret;
}

int GB28181RtpPsSender::OnRtpPacket(const void* packet, int bytes, uint32_t timestamp, int flags)
{
    (void)timestamp;
    (void)flags;

    if (m_state == NULL || !m_state->opened || packet == NULL || bytes <= 0) {
        return -1;
    }

    const std::string transport = ToLowerCopy(m_param.transport);
    const bool isTcp = IsTcpTransport(transport);
    const bool isTcpPassive = IsTcpPassiveTransport(transport);
    if (isTcpPassive && m_state->sockfd < 0 && m_state->listen_sockfd >= 0) {
        if (WaitForSocketReadable(m_state->listen_sockfd, 20) == 0) {
            struct sockaddr_storage remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);
            memset(&remoteAddr, 0, sizeof(remoteAddr));
            const int clientSockfd = accept(m_state->listen_sockfd,
                                            (struct sockaddr*)&remoteAddr,
                                            &remoteAddrLen);
            if (clientSockfd >= 0) {
                SetSocketSendTimeout(clientSockfd, 50);
                m_state->sockfd = clientSockfd;

                char remoteIp[INET6_ADDRSTRLEN] = {0};
                int remotePort = 0;
                protocol::socket_compat::SockaddrToString((struct sockaddr*)&remoteAddr,
                                                          remoteAddrLen,
                                                          remoteIp,
                                                          sizeof(remoteIp),
                                                          &remotePort);
                printf("[GB28181][RtpPs] tcp passive accept remote=%s:%d local_port=%d\n",
                       remoteIp,
                       remotePort,
                       m_state->local_port);
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("[GB28181][RtpPs] tcp passive accept failed errno=%d local_port=%d\n",
                       errno,
                       m_state->local_port);
            }
        }
        if (m_state->sockfd < 0) {
            return 0;
        }
    }

    if (m_state->sockfd < 0) {
        return -1;
    }

    if (isTcp) {
        std::vector<uint8_t> framed(static_cast<size_t>(bytes) + 2U);
        framed[0] = static_cast<uint8_t>((bytes >> 8) & 0xFF);
        framed[1] = static_cast<uint8_t>(bytes & 0xFF);
        memcpy(&framed[2], packet, static_cast<size_t>(bytes));
        if (SendAll(m_state->sockfd, &framed[0], framed.size()) != 0) {
            const int savedErrno = errno;
            if (savedErrno == EPIPE || savedErrno == ECONNRESET || savedErrno == ENOTCONN) {
                close(m_state->sockfd);
                m_state->sockfd = -1;
            }
            printf("[GB28181][RtpPs] tcp framed send failed bytes=%d errno=%d transport=%s\n",
                   bytes,
                   savedErrno,
                   m_param.transport.c_str());
            return -2;
        }
    } else {
        const int n = sendto(m_state->sockfd,
                             packet,
                             bytes,
                             0,
                             (struct sockaddr*)&m_state->remote_addr,
                             m_state->remote_addr_len);
        if (n != bytes) {
            const int savedErrno = errno;
            if (savedErrno == EPIPE || savedErrno == ECONNRESET || savedErrno == ENOTCONN) {
                close(m_state->sockfd);
                m_state->sockfd = -1;
            }
            printf("[GB28181][RtpPs] transport send failed n=%d expect=%d errno=%d transport=%s\n",
                   n,
                   bytes,
                   savedErrno,
                   m_param.transport.c_str());
            return -2;
        }
    }

    m_state->total_packets += 1;
    m_state->total_bytes += static_cast<unsigned long long>(isTcp ? (bytes + 2) : bytes);

    return 0;
}

void* GB28181RtpPsSender::OnPsAlloc(void* param, size_t bytes)
{
    (void)param;
    return malloc(bytes);
}

void GB28181RtpPsSender::OnPsFree(void* param, void* packet)
{
    (void)param;
    free(packet);
}

int GB28181RtpPsSender::OnPsWrite(void* param, int stream, void* packet, size_t bytes)
{
    GB28181RtpPsSender* self = (GB28181RtpPsSender*)param;
    if (self == NULL) {
        return -1;
    }

    return self->OnPsPacket(stream, packet, bytes);
}

void* GB28181RtpPsSender::OnRtpAlloc(void* param, int bytes)
{
    (void)param;
    if (bytes <= 0) {
        return NULL;
    }

    return malloc(static_cast<size_t>(bytes));
}

void GB28181RtpPsSender::OnRtpFree(void* param, void* packet)
{
    (void)param;
    free(packet);
}

int GB28181RtpPsSender::OnRtpPacketWrite(void* param,
                                         const void* packet,
                                         int bytes,
                                         uint32_t timestamp,
                                         int flags)
{
    GB28181RtpPsSender* self = (GB28181RtpPsSender*)param;
    if (self == NULL) {
        return -1;
    }

    return self->OnRtpPacket(packet, bytes, timestamp, flags);
}

}
