#include "GB28181ListenBridge.h"
#include "ProtocolLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

extern "C"
{
#include "PAL/Audio.h"
#include "PAL/Capture.h"
#include "PAL/Audio_coder.h"
}

#define printf protocol::ProtocolPrintf

namespace protocol
{
class GB28181ListenBridge;
}

namespace
{

static protocol::GB28181ListenBridge* g_listen_bridge_instance = NULL;
static const char* kListenDmcModuleName = "gb28181_listen";

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

static std::string NormalizeCodec(const std::string& codecIn)
{
    const std::string codec = ToLowerCopy(codecIn);
    if (codec == "pcma" || codec == "g711a") {
        return "g711a";
    }

    if (codec == "pcmu" || codec == "g711u") {
        return "g711u";
    }

    if (codec == "pcm" || codec == "pcm16" || codec == "pcm_s16le" || codec == "l16") {
        return "pcm";
    }

    return "";
}

static int ResolvePayloadType(const std::string& codec)
{
    const std::string normalized = NormalizeCodec(codec);
    if (normalized == "g711a") {
        return 8;
    }

    if (normalized == "g711u") {
        return 0;
    }

    return 96;
}

static int ResolveAudioEncodeType(const std::string& codec)
{
    const std::string normalized = NormalizeCodec(codec);
    if (normalized == "g711a") {
        return G711_ALAW;
    }

    if (normalized == "g711u") {
        return G711_ULAW;
    }

    return PCM_8TO16BIT;
}

static uint32_t ResolveSampleCount(const std::string& codec, size_t bytes)
{
    const std::string normalized = NormalizeCodec(codec);
    if (normalized == "g711a" || normalized == "g711u") {
        return static_cast<uint32_t>(bytes);
    }

    if (bytes >= 2) {
        return static_cast<uint32_t>(bytes / 2);
    }

    return 160;
}

static uint64_t GetNowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static uint32_t GetNowSec()
{
    return static_cast<uint32_t>(time(NULL));
}

static uint32_t MakeSsrcSeed()
{
    const uint64_t now = GetNowMs();
    const uint32_t pidv = static_cast<uint32_t>(getpid());
    return (uint32_t)(now & 0xFFFFFFFFu) ^ (pidv << 16) ^ 0x6b281810u;
}

static int SendAll(int sock, const uint8_t* data, size_t size)
{
    size_t sent = 0;
    while (sent < size) {
        const int n = send(sock, data + sent, size - sent, 0);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        sent += static_cast<size_t>(n);
    }

    return 0;
}

}

namespace protocol
{

GB28181ListenBridge::GB28181ListenBridge()
    : m_running(false),
      m_sock(-1),
      m_capture_channel(0),
      m_capture_inited(false),
      m_capture_thread_started(false),
      m_capture_loop(false),
      m_seq(0),
      m_timestamp(0),
      m_ssrc(MakeSsrcSeed()),
      m_sent_packets(0),
      m_sent_bytes(0),
      m_last_log_sec(0)
{
    memset(&m_remote_addr, 0, sizeof(m_remote_addr));
}

GB28181ListenBridge::~GB28181ListenBridge()
{
    StopSession();
}

int GB28181ListenBridge::StartSession(const GbListenParam& param)
{
    if (param.target_ip.empty() || param.target_port <= 0) {
        printf("[GB28181][Listen] invalid target endpoint\n");
        return -1;
    }

    if (param.transport != "udp" && param.transport != "tcp") {
        printf("[GB28181][Listen] invalid transport: %s\n", param.transport.c_str());
        return -2;
    }

    const std::string normalizedCodec = NormalizeCodec(param.codec);
    if (normalizedCodec.empty()) {
        printf("[GB28181][Listen] unsupported codec: %s\n", param.codec.c_str());
        return -3;
    }

    if (param.packet_ms < 10 || param.packet_ms > 200) {
        printf("[GB28181][Listen] invalid packet_ms=%d\n", param.packet_ms);
        return -4;
    }

    if (param.sample_rate <= 0) {
        printf("[GB28181][Listen] invalid sample_rate=%d\n", param.sample_rate);
        return -5;
    }

    StopSession();

    m_param = param;
    m_param.codec = normalizedCodec;
    m_seq = 0;
    m_timestamp = 0;
    m_ssrc = MakeSsrcSeed();
    m_sent_packets = 0;
    m_sent_bytes = 0;
    m_last_log_sec = 0;

    int ret = OpenTransport();
    if (ret != 0) {
        return ret;
    }

    ret = InitAudioInput();
    if (ret != 0) {
        CloseTransport();
        return ret;
    }

    m_running = true;
    ret = StartCaptureThread();
    if (ret != 0) {
        m_running = false;
        UninitAudioInput();
        CloseTransport();
        return ret;
    }

    printf("[GB28181][Listen] start session target=%s:%d codec=%s transport=%s packet_ms=%d sample_rate=%d\n",
           m_param.target_ip.c_str(),
           m_param.target_port,
           m_param.codec.c_str(),
           m_param.transport.c_str(),
           m_param.packet_ms,
           m_param.sample_rate);

    return 0;
}

void GB28181ListenBridge::StopSession()
{
    if (!m_running && !m_capture_thread_started && !m_capture_inited && m_sock < 0) {
        return;
    }

    m_running = false;
    StopCaptureThread();
    UninitAudioInput();
    CloseTransport();

    printf("[GB28181][Listen] stop session\n");
}

int GB28181ListenBridge::OpenTransport()
{
    memset(&m_remote_addr, 0, sizeof(m_remote_addr));
    m_remote_addr.sin_family = AF_INET;
    m_remote_addr.sin_port = htons(static_cast<uint16_t>(m_param.target_port));

    if (inet_pton(AF_INET, m_param.target_ip.c_str(), &m_remote_addr.sin_addr) != 1) {
        printf("[GB28181][Listen] invalid target_ip=%s\n", m_param.target_ip.c_str());
        return -10;
    }

    const int sockType = (m_param.transport == "tcp") ? SOCK_STREAM : SOCK_DGRAM;
    m_sock = socket(AF_INET, sockType, 0);
    if (m_sock < 0) {
        printf("[GB28181][Listen] create socket failed errno=%d\n", errno);
        return -11;
    }

    const int sendTimeoutMs = 50;
    struct timeval sndTimeout;
    sndTimeout.tv_sec = sendTimeoutMs / 1000;
    sndTimeout.tv_usec = (sendTimeoutMs % 1000) * 1000;
    if (setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, &sndTimeout, sizeof(sndTimeout)) != 0) {
        printf("[GB28181][Listen] set SO_SNDTIMEO failed errno=%d\n", errno);
    }

    if (m_param.transport == "tcp") {
        if (connect(m_sock, (struct sockaddr*)&m_remote_addr, sizeof(m_remote_addr)) != 0) {
            printf("[GB28181][Listen] tcp connect failed errno=%d\n", errno);
            CloseTransport();
            return -12;
        }
    }

    return 0;
}
void GB28181ListenBridge::CloseTransport()
{
    if (m_sock >= 0) {
        close(m_sock);
        m_sock = -1;
    }
}

int GB28181ListenBridge::InitAudioInput()
{
    const int retCreate = AudioInCreate(m_capture_channel, 100, 100);
    if (retCreate != 0) {
        printf("[GB28181][Listen] AudioInCreate failed ret=%d ch=%d\n", retCreate, m_capture_channel);
        return -20;
    }

    m_capture_inited = true;

    AUDIOIN_FORMAT format;
    memset(&format, 0, sizeof(format));
    format.BitRate = 64;
    format.SampleRate = m_param.sample_rate;
    format.SampleBit = 16;
    format.EncodeType = ResolveAudioEncodeType(m_param.codec);

    const int retSet = AudioInSetFormat(m_capture_channel, &format);
    if (retSet != 0) {
        printf("[GB28181][Listen] AudioInSetFormat failed ret=%d codec=%s\n", retSet, m_param.codec.c_str());
        UninitAudioInput();
        return -21;
    }

    const int retStart = AudioInStart(m_capture_channel);
    if (retStart != 0) {
        printf("[GB28181][Listen] AudioInStart failed ret=%d ch=%d\n", retStart, m_capture_channel);
        UninitAudioInput();
        return -22;
    }

    return 0;
}

void GB28181ListenBridge::UninitAudioInput()
{
    if (!m_capture_inited) {
        return;
    }

    AudioInStop(m_capture_channel);
    AudioInDestroy(m_capture_channel);
    m_capture_inited = false;
}

int GB28181ListenBridge::StartCaptureThread()
{
    if (m_capture_thread_started) {
        return 0;
    }

    if (g_listen_bridge_instance != NULL && g_listen_bridge_instance != this) {
        printf("[GB28181][Listen] dmc callback already occupied by another instance\n");
        return -31;
    }

    m_capture_loop = true;
    g_listen_bridge_instance = this;

    const int ret = CaptureSetStreamCallBack((char*)kListenDmcModuleName,
                                             DMC_MEDIA_TYPE_AUDIO,
                                             DmcAudioCallback);
    if (ret != 0) {
        g_listen_bridge_instance = NULL;
        m_capture_loop = false;
        printf("[GB28181][Listen] subscribe dmc audio callback failed ret=%d\n", ret);
        return -30;
    }

    m_capture_thread_started = true;
    return 0;
}

void GB28181ListenBridge::StopCaptureThread()
{
    if (!m_capture_thread_started) {
        return;
    }

    m_capture_loop = false;
    CaptureSetStreamCallBack((char*)kListenDmcModuleName, DMC_MEDIA_TYPE_AUDIO, NULL);
    if (g_listen_bridge_instance == this) {
        g_listen_bridge_instance = NULL;
    }

    m_capture_thread_started = false;
}

void* GB28181ListenBridge::CaptureThreadEntry(void* arg)
{
    (void)arg;
    return NULL;
}

int GB28181ListenBridge::RunCaptureLoop()
{
    while (m_capture_loop && m_running) {
        usleep(100000);
    }

    return 0;
}

int GB28181ListenBridge::DmcAudioCallback(int media_chn,
                                          int media_type,
                                          int media_subtype,
                                          unsigned long long frame_pts,
                                          unsigned char* frame_data,
                                          int frame_len,
                                          int frame_end_flag)
{
    if (g_listen_bridge_instance == NULL) {
        return 0;
    }

    return g_listen_bridge_instance->HandleDmcAudioFrame(media_chn,
                                                          media_type,
                                                          media_subtype,
                                                          frame_pts,
                                                          frame_data,
                                                          frame_len,
                                                          frame_end_flag);
}

int GB28181ListenBridge::HandleDmcAudioFrame(int media_chn,
                                             int media_type,
                                             int media_subtype,
                                             unsigned long long frame_pts,
                                             unsigned char* frame_data,
                                             int frame_len,
                                             int frame_end_flag)
{
    (void)frame_end_flag;

    if (!m_running || !m_capture_loop) {
        return 0;
    }

    if (media_type != DMC_MEDIA_TYPE_AUDIO || frame_data == NULL || frame_len <= 0) {
        return 0;
    }

    const std::string codec = NormalizeCodec(m_param.codec);
    const uint64_t timestamp = (frame_pts > 0) ? static_cast<uint64_t>(frame_pts) : GetNowMs();

    if (codec == "pcm") {
        if (media_chn != DMC_MEDIA_AUDIO_FRIST_STREAM || media_subtype != DMC_MEDIA_SUBTYPE_PCM) {
            return 0;
        }

        return PushAudioFrame(frame_data, static_cast<size_t>(frame_len), timestamp);
    }

    if (codec == "g711u") {
        if (media_subtype == DMC_MEDIA_SUBTYPE_ULAW) {
            return PushAudioFrame(frame_data, static_cast<size_t>(frame_len), timestamp);
        }

        if (media_subtype == DMC_MEDIA_SUBTYPE_PCM) {
            std::vector<uint8_t> encoded(static_cast<size_t>(frame_len) + 16);
            const int outLen = DG_encode_g711u((char*)frame_data, (char*)&encoded[0], frame_len);
            if (outLen > 0) {
                return PushAudioFrame(&encoded[0], static_cast<size_t>(outLen), timestamp);
            }
        }

        return 0;
    }

    if (codec == "g711a") {
        if (media_subtype == DMC_MEDIA_SUBTYPE_ALAW) {
            return PushAudioFrame(frame_data, static_cast<size_t>(frame_len), timestamp);
        }

        if (media_subtype == DMC_MEDIA_SUBTYPE_PCM) {
            std::vector<uint8_t> encoded(static_cast<size_t>(frame_len) + 16);
            const int outLen = DG_encode_g711a((char*)frame_data, (char*)&encoded[0], frame_len);
            if (outLen > 0) {
                return PushAudioFrame(&encoded[0], static_cast<size_t>(outLen), timestamp);
            }
            return 0;
        }

        if (media_subtype == DMC_MEDIA_SUBTYPE_ULAW) {
            std::vector<uint8_t> pcm(static_cast<size_t>(frame_len) * 2 + 16);
            const int pcmLen = DG_decode_g711u((char*)frame_data, (char*)&pcm[0], frame_len);
            if (pcmLen <= 0) {
                return 0;
            }

            std::vector<uint8_t> alaw(static_cast<size_t>(pcmLen) + 16);
            const int alawLen = DG_encode_g711a((char*)&pcm[0], (char*)&alaw[0], pcmLen);
            if (alawLen > 0) {
                return PushAudioFrame(&alaw[0], static_cast<size_t>(alawLen), timestamp);
            }
        }

        return 0;
    }

    return 0;
}

int GB28181ListenBridge::SendPacketBuffer(const uint8_t* packet, size_t size)
{
    if (m_sock < 0 || packet == NULL || size == 0) {
        return -1;
    }

    if (m_param.transport == "udp") {
        const int n = sendto(m_sock,
                             packet,
                             size,
                             0,
                             (struct sockaddr*)&m_remote_addr,
                             sizeof(m_remote_addr));
        if (n != static_cast<int>(size)) {
            return -2;
        }
        return 0;
    }

    uint8_t prefix[2] = {0};
    prefix[0] = static_cast<uint8_t>((size >> 8) & 0xFF);
    prefix[1] = static_cast<uint8_t>(size & 0xFF);

    if (SendAll(m_sock, prefix, sizeof(prefix)) != 0) {
        return -3;
    }

    if (SendAll(m_sock, packet, size) != 0) {
        return -4;
    }

    return 0;
}

int GB28181ListenBridge::SendRtpPacket(const uint8_t* payload, size_t size, uint32_t timestamp, bool marker)
{
    if (payload == NULL || size == 0) {
        return -1;
    }

    const int payloadType = ResolvePayloadType(m_param.codec);

    std::vector<uint8_t> packet;
    packet.resize(12 + size);

    packet[0] = 0x80;
    packet[1] = static_cast<uint8_t>(payloadType & 0x7F);
    if (marker) {
        packet[1] |= 0x80;
    }

    packet[2] = static_cast<uint8_t>((m_seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(m_seq & 0xFF);

    packet[4] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
    packet[7] = static_cast<uint8_t>(timestamp & 0xFF);

    packet[8] = static_cast<uint8_t>((m_ssrc >> 24) & 0xFF);
    packet[9] = static_cast<uint8_t>((m_ssrc >> 16) & 0xFF);
    packet[10] = static_cast<uint8_t>((m_ssrc >> 8) & 0xFF);
    packet[11] = static_cast<uint8_t>(m_ssrc & 0xFF);

    memcpy(&packet[12], payload, size);

    const int ret = SendPacketBuffer(&packet[0], packet.size());
    if (ret != 0) {
        printf("[GB28181][Listen] send rtp failed ret=%d errno=%d\n", ret, errno);
        return -2;
    }

    ++m_seq;
    ++m_sent_packets;
    m_sent_bytes += packet.size();

    const uint32_t now = GetNowSec();
    if (now != m_last_log_sec) {
        m_last_log_sec = now;
        printf("[GB28181][Listen] tx packets=%u bytes=%llu target=%s:%d transport=%s codec=%s\n",
               m_sent_packets,
               m_sent_bytes,
               m_param.target_ip.c_str(),
               m_param.target_port,
               m_param.transport.c_str(),
               m_param.codec.c_str());
    }

    return 0;
}

int GB28181ListenBridge::PushAudioFrame(const uint8_t* data, size_t size, uint64_t timestamp)
{
    if (!m_running) {
        return -1;
    }

    if (data == NULL || size == 0) {
        return -2;
    }

    if (m_timestamp == 0) {
        const uint64_t baseMs = (timestamp > 0) ? timestamp : GetNowMs();
        m_timestamp = static_cast<uint32_t>((baseMs * 8ULL) & 0xFFFFFFFFu);
    }

    const uint32_t currentTs = m_timestamp;
    const int ret = SendRtpPacket(data, size, currentTs, true);
    if (ret != 0) {
        return -3;
    }

    const uint32_t samples = ResolveSampleCount(m_param.codec, size);
    m_timestamp += samples;
    return 0;
}

bool GB28181ListenBridge::IsRunning() const
{
    return m_running;
}

}


