#include "LocalConfigProvider.h"

#include <stdio.h>

namespace
{

std::string JoinConfigList(const std::vector<std::string>& values)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += values[i];
    }
    return out;
}

std::string BuildConfigLogSummary(const protocol::ProtocolExternalConfig& cfg)
{
    char buffer[512] = {0};
    snprintf(buffer,
             sizeof(buffer),
             "version=%s gb=%s:%d live=%s/%s:%d gat=%s:%d/%d broadcast=%s/%d listen=%s/%s:%d",
             cfg.version.c_str(),
             cfg.gb_register.server_ip.c_str(),
             cfg.gb_register.server_port,
             cfg.gb_live.transport.c_str(),
             cfg.gb_live.target_ip.c_str(),
             cfg.gb_live.target_port,
             cfg.gat_register.server_ip.c_str(),
             cfg.gat_register.server_port,
             cfg.gat_register.listen_port,
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

}

namespace protocol
{

LocalConfigProvider::LocalConfigProvider(const std::string& sourceTag)
    : m_source_tag(sourceTag)
{
    InitDefaultConfig();
}

LocalConfigProvider::~LocalConfigProvider()
{
}

void LocalConfigProvider::InitDefaultConfig()
{
    m_cached_cfg.version = "v1-default";

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
    m_cached_cfg.gb_live.video_codec = "h265";
    m_cached_cfg.gb_live.audio_codec = "g711a";
    m_cached_cfg.gb_live.mtu = 1200;
    m_cached_cfg.gb_live.payload_type = 96;
    m_cached_cfg.gb_live.ssrc = 0;
    m_cached_cfg.gb_live.clock_rate = 90000;
    m_cached_cfg.gb_video.main_codec = "H.265";
    m_cached_cfg.gb_video.main_resolution = "3840x1080";
    m_cached_cfg.gb_video.main_fps = 25;
    m_cached_cfg.gb_video.main_bitrate_kbps = 2048;
    m_cached_cfg.gb_video.sub_codec = "H.265";
    m_cached_cfg.gb_video.sub_resolution = "640x360";
    m_cached_cfg.gb_video.sub_fps = 15;
    m_cached_cfg.gb_video.sub_bitrate_kbps = 512;
    m_cached_cfg.gb_image.flip_mode = "close";

    m_cached_cfg.gat_register.server_ip = "127.0.0.1";
    m_cached_cfg.gat_register.server_port = 80;
    m_cached_cfg.gat_register.device_id = "34020000001320000001";
    m_cached_cfg.gat_register.username = "admin";
    m_cached_cfg.gat_register.password = "admin";
    m_cached_cfg.gat_register.auth_method = "digest";
    m_cached_cfg.gat_register.listen_port = 18080;
    m_cached_cfg.gat_register.expires_sec = 3600;
    m_cached_cfg.gat_register.keepalive_interval_sec = 60;
    m_cached_cfg.gat_register.max_retry = 3;

    m_cached_cfg.gb_broadcast.input_mode = "stream";
    m_cached_cfg.gb_broadcast.codec = "g711a";
    m_cached_cfg.gb_broadcast.recv_port = 30001;
    m_cached_cfg.gb_broadcast.file_cache_dir = "/tmp";

    m_cached_cfg.gb_listen.transport = "udp";
    m_cached_cfg.gb_listen.target_ip = "127.0.0.1";
    m_cached_cfg.gb_listen.target_port = 30002;
    m_cached_cfg.gb_listen.codec = "g711a";
}

int LocalConfigProvider::PullLatest(ProtocolExternalConfig& out)
{
    out = m_cached_cfg;
    printf("[Protocol][Config] module=config event=config_pull_success trace=provider error=0 source=local cache=%s tag=%s %s\n",
           m_cached_cfg.version.c_str(),
           m_source_tag.c_str(),
           BuildConfigLogSummary(m_cached_cfg).c_str());
    return 0;
}

int LocalConfigProvider::PushApply(const ProtocolExternalConfig& cfg)
{
    const int check = Validate(cfg);
    if (check != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d source=local stage=validate version=%s tag=%s\n",
               check,
               cfg.version.c_str(),
               m_source_tag.c_str());
        return check;
    }

    m_cached_cfg = cfg;
    printf("[Protocol][Config] module=config event=config_apply_success trace=provider error=0 source=local version=%s tag=%s\n",
           cfg.version.c_str(),
           m_source_tag.c_str());
    return 0;
}

int LocalConfigProvider::Validate(const ProtocolExternalConfig& cfg)
{
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

    if (cfg.gat_register.server_ip.empty() || cfg.gat_register.server_port <= 0) {
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

    if (cfg.gat_upload.batch_size <= 0 || cfg.gat_upload.flush_interval_ms <= 0) {
        LogConfigValidateFail(cfg, -14, "gat_upload");
        return -14;
    }

    if (cfg.gat_capture.concurrent_limit <= 0) {
        LogConfigValidateFail(cfg, -15, "gat_capture_concurrent_limit");
        return -15;
    }

    if (cfg.gb_broadcast.input_mode != "file" && cfg.gb_broadcast.input_mode != "stream") {
        LogConfigValidateFail(cfg, -8, "gb_broadcast_input_mode");
        return -8;
    }

    if (cfg.gb_broadcast.codec.empty() || cfg.gb_broadcast.recv_port <= 0) {
        LogConfigValidateFail(cfg, -9, "gb_broadcast_params");
        return -9;
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

    return 0;
}

int LocalConfigProvider::QueryCapabilities(std::string& outJson)
{
    outJson =
        "{\"mandatory\":[\"gb.register\",\"gb.live\",\"gb.playback\",\"gb.talk\",\"gb.ptz\",\"gb.video\","
        "\"gb.reboot\",\"gb.image\",\"gb.osd\",\"gb.alarm\",\"gb.multistream\",\"gb.broadcast\",\"gb.listen\","
        "\"gat.register\",\"gat.upload\",\"gat.capture\",\"cloud.fast_access\"],"
        "\"storage\":\"local\",\"upgrade_verify_modes\":\"" + m_cached_cfg.gb_upgrade.verify_mode + "\","
        "\"gb_upgrade_url_whitelist\":\"" + JoinConfigList(m_cached_cfg.gb_upgrade.url_whitelist) + "\"}";
    return 0;
}

void LocalConfigProvider::SubscribeChange()
{
    printf("[Protocol][Config] module=config event=config_subscribe_noop trace=provider source=local tag=%s\n",
           m_source_tag.c_str());
}

void LocalConfigProvider::SetMockConfig(const ProtocolExternalConfig& cfg)
{
    m_cached_cfg = cfg;
}

}
