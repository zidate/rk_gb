#include "GB28181RtpPsSender.h"

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

static int WaitForSocketWritable(int sockfd, int timeoutMs)
{
    if (sockfd < 0 || timeoutMs <= 0) {
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    const int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        return -1;
    }

    return FD_ISSET(sockfd, &wfds) ? 0 : -1;
}

static int ConnectTcpWithTimeout(int sockfd, const struct sockaddr_in* remoteAddr, int timeoutMs)
{
    if (sockfd < 0 || remoteAddr == NULL || timeoutMs <= 0) {
        return -1;
    }

    const int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    int ret = connect(sockfd, (const struct sockaddr*)remoteAddr, sizeof(*remoteAddr));
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
        return -1;
    }

    const uint8_t* cursor = (const uint8_t*)data;
    size_t sent = 0;
    while (sent < bytes) {
        const int n = send(sockfd, cursor + sent, bytes - sent, 0);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -2;
        }
        sent += (size_t)n;
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
    bool opened;

    struct sockaddr_in remote_addr;

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
          opened(false),
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
    const bool isTcp = (transport == "tcp");
    if (!isTcp && transport != "udp") {
        printf("[GB28181][RtpPs] unsupported transport=%s\n", m_param.transport.c_str());
        return -2;
    }

    if (m_param.target_ip.empty() || m_param.target_port <= 0) {
        printf("[GB28181][RtpPs] invalid target endpoint %s:%d\n", m_param.target_ip.c_str(), m_param.target_port);
        return -3;
    }

    if (m_state->sockfd >= 0) {
        close(m_state->sockfd);
        m_state->sockfd = -1;
    }
    m_state->local_port = 0;

    const int sockType = isTcp ? SOCK_STREAM : SOCK_DGRAM;
    m_state->sockfd = socket(AF_INET, sockType, 0);
    if (m_state->sockfd < 0) {
        printf("[GB28181][RtpPs] create %s socket failed errno=%d\n", isTcp ? "tcp" : "udp", errno);
        return -4;
    }

    const int reuse = 1;
    if (setsockopt(m_state->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        printf("[GB28181][RtpPs] set SO_REUSEADDR failed errno=%d\n", errno);
    }

    if (isTcp && m_param.local_port > 0) {
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(static_cast<uint16_t>(m_param.local_port));
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(m_state->sockfd, (const struct sockaddr*)&localAddr, sizeof(localAddr)) != 0) {
            printf("[GB28181][RtpPs] bind local port failed errno=%d local_port=%d\n",
                   errno,
                   m_param.local_port);
            close(m_state->sockfd);
            m_state->sockfd = -1;
            return -5;
        }
    }

    memset(&m_state->remote_addr, 0, sizeof(m_state->remote_addr));
    m_state->remote_addr.sin_family = AF_INET;
    m_state->remote_addr.sin_port = htons(static_cast<uint16_t>(m_param.target_port));
    if (1 != inet_pton(AF_INET, m_param.target_ip.c_str(), &m_state->remote_addr.sin_addr)) {
        printf("[GB28181][RtpPs] invalid target ip: %s\n", m_param.target_ip.c_str());
        close(m_state->sockfd);
        m_state->sockfd = -1;
        return -6;
    }

    if (isTcp) {
        const int connectTimeoutMs = 3000;
        if (ConnectTcpWithTimeout(m_state->sockfd, &m_state->remote_addr, connectTimeoutMs) != 0) {
            printf("[GB28181][RtpPs] tcp connect failed errno=%d target=%s:%d\n",
                   errno,
                   m_param.target_ip.c_str(),
                   m_param.target_port);
            close(m_state->sockfd);
            m_state->sockfd = -1;
            return -7;
        }
    }

    struct sockaddr_in localAddr;
    socklen_t localAddrLen = sizeof(localAddr);
    memset(&localAddr, 0, sizeof(localAddr));
    if (getsockname(m_state->sockfd, (struct sockaddr*)&localAddr, &localAddrLen) == 0) {
        m_state->local_port = static_cast<int>(ntohs(localAddr.sin_port));
    } else {
        printf("[GB28181][RtpPs] getsockname failed errno=%d\n", errno);
    }

    const int sendTimeoutMs = 50;
    struct timeval sndTimeout;
    sndTimeout.tv_sec = sendTimeoutMs / 1000;
    sndTimeout.tv_usec = (sendTimeoutMs % 1000) * 1000;
    if (setsockopt(m_state->sockfd, SOL_SOCKET, SO_SNDTIMEO, &sndTimeout, sizeof(sndTimeout)) != 0) {
        printf("[GB28181][RtpPs] set SO_SNDTIMEO failed errno=%d\n", errno);
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

    const uint32_t now = NowSeconds();
    if ((isVideo && m_state->es_video_frames == 1) ||
        (!isVideo && m_state->es_audio_frames == 1) ||
        now != m_state->last_es_log_sec) {
        m_state->last_es_log_sec = now;
        printf("[GB28181][RtpPs] es input media=%s frames=%u size=%lu pts90k=%llu key=%d codec=%d\n",
               isVideo ? "video" : "audio",
               isVideo ? m_state->es_video_frames : m_state->es_audio_frames,
               static_cast<unsigned long>(size),
               static_cast<unsigned long long>(pts90k),
               keyFrame ? 1 : 0,
               codecId);
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

    const uint32_t now = NowSeconds();
    if (m_state->ps_packets == 1 || now != m_state->last_ps_log_sec) {
        m_state->last_ps_log_sec = now;
        printf("[GB28181][RtpPs] ps output packets=%u bytes=%llu current_ps=%lu ts90k=%u\n",
               m_state->ps_packets,
               m_state->ps_bytes,
               static_cast<unsigned long>(bytes),
               m_state->current_timestamp90k);
    }

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

    if (m_state == NULL || m_state->sockfd < 0 || packet == NULL || bytes <= 0) {
        return -1;
    }

    const bool isTcp = (ToLowerCopy(m_param.transport) == "tcp");
    if (isTcp) {
        std::vector<uint8_t> framed(static_cast<size_t>(bytes) + 2U);
        framed[0] = static_cast<uint8_t>((bytes >> 8) & 0xFF);
        framed[1] = static_cast<uint8_t>(bytes & 0xFF);
        memcpy(&framed[2], packet, static_cast<size_t>(bytes));
        if (SendAll(m_state->sockfd, &framed[0], framed.size()) != 0) {
            printf("[GB28181][RtpPs] tcp framed send failed bytes=%d errno=%d transport=%s\n",
                   bytes,
                   errno,
                   m_param.transport.c_str());
            return -2;
        }
    } else {
        const int n = sendto(m_state->sockfd,
                             packet,
                             bytes,
                             0,
                             (struct sockaddr*)&m_state->remote_addr,
                             sizeof(m_state->remote_addr));
        if (n != bytes) {
            printf("[GB28181][RtpPs] transport send failed n=%d expect=%d errno=%d transport=%s\n",
                   n,
                   bytes,
                   errno,
                   m_param.transport.c_str());
            return -2;
        }
    }

    m_state->total_packets += 1;
    m_state->total_bytes += static_cast<unsigned long long>(isTcp ? (bytes + 2) : bytes);

    const uint32_t now = NowSeconds();
    if (now != m_state->last_log_sec) {
        m_state->last_log_sec = now;
        printf("[GB28181][RtpPs] tx packets=%u bytes=%llu target=%s:%d\n",
               m_state->total_packets,
               m_state->total_bytes,
               m_param.target_ip.c_str(),
               m_param.target_port);
    }

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
