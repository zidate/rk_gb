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

const char* kLocalConfigDir = "/userdata/conf/Config/GB";
const char* kLocalGbConfigFile = "/userdata/conf/Config/GB/gb28181.ini";
const char* kLocalGbConfigSection = "gb28181";
const char* kLocalZeroConfigFile = "/userdata/conf/Config/GB/zero_config.ini";
const char* kLocalZeroConfigSection = "zero_config";
const char* kLocalGatConfigFile = "/userdata/conf/Config/GB/gat1400.ini";
const char* kLocalGatConfigSection = "gat1400";

enum LocalConfigLoadSource
{
    kLocalConfigLoadNone = 0,
    kLocalConfigLoadCurrent = 1
};

struct LocalConfigSyncState
{
    LocalConfigLoadSource gb_source;
    LocalConfigLoadSource zero_source;
    LocalConfigLoadSource gat_source;
    int gb_sync_ret;
    int zero_sync_ret;
    int gat_sync_ret;

    LocalConfigSyncState()
        : gb_source(kLocalConfigLoadNone),
          zero_source(kLocalConfigLoadNone),
          gat_source(kLocalConfigLoadNone),
          gb_sync_ret(0),
          zero_sync_ret(0),
          gat_sync_ret(0) {}
};

const int kZeroConfigFileMissingError = -29;
const int kInvalidGbRegisterModeError = -30;

int RequiredZeroConfigFileError()
{
    return kZeroConfigFileMissingError;
}

bool IsZeroConfigFileRequired(const protocol::GbRegisterParam& param)
{
    return protocol::IsGbRegisterModeZeroConfig(param);
}

bool IsZeroConfigFileMissing(const LocalConfigSyncState& state,
                             const protocol::GbRegisterParam& param)
{
    return IsZeroConfigFileRequired(param) &&
           state.zero_source != kLocalConfigLoadCurrent;
}

void LogZeroConfigFileMissing(const char* tag)
{
    printf("[Protocol][Config] module=config event=zero_config_file_missing trace=provider error=%d path=%s tag=%s\n",
           RequiredZeroConfigFileError(),
           kLocalZeroConfigFile,
           tag != NULL ? tag : "");
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

std::string BuildConfigLogSummary(const protocol::ProtocolExternalConfig& cfg)
{
    char buffer[768] = {0};
    snprintf(buffer,
             sizeof(buffer),
             "version=%s gb_enable=%d gb_mode=%s gb=%s:%d gb_id=%s string=%s redirect=%s/%s live=%s/%s:%d flip=%s gat=%s://%s:%d%s listen=%d timeout=%d queue=%s apes_post_compat=%d talk=%s/%d/%d broadcast=%s/%d listen=%s/%s:%d",
             cfg.version.c_str(),
             cfg.gb_register.enabled,
             protocol::NormalizeGbRegisterMode(cfg.gb_register.register_mode).c_str(),
             cfg.gb_register.server_ip.c_str(),
             cfg.gb_register.server_port,
             cfg.gb_register.device_id.c_str(),
             cfg.gb_register.string_code.c_str(),
             cfg.gb_register.redirect_server_id.c_str(),
             cfg.gb_register.redirect_domain.c_str(),
             cfg.gb_live.transport.c_str(),
             cfg.gb_live.target_ip.c_str(),
             cfg.gb_live.target_port,
             cfg.gb_image.flip_mode.c_str(),
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

protocol::GbRegisterParam BuildDefaultGbRegisterParam()
{
    protocol::GbRegisterParam param;
    param.enabled = 1;
    param.register_mode = protocol::kGbRegisterModeStandard;
    param.server_ip = "183.252.186.135";
    param.server_port = 16656;
    param.device_id = "35010101001320124929";
    param.device_name = "IPC";
    param.username = "35010000002000000001";
    param.password = "2CE09989747C";
    param.string_code = "C04403261010000000101";
    param.mac_address = "2CE09989747C";
    param.line_id = "1";
    param.redirect_domain = "35010000002000000001";
    param.redirect_server_id = "35010000002000000001";
    param.custom_protocol_version = "3.0";
    param.manufacturer = "IPC";
    param.model = "RC0240";
    return param;
}

protocol::GbZeroConfigParam BuildDefaultGbZeroConfigParam()
{
    protocol::GbZeroConfigParam param;
    const protocol::GbRegisterParam defaults = BuildDefaultGbRegisterParam();
    param.string_code = defaults.string_code;
    param.mac_address = defaults.mac_address;
    return param;
}

void ApplyGbRuntimeZeroConfigFields(protocol::GbRegisterParam& param)
{
    param.register_mode = protocol::NormalizeGbRegisterMode(param.register_mode);
    if (!protocol::IsGbRegisterModeZeroConfig(param)) {
        return;
    }

    const protocol::GbRegisterParam defaults = BuildDefaultGbRegisterParam();
    param.server_ip = defaults.server_ip;
    param.server_port = defaults.server_port;
    param.redirect_domain = defaults.redirect_domain;
    param.redirect_server_id = defaults.redirect_server_id;
}

protocol::GatRegisterParam BuildDefaultGatRegisterParam()
{
    protocol::GatRegisterParam param;
    param.server_ip = "183.252.186.166";
    param.server_port = 33855;
    param.scheme = "http";
    param.base_path = "";
    param.device_id = "35010200001190000010";
    param.username = "admin";
    param.password = "Dz8h6kM9";
    param.auth_method = "digest";
    param.listen_port = 18080;
    param.expires_sec = 3600;
    param.keepalive_interval_sec = 60;
    param.max_retry = 3;
    param.request_timeout_ms = 5000;
    param.retry_backoff_policy = "5,10,30";
    return param;
}

void ApplyGbRegisterEditableFields(protocol::GbRegisterParam& target, const protocol::GbRegisterParam& source)
{
    target.enabled = (source.enabled != 0) ? 1 : 0;
    target.register_mode = protocol::NormalizeGbRegisterMode(source.register_mode);
    target.server_ip = source.server_ip;
    target.server_port = source.server_port;
    target.device_id = source.device_id;
    target.username = source.username;
    target.password = source.password;
}

void ApplyGbZeroConfigEditableFields(protocol::GbRegisterParam& target, const protocol::GbZeroConfigParam& source)
{
    target.string_code = source.string_code;
    target.mac_address = source.mac_address;
}

int ValidateGbRegisterEditableFields(const protocol::GbRegisterParam& param)
{
    if (!protocol::IsValidGbRegisterMode(param.register_mode)) {
        return kInvalidGbRegisterModeError;
    }

    if (param.enabled != 0 && (param.server_ip.empty() || param.server_port <= 0)) {
        return -1;
    }

    if (param.enabled != 0 &&
        protocol::IsGbRegisterModeZeroConfig(param) &&
        param.string_code.empty()) {
        return -2;
    }

    if (param.enabled != 0 &&
        protocol::IsGbRegisterModeZeroConfig(param) &&
        param.redirect_server_id.empty()) {
        return -3;
    }
    return 0;
}

void ApplyGatRegisterEditableFields(protocol::GatRegisterParam& target, const protocol::GatRegisterParam& source)
{
    target = source;
}

int ValidateGatRegisterEditableFields(const protocol::GatRegisterParam& param)
{
    if ((param.scheme != "http" && param.scheme != "https") ||
        param.server_ip.empty() || param.server_port <= 0) {
        return -1;
    }

    if (param.listen_port <= 0) {
        return -2;
    }

    if (param.keepalive_interval_sec <= 0 || param.max_retry <= 0) {
        return -3;
    }

    if (param.request_timeout_ms <= 0) {
        return -4;
    }

    return 0;
}

void InitDefaultLocalConfig(protocol::ProtocolExternalConfig& cfg)
{
    cfg.version = "v1-default";

    cfg.gb_register = BuildDefaultGbRegisterParam();

    cfg.gb_live.transport = "udp";
    cfg.gb_live.target_ip = "127.0.0.1";
    cfg.gb_live.target_port = 30000;
    cfg.gb_live.video_stream_id = "main";
    cfg.gb_live.video_codec = "h265";
    cfg.gb_live.audio_codec = "g711a";
    cfg.gb_live.mtu = 1200;
    cfg.gb_live.payload_type = 96;
    cfg.gb_live.ssrc = 0;
    cfg.gb_live.clock_rate = 90000;
    cfg.gb_video.main_codec = "H.265";
    cfg.gb_video.main_resolution = "3840x1080";
    cfg.gb_video.main_fps = 25;
    cfg.gb_video.main_bitrate_kbps = 2048;
    cfg.gb_video.sub_codec = "H.265";
    cfg.gb_video.sub_resolution = "640x360";
    cfg.gb_video.sub_fps = 15;
    cfg.gb_video.sub_bitrate_kbps = 512;
    cfg.gb_image.flip_mode = "close";

    cfg.gat_register = BuildDefaultGatRegisterParam();
    cfg.gat_upload.queue_dir = "/tmp/gat1400_queue";
    cfg.gat_upload.max_pending_count = 200;
    cfg.gat_upload.replay_interval_sec = 15;
    cfg.gat_upload.enable_apes_post_compat = 0;

    cfg.gb_talk.codec = "g711a";
    cfg.gb_talk.recv_port = 30003;
    cfg.gb_talk.sample_rate = 8000;
    cfg.gb_talk.jitter_buffer_ms = 80;
    cfg.gb_reboot.cooldown_sec = 60;
    cfg.gb_reboot.require_auth_level = 1;
    cfg.gb_upgrade.url_whitelist.clear();
    cfg.gb_upgrade.max_package_mb = 128;
    cfg.gb_upgrade.verify_mode = "md5";
    cfg.gb_upgrade.timeout_sec = 120;

    cfg.gb_broadcast.input_mode = "stream";
    cfg.gb_broadcast.codec = "g711a";
    cfg.gb_broadcast.recv_port = 30001;
    cfg.gb_broadcast.file_cache_dir = "/tmp";
    cfg.gb_broadcast.transport = "tcp";

    cfg.gb_listen.transport = "udp";
    cfg.gb_listen.target_ip = "127.0.0.1";
    cfg.gb_listen.target_port = 30002;
    cfg.gb_listen.codec = "g711a";
    cfg.gb_listen.sample_rate = 8000;
}

bool ReadIniString(CInifile& ini,
                   const char* section,
                   const char* key,
                   const char* path,
                   std::string& out)
{
    char value[256] = {0};
    if (ini.read_profile_string(section, key, value, sizeof(value), path) != 0) {
        return false;
    }
    out = value;
    return true;
}

bool ReadIniInt(CInifile& ini,
                const char* section,
                const char* key,
                const char* path,
                int& out)
{
    char value[256] = {0};
    if (ini.read_profile_string(section, key, value, sizeof(value), path) != 0) {
        return false;
    }
    out = atoi(value);
    return true;
}

void NormalizeGbRegisterConfig(protocol::GbRegisterParam& param)
{
    param.register_mode = protocol::NormalizeGbRegisterMode(param.register_mode);
}

bool LoadGbRegisterConfigFromFile(const char* path, protocol::GbRegisterParam& out)
{
    if (path == NULL || access(path, F_OK) != 0) {
        return false;
    }

    CInifile ini;
    int ivalue = 0;
    if (ReadIniInt(ini, kLocalGbConfigSection, "enable", path, ivalue)) {
        out.enabled = (ivalue != 0) ? 1 : 0;
    }
    ReadIniString(ini, kLocalGbConfigSection, "register_mode", path, out.register_mode);
    ReadIniString(ini, kLocalGbConfigSection, "username", path, out.username);
    ReadIniString(ini, kLocalGbConfigSection, "server_ip", path, out.server_ip);
    ReadIniInt(ini, kLocalGbConfigSection, "server_port", path, out.server_port);
    ReadIniString(ini, kLocalGbConfigSection, "device_id", path, out.device_id);
    ReadIniString(ini, kLocalGbConfigSection, "password", path, out.password);
    NormalizeGbRegisterConfig(out);
    return true;
}

bool LoadGbZeroConfigFromFile(const char* path, protocol::GbRegisterParam& out)
{
    if (path == NULL || access(path, F_OK) != 0) {
        return false;
    }

    CInifile ini;
    bool loaded = false;
    loaded = ReadIniString(ini, kLocalZeroConfigSection, "string_code", path, out.string_code) || loaded;
    loaded = ReadIniString(ini, kLocalZeroConfigSection, "mac_address", path, out.mac_address) || loaded;
    NormalizeGbRegisterConfig(out);
    return loaded;
}

bool LoadGatRegisterConfigFromFile(const char* path, protocol::GatRegisterParam& out)
{
    if (path == NULL || access(path, F_OK) != 0) {
        return false;
    }

    CInifile ini;
    ReadIniString(ini, kLocalGatConfigSection, "scheme", path, out.scheme);
    ReadIniString(ini, kLocalGatConfigSection, "server_ip", path, out.server_ip);
    ReadIniInt(ini, kLocalGatConfigSection, "server_port", path, out.server_port);
    ReadIniString(ini, kLocalGatConfigSection, "base_path", path, out.base_path);
    ReadIniString(ini, kLocalGatConfigSection, "device_id", path, out.device_id);
    ReadIniString(ini, kLocalGatConfigSection, "username", path, out.username);
    ReadIniString(ini, kLocalGatConfigSection, "password", path, out.password);
    ReadIniString(ini, kLocalGatConfigSection, "auth_method", path, out.auth_method);
    ReadIniInt(ini, kLocalGatConfigSection, "listen_port", path, out.listen_port);
    ReadIniInt(ini, kLocalGatConfigSection, "expires_sec", path, out.expires_sec);
    ReadIniInt(ini, kLocalGatConfigSection, "keepalive_interval_sec", path, out.keepalive_interval_sec);
    ReadIniInt(ini, kLocalGatConfigSection, "max_retry", path, out.max_retry);
    ReadIniInt(ini, kLocalGatConfigSection, "request_timeout_ms", path, out.request_timeout_ms);
    ReadIniString(ini, kLocalGatConfigSection, "retry_backoff_policy", path, out.retry_backoff_policy);
    return true;
}

LocalConfigLoadSource LoadExistingGbRegisterConfig(protocol::GbRegisterParam& out)
{
    if (LoadGbRegisterConfigFromFile(kLocalGbConfigFile, out)) {
        return kLocalConfigLoadCurrent;
    }
    return kLocalConfigLoadNone;
}

LocalConfigLoadSource LoadExistingGbZeroConfig(protocol::GbRegisterParam& out)
{
    if (LoadGbZeroConfigFromFile(kLocalZeroConfigFile, out)) {
        return kLocalConfigLoadCurrent;
    }
    return kLocalConfigLoadNone;
}

LocalConfigLoadSource LoadExistingGatRegisterConfig(protocol::GatRegisterParam& out)
{
    if (LoadGatRegisterConfigFromFile(kLocalGatConfigFile, out)) {
        return kLocalConfigLoadCurrent;
    }
    return kLocalConfigLoadNone;
}

int SaveLocalGbConfigFile(const protocol::GbRegisterParam& param)
{
    protocol::GbRegisterParam normalized = param;
    NormalizeGbRegisterConfig(normalized);

    if (!EnsureDirectoryExists(kLocalConfigDir)) {
        return -1;
    }

    FILE* fp = fopen(kLocalGbConfigFile, "w");
    if (fp == NULL) {
        return -2;
    }

    fprintf(fp, "[%s]\n", kLocalGbConfigSection);
    fprintf(fp, "enable=%d\n", normalized.enabled != 0 ? 1 : 0);
    fprintf(fp, "register_mode=%s\n", protocol::NormalizeGbRegisterMode(normalized.register_mode).c_str());
    fprintf(fp, "username=%s\n", normalized.username.c_str());
    fprintf(fp, "server_ip=%s\n", normalized.server_ip.c_str());
    fprintf(fp, "server_port=%d\n", normalized.server_port);
    fprintf(fp, "device_id=%s\n", normalized.device_id.c_str());
    fprintf(fp, "password=%s\n", normalized.password.c_str());

    const int flushRet = fflush(fp);
    const int closeRet = fclose(fp);
    if (flushRet != 0 || closeRet != 0) {
        return -3;
    }

    return 0;
}

int SaveLocalGbZeroConfigFile(const protocol::GbRegisterParam& param)
{
    protocol::GbRegisterParam normalized = param;
    NormalizeGbRegisterConfig(normalized);

    if (!EnsureDirectoryExists(kLocalConfigDir)) {
        return -1;
    }

    FILE* fp = fopen(kLocalZeroConfigFile, "w");
    if (fp == NULL) {
        return -2;
    }

    fprintf(fp, "[%s]\n", kLocalZeroConfigSection);
    fprintf(fp, "string_code=%s\n", normalized.string_code.c_str());
    fprintf(fp, "mac_address=%s\n", normalized.mac_address.c_str());

    const int flushRet = fflush(fp);
    const int closeRet = fclose(fp);
    if (flushRet != 0 || closeRet != 0) {
        return -3;
    }

    return 0;
}

int SaveLocalGatConfigFile(const protocol::GatRegisterParam& param)
{
    if (!EnsureDirectoryExists(kLocalConfigDir)) {
        return -1;
    }

    FILE* fp = fopen(kLocalGatConfigFile, "w");
    if (fp == NULL) {
        return -2;
    }

    fprintf(fp, "[%s]\n", kLocalGatConfigSection);
    fprintf(fp, "scheme=%s\n", param.scheme.c_str());
    fprintf(fp, "server_ip=%s\n", param.server_ip.c_str());
    fprintf(fp, "server_port=%d\n", param.server_port);
    fprintf(fp, "base_path=%s\n", param.base_path.c_str());
    fprintf(fp, "device_id=%s\n", param.device_id.c_str());
    fprintf(fp, "username=%s\n", param.username.c_str());
    fprintf(fp, "password=%s\n", param.password.c_str());
    fprintf(fp, "auth_method=%s\n", param.auth_method.c_str());
    fprintf(fp, "listen_port=%d\n", param.listen_port);
    fprintf(fp, "expires_sec=%d\n", param.expires_sec);
    fprintf(fp, "keepalive_interval_sec=%d\n", param.keepalive_interval_sec);
    fprintf(fp, "max_retry=%d\n", param.max_retry);
    fprintf(fp, "request_timeout_ms=%d\n", param.request_timeout_ms);
    fprintf(fp, "retry_backoff_policy=%s\n", param.retry_backoff_policy.c_str());

    const int flushRet = fflush(fp);
    const int closeRet = fclose(fp);
    if (flushRet != 0 || closeRet != 0) {
        return -3;
    }

    return 0;
}

const char* DescribeLocalConfigSource(LocalConfigLoadSource source)
{
    switch (source) {
        case kLocalConfigLoadCurrent:
            return "current";
        default:
            return "default";
    }
}

LocalConfigSyncState SyncLocalConfigFiles(protocol::ProtocolExternalConfig& cfg)
{
    LocalConfigSyncState state;

    state.gb_source = LoadExistingGbRegisterConfig(cfg.gb_register);
    NormalizeGbRegisterConfig(cfg.gb_register);
    if (state.gb_source != kLocalConfigLoadCurrent) {
        state.gb_sync_ret = SaveLocalGbConfigFile(cfg.gb_register);
    }

    state.zero_source = LoadExistingGbZeroConfig(cfg.gb_register);
    NormalizeGbRegisterConfig(cfg.gb_register);
    if (IsZeroConfigFileMissing(state, cfg.gb_register)) {
        state.zero_sync_ret = RequiredZeroConfigFileError();
    }
    ApplyGbRuntimeZeroConfigFields(cfg.gb_register);

    state.gat_source = LoadExistingGatRegisterConfig(cfg.gat_register);
    if (state.gat_source != kLocalConfigLoadCurrent) {
        state.gat_sync_ret = SaveLocalGatConfigFile(cfg.gat_register);
    }

    return state;
}

}

namespace protocol
{

LocalConfigProvider::LocalConfigProvider(const std::string& sourceTag)
    : m_source_tag(sourceTag)
{
    InitDefaultConfig();
    ProtocolExternalConfig next = m_cached_cfg;
    const LocalConfigSyncState sync = SyncLocalConfigFiles(next);
    m_cached_cfg = next;
    if (IsZeroConfigFileMissing(sync, m_cached_cfg.gb_register)) {
        LogZeroConfigFileMissing(m_source_tag.c_str());
    } else if (sync.gb_source != kLocalConfigLoadNone ||
               sync.zero_source != kLocalConfigLoadNone ||
               sync.gat_source != kLocalConfigLoadNone) {
        m_cached_cfg.version = "v1-local-file";
        printf("[Protocol][Config] module=config event=config_file_load_success trace=provider gb_path=%s gb_source=%s gb_ret=%d zero_path=%s zero_source=%s zero_ret=%d gat_path=%s gat_source=%s gat_ret=%d tag=%s\n",
               kLocalGbConfigFile,
               DescribeLocalConfigSource(sync.gb_source),
               sync.gb_sync_ret,
               kLocalZeroConfigFile,
               DescribeLocalConfigSource(sync.zero_source),
               sync.zero_sync_ret,
               kLocalGatConfigFile,
               DescribeLocalConfigSource(sync.gat_source),
               sync.gat_sync_ret,
               m_source_tag.c_str());
    } else {
        printf("[Protocol][Config] module=config event=config_file_init trace=provider gb_path=%s gb_ret=%d zero_path=%s zero_ret=%d gat_path=%s gat_ret=%d tag=%s\n",
               kLocalGbConfigFile,
               sync.gb_sync_ret,
               kLocalZeroConfigFile,
               sync.zero_sync_ret,
               kLocalGatConfigFile,
               sync.gat_sync_ret,
               m_source_tag.c_str());
    }
}

LocalConfigProvider::~LocalConfigProvider()
{
}

GbRegisterParam LocalConfigProvider::BuildDefaultGbRegisterConfig()
{
    return BuildDefaultGbRegisterParam();
}

GbZeroConfigParam LocalConfigProvider::BuildDefaultGbZeroConfig()
{
    return BuildDefaultGbZeroConfigParam();
}

int LocalConfigProvider::LoadGbZeroConfig(GbZeroConfigParam& out)
{
    out = BuildDefaultGbZeroConfig();

    GbRegisterParam loaded = BuildDefaultGbRegisterConfig();
    if (LoadExistingGbZeroConfig(loaded) != kLocalConfigLoadCurrent) {
        return -1;
    }

    out.string_code = loaded.string_code;
    out.mac_address = loaded.mac_address;
    return 0;
}

int LocalConfigProvider::LoadOrCreateGbRegisterConfig(GbRegisterParam& out)
{
    out = BuildDefaultGbRegisterConfig();
    const LocalConfigLoadSource gbSource = LoadExistingGbRegisterConfig(out);
    const LocalConfigLoadSource zeroSource = LoadExistingGbZeroConfig(out);
    if (gbSource != kLocalConfigLoadCurrent) {
        const int saveGbRet = SaveLocalGbConfigFile(out);
        if (saveGbRet != 0) {
            return saveGbRet;
        }
    }

    if (IsZeroConfigFileRequired(out) && zeroSource != kLocalConfigLoadCurrent) {
        LogZeroConfigFileMissing("LoadOrCreateGbRegisterConfig");
        return RequiredZeroConfigFileError();
    }

    return 0;
}

int LocalConfigProvider::LoadOrCreateGbRuntimeRegisterConfig(GbRegisterParam& out)
{
    const int ret = LoadOrCreateGbRegisterConfig(out);
    if (ret != 0) {
        return ret;
    }

    ApplyGbRuntimeZeroConfigFields(out);
    return 0;
}

int LocalConfigProvider::UpdateGbRegisterConfig(const GbRegisterParam& param)
{
    GbRegisterParam next = BuildDefaultGbRegisterConfig();
    LoadExistingGbRegisterConfig(next);
    LoadExistingGbZeroConfig(next);
    ApplyGbRegisterEditableFields(next, param);
    NormalizeGbRegisterConfig(next);
    const int check = ValidateGbRegisterEditableFields(next);
    if (check != 0) {
        return check;
    }

    const int saveGbRet = SaveLocalGbConfigFile(next);
    if (saveGbRet != 0) {
        return saveGbRet;
    }

    return 0;
}

int LocalConfigProvider::UpdateGbZeroConfig(const GbZeroConfigParam& param)
{
    GbRegisterParam next = BuildDefaultGbRegisterConfig();
    LoadExistingGbRegisterConfig(next);
    LoadExistingGbZeroConfig(next);
    ApplyGbZeroConfigEditableFields(next, param);
    NormalizeGbRegisterConfig(next);
    const int check = ValidateGbRegisterEditableFields(next);
    if (check != 0) {
        return check;
    }

    return SaveLocalGbZeroConfigFile(next);
}

GatRegisterParam LocalConfigProvider::BuildDefaultGatRegisterConfig()
{
    return BuildDefaultGatRegisterParam();
}

int LocalConfigProvider::LoadOrCreateGatRegisterConfig(GatRegisterParam& out)
{
    out = BuildDefaultGatRegisterConfig();
    const LocalConfigLoadSource source = LoadExistingGatRegisterConfig(out);
    if (source == kLocalConfigLoadCurrent) {
        return 0;
    }
    return SaveLocalGatConfigFile(out);
}

int LocalConfigProvider::UpdateGatRegisterConfig(const GatRegisterParam& param)
{
    GatRegisterParam next = BuildDefaultGatRegisterConfig();
    LoadExistingGatRegisterConfig(next);
    ApplyGatRegisterEditableFields(next, param);
    const int check = ValidateGatRegisterEditableFields(next);
    if (check != 0) {
        return check;
    }

    return SaveLocalGatConfigFile(next);
}

void LocalConfigProvider::InitDefaultConfig()
{
    InitDefaultLocalConfig(m_cached_cfg);
}

int LocalConfigProvider::PullLatest(ProtocolExternalConfig& out)
{
    ProtocolExternalConfig next = m_cached_cfg;
    const LocalConfigSyncState sync = SyncLocalConfigFiles(next);
    if (IsZeroConfigFileMissing(sync, next.gb_register)) {
        LogZeroConfigFileMissing(m_source_tag.c_str());
        return RequiredZeroConfigFileError();
    }

    if (sync.gb_source != kLocalConfigLoadNone ||
        sync.zero_source != kLocalConfigLoadNone ||
        sync.gat_source != kLocalConfigLoadNone) {
        next.version = "v1-local-file";
        m_cached_cfg = next;
    }

    if (sync.gb_source == kLocalConfigLoadNone ||
        sync.zero_source == kLocalConfigLoadNone ||
        sync.gat_source == kLocalConfigLoadNone) {
        printf("[Protocol][Config] module=config event=config_file_missing trace=provider gb_path=%s gb_ret=%d zero_path=%s zero_ret=%d gat_path=%s gat_ret=%d tag=%s\n",
               kLocalGbConfigFile,
               sync.gb_sync_ret,
               kLocalZeroConfigFile,
               sync.zero_sync_ret,
               kLocalGatConfigFile,
               sync.gat_sync_ret,
               m_source_tag.c_str());
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

    ProtocolExternalConfig normalized = cfg;
    NormalizeGbRegisterConfig(normalized.gb_register);
    m_cached_cfg = normalized;

    GbRegisterParam persistedGb = BuildDefaultGbRegisterConfig();
    LoadExistingGbRegisterConfig(persistedGb);
    LoadExistingGbZeroConfig(persistedGb);
    const std::string storedServerIp = persistedGb.server_ip;
    const int storedServerPort = persistedGb.server_port;
    ApplyGbRegisterEditableFields(persistedGb, normalized.gb_register);
    if (protocol::IsGbRegisterModeZeroConfig(normalized.gb_register)) {
        persistedGb.server_ip = storedServerIp;
        persistedGb.server_port = storedServerPort;
    }
    persistedGb.string_code = normalized.gb_register.string_code;
    persistedGb.mac_address = normalized.gb_register.mac_address;

    const int saveGbRet = SaveLocalGbConfigFile(persistedGb);
    if (saveGbRet != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d source=local stage=persist version=%s tag=%s path=%s\n",
               saveGbRet,
               cfg.version.c_str(),
               m_source_tag.c_str(),
               kLocalGbConfigFile);
        return saveGbRet;
    }

    const int saveZeroRet = SaveLocalGbZeroConfigFile(persistedGb);
    if (saveZeroRet != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d source=local stage=persist version=%s tag=%s path=%s\n",
               saveZeroRet,
               cfg.version.c_str(),
               m_source_tag.c_str(),
               kLocalZeroConfigFile);
        return saveZeroRet;
    }

    const int saveGatRet = SaveLocalGatConfigFile(m_cached_cfg.gat_register);
    if (saveGatRet != 0) {
        printf("[Protocol][Config] module=config event=config_apply_fail trace=provider error=%d source=local stage=persist version=%s tag=%s path=%s\n",
               saveGatRet,
               cfg.version.c_str(),
               m_source_tag.c_str(),
               kLocalGatConfigFile);
        return saveGatRet;
    }

    printf("[Protocol][Config] module=config event=config_apply_success trace=provider error=0 source=local version=%s tag=%s\n",
           cfg.version.c_str(),
           m_source_tag.c_str());
    return 0;
}

int LocalConfigProvider::Validate(const ProtocolExternalConfig& cfg)
{
    if (!protocol::IsValidGbRegisterMode(cfg.gb_register.register_mode)) {
        LogConfigValidateFail(cfg, kInvalidGbRegisterModeError, "gb_register_mode");
        return kInvalidGbRegisterModeError;
    }

    if (cfg.gb_register.enabled != 0) {
        if (cfg.gb_register.server_ip.empty() || cfg.gb_register.server_port <= 0) {
            LogConfigValidateFail(cfg, -1, "gb_register_endpoint");
            return -1;
        }

        if (protocol::IsGbRegisterModeZeroConfig(cfg.gb_register) &&
            cfg.gb_register.string_code.empty()) {
            LogConfigValidateFail(cfg, -27, "gb_register_string_code");
            return -27;
        }

        if (protocol::IsGbRegisterModeZeroConfig(cfg.gb_register) &&
            cfg.gb_register.redirect_server_id.empty()) {
            LogConfigValidateFail(cfg, -28, "gb_register_redirect_server_id");
            return -28;
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

    if (cfg.gb_reboot.cooldown_sec < 0 || cfg.gb_reboot.require_auth_level < 0) {
        LogConfigValidateFail(cfg, -22, "gb_reboot");
        return -22;
    }

    const std::string verifyMode = TrimCopy(cfg.gb_upgrade.verify_mode);
    if (cfg.gb_upgrade.max_package_mb <= 0 || cfg.gb_upgrade.timeout_sec <= 0 ||
        (verifyMode != "md5" && verifyMode != "sha256")) {
        LogConfigValidateFail(cfg, -23, "gb_upgrade");
        return -23;
    }

    return 0;
}

}
