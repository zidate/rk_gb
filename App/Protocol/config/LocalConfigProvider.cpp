#include "LocalConfigProvider.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Inifile.h"

namespace
{

const char* kLocalGbConfigDir = "/userdata/conf/Config";
const char* kLocalGbConfigFile = "/userdata/conf/Config/gb28181.ini";
const char* kLocalGbConfigSection = "gb28181";

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
             "version=%s gb_enable=%d gb=%s:%d live=%s/%s:%d flip=%s gat=%s:%d/%d talk=%s/%d/%d broadcast=%s/%d listen=%s/%s:%d",
             cfg.version.c_str(),
             cfg.gb_register.enabled,
             cfg.gb_register.server_ip.c_str(),
             cfg.gb_register.server_port,
             cfg.gb_live.transport.c_str(),
             cfg.gb_live.target_ip.c_str(),
             cfg.gb_live.target_port,
             cfg.gb_image.flip_mode.c_str(),
             cfg.gat_register.server_ip.c_str(),
             cfg.gat_register.server_port,
             cfg.gat_register.listen_port,
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

bool EnsureDirectoryExists(const char* dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return false;
    }

    std::string path(dir);
    if (path[path.size() - 1] != '/') {
        path += "/";
    }

    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] != '/') {
            continue;
        }

        if (current.empty()) {
            continue;
        }

        if (access(current.c_str(), F_OK) == 0) {
            continue;
        }

        if (mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
            return false;
        }
    }

    return true;
}

bool LoadLocalConfigFile(protocol::ProtocolExternalConfig& cfg)
{
    if (access(kLocalGbConfigFile, F_OK) != 0) {
        return false;
    }

    CInifile ini;
    char value[256] = {0};
    if (ini.read_profile_string(kLocalGbConfigSection, "enable", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.enabled = (atoi(value) != 0) ? 1 : 0;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "username", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.username = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "server_ip", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.server_ip = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "server_port", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.server_port = atoi(value);
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "device_id", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.device_id = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "password", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_register.password = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "image_flip_mode", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_image.flip_mode = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "talk_codec", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_talk.codec = value;
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "talk_recv_port", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_talk.recv_port = atoi(value);
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "talk_sample_rate", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_talk.sample_rate = atoi(value);
    }

    memset(value, 0, sizeof(value));
    if (ini.read_profile_string(kLocalGbConfigSection, "talk_jitter_buffer_ms", value, sizeof(value), kLocalGbConfigFile) == 0) {
        cfg.gb_talk.jitter_buffer_ms = atoi(value);
    }

    return true;
}

int SaveLocalConfigFile(const protocol::ProtocolExternalConfig& cfg)
{
    if (!EnsureDirectoryExists(kLocalGbConfigDir)) {
        return -1;
    }

    FILE* fp = fopen(kLocalGbConfigFile, "w");
    if (fp == NULL) {
        return -2;
    }

    fprintf(fp, "[%s]\n", kLocalGbConfigSection);
    fprintf(fp, "enable=%d\n", cfg.gb_register.enabled != 0 ? 1 : 0);
    fprintf(fp, "username=%s\n", cfg.gb_register.username.c_str());
    fprintf(fp, "server_ip=%s\n", cfg.gb_register.server_ip.c_str());
    fprintf(fp, "server_port=%d\n", cfg.gb_register.server_port);
    fprintf(fp, "device_id=%s\n", cfg.gb_register.device_id.c_str());
    fprintf(fp, "password=%s\n", cfg.gb_register.password.c_str());
    fprintf(fp, "image_flip_mode=%s\n", cfg.gb_image.flip_mode.c_str());
    fprintf(fp, "talk_codec=%s\n", cfg.gb_talk.codec.c_str());
    fprintf(fp, "talk_recv_port=%d\n", cfg.gb_talk.recv_port);
    fprintf(fp, "talk_sample_rate=%d\n", cfg.gb_talk.sample_rate);
    fprintf(fp, "talk_jitter_buffer_ms=%d\n", cfg.gb_talk.jitter_buffer_ms);

    const int flushRet = fflush(fp);
    const int closeRet = fclose(fp);
    if (flushRet != 0 || closeRet != 0) {
        return -3;
    }

    return 0;
}

}

namespace protocol
{

LocalConfigProvider::LocalConfigProvider(const std::string& sourceTag)
    : m_source_tag(sourceTag)
{
    InitDefaultConfig();
    ProtocolExternalConfig next = m_cached_cfg;
    if (LoadLocalConfigFile(next)) {
        m_cached_cfg = next;
        m_cached_cfg.version = "v1-local-file";
        printf("[Protocol][Config] module=config event=config_file_load_success trace=provider path=%s tag=%s\n",
               kLocalGbConfigFile,
               m_source_tag.c_str());
    } else {
        const int saveRet = SaveLocalConfigFile(m_cached_cfg);
        printf("[Protocol][Config] module=config event=config_file_init trace=provider path=%s tag=%s ret=%d\n",
               kLocalGbConfigFile,
               m_source_tag.c_str(),
               saveRet);
    }
}

LocalConfigProvider::~LocalConfigProvider()
{
}

void LocalConfigProvider::InitDefaultConfig()
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

    m_cached_cfg.gb_talk.codec = "g711a";
    m_cached_cfg.gb_talk.recv_port = 30003;
    m_cached_cfg.gb_talk.sample_rate = 8000;
    m_cached_cfg.gb_talk.jitter_buffer_ms = 80;

    m_cached_cfg.gb_broadcast.input_mode = "stream";
    m_cached_cfg.gb_broadcast.codec = "g711a";
    m_cached_cfg.gb_broadcast.recv_port = 30001;
    m_cached_cfg.gb_broadcast.file_cache_dir = "/tmp";

    m_cached_cfg.gb_listen.transport = "udp";
    m_cached_cfg.gb_listen.target_ip = "127.0.0.1";
    m_cached_cfg.gb_listen.target_port = 30002;
    m_cached_cfg.gb_listen.codec = "g711a";
    m_cached_cfg.gb_listen.sample_rate = 8000;
}

int LocalConfigProvider::PullLatest(ProtocolExternalConfig& out)
{
    ProtocolExternalConfig next = m_cached_cfg;
    if (LoadLocalConfigFile(next)) {
        next.version = "v1-local-file";
        m_cached_cfg = next;
    } else {
        const int saveRet = SaveLocalConfigFile(m_cached_cfg);
        printf("[Protocol][Config] module=config event=config_file_missing trace=provider path=%s tag=%s ret=%d\n",
               kLocalGbConfigFile,
               m_source_tag.c_str(),
               saveRet);
    }

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
    const int saveRet = SaveLocalConfigFile(m_cached_cfg);
    if (saveRet != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d source=local stage=persist version=%s tag=%s path=%s\n",
               saveRet,
               cfg.version.c_str(),
               m_source_tag.c_str(),
               kLocalGbConfigFile);
        return saveRet;
    }

    printf("[Protocol][Config] module=config event=config_apply_success trace=provider error=0 source=local version=%s tag=%s\n",
           cfg.version.c_str(),
           m_source_tag.c_str());
    return 0;
}

int LocalConfigProvider::Validate(const ProtocolExternalConfig& cfg)
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

        if (cfg.gb_talk.codec.empty() || cfg.gb_talk.recv_port <= 0 || cfg.gb_talk.sample_rate <= 0) {
            LogConfigValidateFail(cfg, -7, "gb_talk_params");
            return -7;
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

    if (cfg.gb_listen.sample_rate <= 0) {
        LogConfigValidateFail(cfg, -21, "gb_listen_sample_rate");
        return -21;
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
