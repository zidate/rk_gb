#include "HttpConfigProvider.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace
{

struct HttpUrlParts
{
    std::string host;
    int port;
    std::string path;

    HttpUrlParts() : port(80), path("/") {}
};

bool FindStringField(const std::string& src, const std::string& key, std::string& out)
{
    const std::string token = "\"" + key + "\"";
    size_t pos = src.find(token);
    if (pos == std::string::npos) {
        return false;
    }

    pos = src.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return false;
    }

    pos = src.find('"', pos + 1);
    if (pos == std::string::npos) {
        return false;
    }

    const size_t end = src.find('"', pos + 1);
    if (end == std::string::npos || end <= pos + 1) {
        return false;
    }

    out = src.substr(pos + 1, end - pos - 1);
    return true;
}

bool FindIntField(const std::string& src, const std::string& key, int& out)
{
    const std::string token = "\"" + key + "\"";
    size_t pos = src.find(token);
    if (pos == std::string::npos) {
        return false;
    }

    pos = src.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return false;
    }

    ++pos;
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) {
        ++pos;
    }

    const bool negative = (pos < src.size() && src[pos] == '-');
    if (negative) {
        ++pos;
    }

    if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') {
        return false;
    }

    int value = 0;
    while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') {
        value = value * 10 + (src[pos] - '0');
        ++pos;
    }

    out = negative ? -value : value;
    return true;
}
std::string TrimCopy(const std::string& input)
{
    size_t begin = 0;
    while (begin < input.size() && (input[begin] == ' ' || input[begin] == '\t' || input[begin] == '\r' || input[begin] == '\n')) {
        ++begin;
    }

    size_t end = input.size();
    while (end > begin && (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' || input[end - 1] == '\n')) {
        --end;
    }

    return input.substr(begin, end - begin);
}

std::vector<std::string> SplitConfigList(const std::string& input)
{
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r') {
            const std::string item = TrimCopy(current);
            if (!item.empty()) {
                out.push_back(item);
            }
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    const std::string tail = TrimCopy(current);
    if (!tail.empty()) {
        out.push_back(tail);
    }
    return out;
}

std::string JoinConfigList(const std::vector<std::string>& values)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        const std::string item = TrimCopy(values[i]);
        if (item.empty()) {
            continue;
        }

        if (!out.empty()) {
            out += ",";
        }
        out += item;
    }
    return out;
}

bool ParseHttpUrl(const std::string& url, HttpUrlParts& out)
{
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    size_t hostBegin = prefix.size();
    size_t pathBegin = url.find('/', hostBegin);
    std::string hostPort = (pathBegin == std::string::npos) ? url.substr(hostBegin) : url.substr(hostBegin, pathBegin - hostBegin);
    if (hostPort.empty()) {
        return false;
    }

    out.path = (pathBegin == std::string::npos) ? "/" : url.substr(pathBegin);

    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostPort.substr(0, colon);
        const std::string portText = hostPort.substr(colon + 1);
        if (out.host.empty() || portText.empty()) {
            return false;
        }
        out.port = atoi(portText.c_str());
        if (out.port <= 0 || out.port > 65535) {
            return false;
        }
    } else {
        out.host = hostPort;
        out.port = 80;
    }

    return !out.host.empty();
}

int WaitForSocketWritable(int sockfd, int timeoutMs)
{
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

int ConnectWithTimeout(const HttpUrlParts& url, int timeoutMs)
{
    char portText[16] = {0};
    snprintf(portText, sizeof(portText), "%d", url.port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = NULL;
    if (getaddrinfo(url.host.c_str(), portText, &hints, &result) != 0 || result == NULL) {
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo* item = result; item != NULL; item = item->ai_next) {
        sockfd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        const int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        }

        int ret = connect(sockfd, item->ai_addr, item->ai_addrlen);
        if (ret != 0 && errno == EINPROGRESS) {
            ret = WaitForSocketWritable(sockfd, timeoutMs);
            if (ret == 0) {
                int soError = 0;
                socklen_t soLen = sizeof(soError);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0 || soError != 0) {
                    ret = -1;
                }
            }
        }

        if (flags >= 0) {
            fcntl(sockfd, F_SETFL, flags);
        }

        if (ret == 0) {
            struct timeval recvTimeout;
            recvTimeout.tv_sec = 3;
            recvTimeout.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
            setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &recvTimeout, sizeof(recvTimeout));
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    return sockfd;
}

int SendHttpBytes(int sockfd, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        const int ret = send(sockfd, data.data() + sent, data.size() - sent, 0);
        if (ret <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += static_cast<size_t>(ret);
    }
    return 0;
}

int ParseHttpResponseBody(const std::string& response, std::string& outBody)
{
    size_t headerEnd = response.find("\r\n\r\n");
    size_t skip = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = response.find("\n\n");
        skip = 2;
    }
    if (headerEnd == std::string::npos) {
        return -1;
    }

    size_t lineEnd = response.find("\r\n");
    if (lineEnd == std::string::npos) {
        lineEnd = response.find('\n');
    }
    if (lineEnd == std::string::npos) {
        return -1;
    }

    const std::string statusLine = response.substr(0, lineEnd);
    if (statusLine.find("200") == std::string::npos && statusLine.find("201") == std::string::npos && statusLine.find("204") == std::string::npos) {
        return -2;
    }

    outBody = response.substr(headerEnd + skip);
    return 0;
}

int HttpRequest(const std::string& method, const std::string& url, const std::string* body, std::string& outBody)
{
    outBody.clear();

    HttpUrlParts parts;
    if (!ParseHttpUrl(url, parts)) {
        return -1;
    }

    const int sockfd = ConnectWithTimeout(parts, 2000);
    if (sockfd < 0) {
        return -2;
    }

    std::string request = method + " " + parts.path + " HTTP/1.1\r\n";
    request += "Host: " + parts.host + "\r\n";
    request += "Accept: application/json\r\n";
    request += "Connection: close\r\n";
    if (body != NULL) {
        char lenText[32] = {0};
        snprintf(lenText, sizeof(lenText), "%u", (unsigned int)body->size());
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: ";
        request += lenText;
        request += "\r\n";
    }
    request += "\r\n";
    if (body != NULL) {
        request += *body;
    }

    int ret = SendHttpBytes(sockfd, request);
    if (ret != 0) {
        close(sockfd);
        return -3;
    }

    std::string response;
    char buffer[512];
    while (1) {
        const int bytes = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(sockfd);
            return -4;
        }
        response.append(buffer, bytes);
    }

    close(sockfd);
    return ParseHttpResponseBody(response, outBody);
}

}
std::string BuildConfigLogSummary(const protocol::ProtocolExternalConfig& cfg)
{
    char buffer[512] = {0};
    snprintf(buffer,
             sizeof(buffer),
             "version=%s gb=%s:%d live=%s/%s:%d gat=%s://%s:%d%s listen=%d timeout=%d queue=%s apes_post_compat=%d talk=%s/%d/%d broadcast=%s/%d listen=%s/%s:%d",
             cfg.version.c_str(),
             cfg.gb_register.server_ip.c_str(),
             cfg.gb_register.server_port,
             cfg.gb_live.transport.c_str(),
             cfg.gb_live.target_ip.c_str(),
             cfg.gb_live.target_port,
             cfg.gat_register.scheme.c_str(),
             cfg.gat_register.server_ip.c_str(),
             cfg.gat_register.server_port,
             cfg.gat_register.base_path.c_str(),
             cfg.gat_register.listen_port,
             cfg.gat_register.request_timeout_ms,
             cfg.gat_upload.queue_dir.c_str(),
             cfg.gat_upload.enable_apes_post_compat,
             cfg.gb_talk.codec.c_str(),
             cfg.gb_talk.recv_port,
             cfg.gb_talk.sample_rate,
             cfg.gb_broadcast.codec.c_str(),
             cfg.gb_broadcast.recv_port,
             cfg.gb_listen.transport.c_str(),
             cfg.gb_listen.target_ip.c_str(),
             cfg.gb_listen.target_port);
    return std::string(buffer);
}

void LogConfigValidateFail(const protocol::ProtocolExternalConfig& cfg, int errorCode, const char* reason)
{
    printf("[Protocol][Config] module=config event=config_validate_fail trace=provider error=%d reason=%s version=%s gb=%s:%d gat=%s:%d\n",
           errorCode,
           reason != NULL ? reason : "unknown",
           cfg.version.c_str(),
           cfg.gb_register.server_ip.c_str(),
           cfg.gb_register.server_port,
           cfg.gat_register.server_ip.c_str(),
           cfg.gat_register.server_port);
}

namespace protocol
{

HttpConfigProvider::HttpConfigProvider(const std::string& endpoint)
    : m_endpoint(endpoint)
{
    InitDefaultConfig();
}

HttpConfigProvider::~HttpConfigProvider()
{
}

void HttpConfigProvider::InitDefaultConfig()
{
    m_cached_cfg.version = "v1-default";

    m_cached_cfg.gb_register.enabled = 1;
    m_cached_cfg.gb_register.server_ip = "183.252.186.165";
    m_cached_cfg.gb_register.server_port = 15566;
    m_cached_cfg.gb_register.device_id = "35010101001320124879";
    m_cached_cfg.gb_register.device_name = "IPC";
    m_cached_cfg.gb_register.username = "35010000002000000001";
    m_cached_cfg.gb_register.password = "CG939Xvv";

    m_cached_cfg.gb_live.transport = "udp";
    m_cached_cfg.gb_live.target_ip = "127.0.0.1";
    m_cached_cfg.gb_live.target_port = 30000;
    m_cached_cfg.gb_live.video_stream_id = "main";
    m_cached_cfg.gb_live.video_codec = "h264";
    m_cached_cfg.gb_live.audio_codec = "g711a";
    m_cached_cfg.gb_live.mtu = 1200;
    m_cached_cfg.gb_live.payload_type = 96;
    m_cached_cfg.gb_live.ssrc = 0;
    m_cached_cfg.gb_live.clock_rate = 90000;
    m_cached_cfg.gb_video.main_codec = "H.264";
    m_cached_cfg.gb_video.main_resolution = "1920x1080";
    m_cached_cfg.gb_video.main_fps = 25;
    m_cached_cfg.gb_video.main_bitrate_kbps = 2048;
    m_cached_cfg.gb_video.sub_codec = "H.264";
    m_cached_cfg.gb_video.sub_resolution = "640x360";
    m_cached_cfg.gb_video.sub_fps = 15;
    m_cached_cfg.gb_video.sub_bitrate_kbps = 512;
    m_cached_cfg.gb_image.flip_mode = "close";

    m_cached_cfg.gat_register.server_ip = "127.0.0.1";
    m_cached_cfg.gat_register.server_port = 80;
    m_cached_cfg.gat_register.scheme = "http";
    m_cached_cfg.gat_register.base_path = "";
    m_cached_cfg.gat_register.device_id = "34020000001320000001";
    m_cached_cfg.gat_register.username = "admin";
    m_cached_cfg.gat_register.password = "admin";
    m_cached_cfg.gat_register.auth_method = "digest";
    m_cached_cfg.gat_register.listen_port = 18080;
    m_cached_cfg.gat_register.expires_sec = 3600;
    m_cached_cfg.gat_register.keepalive_interval_sec = 60;
    m_cached_cfg.gat_register.max_retry = 3;
    m_cached_cfg.gat_register.request_timeout_ms = 5000;
    m_cached_cfg.gat_register.retry_backoff_policy = "5,10,30";
    m_cached_cfg.gat_upload.queue_dir = "/tmp/gat1400_queue";
    m_cached_cfg.gat_upload.max_pending_count = 200;
    m_cached_cfg.gat_upload.replay_interval_sec = 15;
    m_cached_cfg.gat_upload.enable_apes_post_compat = 0;

    m_cached_cfg.gb_talk.codec = "g711a";
    m_cached_cfg.gb_talk.recv_port = 30003;
    m_cached_cfg.gb_talk.sample_rate = 8000;
    m_cached_cfg.gb_talk.jitter_buffer_ms = 80;

    m_cached_cfg.gb_broadcast.input_mode = "stream";
    m_cached_cfg.gb_broadcast.codec = "g711a";
    m_cached_cfg.gb_broadcast.recv_port = 30001;
    m_cached_cfg.gb_broadcast.file_cache_dir = "/tmp";
    m_cached_cfg.gb_broadcast.transport = "tcp";

    m_cached_cfg.gb_listen.transport = "udp";
    m_cached_cfg.gb_listen.target_ip = "127.0.0.1";
    m_cached_cfg.gb_listen.target_port = 30002;
    m_cached_cfg.gb_listen.codec = "g711a";
    m_cached_cfg.gb_listen.sample_rate = 8000;
}
std::string HttpConfigProvider::BuildUrl(const std::string& suffix) const
{
    if (m_endpoint.empty()) {
        return suffix;
    }

    if (m_endpoint[m_endpoint.size() - 1] == '/' && !suffix.empty() && suffix[0] == '/') {
        return m_endpoint + suffix.substr(1);
    }

    if (m_endpoint[m_endpoint.size() - 1] != '/' && !suffix.empty() && suffix[0] != '/') {
        return m_endpoint + "/" + suffix;
    }

    return m_endpoint + suffix;
}

int HttpConfigProvider::HttpGet(const std::string& url, std::string& outBody) const
{
    return HttpRequest("GET", url, NULL, outBody);
}

int HttpConfigProvider::HttpPostJson(const std::string& url, const std::string& jsonBody, std::string& outBody) const
{
    return HttpRequest("POST", url, &jsonBody, outBody);
}

std::string HttpConfigProvider::ToMinimalJson(const ProtocolExternalConfig& cfg) const
{
    char tmp[128] = {0};
    std::string json = "{";

    json += "\"version\":\"" + cfg.version + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_register.enabled);
    json += "\"gb_register_enabled\":" + std::string(tmp) + ",";
    json += "\"gb_register_server_ip\":\"" + cfg.gb_register.server_ip + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_register.server_port);
    json += "\"gb_register_server_port\":" + std::string(tmp) + ",";
    json += "\"gb_register_device_name\":\"" + cfg.gb_register.device_name + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_register.expires_sec);
    json += "\"gb_register_expires_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_keepalive.interval_sec);
    json += "\"gb_keepalive_interval_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_keepalive.timeout_sec);
    json += "\"gb_keepalive_timeout_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_keepalive.max_retry);
    json += "\"gb_keepalive_max_retry\":" + std::string(tmp) + ",";

    json += "\"gb_live_transport\":\"" + cfg.gb_live.transport + "\",";
    json += "\"gb_live_target_ip\":\"" + cfg.gb_live.target_ip + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_live.target_port);
    json += "\"gb_live_target_port\":" + std::string(tmp) + ",";
    json += "\"gb_live_video_stream_id\":\"" + cfg.gb_live.video_stream_id + "\",";
    json += "\"gb_live_video_codec\":\"" + cfg.gb_live.video_codec + "\",";
    json += "\"gb_live_audio_codec\":\"" + cfg.gb_live.audio_codec + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_live.mtu);
    json += "\"gb_live_mtu\":" + std::string(tmp) + ",";
    json += "\"gb_talk_codec\":\"" + cfg.gb_talk.codec + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_talk.recv_port);
    json += "\"gb_talk_recv_port\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_talk.sample_rate);
    json += "\"gb_talk_sample_rate\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_talk.jitter_buffer_ms);
    json += "\"gb_talk_jitter_buffer_ms\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_live.payload_type);
    json += "\"gb_live_payload_type\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_live.ssrc);
    json += "\"gb_live_ssrc\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_live.clock_rate);
    json += "\"gb_live_clock_rate\":" + std::string(tmp) + ",";

    json += "\"gb_video_main_codec\":\"" + cfg.gb_video.main_codec + "\",";
    json += "\"gb_video_main_resolution\":\"" + cfg.gb_video.main_resolution + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_video.main_fps);
    json += "\"gb_video_main_fps\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_video.main_bitrate_kbps);
    json += "\"gb_video_main_bitrate_kbps\":" + std::string(tmp) + ",";
    json += "\"gb_video_sub_codec\":\"" + cfg.gb_video.sub_codec + "\",";
    json += "\"gb_video_sub_resolution\":\"" + cfg.gb_video.sub_resolution + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_video.sub_fps);
    json += "\"gb_video_sub_fps\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_video.sub_bitrate_kbps);
    json += "\"gb_video_sub_bitrate_kbps\":" + std::string(tmp) + ",";
    json += "\"gb_image_flip_mode\":\"" + cfg.gb_image.flip_mode + "\",";
    json += "\"gb_osd_text_template\":\"" + cfg.gb_osd.text_template + "\",";
    json += "\"gb_osd_time_format\":\"" + cfg.gb_osd.time_format + "\",";
    json += "\"gb_osd_position\":\"" + cfg.gb_osd.position + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_osd.time_enabled);
    json += "\"gb_osd_time_enabled\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_osd.event_enabled);
    json += "\"gb_osd_event_enabled\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_osd.alert_enabled);
    json += "\"gb_osd_alert_enabled\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_reboot.cooldown_sec);
    json += "\"gb_reboot_cooldown_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_reboot.require_auth_level);
    json += "\"gb_reboot_require_auth_level\":" + std::string(tmp) + ",";

    json += "\"gat_register_server_ip\":\"" + cfg.gat_register.server_ip + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.server_port);
    json += "\"gat_register_server_port\":" + std::string(tmp) + ",";
    json += "\"gat_register_scheme\":\"" + cfg.gat_register.scheme + "\",";
    json += "\"gat_register_base_path\":\"" + cfg.gat_register.base_path + "\",";
    json += "\"gat_register_device_id\":\"" + cfg.gat_register.device_id + "\",";
    json += "\"gat_register_username\":\"" + cfg.gat_register.username + "\",";
    json += "\"gat_register_password\":\"" + cfg.gat_register.password + "\",";
    json += "\"gat_register_auth_method\":\"" + cfg.gat_register.auth_method + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.listen_port);
    json += "\"gat_register_listen_port\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.expires_sec);
    json += "\"gat_register_expires_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.keepalive_interval_sec);
    json += "\"gat_register_keepalive_interval_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.max_retry);
    json += "\"gat_register_max_retry\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_register.request_timeout_ms);
    json += "\"gat_register_request_timeout_ms\":" + std::string(tmp) + ",";
    json += "\"gat_register_retry_backoff_policy\":\"" + cfg.gat_register.retry_backoff_policy + "\",";

    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_upload.batch_size);
    json += "\"gat_upload_batch_size\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_upload.flush_interval_ms);
    json += "\"gat_upload_flush_interval_ms\":" + std::string(tmp) + ",";
    json += "\"gat_upload_retry_policy\":\"" + cfg.gat_upload.retry_policy + "\",";
    json += "\"gat_upload_queue_dir\":\"" + cfg.gat_upload.queue_dir + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_upload.max_pending_count);
    json += "\"gat_upload_max_pending_count\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_upload.replay_interval_sec);
    json += "\"gat_upload_replay_interval_sec\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_upload.enable_apes_post_compat);
    json += "\"gat_upload_enable_apes_post_compat\":" + std::string(tmp) + ",";

    json += "\"gat_capture_face_profile\":\"" + cfg.gat_capture.face_profile + "\",";
    json += "\"gat_capture_nonmotor_profile\":\"" + cfg.gat_capture.nonmotor_profile + "\",";
    json += "\"gat_capture_vehicle_profile\":\"" + cfg.gat_capture.vehicle_profile + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gat_capture.concurrent_limit);
    json += "\"gat_capture_concurrent_limit\":" + std::string(tmp) + ",";

    json += "\"gb_broadcast_input_mode\":\"" + cfg.gb_broadcast.input_mode + "\",";
    json += "\"gb_broadcast_codec\":\"" + cfg.gb_broadcast.codec + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_broadcast.recv_port);
    json += "\"gb_broadcast_recv_port\":" + std::string(tmp) + ",";
    json += "\"gb_broadcast_file_cache_dir\":\"" + cfg.gb_broadcast.file_cache_dir + "\",";
    json += "\"gb_broadcast_transport\":\"" + cfg.gb_broadcast.transport + "\",";
    json += "\"gb_upgrade_url_whitelist\":\"" + JoinConfigList(cfg.gb_upgrade.url_whitelist) + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_upgrade.max_package_mb);
    json += "\"gb_upgrade_max_package_mb\":" + std::string(tmp) + ",";
    json += "\"gb_upgrade_verify_mode\":\"" + cfg.gb_upgrade.verify_mode + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_upgrade.timeout_sec);
    json += "\"gb_upgrade_timeout_sec\":" + std::string(tmp) + ",";

    json += "\"gb_listen_transport\":\"" + cfg.gb_listen.transport + "\",";
    json += "\"gb_listen_target_ip\":\"" + cfg.gb_listen.target_ip + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_listen.target_port);
    json += "\"gb_listen_target_port\":" + std::string(tmp) + ",";
    json += "\"gb_listen_codec\":\"" + cfg.gb_listen.codec + "\",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_listen.packet_ms);
    json += "\"gb_listen_packet_ms\":" + std::string(tmp) + ",";
    snprintf(tmp, sizeof(tmp), "%d", cfg.gb_listen.sample_rate);
    json += "\"gb_listen_sample_rate\":" + std::string(tmp);

    json += "}";
    return json;
}
int HttpConfigProvider::PullLatest(ProtocolExternalConfig& out)
{
    printf("[Protocol][Config] module=config event=config_pull_start trace=provider error=0 endpoint=%s cache_version=%s\n",
           m_endpoint.c_str(),
           m_cached_cfg.version.c_str());

    std::string body;
    const int rc = HttpGet(BuildUrl("/config"), body);
    if (rc != 0) {
        printf("[Protocol][Config] module=config event=config_pull_fail trace=provider error=%d endpoint=%s source=cache cache_version=%s\n",
               rc,
               m_endpoint.c_str(),
               m_cached_cfg.version.c_str());
        out = m_cached_cfg;
        return 0;
    }

    ProtocolExternalConfig next = m_cached_cfg;
    std::string value;
    int ivalue = 0;

    if (FindStringField(body, "version", value)) {
        next.version = value;
    }

    if (FindIntField(body, "gb_register_enabled", ivalue)) {
        next.gb_register.enabled = (ivalue != 0) ? 1 : 0;
    }
    if (FindStringField(body, "gb_register_server_ip", value)) {
        next.gb_register.server_ip = value;
    }
    if (FindIntField(body, "gb_register_server_port", ivalue)) {
        next.gb_register.server_port = ivalue;
    }
    if (FindStringField(body, "gb_register_device_name", value)) {
        next.gb_register.device_name = value;
    }
    if (FindIntField(body, "gb_register_expires_sec", ivalue)) {
        next.gb_register.expires_sec = ivalue;
    }
    if (FindIntField(body, "gb_keepalive_interval_sec", ivalue)) {
        next.gb_keepalive.interval_sec = ivalue;
    }
    if (FindIntField(body, "gb_keepalive_timeout_sec", ivalue)) {
        next.gb_keepalive.timeout_sec = ivalue;
    }
    if (FindIntField(body, "gb_keepalive_max_retry", ivalue)) {
        next.gb_keepalive.max_retry = ivalue;
    }

    if (FindStringField(body, "gb_live_transport", value)) {
        next.gb_live.transport = value;
    }
    if (FindStringField(body, "gb_live_target_ip", value)) {
        next.gb_live.target_ip = value;
    }
    if (FindIntField(body, "gb_live_target_port", ivalue)) {
        next.gb_live.target_port = ivalue;
    }
    if (FindStringField(body, "gb_live_video_stream_id", value)) {
        next.gb_live.video_stream_id = value;
    }
    if (FindStringField(body, "gb_live_video_codec", value)) {
        next.gb_live.video_codec = value;
    }
    if (FindStringField(body, "gb_live_audio_codec", value)) {
        next.gb_live.audio_codec = value;
    }
    if (FindIntField(body, "gb_live_mtu", ivalue)) {
        next.gb_live.mtu = ivalue;
    }
    if (FindStringField(body, "gb_talk_codec", value)) {
        next.gb_talk.codec = value;
    }
    if (FindIntField(body, "gb_talk_recv_port", ivalue)) {
        next.gb_talk.recv_port = ivalue;
    }
    if (FindIntField(body, "gb_talk_sample_rate", ivalue)) {
        next.gb_talk.sample_rate = ivalue;
    }
    if (FindIntField(body, "gb_talk_jitter_buffer_ms", ivalue)) {
        next.gb_talk.jitter_buffer_ms = ivalue;
    }
    if (FindIntField(body, "gb_live_payload_type", ivalue)) {
        next.gb_live.payload_type = ivalue;
    }
    if (FindIntField(body, "gb_live_ssrc", ivalue)) {
        next.gb_live.ssrc = ivalue;
    }
    if (FindIntField(body, "gb_live_clock_rate", ivalue)) {
        next.gb_live.clock_rate = ivalue;
    }

    if (FindStringField(body, "gb_video_main_codec", value)) {
        next.gb_video.main_codec = value;
    }
    if (FindStringField(body, "gb_video_main_resolution", value)) {
        next.gb_video.main_resolution = value;
    }
    if (FindIntField(body, "gb_video_main_fps", ivalue)) {
        next.gb_video.main_fps = ivalue;
    }
    if (FindIntField(body, "gb_video_main_bitrate_kbps", ivalue)) {
        next.gb_video.main_bitrate_kbps = ivalue;
    }
    if (FindStringField(body, "gb_video_sub_codec", value)) {
        next.gb_video.sub_codec = value;
    }
    if (FindStringField(body, "gb_video_sub_resolution", value)) {
        next.gb_video.sub_resolution = value;
    }
    if (FindIntField(body, "gb_video_sub_fps", ivalue)) {
        next.gb_video.sub_fps = ivalue;
    }
    if (FindIntField(body, "gb_video_sub_bitrate_kbps", ivalue)) {
        next.gb_video.sub_bitrate_kbps = ivalue;
    }
    if (FindStringField(body, "gb_image_flip_mode", value)) {
        next.gb_image.flip_mode = value;
    }
    if (FindStringField(body, "gb_osd_text_template", value)) {
        next.gb_osd.text_template = value;
    }
    if (FindStringField(body, "gb_osd_time_format", value)) {
        next.gb_osd.time_format = value;
    }
    if (FindStringField(body, "gb_osd_position", value)) {
        next.gb_osd.position = value;
    }
    if (FindIntField(body, "gb_osd_time_enabled", ivalue)) {
        next.gb_osd.time_enabled = ivalue;
    }
    if (FindIntField(body, "gb_osd_event_enabled", ivalue)) {
        next.gb_osd.event_enabled = ivalue;
    }
    if (FindIntField(body, "gb_osd_alert_enabled", ivalue)) {
        next.gb_osd.alert_enabled = ivalue;
    }
    if (FindIntField(body, "gb_reboot_cooldown_sec", ivalue)) {
        next.gb_reboot.cooldown_sec = ivalue;
    }
    if (FindIntField(body, "gb_reboot_require_auth_level", ivalue)) {
        next.gb_reboot.require_auth_level = ivalue;
    }

    if (FindStringField(body, "gat_register_server_ip", value)) {
        next.gat_register.server_ip = value;
    }
    if (FindIntField(body, "gat_register_server_port", ivalue)) {
        next.gat_register.server_port = ivalue;
    }
    if (FindStringField(body, "gat_register_scheme", value)) {
        next.gat_register.scheme = value;
    }
    if (FindStringField(body, "gat_register_base_path", value)) {
        next.gat_register.base_path = value;
    }
    if (FindStringField(body, "gat_register_device_id", value)) {
        next.gat_register.device_id = value;
    }
    if (FindStringField(body, "gat_register_username", value)) {
        next.gat_register.username = value;
    }
    if (FindStringField(body, "gat_register_password", value)) {
        next.gat_register.password = value;
    }
    if (FindStringField(body, "gat_register_auth_method", value)) {
        next.gat_register.auth_method = value;
    }
    if (FindIntField(body, "gat_register_listen_port", ivalue)) {
        next.gat_register.listen_port = ivalue;
    }
    if (FindIntField(body, "gat_register_expires_sec", ivalue)) {
        next.gat_register.expires_sec = ivalue;
    }
    if (FindIntField(body, "gat_register_keepalive_interval_sec", ivalue)) {
        next.gat_register.keepalive_interval_sec = ivalue;
    }
    if (FindIntField(body, "gat_register_max_retry", ivalue)) {
        next.gat_register.max_retry = ivalue;
    }
    if (FindIntField(body, "gat_register_request_timeout_ms", ivalue)) {
        next.gat_register.request_timeout_ms = ivalue;
    }
    if (FindStringField(body, "gat_register_retry_backoff_policy", value)) {
        next.gat_register.retry_backoff_policy = value;
    }

    if (FindIntField(body, "gat_upload_batch_size", ivalue)) {
        next.gat_upload.batch_size = ivalue;
    }
    if (FindIntField(body, "gat_upload_flush_interval_ms", ivalue)) {
        next.gat_upload.flush_interval_ms = ivalue;
    }
    if (FindStringField(body, "gat_upload_retry_policy", value)) {
        next.gat_upload.retry_policy = value;
    }
    if (FindStringField(body, "gat_upload_queue_dir", value)) {
        next.gat_upload.queue_dir = value;
    }
    if (FindIntField(body, "gat_upload_max_pending_count", ivalue)) {
        next.gat_upload.max_pending_count = ivalue;
    }
    if (FindIntField(body, "gat_upload_replay_interval_sec", ivalue)) {
        next.gat_upload.replay_interval_sec = ivalue;
    }
    if (FindIntField(body, "gat_upload_enable_apes_post_compat", ivalue)) {
        next.gat_upload.enable_apes_post_compat = (ivalue != 0) ? 1 : 0;
    }
    if (FindStringField(body, "gat_capture_face_profile", value)) {
        next.gat_capture.face_profile = value;
    }
    if (FindStringField(body, "gat_capture_nonmotor_profile", value)) {
        next.gat_capture.nonmotor_profile = value;
    }
    if (FindStringField(body, "gat_capture_vehicle_profile", value)) {
        next.gat_capture.vehicle_profile = value;
    }
    if (FindIntField(body, "gat_capture_concurrent_limit", ivalue)) {
        next.gat_capture.concurrent_limit = ivalue;
    }
    if (FindStringField(body, "gb_broadcast_input_mode", value)) {
        next.gb_broadcast.input_mode = value;
    }
    if (FindStringField(body, "gb_broadcast_codec", value)) {
        next.gb_broadcast.codec = value;
    }
    if (FindIntField(body, "gb_broadcast_recv_port", ivalue)) {
        next.gb_broadcast.recv_port = ivalue;
    }
    if (FindStringField(body, "gb_broadcast_file_cache_dir", value)) {
        next.gb_broadcast.file_cache_dir = value;
    }
    if (FindStringField(body, "gb_broadcast_transport", value)) {
        next.gb_broadcast.transport = value;
    }
    if (FindStringField(body, "gb_upgrade_url_whitelist", value)) {
        next.gb_upgrade.url_whitelist = SplitConfigList(value);
    }
    if (FindIntField(body, "gb_upgrade_max_package_mb", ivalue)) {
        next.gb_upgrade.max_package_mb = ivalue;
    }
    if (FindStringField(body, "gb_upgrade_verify_mode", value)) {
        next.gb_upgrade.verify_mode = value;
    }
    if (FindIntField(body, "gb_upgrade_timeout_sec", ivalue)) {
        next.gb_upgrade.timeout_sec = ivalue;
    }

    if (FindStringField(body, "gb_listen_transport", value)) {
        next.gb_listen.transport = value;
    }
    if (FindStringField(body, "gb_listen_target_ip", value)) {
        next.gb_listen.target_ip = value;
    }
    if (FindIntField(body, "gb_listen_target_port", ivalue)) {
        next.gb_listen.target_port = ivalue;
    }
    if (FindStringField(body, "gb_listen_codec", value)) {
        next.gb_listen.codec = value;
    }
    if (FindIntField(body, "gb_listen_packet_ms", ivalue)) {
        next.gb_listen.packet_ms = ivalue;
    }
    if (FindIntField(body, "gb_listen_sample_rate", ivalue)) {
        next.gb_listen.sample_rate = ivalue;
    }

    out = next;
    m_cached_cfg = next;
    printf("[Protocol][Config] module=config event=config_pull_success trace=provider error=0 endpoint=%s source=remote %s\n",
           m_endpoint.c_str(),
           BuildConfigLogSummary(next).c_str());
    return 0;
}

int HttpConfigProvider::PushApply(const ProtocolExternalConfig& cfg)
{
    printf("[Protocol][Config] module=config event=config_apply_start trace=provider error=0 endpoint=%s version=%s\n",
           m_endpoint.c_str(),
           cfg.version.c_str());

    const int check = Validate(cfg);
    if (check != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d endpoint=%s stage=validate version=%s\n",
               check,
               m_endpoint.c_str(),
               cfg.version.c_str());
        return check;
    }

    std::string resp;
    const int rc = HttpPostJson(BuildUrl("/config"), ToMinimalJson(cfg), resp);
    if (rc != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d endpoint=%s stage=push_remote version=%s\n",
               rc,
               m_endpoint.c_str(),
               cfg.version.c_str());
    } else {
        printf("[Protocol][Config] module=config event=config_apply_success trace=provider error=0 endpoint=%s version=%s\n",
               m_endpoint.c_str(),
               cfg.version.c_str());
    }

    m_cached_cfg = cfg;
    return 0;
}

int HttpConfigProvider::Validate(const ProtocolExternalConfig& cfg)
{
    if (cfg.gb_register.enabled != 0) {
        if (cfg.gb_register.server_ip.empty() || cfg.gb_register.server_port <= 0) {
            LogConfigValidateFail(cfg, -1, "gb_register_endpoint");
            return -1;
        }

        if (cfg.gb_live.transport != "udp" && cfg.gb_live.transport != "tcp") {
            LogConfigValidateFail(cfg, -2, "gb_live_transport");
            return -2;
        }

        if (cfg.gb_live.target_ip.empty() || cfg.gb_live.target_port <= 0) {
            LogConfigValidateFail(cfg, -3, "gb_live_target");
            return -3;
        }

        if (cfg.gb_live.video_codec.empty()) {
            LogConfigValidateFail(cfg, -4, "gb_live_video_codec");
            return -4;
        }

        if (cfg.gb_live.mtu < 256 || cfg.gb_live.mtu > 1500) {
            LogConfigValidateFail(cfg, -5, "gb_live_mtu");
            return -5;
        }

        if (cfg.gb_live.payload_type < 0 || cfg.gb_live.payload_type > 127) {
            LogConfigValidateFail(cfg, -6, "gb_live_payload_type");
            return -6;
        }

        if (cfg.gb_video.main_codec.empty() || cfg.gb_video.sub_codec.empty()) {
            LogConfigValidateFail(cfg, -16, "gb_video_codec");
            return -16;
        }

        if (cfg.gb_video.main_resolution.empty() || cfg.gb_video.sub_resolution.empty()) {
            LogConfigValidateFail(cfg, -17, "gb_video_resolution");
            return -17;
        }

        if (cfg.gb_video.main_fps <= 0 || cfg.gb_video.sub_fps <= 0) {
            LogConfigValidateFail(cfg, -18, "gb_video_fps");
            return -18;
        }

        if (cfg.gb_video.main_bitrate_kbps <= 0 || cfg.gb_video.sub_bitrate_kbps <= 0) {
            LogConfigValidateFail(cfg, -19, "gb_video_bitrate");
            return -19;
        }
    }

    if ((cfg.gat_register.scheme != "http" && cfg.gat_register.scheme != "https") ||
        cfg.gat_register.server_ip.empty() || cfg.gat_register.server_port <= 0) {
        LogConfigValidateFail(cfg, -7, "gat_register_endpoint");
        return -7;
    }

    if (cfg.gat_register.listen_port <= 0) {
        LogConfigValidateFail(cfg, -12, "gat_register_listen_port");
        return -12;
    }

    if (cfg.gat_register.keepalive_interval_sec <= 0 || cfg.gat_register.max_retry <= 0) {
        LogConfigValidateFail(cfg, -13, "gat_register_keepalive");
        return -13;
    }

    if (cfg.gat_register.request_timeout_ms <= 0) {
        LogConfigValidateFail(cfg, -24, "gat_register_request_timeout_ms");
        return -24;
    }

    if (cfg.gat_upload.batch_size <= 0 || cfg.gat_upload.flush_interval_ms <= 0) {
        LogConfigValidateFail(cfg, -14, "gat_upload");
        return -14;
    }

    if (cfg.gat_upload.queue_dir.empty() || cfg.gat_upload.max_pending_count <= 0 || cfg.gat_upload.replay_interval_sec <= 0) {
        LogConfigValidateFail(cfg, -25, "gat_upload_queue");
        return -25;
    }

    if (cfg.gat_upload.enable_apes_post_compat != 0 && cfg.gat_upload.enable_apes_post_compat != 1) {
        LogConfigValidateFail(cfg, -26, "gat_upload_enable_apes_post_compat");
        return -26;
    }

    if (cfg.gat_capture.concurrent_limit <= 0) {
        LogConfigValidateFail(cfg, -15, "gat_capture_concurrent_limit");
        return -15;
    }

    if (cfg.gb_broadcast.input_mode != "file" && cfg.gb_broadcast.input_mode != "stream") {
        LogConfigValidateFail(cfg, -8, "gb_broadcast_input_mode");
        return -8;
    }

    if (cfg.gb_talk.codec.empty() || cfg.gb_talk.recv_port <= 0 || cfg.gb_talk.sample_rate <= 0) {
        LogConfigValidateFail(cfg, -7, "gb_talk_params");
        return -7;
    }

    if (cfg.gb_broadcast.codec.empty() || cfg.gb_broadcast.recv_port <= 0) {
        LogConfigValidateFail(cfg, -9, "gb_broadcast_params");
        return -9;
    }

    if (cfg.gb_broadcast.transport != "udp" && cfg.gb_broadcast.transport != "tcp") {
        LogConfigValidateFail(cfg, -22, "gb_broadcast_transport");
        return -22;
    }

    if (cfg.gb_listen.target_ip.empty() || cfg.gb_listen.target_port <= 0) {
        LogConfigValidateFail(cfg, -10, "gb_listen_target");
        return -10;
    }

    if (cfg.gb_listen.transport != "udp" && cfg.gb_listen.transport != "tcp") {
        LogConfigValidateFail(cfg, -11, "gb_listen_transport");
        return -11;
    }

    if (cfg.gb_listen.packet_ms <= 0) {
        LogConfigValidateFail(cfg, -20, "gb_listen_packet_ms");
        return -20;
    }

    if (cfg.gb_listen.sample_rate <= 0) {
        LogConfigValidateFail(cfg, -21, "gb_listen_sample_rate");
        return -21;
    }

    std::string resp;
    const int rc = HttpPostJson(BuildUrl("/config/validate"), ToMinimalJson(cfg), resp);
    if (rc != 0) {
        printf("[Protocol][Config] module=config event=config_validate_fail trace=provider error=%d stage=remote_validate_optional endpoint=%s version=%s\n",
               rc,
               m_endpoint.c_str(),
               cfg.version.c_str());
    }

    return 0;
}

int HttpConfigProvider::QueryCapabilities(std::string& outJson)
{
    std::string body;
    const int rc = HttpGet(BuildUrl("/capabilities"), body);
    if (rc == 0 && !body.empty()) {
        outJson = body;
        return 0;
    }

    outJson = "{\"mandatory\":[\"gb.register\",\"gb.live\",\"gb.playback\",\"gb.talk\",\"gb.ptz\",\"gb.video\",\"gb.reboot\",\"gb.upgrade\",\"gb.image\",\"gb.osd\",\"gb.alarm\",\"gb.multistream\",\"gb.broadcast\",\"gb.listen\",\"gat.register\",\"gat.upload\",\"gat.capture\",\"cloud.fast_access\"]}";
    return 0;
}

void HttpConfigProvider::SubscribeChange()
{
    printf("[Protocol][Config] SubscribeChange from endpoint: %s\n", m_endpoint.c_str());
}

void HttpConfigProvider::SetMockConfig(const ProtocolExternalConfig& cfg)
{
    m_cached_cfg = cfg;
}

}









