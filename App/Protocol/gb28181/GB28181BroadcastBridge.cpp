#include "GB28181BroadcastBridge.h"
#include "ProtocolLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <map>
#include <sstream>

#include "Media/AudioManager.h"
#include "Media/AudioPrompt.h"
#include "GB28181Defs.h"
#include "GB28181ProtocolConstants.h"
extern "C"
{
#include "PAL/Audio_coder.h"
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

static uint64_t GetNowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static std::string TrimLineCrlf(const std::string& line)
{
    if (!line.empty() && line[line.size() - 1] == '\r') {
        return line.substr(0, line.size() - 1);
    }

    return line;
}

static bool ParseAudioMediaLine(const std::string& sdp,
                                int& outPort,
                                std::string& outProto,
                                std::vector<int>& outPts)
{
    outPort = 0;
    outProto.clear();
    outPts.clear();

    std::istringstream ss(sdp);
    std::string line;
    while (std::getline(ss, line)) {
        line = TrimLineCrlf(line);
        if (line.compare(0,
                         sizeof(protocol::gb28181::kSdpMediaAudioPrefix) - 1,
                         protocol::gb28181::kSdpMediaAudioPrefix) != 0) {
            continue;
        }

        std::istringstream ls(line.substr(sizeof(protocol::gb28181::kSdpMediaAudioPrefix) - 1));
        if (!(ls >> outPort >> outProto)) {
            return false;
        }

        int pt = 0;
        while (ls >> pt) {
            outPts.push_back(pt);
        }

        return !outPts.empty();
    }

    return false;
}

static void ParseRtpMap(const std::string& sdp, std::map<int, std::string>& outPtCodec)
{
    outPtCodec.clear();

    std::istringstream ss(sdp);
    std::string line;
    while (std::getline(ss, line)) {
        line = TrimLineCrlf(line);
        if (line.compare(0, 9, "a=rtpmap:") != 0) {
            continue;
        }

        const size_t split = line.find(' ');
        if (split == std::string::npos || split <= 9) {
            continue;
        }

        const int pt = atoi(line.substr(9, split - 9).c_str());
        if (pt < 0 || pt > 127) {
            continue;
        }

        std::string codec = line.substr(split + 1);
        const size_t slash = codec.find('/');
        if (slash != std::string::npos) {
            codec = codec.substr(0, slash);
        }

        codec = ToLowerCopy(codec);
        outPtCodec[pt] = codec;
    }
}

static std::string NormalizeCodecByPt(int pt, const std::map<int, std::string>& ptCodec)
{
    std::map<int, std::string>::const_iterator it = ptCodec.find(pt);
    std::string codec = (it != ptCodec.end()) ? it->second : "";

    if (codec.empty()) {
        if (pt == protocol::gb28181::kAudioPayloadTypePcma) {
            codec = protocol::gb28181::kAudioMimePcma;
        } else if (pt == protocol::gb28181::kAudioPayloadTypePcmu) {
            codec = protocol::gb28181::kAudioMimePcmu;
        }
    }

    return protocol::gb28181::NormalizeGbAudioCodec(codec);
}

static std::string NormalizeCodecText(const std::string& codecIn)
{
    return protocol::gb28181::NormalizeGbAudioCodec(codecIn);
}

static bool SelectSupportedCodec(const std::vector<int>& pts,
                                 const std::map<int, std::string>& ptCodec,
                                 std::string& outCodec,
                                 int& outPt)
{
    outCodec.clear();
    outPt = -1;

    for (size_t i = 0; i < pts.size(); ++i) {
        const std::string codec = NormalizeCodecByPt(pts[i], ptCodec);
        if (!codec.empty()) {
            outCodec = codec;
            outPt = pts[i];
            return true;
        }
    }

    return false;
}

static std::string RtpMapNameByCodec(const std::string& codec)
{
    return protocol::gb28181::ResolveGbAudioMimeName(codec);
}

static std::string ParseAudioSetupAttr(const std::string& sdp)
{
    bool inAudioSection = false;

    std::istringstream ss(sdp);
    std::string line;
    while (std::getline(ss, line)) {
        line = TrimLineCrlf(line);
        if (line.compare(0, 2, "m=") == 0) {
            inAudioSection = (line.compare(0,
                                           sizeof(protocol::gb28181::kSdpMediaAudioPrefix) - 1,
                                           protocol::gb28181::kSdpMediaAudioPrefix) == 0);
            continue;
        }

        if (!inAudioSection) {
            continue;
        }

        if (line.compare(0,
                         sizeof(protocol::gb28181::kSdpAttributeSetupPrefix) - 1,
                         protocol::gb28181::kSdpAttributeSetupPrefix) == 0) {
            return ToLowerCopy(line.substr(sizeof(protocol::gb28181::kSdpAttributeSetupPrefix) - 1));
        }
    }

    return "";
}

static int ResolveTransportTypeBySdp(const std::string& proto, const std::string& setup)
{
    if (ToLowerCopy(proto).find("tcp") == std::string::npos) {
        return kRtpOverUdp;
    }

    if (setup == "active") {
        return kRtpOverTcpActive;
    }

    return kRtpOverTcpPassive;
}

static bool IsTcpTransportType(int transportType)
{
    return transportType == kRtpOverTcp ||
           transportType == kRtpOverTcpActive ||
           transportType == kRtpOverTcpPassive;
}

static const char* TransportTypeName(int transportType)
{
    switch (transportType) {
    case kRtpOverTcp:
        return "tcp";
    case kRtpOverTcpActive:
        return "tcp-active";
    case kRtpOverTcpPassive:
        return "tcp-passive";
    case kRtpOverUdp:
    default:
        return "udp";
    }
}

static void SetSocketRecvTimeout(int sock, int timeoutMs)
{
    if (sock < 0 || timeoutMs <= 0) {
        return;
    }

    struct timeval rcvTimeout;
    rcvTimeout.tv_sec = timeoutMs / 1000;
    rcvTimeout.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout)) != 0) {
        printf("[GB28181][Broadcast] set SO_RCVTIMEO failed errno=%d sock=%d\n", errno, sock);
    }
}

} 

namespace protocol
{

GB28181BroadcastBridge::GB28181BroadcastBridge()
    : m_running(false),
      m_audio_opened(false),
      m_recv_sock(-1),
      m_listen_sock(-1),
      m_recv_thread_started(false),
      m_recv_loop(false),
      m_negotiated_payload_type(-1),
      m_transport_type(kRtpOverUdp),
      m_remote_port(0),
      m_pcm_callback(NULL),
      m_pcm_callback_user(NULL)
{
}

GB28181BroadcastBridge::~GB28181BroadcastBridge()
{
    StopSession();
}

int GB28181BroadcastBridge::StartSession(const GbBroadcastParam& param)
{
    if (param.codec.empty()) {
        printf("[GB28181][Broadcast] codec empty\n");
        return -1;
    }

    if (param.input_mode != "file" && param.input_mode != "stream") {
        printf("[GB28181][Broadcast] invalid input_mode: %s\n", param.input_mode.c_str());
        return -2;
    }

    StopSession();

    m_param = param;
    m_running = true;
    m_negotiated_payload_type = -1;
    m_transport_type = kRtpOverUdp;
    m_remote_ip.clear();
    m_remote_port = 0;
    m_tcp_recv_buffer.clear();

    if (m_param.input_mode == "stream") {
        if (m_param.recv_port <= 0) {
            printf("[GB28181][Broadcast] invalid recv_port=%d for stream mode\n", m_param.recv_port);
            m_running = false;
            return -3;
        }

        int ret = SetupRecvSocket();
        if (ret != 0) {
            m_running = false;
            return ret;
        }

        ret = StartRecvThread();
        if (ret != 0) {
            CloseRecvSocket();
            m_running = false;
            return ret;
        }
    }

    printf("[GB28181][Broadcast] start session mode=%s codec=%s recv_port=%d transport=%s\n",
           m_param.input_mode.c_str(),
           m_param.codec.c_str(),
           m_param.recv_port,
           TransportTypeName(m_transport_type));
    return 0;
}

void GB28181BroadcastBridge::StopSession()
{
    if (!m_running && !m_recv_thread_started && !m_audio_opened) {
        return;
    }

    m_running = false;
    StopRecvThread();
    CloseRecvSocket();
    m_transport_type = kRtpOverUdp;
    m_negotiated_payload_type = -1;
    m_remote_ip.clear();
    m_remote_port = 0;
    m_tcp_recv_buffer.clear();

    if (m_audio_opened) {
        IAudioManager::instance()->StopAudioOut(IAudioManager::AUDIO_TALK_TYPE, true);
        m_audio_opened = false;
    }

    printf("[GB28181][Broadcast] stop session\n");
}

int GB28181BroadcastBridge::EnsureAudioOutputStarted()
{
    if (m_audio_opened) {
        return 0;
    }

    if (!IAudioManager::instance()->StartAudioOut(IAudioManager::AUDIO_TALK_TYPE)) {
        printf("[GB28181][Broadcast] start audio output failed\n");
        return -1;
    }

    m_audio_opened = true;
    return 0;
}

int GB28181BroadcastBridge::DecodeToPcm16(const uint8_t* data, size_t size, std::vector<char>& pcmOut)
{
    if (data == NULL || size == 0) {
        return -1;
    }

    const std::string codec = ToLowerCopy(m_param.codec);
    if (codec == "pcm" || codec == "pcm16" || codec == "pcm_s16le") {
        pcmOut.resize(size);
        memcpy(&pcmOut[0], data, size);
        return static_cast<int>(size);
    }

    if (codec == "g711u" || codec == "pcmu") {
        pcmOut.resize(size * 2 + 8);
        const int outLen = DG_decode_g711u((char*)data, &pcmOut[0], static_cast<int>(size));
        if (outLen <= 0) {
            return -2;
        }

        pcmOut.resize(static_cast<size_t>(outLen));
        return outLen;
    }

    if (codec == "g711a" || codec == "pcma") {
        pcmOut.resize(size * 2 + 8);
        const int outLen = DG_decode_g711a((char*)data, &pcmOut[0], static_cast<int>(size));
        if (outLen <= 0) {
            return -3;
        }

        pcmOut.resize(static_cast<size_t>(outLen));
        return outLen;
    }

    return -4;
}

int GB28181BroadcastBridge::SetupRecvSocket()
{
    const bool isTcp = IsTcpTransportType(m_transport_type);
    const int sockType = isTcp ? SOCK_STREAM : SOCK_DGRAM;
    const int sockfd = socket(AF_INET, sockType, 0);
    if (sockfd < 0) {
        printf("[GB28181][Broadcast] create %s socket failed errno=%d\n",
               TransportTypeName(m_transport_type),
               errno);
        return -11;
    }

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    SetSocketRecvTimeout(sockfd, 200);

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<uint16_t>(m_param.recv_port));

    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) != 0) {
        printf("[GB28181][Broadcast] bind %s port=%d failed errno=%d\n",
               TransportTypeName(m_transport_type),
               m_param.recv_port,
               errno);
        close(sockfd);
        return -12;
    }

    if (!isTcp) {
        m_recv_sock = sockfd;
        return 0;
    }

    if (m_transport_type == kRtpOverTcpActive) {
        if (m_remote_ip.empty() || m_remote_port <= 0) {
            printf("[GB28181][Broadcast] tcp active missing remote endpoint=%s:%d\n",
                   m_remote_ip.c_str(),
                   m_remote_port);
            close(sockfd);
            return -13;
        }

        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(static_cast<uint16_t>(m_remote_port));
        if (inet_pton(AF_INET, m_remote_ip.c_str(), &remote_addr.sin_addr) != 1) {
            printf("[GB28181][Broadcast] invalid tcp active remote ip=%s\n", m_remote_ip.c_str());
            close(sockfd);
            return -14;
        }

        if (connect(sockfd, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) != 0) {
            printf("[GB28181][Broadcast] tcp active connect failed errno=%d remote=%s:%d\n",
                   errno,
                   m_remote_ip.c_str(),
                   m_remote_port);
            close(sockfd);
            return -15;
        }

        m_recv_sock = sockfd;
        printf("[GB28181][Broadcast] tcp active connected remote=%s:%d\n",
               m_remote_ip.c_str(),
               m_remote_port);
        return 0;
    }

    if (listen(sockfd, 1) != 0) {
        printf("[GB28181][Broadcast] tcp passive listen failed errno=%d port=%d\n", errno, m_param.recv_port);
        close(sockfd);
        return -16;
    }

    m_listen_sock = sockfd;
    printf("[GB28181][Broadcast] tcp passive listen ready port=%d\n", m_param.recv_port);
    return 0;
}

void GB28181BroadcastBridge::CloseAcceptedSocket()
{
    if (m_recv_sock >= 0) {
        close(m_recv_sock);
        m_recv_sock = -1;
    }
    m_tcp_recv_buffer.clear();
}

void GB28181BroadcastBridge::CloseRecvSocket()
{
    CloseAcceptedSocket();
    if (m_listen_sock >= 0) {
        close(m_listen_sock);
        m_listen_sock = -1;
    }
}

int GB28181BroadcastBridge::StartRecvThread()
{
    if (m_recv_thread_started) {
        return 0;
    }

    m_recv_loop = true;
    const int ret = pthread_create(&m_recv_thread, NULL, RecvThreadEntry, this);
    if (ret != 0) {
        printf("[GB28181][Broadcast] create recv thread failed ret=%d\n", ret);
        m_recv_loop = false;
        return -13;
    }

    m_recv_thread_started = true;
    return 0;
}

void GB28181BroadcastBridge::StopRecvThread()
{
    if (!m_recv_thread_started) {
        return;
    }

    m_recv_loop = false;
    if (m_recv_sock >= 0) {
        shutdown(m_recv_sock, SHUT_RDWR);
    }
    if (m_listen_sock >= 0) {
        shutdown(m_listen_sock, SHUT_RDWR);
    }

    pthread_join(m_recv_thread, NULL);
    m_recv_thread_started = false;
}

void* GB28181BroadcastBridge::RecvThreadEntry(void* arg)
{
    GB28181BroadcastBridge* self = (GB28181BroadcastBridge*)arg;
    if (self != NULL) {
        self->RunRecvLoop();
    }

    return NULL;
}

int GB28181BroadcastBridge::RunRecvLoop()
{
    static const size_t kMaxPacket = 2048;
    static const size_t kMaxTcpFrame = 8192;
    uint8_t packet[kMaxPacket];

    while (m_recv_loop && m_running) {
        if (m_transport_type == kRtpOverTcp || m_transport_type == kRtpOverTcpPassive) {
            if (m_recv_sock < 0) {
                struct sockaddr_in remote_addr;
                socklen_t addr_len = sizeof(remote_addr);
                const int clientSock = accept(m_listen_sock, (struct sockaddr*)&remote_addr, &addr_len);
                if (clientSock < 0) {
                    if (!m_recv_loop || !m_running) {
                        break;
                    }

                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }

                    usleep(20000);
                    continue;
                }

                SetSocketRecvTimeout(clientSock, 200);

                char sourceIp[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &remote_addr.sin_addr, sourceIp, sizeof(sourceIp));
                const int sourcePort = ntohs(remote_addr.sin_port);
                if (!m_remote_ip.empty() && m_remote_ip != sourceIp) {
                    printf("[GB28181][Broadcast] tcp passive reject peer remote=%s:%d expect_ip=%s\n",
                           sourceIp,
                           sourcePort,
                           m_remote_ip.c_str());
                    close(clientSock);
                    continue;
                }

                CloseAcceptedSocket();
                m_recv_sock = clientSock;
                printf("[GB28181][Broadcast] tcp passive accept remote=%s:%d local_port=%d\n",
                       sourceIp,
                       sourcePort,
                       m_param.recv_port);
                continue;
            }

            const int n = recv(m_recv_sock, packet, sizeof(packet), 0);
            if (n <= 0) {
                if (!m_recv_loop || !m_running) {
                    break;
                }

                if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    printf("[GB28181][Broadcast] tcp passive peer disconnected errno=%d\n", errno);
                    CloseAcceptedSocket();
                    continue;
                }

                continue;
            }

            m_tcp_recv_buffer.insert(m_tcp_recv_buffer.end(), packet, packet + n);
            while (m_tcp_recv_buffer.size() >= 2) {
                const size_t frameSize = ((size_t)m_tcp_recv_buffer[0] << 8) | (size_t)m_tcp_recv_buffer[1];
                if (frameSize == 0 || frameSize > kMaxTcpFrame) {
                    printf("[GB28181][Broadcast] invalid tcp frame size=%lu\n", (unsigned long)frameSize);
                    CloseAcceptedSocket();
                    break;
                }

                if (m_tcp_recv_buffer.size() < frameSize + 2) {
                    break;
                }

                const uint8_t* frameData = &m_tcp_recv_buffer[2];
                ParsedPacket parsed;
                if (TryParseRtpPacket(frameData, frameSize, parsed)) {
                    if (m_negotiated_payload_type < 0 || parsed.payload_type == (uint8_t)m_negotiated_payload_type) {
                        OnAudioFrame(parsed.payload, parsed.payload_size, parsed.timestamp);
                    }
                } else {
                    OnAudioFrame(frameData, frameSize, GetNowMs());
                }

                const size_t consumed = frameSize + 2;
                m_tcp_recv_buffer.erase(m_tcp_recv_buffer.begin(), m_tcp_recv_buffer.begin() + consumed);
            }

            continue;
        }

        if (m_transport_type == kRtpOverTcpActive) {
            if (m_recv_sock < 0) {
                if (SetupRecvSocket() != 0) {
                    usleep(200000);
                }
                continue;
            }

            const int n = recv(m_recv_sock, packet, sizeof(packet), 0);
            if (n <= 0) {
                if (!m_recv_loop || !m_running) {
                    break;
                }

                if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    printf("[GB28181][Broadcast] tcp active disconnected errno=%d remote=%s:%d, wait next invite\n",
                           errno,
                           m_remote_ip.c_str(),
                           m_remote_port);
                    CloseAcceptedSocket();
                    break;
                }

                continue;
            }

            m_tcp_recv_buffer.insert(m_tcp_recv_buffer.end(), packet, packet + n);
            while (m_tcp_recv_buffer.size() >= 2) {
                const size_t frameSize = ((size_t)m_tcp_recv_buffer[0] << 8) | (size_t)m_tcp_recv_buffer[1];
                if (frameSize == 0 || frameSize > kMaxTcpFrame) {
                    printf("[GB28181][Broadcast] invalid tcp active frame size=%lu, wait next invite\n",
                           (unsigned long)frameSize);
                    CloseAcceptedSocket();
                    break;
                }

                if (m_tcp_recv_buffer.size() < frameSize + 2) {
                    break;
                }

                const uint8_t* frameData = &m_tcp_recv_buffer[2];
                ParsedPacket parsed;
                if (TryParseRtpPacket(frameData, frameSize, parsed)) {
                    if (m_negotiated_payload_type < 0 || parsed.payload_type == (uint8_t)m_negotiated_payload_type) {
                        OnAudioFrame(parsed.payload, parsed.payload_size, parsed.timestamp);
                    }
                } else {
                    OnAudioFrame(frameData, frameSize, GetNowMs());
                }

                const size_t consumed = frameSize + 2;
                m_tcp_recv_buffer.erase(m_tcp_recv_buffer.begin(), m_tcp_recv_buffer.begin() + consumed);
            }

            continue;
        }

        struct sockaddr_in remote_addr;
        socklen_t addr_len = sizeof(remote_addr);
        const int n = recvfrom(m_recv_sock,
                               packet,
                               sizeof(packet),
                               0,
                               (struct sockaddr*)&remote_addr,
                               &addr_len);
        if (n <= 0) {
            if (!m_recv_loop || !m_running) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            usleep(20000);
            continue;
        }

        char sourceIp[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &remote_addr.sin_addr, sourceIp, sizeof(sourceIp));
        const int sourcePort = ntohs(remote_addr.sin_port);

        if (!m_remote_ip.empty() && m_remote_ip != sourceIp) {
            continue;
        }

        if (m_remote_port > 0 && m_remote_port != sourcePort) {
            continue;
        }

        ParsedPacket parsed;
        if (TryParseRtpPacket(packet, static_cast<size_t>(n), parsed)) {
            if (m_negotiated_payload_type >= 0 && parsed.payload_type != (uint8_t)m_negotiated_payload_type) {
                continue;
            }

            OnAudioFrame(parsed.payload, parsed.payload_size, parsed.timestamp);
        } else {
            OnAudioFrame(packet, static_cast<size_t>(n), GetNowMs());
        }
    }

    return 0;
}

bool GB28181BroadcastBridge::TryParseRtpPacket(const uint8_t* data, size_t size, ParsedPacket& out) const
{
    if (data == NULL || size < 12) {
        return false;
    }

    const uint8_t vpxcc = data[0];
    const uint8_t version = (uint8_t)((vpxcc >> 6) & 0x03);
    if (version != 2) {
        return false;
    }

    const bool has_padding = ((vpxcc & 0x20) != 0);
    const bool has_extension = ((vpxcc & 0x10) != 0);
    const size_t csrc_count = (size_t)(vpxcc & 0x0F);

    size_t offset = 12 + csrc_count * 4;
    if (offset > size) {
        return false;
    }

    if (has_extension) {
        if (offset + 4 > size) {
            return false;
        }

        const uint16_t ext_len_words = (uint16_t)((data[offset + 2] << 8) | data[offset + 3]);
        offset += 4 + (size_t)ext_len_words * 4;
        if (offset > size) {
            return false;
        }
    }

    size_t payload_size = size - offset;
    if (has_padding) {
        const uint8_t pad_len = data[size - 1];
        if (pad_len == 0 || pad_len > payload_size) {
            return false;
        }

        payload_size -= pad_len;
    }

    if (payload_size == 0) {
        return false;
    }

    uint32_t rtp_ts = 0;
    rtp_ts |= ((uint32_t)data[4] << 24);
    rtp_ts |= ((uint32_t)data[5] << 16);
    rtp_ts |= ((uint32_t)data[6] << 8);
    rtp_ts |= (uint32_t)data[7];

    out.payload = data + offset;
    out.payload_size = payload_size;
    out.payload_type = (uint8_t)(data[1] & 0x7F);
    out.timestamp = (uint64_t)rtp_ts;
    return true;
}

int GB28181BroadcastBridge::OnAudioFile(const std::string& filePath)
{
    if (!m_running) {
        return -1;
    }

    if (filePath.empty()) {
        return -2;
    }

    CAudioPrompt::AudioFileParm audioFile;
    audioFile.strFileName = filePath;
    audioFile.type = 0;
    audioFile.clean = false;

    const int ret = g_AudioPrompt.aoPlay(audioFile);
    if (ret != 0) {
        printf("[GB28181][Broadcast] play file failed ret=%d path=%s\n", ret, filePath.c_str());
        return -3;
    }

    printf("[GB28181][Broadcast] receive file and dispatch to AudioPrompt: %s\n", filePath.c_str());
    return 0;
}

int GB28181BroadcastBridge::OnAudioFrame(const uint8_t* data, size_t size, uint64_t timestamp)
{
    if (!m_running) {
        return -1;
    }

    if (data == NULL || size == 0) {
        return -2;
    }

    int ret = EnsureAudioOutputStarted();
    if (ret != 0) {
        return -3;
    }

    std::vector<char> pcm;
    ret = DecodeToPcm16(data, size, pcm);
    if (ret < 0) {
        printf("[GB28181][Broadcast] decode frame failed codec=%s ret=%d size=%lu\n",
               m_param.codec.c_str(),
               ret,
               (unsigned long)size);
        return -4;
    }

    if (m_pcm_callback != NULL && !pcm.empty()) {
        m_pcm_callback((const uint8_t*)&pcm[0], pcm.size(), timestamp, m_pcm_callback_user);
    }

    if (!IAudioManager::instance()->PlayVoice((unsigned char*)&pcm[0],
                                              static_cast<int>(pcm.size()),
                                              IAudioManager::AUDIO_TALK_TYPE)) {
        printf("[GB28181][Broadcast] play voice failed pcm_size=%lu\n", (unsigned long)pcm.size());
        return -5;
    }

    return 0;
}

int GB28181BroadcastBridge::ApplySdpOffer(const std::string& sdp, const std::string& remoteIpHint)
{
    std::vector<int> pts;
    int mediaPort = 0;
    std::string mediaProto;
    if (!ParseAudioMediaLine(sdp, mediaPort, mediaProto, pts)) {
        printf("[GB28181][Broadcast] sdp missing m=audio\n");
        return -21;
    }

    std::map<int, std::string> ptCodec;
    ParseRtpMap(sdp, ptCodec);

    std::string codec;
    int pt = -1;
    if (!SelectSupportedCodec(pts, ptCodec, codec, pt)) {
        printf("[GB28181][Broadcast] no supported audio codec in sdp\n");
        return -22;
    }

    const int transportType = ResolveTransportTypeBySdp(mediaProto, ParseAudioSetupAttr(sdp));
    return ApplyTransportHint(remoteIpHint, mediaPort, pt, codec, transportType);
}

int GB28181BroadcastBridge::ApplyTransportHint(const std::string& remoteIp,
                                               int remotePort,
                                               int payloadType,
                                               const std::string& codec,
                                               int transportType)
{
    if (remotePort <= 0) {
        printf("[GB28181][Broadcast] invalid remote audio port=%d\n", remotePort);
        return -23;
    }

    if (payloadType < 0 || payloadType > 127) {
        printf("[GB28181][Broadcast] invalid payload type=%d\n", payloadType);
        return -24;
    }

    const std::string normalized = NormalizeCodecText(codec);
    if (normalized.empty()) {
        printf("[GB28181][Broadcast] unsupported codec from offer: %s\n", codec.c_str());
        return -25;
    }

    int normalizedTransport = transportType;
    if (normalizedTransport == kRtpOverTcp) {
        normalizedTransport = kRtpOverTcpPassive;
    }
    if (normalizedTransport != kRtpOverUdp &&
        normalizedTransport != kRtpOverTcpActive &&
        normalizedTransport != kRtpOverTcpPassive) {
        printf("[GB28181][Broadcast] unsupported transport type=%d\n", transportType);
        return -26;
    }

    const GbBroadcastParam oldParam = m_param;
    const int oldPayloadType = m_negotiated_payload_type;
    const int oldTransportType = m_transport_type;
    const std::string oldRemoteIp = m_remote_ip;
    const int oldRemotePort = m_remote_port;

    m_param.codec = normalized;
    m_negotiated_payload_type = payloadType;
    m_transport_type = normalizedTransport;
    m_remote_port = remotePort;
    m_remote_ip = remoteIp;

    const bool needRestart = m_running && m_param.input_mode == "stream";
    if (needRestart) {
        StopRecvThread();
        CloseRecvSocket();

        int ret = SetupRecvSocket();
        if (ret == 0) {
            ret = StartRecvThread();
        }
        if (ret != 0) {
            m_param = oldParam;
            m_negotiated_payload_type = oldPayloadType;
            m_transport_type = oldTransportType;
            m_remote_ip = oldRemoteIp;
            m_remote_port = oldRemotePort;
            if (m_running && m_param.input_mode == "stream") {
                if (SetupRecvSocket() == 0) {
                    StartRecvThread();
                }
            }
            return ret;
        }
    }

    printf("[GB28181][Broadcast] apply transport transport=%s codec=%s pt=%d remote=%s:%d local_port=%d\n",
           TransportTypeName(m_transport_type),
           m_param.codec.c_str(),
           m_negotiated_payload_type,
           m_remote_ip.c_str(),
           m_remote_port,
           m_param.recv_port);
    return 0;
}

std::string GB28181BroadcastBridge::BuildLocalAnswerSdp(const std::string& localIp) const
{
    std::string ip = localIp.empty() ? "0.0.0.0" : localIp;

    int pt = m_negotiated_payload_type;
    if (pt < 0 || pt > 127) {
        pt = protocol::gb28181::ResolveGbAudioPayloadType(m_param.codec);
    }

    const std::string codecName = RtpMapNameByCodec(m_param.codec);
    const bool tcpTransport = IsTcpTransportType(m_transport_type);

    std::ostringstream out;
    out << "v=0\r\n";
    out << "o=- 0 0 IN IP4 " << ip << "\r\n";
    out << "s=GB28181-Broadcast\r\n";
    out << "c=IN IP4 " << ip << "\r\n";
    out << "t=0 0\r\n";
    out << protocol::gb28181::kSdpMediaAudioPrefix << m_param.recv_port << " "
        << (tcpTransport ? protocol::gb28181::kSdpTransportTcpRtpAvp
                         : protocol::gb28181::kSdpTransportRtpAvp)
        << " " << pt << "\r\n";
    out << "a=recvonly\r\n";
    if (tcpTransport) {
        out << protocol::gb28181::kSdpAttributeSetupPrefix
            << ((m_transport_type == kRtpOverTcpActive) ? "active" : "passive")
            << "\r\n";
        out << "a=connection:new\r\n";
    }
    out << "a=rtpmap:" << pt << " " << codecName << "/8000\r\n";
    return out.str();
}

int GB28181BroadcastBridge::GetNegotiatedTransport(std::string& remoteIp,
                                                   int& remotePort,
                                                   int& payloadType,
                                                   std::string& codec,
                                                   int& localRecvPort,
                                                   int& transportType) const
{
    if (!m_running) {
        return -1;
    }

    remoteIp = m_remote_ip;
    remotePort = m_remote_port;
    payloadType = m_negotiated_payload_type;
    codec = m_param.codec;
    localRecvPort = m_param.recv_port;
    transportType = m_transport_type;

    if (payloadType < 0 || payloadType > 127 || codec.empty() || localRecvPort <= 0) {
        return -2;
    }

    return 0;
}

void GB28181BroadcastBridge::SetPcmFrameCallback(PcmFrameCallback cb, void* userData)
{
    m_pcm_callback = cb;
    m_pcm_callback_user = userData;
}

bool GB28181BroadcastBridge::IsRunning() const
{
    return m_running;
}

}









