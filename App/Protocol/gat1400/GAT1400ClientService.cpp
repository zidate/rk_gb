#include "GAT1400ClientService.h"
#include "ProtocolManager.h"
#include "ProtocolLog.h"
#include "Base64Coder.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <functional>
#include <iterator>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "GAT1400Json.h"
#include "http_auth.h"

#define printf protocol::ProtocolPrintf

namespace protocol
{
namespace
{

static const int kHttpTimeoutMs = 5000;
static const int kAcceptWaitMs = 500;
static const char* kJsonContentType = "application/VIID+json;charset=UTF-8";
static const char* kBinaryContentType = "application/octet-stream";
static const char* kPendingUploadFileSuffix = ".req";
static const char* kResponseKindList = "list";
static const char* kResponseKindStatus = "status";
static const int kClientErrorUnsupportedUri = -6;
static const int kClientErrorApePostCompatDisabled = -7;
static const char* kKeepaliveDemoImagePath = "/mnt/sdcard/test.jpeg";

struct RequestTarget
{
    std::string scheme;
    std::string host;
    int port;
    std::string request_path;
    int timeout_ms;

    RequestTarget() : scheme("http"), port(80), request_path("/"), timeout_ms(kHttpTimeoutMs) {}
};

std::string ToLowerCopy(const std::string& input)
{
    std::string output(input);
    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return output;
}

std::string TrimCopy(const std::string& input)
{
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(begin, end - begin);
}

void CopyToArray(char* dst, size_t dstSize, const std::string& value)
{
    if (dst == NULL || dstSize == 0) {
        return;
    }
    memset(dst, 0, dstSize);
    if (!value.empty()) {
        strncpy(dst, value.c_str(), dstSize - 1);
    }
}

std::string ResolveGbRuntimeDeviceId(const GbRegisterParam& gbRegister)
{
    if (!gbRegister.device_id.empty()) {
        return gbRegister.device_id;
    }
    return gbRegister.username;
}

std::string ResolveGatRuntimeDeviceId(const ProtocolExternalConfig& cfg,
                                      const GbRegisterParam* gbRegisterOverride = NULL)
{
    if (!cfg.gat_register.device_id.empty()) {
        return cfg.gat_register.device_id;
    }

    if (gbRegisterOverride != NULL) {
        const std::string gbDeviceId = ResolveGbRuntimeDeviceId(*gbRegisterOverride);
        if (!gbDeviceId.empty()) {
            return gbDeviceId;
        }
    } else {
        const ProtocolManager* manager = ProtocolManager::InstanceIfCreated();
        if (manager != NULL) {
            const std::string gbDeviceId = ResolveGbRuntimeDeviceId(manager->GetGbRegisterConfig());
            if (!gbDeviceId.empty()) {
                return gbDeviceId;
            }
        }
    }

    return cfg.gat_register.username;
}

std::string FormatCurrentTime()
{
    char buffer[32] = {0};
    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &tmNow);
    return std::string(buffer);
}

bool IsEmptyCString(const char* value)
{
    return value == NULL || value[0] == '\0';
}

bool ResolveCaptureSourceId(const media::GAT1400CaptureEvent& event, std::string& sourceId)
{
    if (!event.image_list.empty() && !IsEmptyCString(event.image_list.front().ImageInfo.ImageID)) {
        sourceId = event.image_list.front().ImageInfo.ImageID;
        return true;
    }
    if (!event.video_slice_list.empty() && !IsEmptyCString(event.video_slice_list.front().VideoSliceInfo.VideoID)) {
        sourceId = event.video_slice_list.front().VideoSliceInfo.VideoID;
        return true;
    }
    if (!event.file_list.empty() && !IsEmptyCString(event.file_list.front().FileInfo.FileID)) {
        sourceId = event.file_list.front().FileInfo.FileID;
        return true;
    }
    return false;
}

bool ResolveCaptureDeviceId(const media::GAT1400CaptureEvent& event, std::string& deviceId)
{
    if (!event.image_list.empty() && !IsEmptyCString(event.image_list.front().ImageInfo.DeviceID)) {
        deviceId = event.image_list.front().ImageInfo.DeviceID;
        return true;
    }
    if (!event.video_slice_list.empty() && !IsEmptyCString(event.video_slice_list.front().VideoSliceInfo.DeviceID)) {
        deviceId = event.video_slice_list.front().VideoSliceInfo.DeviceID;
        return true;
    }
    return false;
}

void BindFaceCaptureIfNeeded(const GAT_1400_Face& face, GAT_1400_ImageSet& imageSet)
{
    if (imageSet.FaceList.empty()) {
        imageSet.FaceList.push_back(face);
    }
}

void BindFaceCaptureIfNeeded(const GAT_1400_Face& face, GAT_1400_VideoSliceSet& videoSliceSet)
{
    if (videoSliceSet.FaceList.empty()) {
        videoSliceSet.FaceList.push_back(face);
    }
}

void BindFaceCaptureIfNeeded(const GAT_1400_Face& face, GAT_1400_FileSet& fileSet)
{
    if (fileSet.FaceList.empty()) {
        fileSet.FaceList.push_back(face);
    }
}

void BindMotorCaptureIfNeeded(const GAT_1400_Motor& motorVehicle, GAT_1400_ImageSet& imageSet)
{
    if (imageSet.MotorVehicleList.empty()) {
        imageSet.MotorVehicleList.push_back(motorVehicle);
    }
}

void BindMotorCaptureIfNeeded(const GAT_1400_Motor& motorVehicle, GAT_1400_VideoSliceSet& videoSliceSet)
{
    if (videoSliceSet.MotorVehicleList.empty()) {
        videoSliceSet.MotorVehicleList.push_back(motorVehicle);
    }
}

void BindMotorCaptureIfNeeded(const GAT_1400_Motor& motorVehicle, GAT_1400_FileSet& fileSet)
{
    if (fileSet.MotorVehicleList.empty()) {
        fileSet.MotorVehicleList.push_back(motorVehicle);
    }
}

bool ParseCompactTime(const char* text, time_t& out)
{
    if (text == NULL || strlen(text) != 14) {
        return false;
    }
    struct tm tmValue;
    memset(&tmValue, 0, sizeof(tmValue));
    char buffer[5] = {0};
    memcpy(buffer, text, 4);
    tmValue.tm_year = atoi(buffer) - 1900;
    memcpy(buffer, text + 4, 2);
    buffer[2] = 0;
    tmValue.tm_mon = atoi(buffer) - 1;
    memcpy(buffer, text + 6, 2);
    tmValue.tm_mday = atoi(buffer);
    memcpy(buffer, text + 8, 2);
    tmValue.tm_hour = atoi(buffer);
    memcpy(buffer, text + 10, 2);
    tmValue.tm_min = atoi(buffer);
    memcpy(buffer, text + 12, 2);
    tmValue.tm_sec = atoi(buffer);
    tmValue.tm_isdst = -1;
    out = mktime(&tmValue);
    return out != static_cast<time_t>(-1);
}

int EvaluateSubscribeStatus(const GAT_1400_Subscribe& subscribe)
{
    if (subscribe.OperateType != 0) {
        return SUBSCRIBE_CANCEL;
    }

    time_t beginTime = 0;
    time_t endTime = 0;
    if (!ParseCompactTime(subscribe.BeginTime, beginTime) || !ParseCompactTime(subscribe.EndTime, endTime)) {
        return subscribe.SubscribeStatus;
    }

    const time_t now = time(NULL);
    if (now > endTime) {
        return SUBSCRIBE_EXPIRATION;
    }
    if (now >= beginTime && now <= endTime) {
        return SUBSCRIBE_ING;
    }
    return SUBSCRIBE_NONE;
}

std::string UrlDecode(const std::string& input)
{
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const char hi = input[i + 1];
            const char lo = input[i + 2];
            if (std::isxdigit(static_cast<unsigned char>(hi)) && std::isxdigit(static_cast<unsigned char>(lo))) {
                char hex[3] = {hi, lo, 0};
                output.push_back(static_cast<char>(strtol(hex, NULL, 16)));
                i += 2;
                continue;
            }
        }
        output.push_back(input[i] == '+' ? ' ' : input[i]);
    }
    return output;
}

std::map<std::string, std::string> ParseQueryString(const std::string& query)
{
    std::map<std::string, std::string> result;
    std::stringstream ss(query);
    std::string item;
    while (std::getline(ss, item, '&')) {
        if (item.empty()) {
            continue;
        }
        const std::string::size_type pos = item.find('=');
        if (pos == std::string::npos) {
            result[UrlDecode(item)] = "";
        } else {
            result[UrlDecode(item.substr(0, pos))] = UrlDecode(item.substr(pos + 1));
        }
    }
    return result;
}

std::string GetSubscribeFieldValue(const GAT_1400_Subscribe& subscribe, const std::string& key)
{
    if (key == "SubscribeID") return subscribe.SubscribeID;
    if (key == "Title") return subscribe.Title;
    if (key == "SubscribeDetail") return subscribe.SubscribeDetail;
    if (key == "ResourceURI") return subscribe.ResourceURI;
    if (key == "ApplicantName") return subscribe.ApplicantName;
    if (key == "ApplicantOrg") return subscribe.ApplicantOrg;
    if (key == "BeginTime") return subscribe.BeginTime;
    if (key == "EndTime") return subscribe.EndTime;
    if (key == "ReceiveAddr") return subscribe.ReceiveAddr;
    if (key == "Reason") return subscribe.Reason;
    if (key == "SubscribeCancelOrg") return subscribe.SubscribeCancelOrg;
    if (key == "SubscribeCancelPerson") return subscribe.SubscribeCancelPerson;
    if (key == "CancelTime") return subscribe.CancelTime;
    if (key == "CancelReason") return subscribe.CancelReason;

    char buffer[32] = {0};
    if (key == "ReportInterval") {
        snprintf(buffer, sizeof(buffer), "%d", subscribe.ReportInterval);
        return buffer;
    }
    if (key == "OperateType") {
        snprintf(buffer, sizeof(buffer), "%d", subscribe.OperateType);
        return buffer;
    }
    if (key == "SubscribeStatus") {
        snprintf(buffer, sizeof(buffer), "%d", subscribe.SubscribeStatus);
        return buffer;
    }
    return "";
}

bool MatchSubscribeCondition(const GAT_1400_Subscribe& subscribe, const std::map<std::string, std::string>& condition)
{
    for (std::map<std::string, std::string>::const_iterator it = condition.begin(); it != condition.end(); ++it) {
        if (GetSubscribeFieldValue(subscribe, it->first) != it->second) {
            return false;
        }
    }
    return true;
}

std::vector<int> ParseRetryPolicy(const std::string& policy)
{
    std::vector<int> delays;
    std::stringstream ss(policy);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = TrimCopy(item);
        if (item.empty()) {
            continue;
        }
        const int delay = atoi(item.c_str());
        if (delay >= 0) {
            delays.push_back(delay);
        }
    }
    return delays;
}

std::string NormalizeBasePath(const std::string& input)
{
    std::string path = TrimCopy(input);
    if (path.empty() || path == "/") {
        return "";
    }

    if (path[0] != '/') {
        path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path[path.size() - 1] == '/') {
        path.erase(path.size() - 1);
    }
    return path;
}

std::string BuildRequestPath(const std::string& basePath, const std::string& path)
{
    const std::string normalizedBase = NormalizeBasePath(basePath);
    std::string normalizedPath = TrimCopy(path);
    if (normalizedPath.empty()) {
        normalizedPath = "/";
    }
    if (normalizedPath[0] != '/') {
        normalizedPath.insert(normalizedPath.begin(), '/');
    }
    if (normalizedBase.empty()) {
        return normalizedPath;
    }
    return (normalizedPath == "/") ? normalizedBase : (normalizedBase + normalizedPath);
}

bool ParseRequestUrl(const std::string& url, RequestTarget& out)
{
    const std::string::size_type schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0) {
        return false;
    }

    out.scheme = ToLowerCopy(url.substr(0, schemeEnd));
    std::string remain = url.substr(schemeEnd + 3);
    std::string::size_type slash = remain.find('/');
    std::string hostPort = remain.substr(0, slash);
    out.request_path = (slash == std::string::npos) ? "/" : remain.substr(slash);
    if (hostPort.empty()) {
        return false;
    }

    out.port = (out.scheme == "https") ? 443 : 80;
    std::string::size_type colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostPort.substr(0, colon);
        const std::string portText = hostPort.substr(colon + 1);
        if (out.host.empty() || portText.empty()) {
            return false;
        }
        out.port = atoi(portText.c_str());
    } else {
        out.host = hostPort;
    }
    return !out.host.empty() && out.port > 0;
}

std::string BuildRequestUrl(const RequestTarget& target)
{
    std::ostringstream oss;
    oss << target.scheme << "://" << target.host << ":" << target.port << target.request_path;
    return oss.str();
}

bool BuildRequestTarget(const ProtocolExternalConfig& cfg,
                        const std::string& path,
                        const std::string* overrideUrl,
                        RequestTarget& out)
{
    out.scheme = ToLowerCopy(cfg.gat_register.scheme.empty() ? "http" : cfg.gat_register.scheme);
    out.host = cfg.gat_register.server_ip;
    out.port = cfg.gat_register.server_port;
    out.timeout_ms = cfg.gat_register.request_timeout_ms > 0 ? cfg.gat_register.request_timeout_ms : kHttpTimeoutMs;
    out.request_path = BuildRequestPath(cfg.gat_register.base_path, path);

    if (overrideUrl == NULL || overrideUrl->empty()) {
        return !out.host.empty() && out.port > 0;
    }

    const std::string trimmed = TrimCopy(*overrideUrl);
    if (trimmed.empty()) {
        return !out.host.empty() && out.port > 0;
    }
    if (trimmed.find("://") != std::string::npos) {
        if (!ParseRequestUrl(trimmed, out)) {
            return false;
        }
        out.timeout_ms = cfg.gat_register.request_timeout_ms > 0 ? cfg.gat_register.request_timeout_ms : kHttpTimeoutMs;
        return true;
    }

    out.request_path = BuildRequestPath(cfg.gat_register.base_path, trimmed);
    return !out.host.empty() && out.port > 0;
}

std::string JoinFilePath(const std::string& dir, const std::string& fileName)
{
    if (dir.empty()) {
        return fileName;
    }
    if (dir[dir.size() - 1] == '/') {
        return dir + fileName;
    }
    return dir + "/" + fileName;
}

bool EnsureDirectoryExists(const std::string& dir)
{
    if (dir.empty()) {
        return false;
    }

    std::string normalized = dir;
    if (normalized[normalized.size() - 1] != '/') {
        normalized += "/";
    }

    std::string current;
    for (size_t i = 0; i < normalized.size(); ++i) {
        current.push_back(normalized[i]);
        if (normalized[i] != '/') {
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

bool ReadFileText(const std::string& path, std::string& out)
{
    out.clear();
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL) {
        return false;
    }

    char buffer[4096];
    size_t bytes = 0;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        out.append(buffer, bytes);
    }
    const bool ok = (ferror(fp) == 0);
    fclose(fp);
    return ok;
}

std::list<GAT_1400_VideoSliceSet> BuildVideoSliceMetadataBatch(const std::list<GAT_1400_VideoSliceSet>& batch)
{
    std::list<GAT_1400_VideoSliceSet> metadataBatch(batch);
    for (std::list<GAT_1400_VideoSliceSet>::iterator it = metadataBatch.begin(); it != metadataBatch.end(); ++it) {
        it->Data.clear();
    }
    return metadataBatch;
}

std::list<GAT_1400_ImageSet> BuildImageMetadataBatch(const std::list<GAT_1400_ImageSet>& batch)
{
    std::list<GAT_1400_ImageSet> metadataBatch(batch);
    for (std::list<GAT_1400_ImageSet>::iterator it = metadataBatch.begin(); it != metadataBatch.end(); ++it) {
        it->Data.clear();
    }
    return metadataBatch;
}

std::list<GAT_1400_FileSet> BuildFileMetadataBatch(const std::list<GAT_1400_FileSet>& batch)
{
    std::list<GAT_1400_FileSet> metadataBatch(batch);
    for (std::list<GAT_1400_FileSet>::iterator it = metadataBatch.begin(); it != metadataBatch.end(); ++it) {
        it->Data.clear();
    }
    return metadataBatch;
}

bool WriteFileText(const std::string& path, const std::string& data)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (fp == NULL) {
        return false;
    }
    bool ok = fwrite(data.data(), 1, data.size(), fp) == data.size();
    if (ok) {
        ok = fflush(fp) == 0;
    }
    const int closeRet = fclose(fp);
    return ok && closeRet == 0;
}

bool RemoveFileIfExists(const std::string& path)
{
    return path.empty() || unlink(path.c_str()) == 0 || errno == ENOENT;
}

std::string BuildPendingFileName(unsigned long long seq)
{
    char buffer[96] = {0};
    snprintf(buffer, sizeof(buffer), "pending_%lld_%llu%s",
             static_cast<long long>(time(NULL)),
             seq,
             kPendingUploadFileSuffix);
    return std::string(buffer);
}

std::string SerializePendingUpload(const GAT1400ClientService::PendingUploadItem& item)
{
    std::ostringstream oss;
    oss << "id=" << item.id << "\n";
    oss << "method=" << item.method << "\n";
    oss << "path=" << item.path << "\n";
    oss << "content_type=" << item.content_type << "\n";
    oss << "response_kind=" << item.response_kind << "\n";
    oss << "category=" << item.category << "\n";
    oss << "object_id=" << item.object_id << "\n";
    oss << "attempt_count=" << item.attempt_count << "\n";
    oss << "enqueue_time=" << static_cast<long long>(item.enqueue_time) << "\n";
    oss << "last_error=" << item.last_error << "\n\n";
    std::string text = oss.str();
    text.append(item.body);
    return text;
}

bool ParsePendingUploadText(const std::string& raw, GAT1400ClientService::PendingUploadItem& item)
{
    const std::string::size_type headerEnd = raw.find("\n\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    std::stringstream ss(raw.substr(0, headerEnd));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        const std::string::size_type pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        if (key == "id") {
            item.id = value;
        } else if (key == "method") {
            item.method = value;
        } else if (key == "path") {
            item.path = value;
        } else if (key == "content_type") {
            item.content_type = value;
        } else if (key == "response_kind") {
            item.response_kind = value;
        } else if (key == "category") {
            item.category = value;
        } else if (key == "object_id") {
            item.object_id = value;
        } else if (key == "attempt_count") {
            item.attempt_count = atoi(value.c_str());
        } else if (key == "enqueue_time") {
            item.enqueue_time = static_cast<time_t>(atoll(value.c_str()));
        } else if (key == "last_error") {
            item.last_error = value;
        }
    }

    item.body = raw.substr(headerEnd + 2);
    if (item.id.empty() || item.method.empty() || item.path.empty() || item.response_kind.empty()) {
        return false;
    }
    if (item.content_type.empty()) {
        item.content_type = kJsonContentType;
    }
    if (item.enqueue_time == 0) {
        item.enqueue_time = time(NULL);
    }
    return true;
}

bool ReadPendingUploadFile(const std::string& path, GAT1400ClientService::PendingUploadItem& item)
{
    std::string raw;
    if (!ReadFileText(path, raw) || !ParsePendingUploadText(raw, item)) {
        return false;
    }
    item.persist_path = path;
    return true;
}

int ParseResponseStatusListBody(const std::string& body, int& outError)
{
    outError = 0;
    std::list<GAT1400_RESPONSESTATUS_ST> responseList;
    if (GAT1400Json::UnPackResponseStatusList(body, responseList) != 0) {
        return -1;
    }

    for (std::list<GAT1400_RESPONSESTATUS_ST>::const_iterator it = responseList.begin(); it != responseList.end(); ++it) {
        if (it->StatusCode != OK) {
            outError = it->StatusCode;
            return -2;
        }
    }
    return 0;
}

int ParseResponseStatusBody(const std::string& body, int& outError)
{
    outError = 0;
    GAT1400_RESPONSESTATUS_ST responseStatus;
    if (GAT1400Json::UnPackResponseStatus(body, responseStatus) != 0) {
        return -1;
    }
    if (responseStatus.StatusCode != OK) {
        outError = responseStatus.StatusCode;
        return -2;
    }
    return 0;
}

const char* HttpMethodToString(unsigned int method)
{
    switch (method) {
        case HTTP_REQUEST_POST:
            return "POST";
        case HTTP_REQUEST_PUT:
            return "PUT";
        case HTTP_REQUEST_GET:
            return "GET";
        case HTTP_REQUEST_DELETE:
            return "DELETE";
        default:
            return NULL;
    }
}

bool IsAbsoluteRequestTarget(const std::string& url)
{
    return url.find("://") != std::string::npos;
}

bool IsListResponseUriType(unsigned int format)
{
    switch (format) {
        case POST_DISPOSITION_LIST:
        case POST_VIDEOSLICES:
        case POST_IMAGES:
        case POST_FILES:
        case POST_PERSONS:
        case POST_FACES:
        case POST_MOTORVEHICLE:
        case POST_NUNMOTORVEHICLE:
        case POST_VIDEOLABELS:
        case POST_SUBSCRIBE_LIST:
            return true;
        default:
            return false;
    }
}

bool IsBinaryUploadUriType(unsigned int format)
{
    return format == POST_VIDEOSLICESDATA || format == POST_IMAGESDATA || format == POST_FILESDATA;
}

std::string DefaultPathForUriType(unsigned int format)
{
    switch (format) {
        case POST_REGISTER:
            return "/VIID/System/Register";
        case POST_UNREGISTER:
            return "/VIID/System/UnRegister";
        case POST_KEEPALIVE:
            return "/VIID/System/Keepalive";
        case POST_DISPOSITION_LIST:
            return "/VIID/Dispositions";
        case POST_VIDEOSLICES:
            return "/VIID/VideoSlices";
        case POST_IMAGES:
            return "/VIID/Images";
        case POST_FILES:
            return "/VIID/Files";
        case POST_PERSONS:
            return "/VIID/Persons";
        case POST_FACES:
            return "/VIID/Faces";
        case POST_MOTORVEHICLE:
            return "/VIID/MotorVehicles";
        case POST_NUNMOTORVEHICLE:
            return "/VIID/NonMotorVehicles";
        case POST_VIDEOLABELS:
            return "/VIID/VideoLabels";
        case POST_SUBSCRIBE_NOTIFICATION_LIST:
            return "/VIID/SubscribeNotifications";
        case POST_SUBSCRIBE_LIST:
            return "/VIID/Subscribes";
        case GET_SYNCTIME:
            return "/VIID/System/Time";
        case GET_APES:
        case PUT_APES:
            return "/VIID/APEs";
        case GET_TOLLGATES:
            return "/VIID/Tollgates";
        case GET_LANES:
            return "/VIID/Lanes";
        case GET_MOTORVEHICLE_LIST:
            return "/VIID/MotorVehicles";
        case GET_NONMOTORVEHICLE_LIST:
            return "/VIID/NonMotorVehicles";
        case GET_THING_LIST:
            return "/VIID/Things";
        case GET_SCENE_LIST:
            return "/VIID/Scenes";
        case GET_VIDEO_SLICE_LIST:
            return "/VIID/VideoSlices";
        case GET_IMAGE_LIST:
            return "/VIID/Images";
        case GET_FILE_LIST:
            return "/VIID/Files";
        case GET_PERSON_LIST:
            return "/VIID/Persons";
        case GET_FACE_LIST:
            return "/VIID/Faces";
        case GET_CASE_LIST:
            return "/VIID/Cases";
        case GET_DISPOSITION_LIST:
        case PUT_DISPOSITION_LIST:
        case DELETE_DISPOSITION_LIST:
            return "/VIID/Dispositions";
        case GET_DISPOSITION_NOTIFICATION_LIST:
        case DELETE_DISPOSITION_NOTIFICATION_LIST:
            return "/VIID/DispositionNotifications";
        case GET_SUBSCRIBE_LIST:
        case PUT_SUBSCRIBE_LIST:
        case DELETE_SUBSCRIBE_LIST:
            return "/VIID/Subscribes";
        case GET_SUBSCRIBE_NOTIFICATION_LIST:
        case DELETE_SUBSCRIBE_NOTIFICATION_LIST:
            return "/VIID/SubscribeNotifications";
        case GET_ANALYSIS_RULE_LIST:
            return "/VIID/AnalysisRules";
        case GET_VIDEO_LABEL_LIST:
            return "/VIID/VideoLabels";
        default:
            return "";
    }
}

bool PathContainsResource(const std::string& requestPath, const char* resource)
{
    if (resource == NULL || resource[0] == '\0') {
        return false;
    }

    const std::string::size_type queryPos = requestPath.find('?');
    const std::string normalized = (queryPos == std::string::npos) ? requestPath : requestPath.substr(0, queryPos);
    const std::string::size_type pos = normalized.find(resource);
    if (pos == std::string::npos) {
        return false;
    }

    const std::string::size_type tail = pos + strlen(resource);
    return tail == normalized.size() || normalized[tail] == '/';
}

bool IsViasRequestPath(const std::string& requestPath)
{
    return requestPath.find("/VIAS/") != std::string::npos;
}

bool IsApeCompatPostPath(const std::string& requestPath)
{
    return PathContainsResource(requestPath, "/VIID/APEs");
}

bool ShouldPersistFailedUpload(int errorCode)
{
    return errorCode != kClientErrorUnsupportedUri && errorCode != kClientErrorApePostCompatDisabled;
}

int ConnectTcp(const std::string& host, int port, int timeoutMs)
{
    char portText[16] = {0};
    snprintf(portText, sizeof(portText), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = NULL;
    if (getaddrinfo(host.c_str(), portText, &hints, &result) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

bool SendAll(int fd, const std::string& data)
{
    size_t total = 0;
    while (total < data.size()) {
        const ssize_t sent = send(fd, data.data() + total, data.size() - total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        total += static_cast<size_t>(sent);
    }
    return true;
}

bool RecvAll(int fd, std::string& out)
{
    char buffer[4096];
    while (true) {
        const ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
        if (len > 0) {
            out.append(buffer, static_cast<size_t>(len));
            continue;
        }
        if (len == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
}

bool DecodeChunkedBody(const std::string& input, std::string& output)
{
    output.clear();
    size_t pos = 0;
    while (pos < input.size()) {
        const size_t lineEnd = input.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            return false;
        }
        const std::string sizeText = input.substr(pos, lineEnd - pos);
        const unsigned long chunkSize = strtoul(sizeText.c_str(), NULL, 16);
        pos = lineEnd + 2;
        if (chunkSize == 0) {
            return true;
        }
        if (pos + chunkSize + 2 > input.size()) {
            return false;
        }
        output.append(input, pos, chunkSize);
        pos += chunkSize + 2;
    }
    return true;
}

bool ParseHttpResponseText(const std::string& raw, GAT1400ClientService::HttpResponse& response)
{
    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    std::stringstream headerStream(raw.substr(0, headerEnd));
    std::string line;
    if (!std::getline(headerStream, line)) {
        return false;
    }
    if (!line.empty() && line[line.size() - 1] == '\r') {
        line.erase(line.size() - 1);
    }

    std::stringstream statusLine(line);
    std::string httpVersion;
    statusLine >> httpVersion >> response.status_code;
    if (response.status_code <= 0) {
        return false;
    }

    response.headers.clear();
    while (std::getline(headerStream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        if (line.empty()) {
            continue;
        }
        const std::string::size_type pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = ToLowerCopy(TrimCopy(line.substr(0, pos)));
        const std::string value = TrimCopy(line.substr(pos + 1));
        response.headers[key] = value;
    }

    response.body = raw.substr(headerEnd + 4);
    std::map<std::string, std::string>::const_iterator it = response.headers.find("transfer-encoding");
    if (it != response.headers.end() && ToLowerCopy(it->second).find("chunked") != std::string::npos) {
        std::string decoded;
        if (!DecodeChunkedBody(response.body, decoded)) {
            return false;
        }
        response.body.swap(decoded);
    } else {
        it = response.headers.find("content-length");
        if (it != response.headers.end()) {
            const long expected = atol(it->second.c_str());
            if (expected >= 0 && static_cast<long>(response.body.size()) > expected) {
                response.body.resize(static_cast<size_t>(expected));
            }
        }
    }

    return true;
}

std::string BuildHttpResponse(int status, const std::string& body)
{
    std::ostringstream oss;
    const char* reason = "OK";
    if (status == 400) {
        reason = "Bad Request";
    } else if (status == 404) {
        reason = "Not Found";
    } else if (status == 500) {
        reason = "Internal Server Error";
    }

    oss << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
    oss << "Content-Type: " << kJsonContentType << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << body;
    return oss.str();
}

template <typename T>
std::list<T> SliceBatch(typename std::list<T>::const_iterator& begin,
                        const typename std::list<T>::const_iterator& end,
                        int batchSize)
{
    std::list<T> batch;
    int count = 0;
    while (begin != end && count < batchSize) {
        batch.push_back(*begin);
        ++begin;
        ++count;
    }
    return batch;
}

}  // namespace

GAT1400ClientService::GAT1400ClientService()
    : m_started(false),
      m_registered(false),
      m_regist_state(EM_REGIST_OFF),
      m_listen_fd(-1),
      m_server_running(false),
      m_heartbeat_running(false),
      m_pending_seq(0),
      m_last_replay_time(0),
      m_keepalive_demo_upload_attempted(false)
{
}

GAT1400ClientService::~GAT1400ClientService()
{
    Stop();
}

int GAT1400ClientService::SnapshotConfig(ProtocolExternalConfig& cfg,
                                         std::string& deviceId) const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_started && !m_registered) {
        return -1;
    }
    cfg = m_cfg;
    deviceId = ResolveGatRuntimeDeviceId(cfg);
    return 0;
}

bool GAT1400ClientService::IsStarted() const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_started;
}

void GAT1400ClientService::UpdateRegistState(regist_state state)
{
    std::vector<CLower1400RegistStatusObserver*> observers;
    {
        std::lock_guard<std::mutex> stateLock(m_state_mutex);
        if (m_regist_state == state) {
            return;
        }
        m_regist_state = state;
    }
    {
        std::lock_guard<std::mutex> observerLock(m_observer_mutex);
        observers = m_regist_observers;
    }
    for (size_t i = 0; i < observers.size(); ++i) {
        if (observers[i] != NULL) {
            observers[i]->UpdateRegistStatus(state);
        }
    }
}

void GAT1400ClientService::AddSubscribeObserver(CLower1400SubscribeObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    std::list<GAT_1400_Subscribe> subscriptions;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        if (std::find(m_subscribe_observers.begin(), m_subscribe_observers.end(), observer) == m_subscribe_observers.end()) {
            m_subscribe_observers.push_back(observer);
        }
    }
    subscriptions = GetSubscriptions();
    for (std::list<GAT_1400_Subscribe>::const_iterator it = subscriptions.begin(); it != subscriptions.end(); ++it) {
        if (it->OperateType == 0 && it->SubscribeStatus != SUBSCRIBE_CANCEL) {
            observer->AddSubscribe(*it);
        }
    }
}

void GAT1400ClientService::RemoveSubscribeObserver(CLower1400SubscribeObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    std::list<GAT_1400_Subscribe> subscriptions = GetSubscriptions();
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        m_subscribe_observers.erase(std::remove(m_subscribe_observers.begin(), m_subscribe_observers.end(), observer),
                                    m_subscribe_observers.end());
    }
    for (std::list<GAT_1400_Subscribe>::const_iterator it = subscriptions.begin(); it != subscriptions.end(); ++it) {
        observer->DelSubscribe(it->SubscribeID);
    }
}

void GAT1400ClientService::AddRegistStatusObserver(CLower1400RegistStatusObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    regist_state state;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        if (std::find(m_regist_observers.begin(), m_regist_observers.end(), observer) == m_regist_observers.end()) {
            m_regist_observers.push_back(observer);
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        state = m_regist_state;
    }
    observer->UpdateRegistStatus(state);
}

void GAT1400ClientService::RemoveRegistStatusObserver(CLower1400RegistStatusObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_observer_mutex);
    m_regist_observers.erase(std::remove(m_regist_observers.begin(), m_regist_observers.end(), observer),
                             m_regist_observers.end());
}

void GAT1400ClientService::DispatchSubscribeAdd(const GAT_1400_Subscribe& subscribe)
{
    std::vector<CLower1400SubscribeObserver*> observers;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        observers = m_subscribe_observers;
    }
    for (size_t i = 0; i < observers.size(); ++i) {
        if (observers[i] != NULL) {
            observers[i]->AddSubscribe(subscribe);
        }
    }
}

void GAT1400ClientService::DispatchSubscribeUpdate(const GAT_1400_Subscribe& subscribe)
{
    std::vector<CLower1400SubscribeObserver*> observers;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        observers = m_subscribe_observers;
    }
    for (size_t i = 0; i < observers.size(); ++i) {
        if (observers[i] != NULL) {
            observers[i]->UpdateSubscribe(subscribe);
        }
    }
}

void GAT1400ClientService::DispatchSubscribeDelete(const std::string& subscribeId)
{
    std::vector<CLower1400SubscribeObserver*> observers;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        observers = m_subscribe_observers;
    }
    for (size_t i = 0; i < observers.size(); ++i) {
        if (observers[i] != NULL) {
            observers[i]->DelSubscribe(subscribeId);
        }
    }
}

std::list<GAT_1400_Subscribe> GAT1400ClientService::GetSubscriptions() const
{
    std::list<GAT_1400_Subscribe> result;
    std::lock_guard<std::mutex> lock(m_subscribe_mutex);
    for (std::map<std::string, GAT_1400_Subscribe>::const_iterator it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        GAT_1400_Subscribe subscribe = it->second;
        subscribe.SubscribeStatus = EvaluateSubscribeStatus(subscribe);
        result.push_back(subscribe);
    }
    return result;
}

int GAT1400ClientService::StartServerLocked()
{
    if (m_listen_fd >= 0) {
        return 0;
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(m_cfg.gat_register.listen_port));

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -2;
    }
    if (listen(fd, 8) != 0) {
        close(fd);
        return -3;
    }

    m_listen_fd = fd;
    m_server_running.store(true);
    m_server_thread = std::thread(&GAT1400ClientService::ServerLoop, this);
    return 0;
}

void GAT1400ClientService::StopServerLocked()
{
    m_server_running.store(false);
    if (m_listen_fd >= 0) {
        shutdown(m_listen_fd, SHUT_RDWR);
        close(m_listen_fd);
        m_listen_fd = -1;
    }
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
}

void GAT1400ClientService::ServerLoop()
{
    while (m_server_running.load()) {
        int listenFd = -1;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            listenFd = m_listen_fd;
        }
        if (listenFd < 0) {
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listenFd, &rfds);
        struct timeval tv;
        tv.tv_sec = kAcceptWaitMs / 1000;
        tv.tv_usec = static_cast<suseconds_t>((kAcceptWaitMs % 1000) * 1000);
        const int ready = select(listenFd + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) {
            continue;
        }

        const int clientFd = accept(listenFd, NULL, NULL);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!m_server_running.load()) {
                break;
            }
            continue;
        }

        HandleServerConnection(clientFd);
        close(clientFd);
    }
}

void GAT1400ClientService::HandleServerConnection(int clientFd)
{
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string rawRequest;
    if (!RecvAll(clientFd, rawRequest)) {
        return;
    }

    const size_t headerEnd = rawRequest.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return;
    }

    const std::string headerText = rawRequest.substr(0, headerEnd);
    std::string body = rawRequest.substr(headerEnd + 4);
    std::stringstream headerStream(headerText);
    std::string requestLine;
    if (!std::getline(headerStream, requestLine)) {
        return;
    }
    if (!requestLine.empty() && requestLine[requestLine.size() - 1] == '\r') {
        requestLine.erase(requestLine.size() - 1);
    }

    std::stringstream requestLineStream(requestLine);
    std::string method;
    std::string requestTarget;
    std::string httpVersion;
    requestLineStream >> method >> requestTarget >> httpVersion;
    if (method.empty() || requestTarget.empty()) {
        return;
    }

    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(headerStream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        const std::string::size_type pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        headers[ToLowerCopy(TrimCopy(line.substr(0, pos)))] = TrimCopy(line.substr(pos + 1));
    }

    std::map<std::string, std::string>::const_iterator lengthIt = headers.find("content-length");
    if (lengthIt != headers.end()) {
        const size_t contentLength = static_cast<size_t>(strtoul(lengthIt->second.c_str(), NULL, 10));
        if (body.size() > contentLength) {
            body.resize(contentLength);
        }
    }

    std::string path = requestTarget;
    std::string query;
    const std::string::size_type pos = requestTarget.find('?');
    if (pos != std::string::npos) {
        path = requestTarget.substr(0, pos);
        query = requestTarget.substr(pos + 1);
    }

    int responseStatus = 404;
    std::string responseBody;
    HandleSubscribeHttp(method, path, query, body, responseStatus, responseBody);
    const std::string response = BuildHttpResponse(responseStatus, responseBody);
    SendAll(clientFd, response);
}

int GAT1400ClientService::HandleSubscribeHttp(const std::string& method,
                                              const std::string& path,
                                              const std::string& query,
                                              const std::string& body,
                                              int& outStatus,
                                              std::string& outBody)
{
    std::string normalized = path;
    if (!normalized.empty() && normalized[normalized.size() - 1] == '/' && normalized.size() > 1) {
        normalized.erase(normalized.size() - 1);
    }

    if (normalized == "/VIID/Subscribes") {
        if (method == "GET") {
            const std::map<std::string, std::string> condition = ParseQueryString(query);
            std::list<GAT_1400_Subscribe> result;
            {
                std::lock_guard<std::mutex> lock(m_subscribe_mutex);
                for (std::map<std::string, GAT_1400_Subscribe>::const_iterator it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
                    GAT_1400_Subscribe subscribe = it->second;
                    subscribe.SubscribeStatus = EvaluateSubscribeStatus(subscribe);
                    if (condition.empty() || MatchSubscribeCondition(subscribe, condition)) {
                        result.push_back(subscribe);
                    }
                }
            }
            outStatus = 200;
            outBody = GAT1400Json::PackSubscribeListJson(result);
            return 0;
        }

        if (method == "POST" || method == "PUT") {
            std::list<GAT_1400_Subscribe> subscribeList;
            std::list<GAT1400_RESPONSESTATUS_ST> responseList;
            const int parseRet = GAT1400Json::UnPackSubscribeList(body, subscribeList);
            if (parseRet != 0) {
                GAT1400_RESPONSESTATUS_ST responseStatus;
                memset(&responseStatus, 0, sizeof(responseStatus));
                CopyToArray(responseStatus.ReqeustURL, sizeof(responseStatus.ReqeustURL), normalized);
                CopyToArray(responseStatus.LocalTime, sizeof(responseStatus.LocalTime), FormatCurrentTime());
                responseStatus.StatusCode = INVALID_JSON_FORMAT;
                responseList.push_back(responseStatus);
                outStatus = 400;
                outBody = GAT1400Json::PackResponseStatusList(responseList);
                return parseRet;
            }

            for (std::list<GAT_1400_Subscribe>::iterator it = subscribeList.begin(); it != subscribeList.end(); ++it) {
                it->SubscribeStatus = EvaluateSubscribeStatus(*it);
                GAT1400_RESPONSESTATUS_ST responseStatus;
                memset(&responseStatus, 0, sizeof(responseStatus));
                CopyToArray(responseStatus.ReqeustURL, sizeof(responseStatus.ReqeustURL), normalized);
                CopyToArray(responseStatus.ID, sizeof(responseStatus.ID), it->SubscribeID);
                CopyToArray(responseStatus.LocalTime, sizeof(responseStatus.LocalTime), FormatCurrentTime());
                responseStatus.StatusCode = OK;
                responseList.push_back(responseStatus);

                {
                    std::lock_guard<std::mutex> lock(m_subscribe_mutex);
                    m_subscriptions[it->SubscribeID] = *it;
                }
                if (method == "POST") {
                    DispatchSubscribeAdd(*it);
                } else {
                    DispatchSubscribeUpdate(*it);
                }
            }

            outStatus = 200;
            outBody = GAT1400Json::PackResponseStatusList(responseList);
            return 0;
        }

        if (method == "DELETE") {
            std::list<GAT1400_RESPONSESTATUS_ST> responseList;
            const std::map<std::string, std::string> condition = ParseQueryString(query);
            std::map<std::string, std::string>::const_iterator idListIt = condition.find("IDList");
            if (idListIt == condition.end()) {
                GAT1400_RESPONSESTATUS_ST responseStatus;
                memset(&responseStatus, 0, sizeof(responseStatus));
                CopyToArray(responseStatus.ReqeustURL, sizeof(responseStatus.ReqeustURL), normalized);
                CopyToArray(responseStatus.LocalTime, sizeof(responseStatus.LocalTime), FormatCurrentTime());
                responseStatus.StatusCode = INVALID_OPERATION;
                responseList.push_back(responseStatus);
                outStatus = 400;
                outBody = GAT1400Json::PackResponseStatusList(responseList);
                return -1;
            }

            std::stringstream ss(idListIt->second);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = TrimCopy(item);
                if (item.empty()) {
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(m_subscribe_mutex);
                    m_subscriptions.erase(item);
                }
                DispatchSubscribeDelete(item);

                GAT1400_RESPONSESTATUS_ST responseStatus;
                memset(&responseStatus, 0, sizeof(responseStatus));
                CopyToArray(responseStatus.ReqeustURL, sizeof(responseStatus.ReqeustURL), normalized);
                CopyToArray(responseStatus.ID, sizeof(responseStatus.ID), item);
                CopyToArray(responseStatus.LocalTime, sizeof(responseStatus.LocalTime), FormatCurrentTime());
                responseStatus.StatusCode = OK;
                responseList.push_back(responseStatus);
            }

            outStatus = 200;
            outBody = GAT1400Json::PackResponseStatusList(responseList);
            return 0;
        }
    }

    const std::string prefix("/VIID/Subscribes/");
    if (normalized.compare(0, prefix.size(), prefix) == 0 && method == "PUT") {
        GAT_1400_Subscribe subscribe;
        GAT1400_RESPONSESTATUS_ST responseStatus;
        memset(&responseStatus, 0, sizeof(responseStatus));
        CopyToArray(responseStatus.ReqeustURL, sizeof(responseStatus.ReqeustURL), normalized);
        CopyToArray(responseStatus.LocalTime, sizeof(responseStatus.LocalTime), FormatCurrentTime());

        const int parseRet = GAT1400Json::UnPackSubscribe(body, subscribe);
        if (parseRet != 0) {
            responseStatus.StatusCode = INVALID_JSON_FORMAT;
            outStatus = 400;
            outBody = GAT1400Json::PackResponseStatus(responseStatus);
            return parseRet;
        }

        const std::string subscribeId = normalized.substr(prefix.size());
        CopyToArray(subscribe.SubscribeID, sizeof(subscribe.SubscribeID), subscribeId);
        subscribe.OperateType = 1;
        subscribe.SubscribeStatus = SUBSCRIBE_CANCEL;
        {
            std::lock_guard<std::mutex> lock(m_subscribe_mutex);
            std::map<std::string, GAT_1400_Subscribe>::iterator it = m_subscriptions.find(subscribeId);
            if (it != m_subscriptions.end()) {
                GAT_1400_Subscribe& current = it->second;
                current.OperateType = subscribe.OperateType;
                current.SubscribeStatus = subscribe.SubscribeStatus;
                memcpy(current.SubscribeCancelOrg, subscribe.SubscribeCancelOrg, sizeof(current.SubscribeCancelOrg));
                memcpy(current.SubscribeCancelPerson, subscribe.SubscribeCancelPerson, sizeof(current.SubscribeCancelPerson));
                memcpy(current.CancelTime, subscribe.CancelTime, sizeof(current.CancelTime));
                memcpy(current.CancelReason, subscribe.CancelReason, sizeof(current.CancelReason));
            }
        }
        DispatchSubscribeDelete(subscribeId);
        CopyToArray(responseStatus.ID, sizeof(responseStatus.ID), subscribeId);
        responseStatus.StatusCode = OK;
        outStatus = 200;
        outBody = GAT1400Json::PackResponseStatus(responseStatus);
        return 0;
    }

    outStatus = 404;
    outBody = "{}";
    return -1;
}

int GAT1400ClientService::ExecuteRequest(const ProtocolExternalConfig& cfg,
                                         const std::string& deviceId,
                                         const std::string& method,
                                         const std::string& path,
                                         const std::string& contentType,
                                         const std::string& body,
                                         HttpResponse& response,
                                         const std::string* overrideUrl) const
{
    RequestTarget target;
    if (!BuildRequestTarget(cfg, path, overrideUrl, target)) {
        return -1;
    }

    if (IsViasRequestPath(target.request_path)) {
        printf("[GAT1400] module=gat1400 event=request trace=client error=%d method=%s path=%s note=vias_not_supported\n",
               kClientErrorUnsupportedUri,
               method.c_str(),
               target.request_path.c_str());
        return kClientErrorUnsupportedUri;
    }

    if (method == "POST" && IsApeCompatPostPath(target.request_path) && cfg.gat_upload.enable_apes_post_compat == 0) {
        printf("[GAT1400] module=gat1400 event=request trace=client error=%d method=%s path=%s note=post_apes_compat_disabled\n",
               kClientErrorApePostCompatDisabled,
               method.c_str(),
               target.request_path.c_str());
        return kClientErrorApePostCompatDisabled;
    }

    if (target.scheme != "http") {
        printf("[GAT1400] module=gat1400 event=request trace=client error=-5 scheme=%s path=%s note=scheme_not_supported\n",
               target.scheme.c_str(),
               target.request_path.c_str());
        return -5;
    }

    const auto doRequest = [&](const std::string* authHeader) -> int {
        const int fd = ConnectTcp(target.host, target.port, target.timeout_ms);
        if (fd < 0) {
            return -2;
        }

        std::ostringstream request;
        request << method << ' ' << target.request_path << " HTTP/1.1\r\n";
        request << "Host: " << target.host << ':' << target.port << "\r\n";
        request << "User-Identify: " << deviceId << "\r\n";
        request << "Content-Type: " << (contentType.empty() ? kJsonContentType : contentType) << "\r\n";
        request << "Connection: close\r\n";
        if (authHeader != NULL && !authHeader->empty()) {
            request << *authHeader << "\r\n";
        }
        request << "Content-Length: " << body.size() << "\r\n\r\n";

        std::string rawRequest = request.str();
        rawRequest.append(body);

        std::string rawResponse;
        const bool sendOk = SendAll(fd, rawRequest);
        const bool recvOk = sendOk && RecvAll(fd, rawResponse);
        close(fd);
        if (!recvOk) {
            return -3;
        }
        if (!ParseHttpResponseText(rawResponse, response)) {
            return -4;
        }
        return 0;
    };

    int ret = doRequest(NULL);
    if (ret != 0) {
        return ret;
    }

    if (response.status_code == 401 && !cfg.gat_register.username.empty()) {
        std::map<std::string, std::string>::const_iterator authIt = response.headers.find("www-authenticate");
        if (authIt != response.headers.end()) {
            CHttpAuth auth(BuildRequestUrl(target), method, cfg.gat_register.username, cfg.gat_register.password);
            std::string authHeader;
            auth.HttpAuthParse(authIt->second, authHeader);
            response = HttpResponse();
            ret = doRequest(&authHeader);
            if (ret != 0) {
                return ret;
            }
        }
    }

    return 0;
}

int GAT1400ClientService::EnqueuePendingUpload(const char* action,
                                               const std::string& method,
                                               const std::string& path,
                                               const std::string& contentType,
                                               const std::string& responseKind,
                                               const std::string& body,
                                               const std::string& objectId,
                                               const std::string& lastError)
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }
    if (!EnsureDirectoryExists(cfg.gat_upload.queue_dir)) {
        return -2;
    }

    PendingUploadItem item;
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        if (m_pending_uploads.empty()) {
            LoadPendingUploadsLocked(cfg);
        }

        item.id = BuildPendingFileName(++m_pending_seq);
        item.method = method;
        item.path = path;
        item.content_type = contentType.empty() ? kJsonContentType : contentType;
        item.response_kind = responseKind.empty() ? kResponseKindStatus : responseKind;
        item.body = body;
        item.category = action != NULL ? action : "post";
        item.object_id = objectId;
        item.last_error = lastError;
        item.enqueue_time = time(NULL);
        item.persist_path = JoinFilePath(cfg.gat_upload.queue_dir, item.id);

        if (!WriteFileText(item.persist_path, SerializePendingUpload(item))) {
            return -3;
        }

        m_pending_uploads.push_back(item);
        while (static_cast<int>(m_pending_uploads.size()) > std::max(1, cfg.gat_upload.max_pending_count)) {
            const PendingUploadItem oldest = m_pending_uploads.front();
            RemoveFileIfExists(oldest.persist_path);
            m_pending_uploads.pop_front();
        }
        m_last_replay_time = 0;
    }

    printf("[GAT1400] module=gat1400 event=%s trace=queue error=0 path=%s reason=%s\n",
           action != NULL ? action : "post",
           path.c_str(),
           lastError.c_str());
    return 0;
}

void GAT1400ClientService::LoadPendingUploadsLocked(const ProtocolExternalConfig& cfg)
{
    ClearPendingUploadsLocked();
    if (!EnsureDirectoryExists(cfg.gat_upload.queue_dir)) {
        return;
    }

    DIR* dir = opendir(cfg.gat_upload.queue_dir.c_str());
    if (dir == NULL) {
        return;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name == NULL) {
            continue;
        }
        const std::string fileName(entry->d_name);
        if (fileName == "." || fileName == "..") {
            continue;
        }
        if (fileName.size() <= strlen(kPendingUploadFileSuffix) ||
            fileName.substr(fileName.size() - strlen(kPendingUploadFileSuffix)) != kPendingUploadFileSuffix) {
            continue;
        }

        PendingUploadItem item;
        const std::string filePath = JoinFilePath(cfg.gat_upload.queue_dir, fileName);
        if (!ReadPendingUploadFile(filePath, item)) {
            RemoveFileIfExists(filePath);
            continue;
        }
        m_pending_uploads.push_back(item);
        if (m_pending_seq < static_cast<unsigned long long>(m_pending_uploads.size())) {
            m_pending_seq = static_cast<unsigned long long>(m_pending_uploads.size());
        }
    }
    closedir(dir);

    m_pending_uploads.sort([](const PendingUploadItem& lhs, const PendingUploadItem& rhs) {
        if (lhs.enqueue_time != rhs.enqueue_time) {
            return lhs.enqueue_time < rhs.enqueue_time;
        }
        return lhs.id < rhs.id;
    });

    while (static_cast<int>(m_pending_uploads.size()) > std::max(1, cfg.gat_upload.max_pending_count)) {
        const PendingUploadItem oldest = m_pending_uploads.front();
        RemoveFileIfExists(oldest.persist_path);
        m_pending_uploads.pop_front();
    }
}

void GAT1400ClientService::ClearPendingUploadsLocked()
{
    m_pending_uploads.clear();
}

int GAT1400ClientService::ReplayPendingUploads()
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    bool registered = false;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        registered = m_registered;
    }
    if (!registered) {
        return -2;
    }

    std::list<PendingUploadItem> replayItems;
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        if (m_pending_uploads.empty()) {
            LoadPendingUploadsLocked(cfg);
        }
        replayItems = m_pending_uploads;
    }
    if (replayItems.empty()) {
        return 0;
    }

    const std::vector<int> retryPolicy = ParseRetryPolicy(cfg.gat_upload.retry_policy);
    int replayedCount = 0;
    int failedCount = 0;
    int droppedCount = 0;
    for (std::list<PendingUploadItem>::const_iterator itemIt = replayItems.begin(); itemIt != replayItems.end(); ++itemIt) {
        const PendingUploadItem& pending = *itemIt;
        bool success = false;
        int finalRet = 0;
        int attemptsUsed = 0;
        std::string lastError;

        for (size_t attempt = 0;; ++attempt) {
            ++attemptsUsed;
            HttpResponse response;
            const std::string* overrideUrl = pending.path.find("://") != std::string::npos ? &pending.path : NULL;
            const std::string requestPath = overrideUrl != NULL ? "" : pending.path;
            const int reqRet = ExecuteRequest(cfg,
                                              deviceId,
                                              pending.method,
                                              requestPath,
                                              pending.content_type,
                                              pending.body,
                                              response,
                                              overrideUrl);
            if (reqRet == 0) {
                int statusCode = 0;
                const int parseRet = (pending.response_kind == kResponseKindList)
                                         ? ParseResponseStatusListBody(response.body, statusCode)
                                         : ParseResponseStatusBody(response.body, statusCode);
                if (parseRet == 0) {
                    success = true;
                    break;
                }
                finalRet = (statusCode != 0) ? statusCode : parseRet;
            } else {
                finalRet = reqRet;
            }

            char errorText[64] = {0};
            snprintf(errorText, sizeof(errorText), "ret=%d", finalRet);
            lastError = errorText;

            if (attempt >= retryPolicy.size()) {
                break;
            }
            sleep(static_cast<unsigned int>(retryPolicy[attempt]));
        }

        std::lock_guard<std::mutex> lock(m_pending_mutex);
        std::list<PendingUploadItem>::iterator current = std::find_if(
            m_pending_uploads.begin(),
            m_pending_uploads.end(),
            [&](const PendingUploadItem& item) { return item.id == pending.id; });
        if (current == m_pending_uploads.end()) {
            continue;
        }

        if (success) {
            RemoveFileIfExists(current->persist_path);
            m_pending_uploads.erase(current);
            ++replayedCount;
            continue;
        }

        if (!ShouldPersistFailedUpload(finalRet)) {
            RemoveFileIfExists(current->persist_path);
            m_pending_uploads.erase(current);
            ++droppedCount;
            continue;
        }

        current->attempt_count += attemptsUsed;
        current->last_error = lastError;
        WriteFileText(current->persist_path, SerializePendingUpload(*current));
        ++failedCount;
    }

    printf("[GAT1400] module=gat1400 event=replay trace=queue error=%d replayed=%d dropped=%d failed=%d\n",
           failedCount == 0 ? 0 : -1,
           replayedCount,
           droppedCount,
           failedCount);
    return failedCount == 0 ? 0 : -1;
}

int GAT1400ClientService::ReplayPendingUploadsIfDue()
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    bool registered = false;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        registered = m_registered;
    }
    if (!registered) {
        return -2;
    }

    const time_t now = time(NULL);
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        if (m_pending_uploads.empty()) {
            LoadPendingUploadsLocked(cfg);
        }
        if (m_pending_uploads.empty()) {
            return 0;
        }
        if (m_last_replay_time != 0 && (now - m_last_replay_time) < cfg.gat_upload.replay_interval_sec) {
            return 0;
        }
        m_last_replay_time = now;
    }

    return ReplayPendingUploads();
}

int GAT1400ClientService::PostJsonWithResponseList(const char* action,
                                                   const char* path,
                                                   const std::string& body,
                                                   const std::string* overrideUrl)
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    const std::vector<int> retryPolicy = ParseRetryPolicy(cfg.gat_upload.retry_policy);
    int finalRet = -1;
    const std::string requestTarget = (overrideUrl != NULL && !overrideUrl->empty()) ? *overrideUrl : (path != NULL ? path : "");
    std::string lastError("ret=-1");
    for (size_t attempt = 0;; ++attempt) {
        HttpResponse response;
        const int reqRet = ExecuteRequest(cfg,
                                          deviceId,
                                          "POST",
                                          path != NULL ? path : "",
                                          kJsonContentType,
                                          body,
                                          response,
                                          overrideUrl);
        if (reqRet == 0) {
            std::list<GAT1400_RESPONSESTATUS_ST> responseList;
            if (GAT1400Json::UnPackResponseStatusList(response.body, responseList) == 0) {
                bool allOk = true;
                int firstError = 0;
                for (std::list<GAT1400_RESPONSESTATUS_ST>::const_iterator it = responseList.begin(); it != responseList.end(); ++it) {
                    if (it->StatusCode != OK) {
                        allOk = false;
                        firstError = it->StatusCode;
                        break;
                    }
                }
                if (allOk) {
                    printf("[GAT1400] module=gat1400 event=%s trace=client error=0 path=%s count=%zu\n",
                           action != NULL ? action : "post",
                           path != NULL ? path : "",
                           responseList.size());
                    return 0;
                }
                finalRet = firstError == 0 ? -2 : firstError;
            } else {
                finalRet = -3;
            }
        } else {
            finalRet = reqRet;
        }

        char errorText[64] = {0};
        snprintf(errorText, sizeof(errorText), "ret=%d", finalRet);
        lastError = errorText;

        if (attempt >= retryPolicy.size()) {
            break;
        }
        sleep(static_cast<unsigned int>(retryPolicy[attempt]));
    }

    if (ShouldPersistFailedUpload(finalRet)) {
        EnqueuePendingUpload(action,
                             "POST",
                             requestTarget,
                             kJsonContentType,
                             kResponseKindList,
                             body,
                             "",
                             lastError);
    }
    return finalRet;
}

int GAT1400ClientService::PostCaptureEvent(const media::GAT1400CaptureEvent& event)
{
    std::lock_guard<std::mutex> lock(m_capture_upload_mutex);

    std::string sourceId;
    std::string deviceId;
    (void)ResolveCaptureSourceId(event, sourceId);
    (void)ResolveCaptureDeviceId(event, deviceId);

    GAT_1400_Face face = event.face;
    if (event.object_type == media::GAT1400_CAPTURE_OBJECT_FACE) {
        if (IsEmptyCString(face.SourceID) && !sourceId.empty()) {
            CopyToArray(face.SourceID, sizeof(face.SourceID), sourceId);
        }
        if (IsEmptyCString(face.DeviceID) && !deviceId.empty()) {
            CopyToArray(face.DeviceID, sizeof(face.DeviceID), deviceId);
        }

        std::list<GAT_1400_Face> faceList;
        faceList.push_back(face);
        const int ret = PostFaces(faceList);
        if (ret != 0) {
            return ret;
        }
    }

    GAT_1400_Motor motorVehicle = event.motor_vehicle;
    if (event.object_type == media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE) {
        if (IsEmptyCString(motorVehicle.SourceID) && !sourceId.empty()) {
            CopyToArray(motorVehicle.SourceID, sizeof(motorVehicle.SourceID), sourceId);
        }
        if (IsEmptyCString(motorVehicle.DeviceID) && !deviceId.empty()) {
            CopyToArray(motorVehicle.DeviceID, sizeof(motorVehicle.DeviceID), deviceId);
        }

        std::list<GAT_1400_Motor> motorList;
        motorList.push_back(motorVehicle);
        const int ret = PostMotorVehicles(motorList);
        if (ret != 0) {
            return ret;
        }
    }

    if (!event.image_list.empty()) {
        std::list<GAT_1400_ImageSet> imageList = event.image_list;
        for (std::list<GAT_1400_ImageSet>::iterator it = imageList.begin(); it != imageList.end(); ++it) {
            if (event.object_type == media::GAT1400_CAPTURE_OBJECT_FACE) {
                BindFaceCaptureIfNeeded(face, *it);
            } else if (event.object_type == media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE) {
                BindMotorCaptureIfNeeded(motorVehicle, *it);
            }
        }
        const int ret = PostImages(imageList);
        if (ret != 0) {
            return ret;
        }
    }

    if (!event.video_slice_list.empty()) {
        std::list<GAT_1400_VideoSliceSet> videoSliceList = event.video_slice_list;
        for (std::list<GAT_1400_VideoSliceSet>::iterator it = videoSliceList.begin(); it != videoSliceList.end(); ++it) {
            if (event.object_type == media::GAT1400_CAPTURE_OBJECT_FACE) {
                BindFaceCaptureIfNeeded(face, *it);
            } else if (event.object_type == media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE) {
                BindMotorCaptureIfNeeded(motorVehicle, *it);
            }
        }
        const int ret = PostVideoSlices(videoSliceList);
        if (ret != 0) {
            return ret;
        }
    }

    if (!event.file_list.empty()) {
        std::list<GAT_1400_FileSet> fileList = event.file_list;
        for (std::list<GAT_1400_FileSet>::iterator it = fileList.begin(); it != fileList.end(); ++it) {
            if (event.object_type == media::GAT1400_CAPTURE_OBJECT_FACE) {
                BindFaceCaptureIfNeeded(face, *it);
            } else if (event.object_type == media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE) {
                BindMotorCaptureIfNeeded(motorVehicle, *it);
            }
        }
        const int ret = PostFiles(fileList);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

int GAT1400ClientService::NotifyCaptureEvent(const media::GAT1400CaptureEvent& event)
{
    bool readyToUpload = false;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        readyToUpload = m_started && m_registered;
    }

    int directRet = 0;
    if (readyToUpload) {
        directRet = PostCaptureEvent(event);
        if (directRet == 0) {
            printf("[GAT1400] module=gat1400 event=capture_notify trace=bridge error=0 mode=direct object=%d images=%zu videos=%zu files=%zu pending=%zu\n",
                   static_cast<int>(event.object_type),
                   event.image_list.size(),
                   event.video_slice_list.size(),
                   event.file_list.size(),
                   media::GAT1400CaptureControl::Instance().PendingCount());
            return 0;
        }
    }

    const int submitRet = media::GAT1400CaptureControl::Instance().Submit(event);
    printf("[GAT1400] module=gat1400 event=capture_notify trace=bridge error=%d mode=%s object=%d images=%zu videos=%zu files=%zu pending=%zu direct=%d\n",
           submitRet,
           readyToUpload ? "queue_fallback" : "queue_only",
           static_cast<int>(event.object_type),
           event.image_list.size(),
           event.video_slice_list.size(),
           event.file_list.size(),
           media::GAT1400CaptureControl::Instance().PendingCount(),
           directRet);
    return submitRet;
}

int GAT1400ClientService::SendKeepaliveDemoUploadOnce(const std::string& deviceId)
{
    bool expected = false;
    if (!m_keepalive_demo_upload_attempted.compare_exchange_strong(expected, true)) {
        return 0;
    }

    std::string imageRaw;
    if (!ReadFileText(kKeepaliveDemoImagePath, imageRaw)) {
        printf("[GAT1400] module=gat1400 event=keepalive_demo trace=client error=-1 note=read_failed image=%s\n",
               kKeepaliveDemoImagePath);
        return -1;
    }
    if (imageRaw.empty()) {
        printf("[GAT1400] module=gat1400 event=keepalive_demo trace=client error=-1 note=empty_file image=%s\n",
               kKeepaliveDemoImagePath);
        return -1;
    }

    std::string imageData;
    if (!CBase64Coder::Encode(reinterpret_cast<const unsigned char*>(imageRaw.data()),
                              static_cast<unsigned long>(imageRaw.size()),
                              imageData)) {
        printf("[GAT1400] module=gat1400 event=keepalive_demo trace=client error=-1 note=base64_encode_failed image=%s\n",
               kKeepaliveDemoImagePath);
        return -1;
    }

    char token[32] = {0};
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tmNow;
    localtime_r(&tv.tv_sec, &tmNow);
    size_t prefixLen = strftime(token, sizeof(token), "%Y%m%d%H%M%S", &tmNow);
    snprintf(token + prefixLen, sizeof(token) - prefixLen, "%03ld", tv.tv_usec / 1000);

    const std::string shotTime(token);
    const std::string imageId = std::string("IMGDEMO") + shotTime;
    const std::string faceId = std::string("FACEDEMO") + shotTime;

    GAT_1400_Face face;
    memset(&face, 0, sizeof(face));
    CopyToArray(face.FaceID, sizeof(face.FaceID), faceId);
    face.InfoKind = 1;
    CopyToArray(face.SourceID, sizeof(face.SourceID), imageId);
    CopyToArray(face.DeviceID, sizeof(face.DeviceID), deviceId);
    face.LeftTopX = 100;
    face.LeftTopY = 100;
    face.RightBtmX = 500;
    face.RightBtmY = 500;

    GAT_1400_ImageSet imageSet;
    memset(&imageSet.ImageInfo, 0, sizeof(imageSet.ImageInfo));
    CopyToArray(imageSet.ImageInfo.ImageID, sizeof(imageSet.ImageInfo.ImageID), imageId);
    imageSet.ImageInfo.InfoKind = 1;
    imageSet.ImageInfo.ImageSource = 1;
    imageSet.ImageInfo.EventSort = 15;
    CopyToArray(imageSet.ImageInfo.DeviceID, sizeof(imageSet.ImageInfo.DeviceID), deviceId);
    CopyToArray(imageSet.ImageInfo.FileFormat, sizeof(imageSet.ImageInfo.FileFormat), "jpg");
    CopyToArray(imageSet.ImageInfo.ShotTime, sizeof(imageSet.ImageInfo.ShotTime), shotTime);
    CopyToArray(imageSet.ImageInfo.Title, sizeof(imageSet.ImageInfo.Title), "test");
    CopyToArray(imageSet.ImageInfo.ContentDescription,
                sizeof(imageSet.ImageInfo.ContentDescription),
                "keepalive demo image");
    CopyToArray(imageSet.ImageInfo.ShotPlaceFullAdress,
                sizeof(imageSet.ImageInfo.ShotPlaceFullAdress),
                "keepalive demo");
    imageSet.ImageInfo.SecurityLevel = 5;
    imageSet.ImageInfo.Width = 1920;
    imageSet.ImageInfo.Height = 1080;
    imageSet.ImageInfo.FileSize = static_cast<int>(imageRaw.size());
    imageSet.FaceList.push_back(face);
    imageSet.Data = imageData;

    std::list<GAT_1400_Face> faceList;
    faceList.push_back(face);
    int ret = PostFaces(faceList);
    if (ret == 0) {
        std::list<GAT_1400_ImageSet> imageList;
        imageList.push_back(imageSet);
        ret = PostImages(imageList);
    }

    printf("[GAT1400] module=gat1400 event=keepalive_demo trace=client error=%d image=%s\n",
           ret,
           kKeepaliveDemoImagePath);
    return ret;
}

int GAT1400ClientService::DrainPendingCaptureEvents()
{
    if (!IsStarted()) {
        return -1;
    }

    int processedCount = 0;
    int lastRet = 0;
    media::GAT1400CaptureEvent event;
    while (media::GAT1400CaptureControl::Instance().PopPending(&event)) {
        ++processedCount;
        const int ret = PostCaptureEvent(event);
        if (ret != 0) {
            lastRet = ret;
        }
    }

    if (processedCount > 0) {
        printf("[GAT1400] module=gat1400 event=capture_drain trace=bridge error=%d count=%d pending=%zu\n",
               lastRet,
               processedCount,
               media::GAT1400CaptureControl::Instance().PendingCount());
    }

    return lastRet;
}

int GAT1400ClientService::PostJsonWithResponseStatus(const char* action,
                                                     const char* path,
                                                     const std::string& body,
                                                     const std::string* overrideUrl)
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    const std::vector<int> retryPolicy = ParseRetryPolicy(cfg.gat_upload.retry_policy);
    int finalRet = -1;
    const std::string requestTarget = (overrideUrl != NULL && !overrideUrl->empty()) ? *overrideUrl : (path != NULL ? path : "");
    std::string lastError("ret=-1");
    for (size_t attempt = 0;; ++attempt) {
        HttpResponse response;
        const int reqRet = ExecuteRequest(cfg, deviceId, "POST", path != NULL ? path : "", kJsonContentType, body, response, overrideUrl);
        if (reqRet == 0) {
            GAT1400_RESPONSESTATUS_ST responseStatus;
            if (GAT1400Json::UnPackResponseStatus(response.body, responseStatus) == 0 && responseStatus.StatusCode == OK) {
                printf("[GAT1400] module=gat1400 event=%s trace=client error=0 path=%s\n",
                       action != NULL ? action : "post",
                       path != NULL ? path : "");
                return 0;
            }
            finalRet = -2;
        } else {
            finalRet = reqRet;
        }

        char errorText[64] = {0};
        snprintf(errorText, sizeof(errorText), "ret=%d", finalRet);
        lastError = errorText;

        if (attempt >= retryPolicy.size()) {
            break;
        }
        sleep(static_cast<unsigned int>(retryPolicy[attempt]));
    }

    if (ShouldPersistFailedUpload(finalRet)) {
        EnqueuePendingUpload(action,
                             "POST",
                             requestTarget,
                             kJsonContentType,
                             kResponseKindStatus,
                             body,
                             "",
                             lastError);
    }
    return finalRet;
}

int GAT1400ClientService::PostBinaryData(const char* action,
                                         const std::string& path,
                                         const std::string& data,
                                         const std::string* overrideUrl)
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    const std::vector<int> retryPolicy = ParseRetryPolicy(cfg.gat_upload.retry_policy);
    int finalRet = -1;
    const std::string requestTarget = (overrideUrl != NULL && !overrideUrl->empty()) ? *overrideUrl : path;
    const std::string requestPath = (overrideUrl != NULL) ? "" : path;
    std::string lastError("ret=-1");
    for (size_t attempt = 0;; ++attempt) {
        HttpResponse response;
        const int reqRet = ExecuteRequest(cfg,
                                          deviceId,
                                          "POST",
                                          requestPath,
                                          kBinaryContentType,
                                          data,
                                          response,
                                          overrideUrl);
        if (reqRet == 0) {
            GAT1400_RESPONSESTATUS_ST responseStatus;
            if (GAT1400Json::UnPackResponseStatus(response.body, responseStatus) == 0 && responseStatus.StatusCode == OK) {
                printf("[GAT1400] module=gat1400 event=%s trace=client error=0 path=%s\n",
                       action != NULL ? action : "post_binary",
                       path.c_str());
                return 0;
            }
            finalRet = -2;
        } else {
            finalRet = reqRet;
        }

        char errorText[64] = {0};
        snprintf(errorText, sizeof(errorText), "ret=%d", finalRet);
        lastError = errorText;

        if (attempt >= retryPolicy.size()) {
            break;
        }
        sleep(static_cast<unsigned int>(retryPolicy[attempt]));
    }

    if (ShouldPersistFailedUpload(finalRet)) {
        EnqueuePendingUpload(action,
                             "POST",
                             requestTarget,
                             kBinaryContentType,
                             kResponseKindStatus,
                             data,
                             "",
                             lastError);
    }
    return finalRet;
}

int GAT1400ClientService::RegisterNow()
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    HttpResponse response;
    const int reqRet = ExecuteRequest(cfg,
                                      deviceId,
                                      "POST",
                                      "/VIID/System/Register",
                                      kJsonContentType,
                                      GAT1400Json::PackRegisterJson(deviceId.c_str()),
                                      response,
                                      NULL);
    if (reqRet != 0) {
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_registered = false;
        }
        UpdateRegistState(EM_REGIST_OFF);
        return reqRet;
    }

    GAT1400_RESPONSESTATUS_ST responseStatus;
    if (GAT1400Json::UnPackResponseStatus(response.body, responseStatus) != 0 || responseStatus.StatusCode != OK) {
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_registered = false;
        }
        UpdateRegistState(EM_REGIST_OFF);
        return -2;
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_registered = true;
    }
    UpdateRegistState(EM_REGIST_ON);
    printf("[GAT1400] module=gat1400 event=register trace=client error=0 endpoint=%s:%d device=%s listen=%d\n",
           cfg.gat_register.server_ip.c_str(),
           cfg.gat_register.server_port,
           deviceId.c_str(),
           cfg.gat_register.listen_port);
    ReplayPendingUploads();
    (void)DrainPendingCaptureEvents();
    return 0;
}

int GAT1400ClientService::UnregisterNow()
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }
    if (deviceId.empty()) {
        return 0;
    }

    HttpResponse response;
    const int reqRet = ExecuteRequest(cfg,
                                      deviceId,
                                      "POST",
                                      "/VIID/System/UnRegister",
                                      kJsonContentType,
                                      GAT1400Json::PackUnRegisterJson(deviceId.c_str()),
                                      response,
                                      NULL);
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_registered = false;
    }
    UpdateRegistState(EM_REGIST_OFF);
    if (reqRet != 0) {
        return reqRet;
    }

    GAT1400_RESPONSESTATUS_ST responseStatus;
    if (GAT1400Json::UnPackResponseStatus(response.body, responseStatus) != 0 || responseStatus.StatusCode != OK) {
        return -2;
    }
    printf("[GAT1400] module=gat1400 event=unregister trace=client error=0 device=%s\n", deviceId.c_str());
    return 0;
}

int GAT1400ClientService::SendKeepaliveNow()
{
    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -1;
    }

    HttpResponse response;
    const int reqRet = ExecuteRequest(cfg,
                                      deviceId,
                                      "POST",
                                      "/VIID/System/Keepalive",
                                      kJsonContentType,
                                      GAT1400Json::PackKeepAliveJson(deviceId.c_str()),
                                      response,
                                      NULL);
    if (reqRet != 0) {
        return reqRet;
    }

    GAT1400_RESPONSESTATUS_ST responseStatus;
    if (GAT1400Json::UnPackResponseStatus(response.body, responseStatus) != 0 || responseStatus.StatusCode != OK) {
        return -2;
    }
    (void)DrainPendingCaptureEvents();
    (void)SendKeepaliveDemoUploadOnce(deviceId);
    return 0;
}

int GAT1400ClientService::GetTime(std::string& outTime)
{
    outTime.clear();
    return 0;
}

int GAT1400ClientService::Register()
{
    return RegisterNow();
}

int GAT1400ClientService::Unregister()
{
    return UnregisterNow();
}

int GAT1400ClientService::Keepalive()
{
    return SendKeepaliveNow();
}

int GAT1400ClientService::SendHttpCommand(const std::string& url,
                                          unsigned int method,
                                          unsigned int format,
                                          const std::string& content)
{
    switch (format) {
        case POST_REGISTER:
            return RegisterNow();
        case POST_UNREGISTER:
            return UnregisterNow();
        case POST_KEEPALIVE:
            return SendKeepaliveNow();
        case GET_SYNCTIME: {
            std::string remoteTime;
            return GetTime(remoteTime);
        }
        default:
            break;
    }

    const char* methodText = HttpMethodToString(method);
    if (methodText == NULL) {
        return -1;
    }

    const std::string target = !TrimCopy(url).empty() ? TrimCopy(url) : DefaultPathForUriType(format);
    if (target.empty()) {
        return -2;
    }

    const std::string* overrideUrl = IsAbsoluteRequestTarget(target) ? &target : NULL;
    const std::string requestPath = (overrideUrl != NULL) ? "" : target;
    if (method == HTTP_REQUEST_POST) {
        if (IsBinaryUploadUriType(format)) {
            return PostBinaryData("sdk_http_cmd", requestPath.empty() ? target : requestPath, content, overrideUrl);
        }
        if (IsListResponseUriType(format)) {
            return PostJsonWithResponseList("sdk_http_cmd", requestPath.c_str(), content, overrideUrl);
        }
        return PostJsonWithResponseStatus("sdk_http_cmd", requestPath.c_str(), content, overrideUrl);
    }

    ProtocolExternalConfig cfg;
    std::string deviceId;
    if (SnapshotConfig(cfg, deviceId) != 0) {
        return -3;
    }

    HttpResponse response;
    const int reqRet = ExecuteRequest(cfg,
                                      deviceId,
                                      methodText,
                                      requestPath,
                                      kJsonContentType,
                                      content,
                                      response,
                                      overrideUrl);
    if (reqRet != 0) {
        return reqRet;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        return -4;
    }

    int statusCode = 0;
    const int parseRet = IsListResponseUriType(format)
                             ? ParseResponseStatusListBody(response.body, statusCode)
                             : ParseResponseStatusBody(response.body, statusCode);
    if (parseRet == 0) {
        return 0;
    }
    if (statusCode != 0) {
        return statusCode;
    }
    return 0;
}

void GAT1400ClientService::HeartbeatLoop()
{
    while (m_heartbeat_running.load()) {
        ProtocolExternalConfig cfg;
        std::string deviceId;
        if (SnapshotConfig(cfg, deviceId) != 0) {
            break;
        }

        bool registered = false;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            registered = m_registered;
        }
        if (!registered) {
            std::vector<int> backoff = ParseRetryPolicy(cfg.gat_register.retry_backoff_policy);
            if (backoff.empty()) {
                backoff.push_back(5);
            }

            size_t attempt = 0;
            while (m_heartbeat_running.load()) {
                const int regRet = RegisterNow();
                if (regRet == 0) {
                    break;
                }

                const int delay = backoff[std::min(attempt, backoff.size() - 1)];
                printf("[GAT1400] module=gat1400 event=register trace=client error=%d attempt=%zu delay=%d\n",
                       regRet,
                       attempt + 1,
                       delay);
                for (int i = 0; i < delay && m_heartbeat_running.load(); ++i) {
                    sleep(1);
                }
                if (attempt + 1 < backoff.size()) {
                    ++attempt;
                }
            }
            continue;
        }

        const int interval = std::max(1, cfg.gat_register.keepalive_interval_sec);
        for (int i = 0; i < interval && m_heartbeat_running.load(); ++i) {
            sleep(1);
        }
        if (!m_heartbeat_running.load()) {
            break;
        }

        const int maxRetry = std::max(1, cfg.gat_register.max_retry);
        int ret = 0;
        int retry = 0;
        do {
            ret = SendKeepaliveNow();
            if (ret == 0) {
                break;
            }
            ++retry;
        } while (retry < maxRetry && m_heartbeat_running.load());

        if (ret == 0) {
            ReplayPendingUploadsIfDue();
            continue;
        }

        printf("[GAT1400] module=gat1400 event=keepalive trace=client error=%d retry=%d\n", ret, retry);
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_registered = false;
        }
        UpdateRegistState(EM_REGIST_OFF);
    }
}

int GAT1400ClientService::Start(const ProtocolExternalConfig& cfg, const GbRegisterParam& gbRegister)
{
    Stop();

    const std::string deviceId = ResolveGatRuntimeDeviceId(cfg, &gbRegister);
    if (cfg.gat_register.server_ip.empty() || cfg.gat_register.server_port <= 0 ||
        cfg.gat_register.listen_port <= 0 || deviceId.empty()) {
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_cfg = cfg;
        m_started = true;
        m_registered = false;
        m_regist_state = EM_REGIST_OFF;
        m_keepalive_demo_upload_attempted.store(false);
        if (StartServerLocked() != 0) {
            m_started = false;
            return -2;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        LoadPendingUploadsLocked(cfg);
        m_last_replay_time = 0;
    }

    const int regRet = RegisterNow();
    if (regRet != 0) {
        printf("[GAT1400] module=gat1400 event=register trace=client error=%d device=%s note=defer_retry\n",
               regRet,
               deviceId.c_str());
        UpdateRegistState(EM_REGIST_OFF);
    }

    if (regRet == 0) {
        printf("[GAT1400] module=gat1400 event=get_time trace=client error=0 device=%s note=disabled\n",
               deviceId.c_str());
    } else {
        printf("[GAT1400] module=gat1400 event=get_time trace=client error=%d device=%s note=skip_register_failed\n",
               regRet,
               deviceId.c_str());
    }

    m_heartbeat_running.store(true);
    m_heartbeat_thread = std::thread(&GAT1400ClientService::HeartbeatLoop, this);
    return 0;
}

void GAT1400ClientService::Stop()
{
    std::string deviceId;
    bool shouldStop = false;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        shouldStop = m_started || m_registered || m_server_running.load();
        deviceId = ResolveGatRuntimeDeviceId(m_cfg);
        m_started = false;
        m_heartbeat_running.store(false);
        m_keepalive_demo_upload_attempted.store(false);
    }

    if (m_heartbeat_thread.joinable()) {
        m_heartbeat_thread.join();
    }

    if (!deviceId.empty()) {
        UnregisterNow();
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        StopServerLocked();
        m_registered = false;
        (void)shouldStop;
    }

    UpdateRegistState(EM_REGIST_OFF);
}

int GAT1400ClientService::Reload(const ProtocolExternalConfig& cfg, const GbRegisterParam& gbRegister)
{
    const std::string nextDeviceId = ResolveGatRuntimeDeviceId(cfg, &gbRegister);
    bool started = false;
    bool restartRequired = false;
    bool pendingConfigChanged = false;
    bool replayConfigChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        started = m_started;
        const std::string currentDeviceId = ResolveGatRuntimeDeviceId(m_cfg);
        restartRequired = (m_cfg.gat_register.server_ip != cfg.gat_register.server_ip) ||
                          (m_cfg.gat_register.server_port != cfg.gat_register.server_port) ||
                          (m_cfg.gat_register.listen_port != cfg.gat_register.listen_port) ||
                          (m_cfg.gat_register.username != cfg.gat_register.username) ||
                          (m_cfg.gat_register.password != cfg.gat_register.password) ||
                          (currentDeviceId != nextDeviceId);
        pendingConfigChanged = (m_cfg.gat_upload.queue_dir != cfg.gat_upload.queue_dir) ||
                               (m_cfg.gat_upload.max_pending_count != cfg.gat_upload.max_pending_count);
        replayConfigChanged = pendingConfigChanged ||
                              (m_cfg.gat_upload.replay_interval_sec != cfg.gat_upload.replay_interval_sec);
        if (!started) {
            m_cfg = cfg;
            return 0;
        }
    }

    if (restartRequired) {
        Stop();
        return Start(cfg, gbRegister);
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_cfg = cfg;
    }

    if (pendingConfigChanged || replayConfigChanged) {
        std::lock_guard<std::mutex> pendingLock(m_pending_mutex);
        if (pendingConfigChanged) {
            LoadPendingUploadsLocked(cfg);
        }
        while (static_cast<int>(m_pending_uploads.size()) > std::max(1, cfg.gat_upload.max_pending_count)) {
            const PendingUploadItem oldest = m_pending_uploads.front();
            RemoveFileIfExists(oldest.persist_path);
            m_pending_uploads.pop_front();
        }
        if (replayConfigChanged) {
            m_last_replay_time = 0;
        }
    }
    return 0;
}

template <typename T, typename PackFn>
int PostBatched(const std::list<T>& input,
                int batchSize,
                const char* action,
                const char* path,
                const PackFn& pack,
                GAT1400ClientService* service)
{
    if (input.empty()) {
        return 0;
    }

    typename std::list<T>::const_iterator it = input.begin();
    while (it != input.end()) {
        std::list<T> batch = SliceBatch<T>(it, input.end(), batchSize);
        const int ret = service->PostJsonWithResponseList(action, path, pack(batch));
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

int GAT1400ClientService::PostFaces(const std::list<GAT_1400_Face>& faceList)
{
    return PostBatched<GAT_1400_Face>(faceList, std::max(1, m_cfg.gat_upload.batch_size), "post_faces", "/VIID/Faces",
                                      [](const std::list<GAT_1400_Face>& batch) { return GAT1400Json::PackFaceListJson(batch); },
                                      this);
}

int GAT1400ClientService::PostPersons(const std::list<GAT_1400_Person>& personList)
{
    return PostBatched<GAT_1400_Person>(personList, std::max(1, m_cfg.gat_upload.batch_size), "post_persons", "/VIID/Persons",
                                        [](const std::list<GAT_1400_Person>& batch) { return GAT1400Json::PackPersonListJson(batch); },
                                        this);
}

int GAT1400ClientService::PostMotorVehicles(const std::list<GAT_1400_Motor>& motorList)
{
    return PostBatched<GAT_1400_Motor>(motorList, std::max(1, m_cfg.gat_upload.batch_size), "post_motor", "/VIID/MotorVehicles",
                                       [](const std::list<GAT_1400_Motor>& batch) { return GAT1400Json::PackMotorVehicleListJson(batch); },
                                       this);
}

int GAT1400ClientService::PostNonMotorVehicles(const std::list<GAT_1400_NonMotor>& nonMotorList)
{
    return PostBatched<GAT_1400_NonMotor>(nonMotorList, std::max(1, m_cfg.gat_upload.batch_size), "post_non_motor", "/VIID/NonMotorVehicles",
                                          [](const std::list<GAT_1400_NonMotor>& batch) { return GAT1400Json::PackNonmotorVehicleListJson(batch); },
                                          this);
}

int GAT1400ClientService::PostThings(const std::list<GAT_1400_Thing>& thingList)
{
    return PostBatched<GAT_1400_Thing>(thingList, std::max(1, m_cfg.gat_upload.batch_size), "post_things", "/VIID/Things",
                                       [](const std::list<GAT_1400_Thing>& batch) { return GAT1400Json::PackThingListJson(batch); },
                                       this);
}

int GAT1400ClientService::PostScenes(const std::list<GAT_1400_Scene>& sceneList)
{
    return PostBatched<GAT_1400_Scene>(sceneList, std::max(1, m_cfg.gat_upload.batch_size), "post_scenes", "/VIID/Scenes",
                                       [](const std::list<GAT_1400_Scene>& batch) { return GAT1400Json::PackSceneListJson(batch); },
                                       this);
}

int GAT1400ClientService::PostAnalysisRules(const std::list<GAT_1400_AnalysisRule>& ruleList)
{
    return PostBatched<GAT_1400_AnalysisRule>(ruleList, std::max(1, m_cfg.gat_upload.batch_size), "post_analysis_rules", "/VIID/AnalysisRules",
                                              [](const std::list<GAT_1400_AnalysisRule>& batch) { return GAT1400Json::PackAnalysisRuleListJson(batch); },
                                              this);
}

int GAT1400ClientService::PostVideoLabels(const std::list<GAT_1400_VideoLabel>& labelList)
{
    return PostBatched<GAT_1400_VideoLabel>(labelList, std::max(1, m_cfg.gat_upload.batch_size), "post_video_labels", "/VIID/VideoLabels",
                                            [](const std::list<GAT_1400_VideoLabel>& batch) { return GAT1400Json::PackVideoLabelListJson(batch); },
                                            this);
}

int GAT1400ClientService::PostDispositions(const std::list<GAT_1400_Disposition>& dispositionList)
{
    return PostBatched<GAT_1400_Disposition>(dispositionList, std::max(1, m_cfg.gat_upload.batch_size), "post_dispositions", "/VIID/Dispositions",
                                             [](const std::list<GAT_1400_Disposition>& batch) { return GAT1400Json::PackDispositionListJson(batch); },
                                             this);
}

int GAT1400ClientService::PostDispositionNotifications(const std::list<GAT_1400_Disposition_Notification>& notificationList)
{
    return PostBatched<GAT_1400_Disposition_Notification>(notificationList, std::max(1, m_cfg.gat_upload.batch_size), "post_disposition_notifications", "/VIID/DispositionNotifications",
                                                          [](const std::list<GAT_1400_Disposition_Notification>& batch) { return GAT1400Json::PackDispositionNotificationListJson(batch); },
                                                          this);
}

int GAT1400ClientService::PostSubscribes(const std::list<GAT_1400_Subscribe>& subscribeList)
{
    return PostBatched<GAT_1400_Subscribe>(subscribeList, std::max(1, m_cfg.gat_upload.batch_size), "post_subscribes", "/VIID/Subscribes",
                                           [](const std::list<GAT_1400_Subscribe>& batch) { return GAT1400Json::PackSubscribeListJson(batch); },
                                           this);
}

int GAT1400ClientService::PostSubscribeNotifications(const std::list<GAT_1400_Subscribe_Notification>& notificationList,
                                                     const std::string& url)
{
    if (notificationList.empty() || url.empty()) {
        return 0;
    }
    return PostJsonWithResponseStatus("post_subscribe_notifications", "", GAT1400Json::PackSubscribeNotificationListJson(notificationList), &url);
}

int GAT1400ClientService::PostApes(const std::list<GAT_1400_Ape>& apeList)
{
    return PostBatched<GAT_1400_Ape>(apeList, std::max(1, m_cfg.gat_upload.batch_size), "post_apes", "/VIID/APEs",
                                     [](const std::list<GAT_1400_Ape>& batch) { return GAT1400Json::PackApeListJson(batch); },
                                     this);
}

int GAT1400ClientService::PostVideoSlices(const std::list<GAT_1400_VideoSliceSet>& videoSliceList)
{
    if (videoSliceList.empty()) {
        return 0;
    }

    const int batchSize = std::max(1, m_cfg.gat_upload.batch_size);
    std::list<GAT_1400_VideoSliceSet>::const_iterator it = videoSliceList.begin();
    while (it != videoSliceList.end()) {
        std::list<GAT_1400_VideoSliceSet> batch = SliceBatch<GAT_1400_VideoSliceSet>(it, videoSliceList.end(), batchSize);
        int ret = PostJsonWithResponseList("post_video_slices",
                                           "/VIID/VideoSlices",
                                           GAT1400Json::PackVideoSliceListJson(BuildVideoSliceMetadataBatch(batch)));
        if (ret != 0) {
            return ret;
        }
        for (std::list<GAT_1400_VideoSliceSet>::const_iterator item = batch.begin(); item != batch.end(); ++item) {
            if (!item->Data.empty()) {
                std::string path = std::string("/VIID/VideoSlices/") + item->VideoSliceInfo.VideoID + "/Data";
                ret = PostBinaryData("post_video_slice_data", path, item->Data);
                if (ret != 0) {
                    return ret;
                }
            }
        }
    }
    return 0;
}

int GAT1400ClientService::PostImages(const std::list<GAT_1400_ImageSet>& imageList)
{
    if (imageList.empty()) {
        return 0;
    }

    const int batchSize = std::max(1, m_cfg.gat_upload.batch_size);
    std::list<GAT_1400_ImageSet>::const_iterator it = imageList.begin();
    while (it != imageList.end()) {
        std::list<GAT_1400_ImageSet> batch = SliceBatch<GAT_1400_ImageSet>(it, imageList.end(), batchSize);
        int ret = PostJsonWithResponseList("post_images",
                                           "/VIID/Images",
                                           GAT1400Json::PackImageListJson(BuildImageMetadataBatch(batch)));
        if (ret != 0) {
            return ret;
        }
        for (std::list<GAT_1400_ImageSet>::const_iterator item = batch.begin(); item != batch.end(); ++item) {
            if (!item->Data.empty()) {
                std::string path = std::string("/VIID/Images/") + item->ImageInfo.ImageID + "/Data";
                ret = PostBinaryData("post_image_data", path, item->Data);
                if (ret != 0) {
                    return ret;
                }
            }
        }
    }
    return 0;
}

int GAT1400ClientService::PostFiles(const std::list<GAT_1400_FileSet>& fileList)
{
    if (fileList.empty()) {
        return 0;
    }

    const int batchSize = std::max(1, m_cfg.gat_upload.batch_size);
    std::list<GAT_1400_FileSet>::const_iterator it = fileList.begin();
    while (it != fileList.end()) {
        std::list<GAT_1400_FileSet> batch = SliceBatch<GAT_1400_FileSet>(it, fileList.end(), batchSize);
        int ret = PostJsonWithResponseList("post_files",
                                           "/VIID/Files",
                                           GAT1400Json::PackFileListJson(BuildFileMetadataBatch(batch)));
        if (ret != 0) {
            return ret;
        }
        for (std::list<GAT_1400_FileSet>::const_iterator item = batch.begin(); item != batch.end(); ++item) {
            if (!item->Data.empty()) {
                std::string path = std::string("/VIID/Files/") + item->FileInfo.FileID + "/Data";
                ret = PostBinaryData("post_file_data", path, item->Data);
                if (ret != 0) {
                    return ret;
                }
            }
        }
    }
    return 0;
}

}  // namespace protocol


