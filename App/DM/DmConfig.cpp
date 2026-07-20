#include "DmConfig.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace dm
{
namespace
{

std::string TrimCopy(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' ||
            value[begin] == '\r' || value[begin] == '\n')) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' ||
            value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool EnsureDirectoryExists(const std::string& filePath)
{
    const size_t slash = filePath.rfind('/');
    if (slash == std::string::npos) {
        return true;
    }

    std::string dir = filePath.substr(0, slash + 1);
    std::string current;
    for (size_t i = 0; i < dir.size(); ++i) {
        current.push_back(dir[i]);
        if (dir[i] != '/') {
            continue;
        }

        if (current.empty() || access(current.c_str(), F_OK) == 0) {
            continue;
        }
        if (mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

}

namespace
{

void SetIfNotEmpty(std::string& target, const std::string& value)
{
    if (!value.empty()) {
        target = value;
    }
}

int ToInt(const std::string& value, int fallback)
{
    if (value.empty()) {
        return fallback;
    }
    return atoi(value.c_str());
}

bool IsPlaceholder(const std::string& value)
{
    return value.empty() || value == kDmNoValue;
}

bool ContainsEndpointSeparator(const std::string& value)
{
    return value.find("||") != std::string::npos;
}

const char* const kKnownDeviceValueKeys[] = {
    "imsi",
    "imsi2",
    "sn",
    "mac",
    "rom",
    "ram",
    "cpu",
    "sysVersion",
    "softwareVer",
    "softwareName",
    "volte",
    "netType",
    "phoneNumber",
    "batteryCapacity",
    "batteryCapacityCurr",
    "screenSize",
    "networkStatus",
    "wearingStatus",
    "routerMac",
    "bluetoothMac",
    "gpu",
    "board",
    "resolution",
};

const size_t kKnownDeviceValueKeyCount =
    sizeof(kKnownDeviceValueKeys) / sizeof(kKnownDeviceValueKeys[0]);

std::string NormalizeOptional(const std::string& value)
{
    return value.empty() ? std::string(kDmNoValue) : value;
}

std::string DeviceValueOrDefault(const DmConfig& cfg, const std::string& key)
{
    std::map<std::string, std::string>::const_iterator it = cfg.device_values.find(key);
    if (it == cfg.device_values.end() || it->second.empty()) {
        return kDmNoValue;
    }
    return it->second;
}

bool IsKnownDeviceValueKey(const std::string& key)
{
    for (size_t i = 0; i < kKnownDeviceValueKeyCount; ++i) {
        if (key == kKnownDeviceValueKeys[i]) {
            return true;
        }
    }
    return false;
}

void WriteDeviceValue(std::ofstream& out, const DmConfig& cfg, const std::string& key)
{
    out << "device_" << key << "=" << DeviceValueOrDefault(cfg, key) << "\n";
}

void WriteDeviceValues(std::ofstream& out, const DmConfig& cfg)
{
    for (size_t i = 0; i < kKnownDeviceValueKeyCount; ++i) {
        WriteDeviceValue(out, cfg, kKnownDeviceValueKeys[i]);
    }

    for (std::map<std::string, std::string>::const_iterator it = cfg.device_values.begin();
         it != cfg.device_values.end();
         ++it) {
        if (!IsKnownDeviceValueKey(it->first)) {
            WriteDeviceValue(out, cfg, it->first);
        }
    }
}

void NormalizeDmConfigForSave(DmConfig& cfg)
{
    cfg.enabled = cfg.enabled != 0 ? 1 : 0;
    if (cfg.server_uri.empty()) {
        cfg.server_uri = kDmDefaultCommercialServerUri;
    }
    if (cfg.api_version.empty()) {
        cfg.api_version = kDmDefaultApiVersion;
    }
    if (cfg.api_type.empty()) {
        cfg.api_type = kDmDefaultApiType;
    }
    cfg.sdk_version = NormalizeOptional(cfg.sdk_version);
    cfg.imei2 = NormalizeOptional(cfg.imei2);
}

void ApplyKeyValue(DmConfig& cfg, const std::string& rawKey, const std::string& rawValue)
{
    const std::string key = TrimCopy(rawKey);
    const std::string value = TrimCopy(rawValue);

    if (key == "enabled") {
        cfg.enabled = ToInt(value, cfg.enabled) != 0 ? 1 : 0;
    } else if (key == "server_uri") {
        SetIfNotEmpty(cfg.server_uri, value);
    } else if (key == "local_port") {
        cfg.local_port = ToInt(value, cfg.local_port);
    } else if (key == "lifetime_sec") {
        cfg.lifetime_sec = ToInt(value, cfg.lifetime_sec);
    } else if (key == "short_server_id") {
        cfg.short_server_id = ToInt(value, cfg.short_server_id);
    } else if (key == "startup_retry_interval_sec") {
        cfg.startup_retry_interval_sec = ToInt(value, cfg.startup_retry_interval_sec);
    } else if (key == "brand") {
        SetIfNotEmpty(cfg.brand, value);
    } else if (key == "model") {
        SetIfNotEmpty(cfg.model, value);
    } else if (key == "app_key") {
        SetIfNotEmpty(cfg.app_key, value);
    } else if (key == "sdk_version") {
        SetIfNotEmpty(cfg.sdk_version, value);
    } else if (key == "api_version") {
        SetIfNotEmpty(cfg.api_version, value);
    } else if (key == "api_type") {
        SetIfNotEmpty(cfg.api_type, value);
    } else if (key == "template_id") {
        SetIfNotEmpty(cfg.template_id, value);
    } else if (key == "imei1") {
        SetIfNotEmpty(cfg.imei1, value);
    } else if (key == "imei2") {
        SetIfNotEmpty(cfg.imei2, value);
    } else if (key == "secret") {
        SetIfNotEmpty(cfg.secret, value);
    } else if (key.size() > 7 && key.compare(0, 7, "device_") == 0) {
        cfg.device_values[key.substr(7)] = value.empty() ? std::string(kDmNoValue) : value;
    }
}

int WriteDefaultConfig(const DmConfig& cfg, const std::string& path)
{
    if (!EnsureDirectoryExists(path)) {
        return -1;
    }

    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return -2;
    }

    out << "[dm]\n";
    out << "enabled=" << cfg.enabled << "\n";
    out << "server_uri=" << cfg.server_uri << "\n";
    out << "local_port=" << cfg.local_port << "\n";
    out << "lifetime_sec=" << cfg.lifetime_sec << "\n";
    out << "short_server_id=" << cfg.short_server_id << "\n";
    out << "startup_retry_interval_sec=" << cfg.startup_retry_interval_sec << "\n";
    out << "brand=" << cfg.brand << "\n";
    out << "model=" << cfg.model << "\n";
    out << "app_key=" << cfg.app_key << "\n";
    out << "sdk_version=" << cfg.sdk_version << "\n";
    out << "api_version=" << cfg.api_version << "\n";
    out << "api_type=" << cfg.api_type << "\n";
    out << "template_id=" << cfg.template_id << "\n";
    out << "imei1=" << cfg.imei1 << "\n";
    out << "imei2=" << cfg.imei2 << "\n";
    out << "secret=" << cfg.secret << "\n";
    WriteDeviceValues(out, cfg);
    return out.good() ? 0 : -3;
}

}

DmConfig::DmConfig()
    : enabled(1),
      server_uri(kDmDefaultCommercialServerUri),
      local_port(0),
      lifetime_sec(86400),
      short_server_id(123),
      startup_retry_interval_sec(60),
      brand(kDmDefaultBrand),
      model(kDmDefaultModel),
      app_key(kDmDefaultAppkey),
      sdk_version(kDmNoValue),
      api_version(kDmDefaultApiVersion),
      api_type(kDmDefaultApiType),
      template_id(kDmDefaultTemplateId),
      imei1(kDmNoValue),
      imei2(kDmNoValue),
      secret(kDmDefaultSecret)
{
    device_values["sysVersion"] = "5.10.160";
    device_values["softwareVer"] = "2.0.0";
}

int LoadDmConfig(DmConfig& out, const std::string& path)
{
    out = DmConfig();

    if (access(path.c_str(), F_OK) != 0) {
        return WriteDefaultConfig(out, path);
    }

    std::ifstream in(path.c_str());
    if (!in.is_open()) {
        return -1;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = TrimCopy(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') {
            continue;
        }

        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        ApplyKeyValue(out, trimmed.substr(0, eq), trimmed.substr(eq + 1));
    }

    out.sdk_version = NormalizeOptional(out.sdk_version);
    out.imei2 = NormalizeOptional(out.imei2);
    return 0;
}

int GetDmConfig(DmConfig& out, const std::string& path)
{
    return LoadDmConfig(out, path);
}

int SetDmConfig(const DmConfig& cfg, const std::string& path)
{
    DmConfig next = cfg;
    NormalizeDmConfigForSave(next);

    std::string reason;
    if (!ValidateDmConfig(next, reason)) {
        return -4;
    }

    return WriteDefaultConfig(next, path);
}

int SaveDmServerUri(const std::string& serverUri, const std::string& path)
{
    DmConfig cfg;
    const int ret = LoadDmConfig(cfg, path);
    if (ret != 0) {
        return ret;
    }
    cfg.server_uri = serverUri;
    return SetDmConfig(cfg, path);
}

std::string BuildDmEndpoint(const DmConfig& cfg)
{
    std::vector<std::string> parts;
    parts.push_back(cfg.brand);
    parts.push_back(cfg.model);
    parts.push_back(cfg.app_key);
    parts.push_back(NormalizeOptional(cfg.sdk_version));
    parts.push_back(cfg.api_version.empty() ? std::string(kDmDefaultApiVersion) : cfg.api_version);
    parts.push_back(cfg.api_type.empty() ? std::string(kDmDefaultApiType) : cfg.api_type);
    parts.push_back(cfg.template_id);
    parts.push_back(cfg.imei1);
    parts.push_back(NormalizeOptional(cfg.imei2));
    parts.push_back(kDmNoValue);
    parts.push_back(kDmNoValue);
    parts.push_back(kDmNoValue);

    std::ostringstream endpoint;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            endpoint << "||";
        }
        endpoint << parts[i];
    }
    return endpoint.str();
}

bool ValidateDmConfig(const DmConfig& cfg, std::string& reason)
{
    if (cfg.enabled == 0) {
        return true;
    }
    if (cfg.server_uri.find("coap://") != 0) {
        reason = "server_uri must start with coap://";
        return false;
    }
    if (cfg.local_port < 0 || cfg.lifetime_sec <= 0 || cfg.short_server_id <= 0) {
        reason = "local_port/lifetime_sec/short_server_id invalid";
        return false;
    }
    if (IsPlaceholder(cfg.brand) || IsPlaceholder(cfg.model) ||
        IsPlaceholder(cfg.app_key) || IsPlaceholder(cfg.template_id) ||
        IsPlaceholder(cfg.imei1) || IsPlaceholder(cfg.secret)) {
        reason = "brand/model/app_key/template_id/imei1/secret required";
        return false;
    }
    if (ContainsEndpointSeparator(cfg.brand) ||
        ContainsEndpointSeparator(cfg.model) ||
        ContainsEndpointSeparator(cfg.app_key) ||
        ContainsEndpointSeparator(cfg.sdk_version) ||
        ContainsEndpointSeparator(cfg.api_version) ||
        ContainsEndpointSeparator(cfg.api_type) ||
        ContainsEndpointSeparator(cfg.template_id) ||
        ContainsEndpointSeparator(cfg.imei1) ||
        ContainsEndpointSeparator(cfg.imei2)) {
        reason = "endpoint fields must not contain ||";
        return false;
    }
    if (BuildDmEndpoint(cfg).find("||") == std::string::npos) {
        reason = "endpoint separator invalid";
        return false;
    }
    return true;
}

std::string GetDmDeviceValue(const DmConfig& cfg, const std::string& field)
{
    if (field == "sn" && cfg.device_values.find("sn") == cfg.device_values.end()) {
        return cfg.imei1;
    }

    std::map<std::string, std::string>::const_iterator it = cfg.device_values.find(field);
    if (it == cfg.device_values.end() || it->second.empty()) {
        return kDmNoValue;
    }
    return it->second;
}

}
