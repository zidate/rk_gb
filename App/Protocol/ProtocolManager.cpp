#include "ProtocolManager.h"
#include "gat1400/GAT1400ClientService.h"



#include <stdio.h>

#include <string.h>

#include <chrono>

#include <time.h>

#include <vector>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>



#ifndef PROTOCOL_HAS_GB28181_CLIENT_SDK

#define PROTOCOL_HAS_GB28181_CLIENT_SDK 0

#endif



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

#include "GB28181ClientSDK.h"

#endif



#include "Storage_api.h"
#include "Manager/ConfigManager.h"
#include "ExchangeAL/ExchangeKind.h"

#include "Ptz/Ptz.h"

extern "C"
{
#include "PAL/Capture.h"
#include "PAL/Audio_coder.h"
int rk_video_get_gop(int stream_id, int *value);
int rk_video_set_gop(int stream_id, int value);
int rk_video_get_max_rate(int stream_id, int *value);
int rk_video_set_max_rate(int stream_id, int value);
int rk_video_get_output_data_type(int stream_id, const char **value);
int rk_video_set_output_data_type(int stream_id, const char *value);
int rk_video_get_frame_rate(int stream_id, char **value);
int rk_video_set_frame_rate(int stream_id, const char *value);
int rk_video_set_resolution(int stream_id, const char *value);
int rk_isp_get_image_flip(int cam_id, const char **value);
int rk_isp_set_image_flip(int cam_id, const char *value);
int rk_osd_get_enabled(int id, int *value);
int rk_osd_set_enabled(int id, int value);
int rk_osd_restart();
int rk_osd_get_position_x(int id, int *value);
int rk_osd_set_position_x(int id, int value);
int rk_osd_get_position_y(int id, int *value);
int rk_osd_set_position_y(int id, int value);
int rk_osd_get_date_style(int id, const char **value);
int rk_osd_set_date_style(int id, const char *value);
int rk_osd_get_time_style(int id, const char **value);
int rk_osd_set_time_style(int id, const char *value);
int rk_osd_get_display_text(int id, const char **value);
int rk_osd_set_display_text(int id, const char *value);
}



extern void NormalRestart();

extern bool CreateDetachedThread(char *threadName, void *(*route)(void*), void *param, bool scope);



namespace protocol

{

class ProtocolManager;

}



namespace

{

static protocol::ProtocolManager* g_gb_live_audio_manager = NULL;

static const char* kGbLiveDmcModuleName = "gb28181_live";
static const int kRkOsdDateTimeId = 1;
static const int kRkOsdCustomTextId = 2;
static const char* kGbUpgradePackagePath = "/tmp/upgrade.bin";
static const char* kGbUpgradePackageTempPath = "/tmp/upgrade.bin.download";
static const char* kGbUpgradeTriggerFlag = "/tmp/ota_upgrade_flag";
static const char* kGbUpgradeConfigPendingKey = "GbPending";
static const char* kGbUpgradeConfigSessionKey = "GbSessionID";
static const char* kGbUpgradeConfigFirmwareKey = "GbFirmware";
static const char* kGbUpgradeConfigDeviceKey = "GbDeviceID";
static const char* kGbUpgradeConfigDescriptionKey = "GbDescription";



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



    if (codec == "pcm" || codec == "l16" || codec == "pcm16" || codec == "pcm_s16le") {

        return "pcm";

    }



    return "";

}

static std::string NormalizeRkVideoCodec(const std::string& codecIn)
{
    const std::string codec = ToLowerCopy(codecIn);
    if (codec.empty()) {
        return "";
    }

    if (codec == "h264" || codec == "h.264" || codec == "avc") {
        return "H.264";
    }

    if (codec == "h265" || codec == "h.265" || codec == "hevc") {
        return "H.265";
    }

    return codecIn;
}

static std::string BuildFrameRateValue(int fps)
{
    if (fps <= 0) {
        return "";
    }

    char buffer[16] = {0};
    snprintf(buffer, sizeof(buffer), "%d", fps);
    return buffer;
}

static std::string NormalizeResolutionValue(const std::string& value, char separator)
{
    std::string compact;
    compact.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != ' ' && value[i] != '\t') {
            compact.push_back(value[i]);
        }
    }

    if (compact.empty()) {
        return "";
    }

    int width = 0;
    int height = 0;
    if (sscanf(compact.c_str(), "%d%*[*xX]%d", &width, &height) == 2 && width > 0 && height > 0) {
        char buffer[32] = {0};
        snprintf(buffer, sizeof(buffer), "%d%c%d", width, separator, height);
        return buffer;
    }

    return compact;
}

static std::string NormalizeGbImageFlipMode(const std::string& modeIn)
{
    const std::string mode = ToLowerCopy(modeIn);
    if (mode.empty() || mode == "none" || mode == "close" || mode == "off" || mode == "restore" || mode == "reset") {
        return "close";
    }

    if (mode == "flip" || mode == "vertical" || mode == "vflip" || mode == "updown" || mode == "up_down") {
        return "flip";
    }

    if (mode == "mirror" || mode == "horizontal" || mode == "hflip" || mode == "leftright" || mode == "left_right") {
        return "mirror";
    }

    if (mode == "centrosymmetric" || mode == "center" || mode == "both" || mode == "all") {
        return "centrosymmetric";
    }

    return modeIn;
}

struct RkOsdAnchor
{
    int x;
    int y;
    bool valid;

    RkOsdAnchor() : x(0), y(0), valid(false) {}
};

static bool ContainsToken(const std::string& text, const char* token)
{
    return token != NULL && text.find(token) != std::string::npos;
}

static std::string NormalizeRkOsdDateStyle(const std::string& formatIn)
{
    const std::string format = ToLowerCopy(formatIn);
    if (format.empty()) {
        return "CHR-YYYY-MM-DD";
    }

    if (ContainsToken(format, "yyyy/mm/dd")) {
        return "CHR-YYYY/MM/DD";
    }
    if (ContainsToken(format, "mm/dd/yyyy")) {
        return "CHR-MM/DD/YYYY";
    }
    if (ContainsToken(format, "dd/mm/yyyy")) {
        return "CHR-DD/MM/YYYY";
    }
    if (ContainsToken(format, "mm-dd-yyyy")) {
        return "CHR-MM-DD-YYYY";
    }
    if (ContainsToken(format, "dd-mm-yyyy")) {
        return "CHR-DD-MM-YYYY";
    }

    return "CHR-YYYY-MM-DD";
}

static std::string NormalizeRkOsdTimeStyle(const std::string& formatIn)
{
    const std::string format = ToLowerCopy(formatIn);
    if (format.empty()) {
        return "24hour";
    }

    if (ContainsToken(format, "tt") ||
        ContainsToken(format, " am") ||
        ContainsToken(format, " pm") ||
        ContainsToken(format, "a/p") ||
        ContainsToken(format, "12hour")) {
        return "12hour";
    }

    return "24hour";
}

static bool QueryMainStreamResolution(int& width, int& height)
{
    width = 0;
    height = 0;
    return CaptureGetResolution(0, &width, &height) == 0 && width > 0 && height > 0;
}

static RkOsdAnchor ResolveRkOsdAnchor(const std::string& positionIn)
{
    RkOsdAnchor anchor;
    const std::string position = ToLowerCopy(positionIn);
    if (position.empty()) {
        return anchor;
    }

    if (sscanf(position.c_str(), "%d,%d", &anchor.x, &anchor.y) == 2 ||
        sscanf(position.c_str(), "%d:%d", &anchor.x, &anchor.y) == 2) {
        anchor.valid = (anchor.x >= 0 && anchor.y >= 0);
        return anchor;
    }

    int width = 1280;
    int height = 720;
    (void)QueryMainStreamResolution(width, height);

    const int marginX = 16;
    const int marginY = 16;
    const int estimatedTextWidth = 176;
    const int estimatedTextHeight = 64;

    if (position == "top_left" || position == "left_top" || position == "top-left") {
        anchor.x = marginX;
        anchor.y = marginY;
        anchor.valid = true;
        return anchor;
    }

    if (position == "top_right" || position == "right_top" || position == "top-right") {
        anchor.x = (width > estimatedTextWidth + marginX) ? (width - estimatedTextWidth) : marginX;
        anchor.y = marginY;
        anchor.valid = true;
        return anchor;
    }

    if (position == "bottom_left" || position == "left_bottom" || position == "bottom-left") {
        anchor.x = marginX;
        anchor.y = (height > estimatedTextHeight + marginY) ? (height - estimatedTextHeight) : marginY;
        anchor.valid = true;
        return anchor;
    }

    if (position == "bottom_right" || position == "right_bottom" || position == "bottom-right") {
        anchor.x = (width > estimatedTextWidth + marginX) ? (width - estimatedTextWidth) : marginX;
        anchor.y = (height > estimatedTextHeight + marginY) ? (height - estimatedTextHeight) : marginY;
        anchor.valid = true;
        return anchor;
    }

    if (position == "center" || position == "middle") {
        anchor.x = width / 2;
        anchor.y = height / 2;
        anchor.valid = true;
        return anchor;
    }

    return anchor;
}



static int ResolvePayloadTypeByCodec(const std::string& codec)

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



static const char* ResolveMimeByCodec(const std::string& codec)

{

    const std::string normalized = NormalizeCodec(codec);

    if (normalized == "g711a") {

        return "PCMA";

    }



    if (normalized == "g711u") {

        return "PCMU";

    }



    return "L16";

}



static void CopyBounded(char* dst, size_t dstSize, const std::string& src)

{

    if (dst == NULL || dstSize == 0) {

        return;

    }



    const size_t copySize = (src.size() < (dstSize - 1)) ? src.size() : (dstSize - 1);

    if (copySize > 0) {

        memcpy(dst, src.data(), copySize);

    }

    dst[copySize] = '\0';

}



static std::string SafeStr(const char* s, size_t maxLen)

{

    if (s == NULL || maxLen == 0) {

        return "";

    }



    size_t len = 0;

    while (len < maxLen && s[len] != '\0') {

        ++len;

    }



    return std::string(s, len);

}




static std::string TrimWhitespaceCopy(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }

    return text.substr(begin, end - begin);
}

static std::string NormalizeHexDigest(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            out.push_back((ch >= 'A' && ch <= 'F') ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
    }
    return out;
}

static std::string EscapeShellSingleQuote(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\'') {
            out += "'\\''";
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

static bool StartsWithNoCase(const std::string& text, const char* prefix)
{
    if (prefix == NULL) {
        return false;
    }

    const size_t prefixLen = strlen(prefix);
    if (text.size() < prefixLen) {
        return false;
    }

    for (size_t i = 0; i < prefixLen; ++i) {
        char left = text[i];
        char right = prefix[i];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }

    return true;
}

static bool IsHttpUrl(const std::string& url)
{
    return StartsWithNoCase(url, "http://") || StartsWithNoCase(url, "https://");
}

static bool IsValidUpgradeSessionId(const std::string& sessionId)
{
    if (sessionId.size() < 32 || sessionId.size() > 128) {
        return false;
    }

    for (size_t i = 0; i < sessionId.size(); ++i) {
        const char ch = sessionId[i];
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = (ch >= '0' && ch <= '9');
        if (!alpha && !digit && ch != '-') {
            return false;
        }
    }

    return true;
}

static bool IsUpgradeUrlAllowed(const std::string& url, const std::vector<std::string>& whitelist)
{
    if (whitelist.empty()) {
        return true;
    }

    for (size_t i = 0; i < whitelist.size(); ++i) {
        const std::string prefix = TrimWhitespaceCopy(whitelist[i]);
        if (!prefix.empty() && url.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }

    return false;
}

static void RemoveFileQuietly(const char* path)
{
    if (path != NULL && path[0] != ' ') {
        remove(path);
    }
}

static bool ReadFileSizeBytes(const char* path, unsigned long long& outSize)
{
    outSize = 0;
    if (path == NULL || path[0] == ' ') {
        return false;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(path, &st) != 0) {
        return false;
    }

    outSize = static_cast<unsigned long long>(st.st_size);
    return true;
}

static int RunShellCommand(const std::string& command)
{
    const int rc = system(command.c_str());
    return rc == 0 ? 0 : -1;
}

static bool ReadShellOutput(const std::string& command, std::string& output)
{
    output.clear();
    FILE* fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        return false;
    }

    char buf[256] = {0};
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        output += buf;
    }

    const int rc = pclose(fp);
    if (rc != 0) {
        output.clear();
        return false;
    }

    output = TrimWhitespaceCopy(output);
    return !output.empty();
}

static int DownloadFileByCurl(const std::string& url, const char* outputPath, int timeoutSec)
{
    if (url.empty() || outputPath == NULL || outputPath[0] == ' ') {
        return -1;
    }

    char timeoutBuf[16] = {0};
    snprintf(timeoutBuf, sizeof(timeoutBuf), "%d", (timeoutSec > 0) ? timeoutSec : 120);
    const std::string command = "curl -L -f -sS --connect-timeout 5 --max-time " +
        std::string(timeoutBuf) +
        " -o '" + EscapeShellSingleQuote(outputPath) + "' '" + EscapeShellSingleQuote(url) + "'";
    return RunShellCommand(command);
}

static std::string ResolveUpgradeDigestMode(const std::string& checksum, const protocol::ProtocolExternalConfig& cfg)
{
    const std::string normalizedChecksum = NormalizeHexDigest(checksum);
    if (normalizedChecksum.size() == 32) {
        return "md5";
    }
    if (normalizedChecksum.size() == 64) {
        return "sha256";
    }

    return ToLowerCopy(TrimWhitespaceCopy(cfg.gb_upgrade.verify_mode));
}

static bool ComputeFileDigest(const char* path, const std::string& mode, std::string& digest)
{
    if (path == NULL || path[0] == ' ') {
        return false;
    }

    std::string command;
    if (mode == "md5") {
        command = "md5sum '" + EscapeShellSingleQuote(path) + "' 2>/dev/null";
    } else if (mode == "sha256") {
        command = "sha256sum '" + EscapeShellSingleQuote(path) + "' 2>/dev/null";
    } else {
        return false;
    }

    std::string output;
    if (!ReadShellOutput(command, output)) {
        return false;
    }

    const size_t spacePos = output.find(' ');
    digest = NormalizeHexDigest((spacePos == std::string::npos) ? output : output.substr(0, spacePos));
    return !digest.empty();
}

static bool TouchFileMarker(const char* path)
{
    if (path == NULL || path[0] == ' ') {
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }

    fclose(fp);
    return true;
}

static bool ExtractAudioTransportHint(const MediaInfo* input,

                                      std::string& remoteIp,

                                      int& remotePort,

                                      int& payloadType,

                                      std::string& codec)

{

    if (input == NULL) {

        return false;

    }



    remoteIp = SafeStr(input->IP, sizeof(input->IP));

    remotePort = (int)input->Port;



    payloadType = -1;

    codec.clear();



    if (input->RtpDescri.DescriNum > 0 && input->RtpDescri.mapDescri != NULL) {

        const RtpMap& map = input->RtpDescri.mapDescri[0];

        payloadType = (int)map.MediaFormat;

        codec = NormalizeCodec(SafeStr(map.MimeType, sizeof(map.MimeType)));

    }



    if (codec.empty() && payloadType == 8) {

        codec = "g711a";

    }



    if (codec.empty() && payloadType == 0) {

        codec = "g711u";

    }



    if (remotePort <= 0 || payloadType < 0 || payloadType > 127 || codec.empty()) {

        return false;

    }



    return true;

}



static bool ExtractStreamEndpoint(const MediaInfo* input,

                                  std::string& remoteIp,

                                  int& remotePort)

{

    if (input == NULL) {

        return false;

    }



    remoteIp = SafeStr(input->IP, sizeof(input->IP));

    remotePort = (int)input->Port;

    if (remoteIp.empty() || remotePort <= 0) {

        return false;

    }



    return true;

}



static uint64_t TimestampMsToPts90k(uint64_t timestampMs)

{

    return timestampMs * 90ULL;

}



static uint64_t GetNowMs()

{

    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(

        std::chrono::system_clock::now().time_since_epoch()).count());

}



static bool IsAudioMimeType(const std::string& mime)

{

    const std::string lower = ToLowerCopy(mime);

    return NormalizeCodec(lower) == "g711a" ||

           NormalizeCodec(lower) == "g711u" ||

           NormalizeCodec(lower) == "pcm" ||

           lower == "aac" ||

           lower == "mpeg4-generic" ||

           lower == "g722";

}



static bool IsVideoPsMimeType(const std::string& mime)

{

    const std::string lower = ToLowerCopy(mime);

    return lower == "ps" || lower == "h264" || lower == "h265" || lower == "hevc" || lower == "mpeg4";

}



static bool IsLiveAudioRequested(const MediaInfo* input)

{

    if (input == NULL || input->RtpDescri.DescriNum == 0 || input->RtpDescri.mapDescri == NULL) {

        return false;

    }



    for (unsigned int i = 0; i < input->RtpDescri.DescriNum; ++i) {

        const RtpMap& map = input->RtpDescri.mapDescri[i];

        const std::string mime = SafeStr(map.MimeType, sizeof(map.MimeType));

        if (IsAudioMimeType(mime)) {

            return true;

        }



        if (!mime.empty() && !IsVideoPsMimeType(mime) && map.SampleRate > 0 && map.SampleRate != 90000) {

            return true;

        }

    }



    return false;

}



static bool PreferSubStream(const std::string& streamId)

{

    const std::string lower = ToLowerCopy(streamId);

    return lower == "sub" ||

           lower == "substream" ||

           lower == "sub_stream" ||

           lower == "minor" ||

           lower == "secondary" ||

           lower == "1";

}

static int ResolveGbStreamNumber(const MediaInfo* input, const protocol::ProtocolExternalConfig& cfg)

{

    if (input != NULL && input->StreamNum >= 0) {

        return (input->StreamNum <= 0) ? 0 : 1;

    }

    return PreferSubStream(cfg.gb_live.video_stream_id) ? 1 : 0;

}

static std::string BuildGbStreamNumberList()

{

    int subWidth = 0;

    int subHeight = 0;

    if (CaptureGetResolution(1, &subWidth, &subHeight) == 0 && subWidth > 0 && subHeight > 0) {

        return "0/1";

    }

    return "0";

}

static std::string ResolveGbDeviceName(const protocol::ProtocolExternalConfig& cfg, const std::string& runtimeName)

{

    if (!runtimeName.empty()) {

        return runtimeName;

    }



    if (!cfg.gb_register.device_name.empty()) {

        return cfg.gb_register.device_name;

    }

    if (!cfg.gb_register.device_id.empty()) {

        return cfg.gb_register.device_id;

    }



    return "IPC";

}

static std::string ResolveGbManufacturerName(const protocol::ProtocolExternalConfig& cfg)
{
    (void)cfg;
    return "IPC";
}

static std::string ResolveGbModelName(const protocol::ProtocolExternalConfig& cfg)
{
    (void)cfg;
    return "RC0240";
}

static std::string ResolveGbDeviceTypeName()
{
    return "IPC";
}

static unsigned int ResolveGbVideoChannelCount()
{
    return 1U;
}



static bool IsConfigTypeRequested(const QueryParam* param, ConfigType type)

{

    if (param == NULL) {

        return false;

    }



    const ConfigDownloadQuery& query = param->query_descri.config_param;

    if (query.Num == 0) {

        return true;

    }



    const unsigned int maxNum = sizeof(query.Type) / sizeof(query.Type[0]);

    const unsigned int count = (query.Num < maxNum) ? query.Num : maxNum;

    for (unsigned int index = 0; index < count; ++index) {

        if (query.Type[index] == type) {

            return true;

        }

    }



    return false;

}



static std::string BuildGbVideoResolutionSummary()

{

    int mainWidth = 0;

    int mainHeight = 0;

    int subWidth = 0;

    int subHeight = 0;



    char buffer[64] = {0};

    const int mainRet = CaptureGetResolution(0, &mainWidth, &mainHeight);

    const int subRet = CaptureGetResolution(1, &subWidth, &subHeight);

    if (mainRet == 0 && mainWidth > 0 && mainHeight > 0 &&

        subRet == 0 && subWidth > 0 && subHeight > 0) {

        snprintf(buffer, sizeof(buffer), "%dx%d/%dx%d", mainWidth, mainHeight, subWidth, subHeight);

        return buffer;

    }



    if (mainRet == 0 && mainWidth > 0 && mainHeight > 0) {

        snprintf(buffer, sizeof(buffer), "%dx%d", mainWidth, mainHeight);

        return buffer;

    }



    if (subRet == 0 && subWidth > 0 && subHeight > 0) {

        snprintf(buffer, sizeof(buffer), "%dx%d", subWidth, subHeight);

        return buffer;

    }



    return "1920x1080/640x360";

}

static bool ReadRkVideoFrameRateString(int streamId, std::string& value)
{
    char buffer[32] = {0};
    char* out = buffer;
    if (rk_video_get_frame_rate(streamId, &out) != 0 || buffer[0] == '\0') {
        return false;
    }

    value = buffer;
    return true;
}

static bool ReadRkVideoOutputTypeString(int streamId, std::string& value)
{
    const char* out = NULL;
    if (rk_video_get_output_data_type(streamId, &out) != 0 || out == NULL || out[0] == '\0') {
        return false;
    }

    value = out;
    return true;
}

static bool ReadRkOsdEnabled(int id, int* value)
{
    if (value == NULL) {
        return false;
    }

    return rk_osd_get_enabled(id, value) == 0;
}

static bool ReadRkOsdStringValue(int (*getter)(int, const char**), int id, std::string& value)
{
    if (getter == NULL) {
        return false;
    }

    const char* out = NULL;
    if (getter(id, &out) != 0 || out == NULL || out[0] == '\0') {
        return false;
    }

    value = out;
    return true;
}

static bool ReadRkOsdPosition(int id, int* x, int* y)
{
    if (x == NULL || y == NULL) {
        return false;
    }

    return rk_osd_get_position_x(id, x) == 0 && rk_osd_get_position_y(id, y) == 0;
}

static bool ReadRkImageFlipMode(std::string& value)
{
    const char* out = NULL;
    if (rk_isp_get_image_flip(0, &out) != 0 || out == NULL || out[0] == '\0') {
        return false;
    }

    value = out;
    return true;
}

static void ApplyRkVideoStreamConfig(const char* streamName,
                                     int streamId,
                                     const std::string& desiredCodec,
                                     const std::string& desiredResolution,
                                     int desiredFps,
                                     int desiredBitrate)
{
    const std::string normalizedCodec = NormalizeRkVideoCodec(desiredCodec);
    if (!normalizedCodec.empty()) {
        std::string currentCodec;
        const bool codecKnown = ReadRkVideoOutputTypeString(streamId, currentCodec);
        if (!codecKnown || NormalizeRkVideoCodec(currentCodec) != normalizedCodec) {
            const int ret = rk_video_set_output_data_type(streamId, normalizedCodec.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=video_codec stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   normalizedCodec.c_str(),
                   codecKnown ? currentCodec.c_str() : "unknown");
        }
    }

    const std::string frameRateValue = BuildFrameRateValue(desiredFps);
    if (!frameRateValue.empty()) {
        std::string currentFrameRate;
        const bool fpsKnown = ReadRkVideoFrameRateString(streamId, currentFrameRate);
        if (!fpsKnown || currentFrameRate != frameRateValue) {
            const int ret = rk_video_set_frame_rate(streamId, frameRateValue.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=video_fps stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   frameRateValue.c_str(),
                   fpsKnown ? currentFrameRate.c_str() : "unknown");
        }
    }

    if (desiredBitrate > 0) {
        int currentBitrate = 0;
        const bool bitrateKnown = (rk_video_get_max_rate(streamId, &currentBitrate) == 0);
        if (!bitrateKnown || currentBitrate != desiredBitrate) {
            const int ret = rk_video_set_max_rate(streamId, desiredBitrate);
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=video_bitrate stream=%s stream_id=%d value=%d current=%d\n",
                   ret,
                   streamName,
                   streamId,
                   desiredBitrate,
                   bitrateKnown ? currentBitrate : -1);
        }
    }

    const std::string normalizedResolution = NormalizeResolutionValue(desiredResolution, '*');
    if (!normalizedResolution.empty()) {
        int currentWidth = 0;
        int currentHeight = 0;
        std::string currentResolution;
        const bool resolutionKnown = (CaptureGetResolution(streamId, &currentWidth, &currentHeight) == 0 &&
                                      currentWidth > 0 &&
                                      currentHeight > 0);
        if (resolutionKnown) {
            char buffer[32] = {0};
            snprintf(buffer, sizeof(buffer), "%d*%d", currentWidth, currentHeight);
            currentResolution = buffer;
        }

        if (!resolutionKnown || currentResolution != normalizedResolution) {
            const int ret = rk_video_set_resolution(streamId, normalizedResolution.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=video_resolution stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   normalizedResolution.c_str(),
                   resolutionKnown ? currentResolution.c_str() : "unknown");
        }
    }
}

static void ApplyRkOsdConfig(const protocol::GbOsdParam& desired)
{
    const int desiredTimeEnabled = (desired.time_enabled != 0) ? 1 : 0;
    const int desiredCustomTextEnabled = (!desired.text_template.empty() && desired.text_template != "null") ? 1 : 0;
    const int desiredCaptureSwitch = (desiredTimeEnabled != 0 || desired.event_enabled != 0 || desired.alert_enabled != 0) ? 1 : 0;

    int currentTimeEnabled = 0;
    const bool timeKnown = ReadRkOsdEnabled(kRkOsdDateTimeId, &currentTimeEnabled);
    if (!timeKnown || currentTimeEnabled != desiredTimeEnabled) {
        const int captureRet = CaptureSetOSDSwitch(desiredCaptureSwitch);
        const int osdRet = rk_osd_set_enabled(kRkOsdDateTimeId, desiredTimeEnabled);
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_datetime_enabled value=%d current=%d capture_ret=%d rk_ret=%d\n",
               (captureRet != 0) ? captureRet : osdRet,
               desiredTimeEnabled,
               timeKnown ? currentTimeEnabled : -1,
               captureRet,
               osdRet);
    }

    int currentCustomTextEnabled = 0;
    const bool customKnown = ReadRkOsdEnabled(kRkOsdCustomTextId, &currentCustomTextEnabled);
    if (!customKnown || currentCustomTextEnabled != desiredCustomTextEnabled) {
        const int osdRet = rk_osd_set_enabled(kRkOsdCustomTextId, desiredCustomTextEnabled);
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_text_enabled value=%d current=%d\n",
               osdRet,
               desiredCustomTextEnabled,
               customKnown ? currentCustomTextEnabled : -1);
    }

    bool needRestart = false;

    if (!desired.text_template.empty()) {
        std::string currentText;
        const bool textKnown = ReadRkOsdStringValue(rk_osd_get_display_text, kRkOsdCustomTextId, currentText);
        if (!textKnown || currentText != desired.text_template) {
            const int ret = rk_osd_set_display_text(kRkOsdCustomTextId, desired.text_template.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_text value=%s current=%s\n",
                   ret,
                   desired.text_template.c_str(),
                   textKnown ? currentText.c_str() : "unknown");
            needRestart = true;
        }
    }

    if (!desired.time_format.empty()) {
        const std::string desiredDateStyle = NormalizeRkOsdDateStyle(desired.time_format);
        const std::string desiredTimeStyle = NormalizeRkOsdTimeStyle(desired.time_format);
        std::string currentDateStyle;
        std::string currentTimeStyle;
        const bool dateKnown = ReadRkOsdStringValue(rk_osd_get_date_style, kRkOsdDateTimeId, currentDateStyle);
        const bool timeStyleKnown = ReadRkOsdStringValue(rk_osd_get_time_style, kRkOsdDateTimeId, currentTimeStyle);
        if (!dateKnown || currentDateStyle != desiredDateStyle) {
            const int ret = rk_osd_set_date_style(kRkOsdDateTimeId, desiredDateStyle.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_date_style value=%s current=%s\n",
                   ret,
                   desiredDateStyle.c_str(),
                   dateKnown ? currentDateStyle.c_str() : "unknown");
            needRestart = true;
        }
        if (!timeStyleKnown || currentTimeStyle != desiredTimeStyle) {
            const int ret = rk_osd_set_time_style(kRkOsdDateTimeId, desiredTimeStyle.c_str());
            printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_time_style value=%s current=%s\n",
                   ret,
                   desiredTimeStyle.c_str(),
                   timeStyleKnown ? currentTimeStyle.c_str() : "unknown");
            needRestart = true;
        }
    }

    if (!desired.position.empty()) {
        const RkOsdAnchor anchor = ResolveRkOsdAnchor(desired.position);
        if (anchor.valid) {
            int currentX = 0;
            int currentY = 0;
            const bool currentPositionKnown = ReadRkOsdPosition(kRkOsdDateTimeId, &currentX, &currentY);
            if (!currentPositionKnown || currentX != anchor.x || currentY != anchor.y) {
                const int retX = rk_osd_set_position_x(kRkOsdDateTimeId, anchor.x);
                const int retY = rk_osd_set_position_y(kRkOsdDateTimeId, anchor.y);
                printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_datetime_position value=%d,%d current=%d,%d\n",
                       (retX != 0) ? retX : retY,
                       anchor.x,
                       anchor.y,
                       currentPositionKnown ? currentX : -1,
                       currentPositionKnown ? currentY : -1);
                needRestart = true;
            }

            currentX = 0;
            currentY = 0;
            const bool currentTextPositionKnown = ReadRkOsdPosition(kRkOsdCustomTextId, &currentX, &currentY);
            if (!currentTextPositionKnown || currentX != anchor.x || currentY != anchor.y) {
                const int retX = rk_osd_set_position_x(kRkOsdCustomTextId, anchor.x);
                const int retY = rk_osd_set_position_y(kRkOsdCustomTextId, anchor.y);
                printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_text_position value=%d,%d current=%d,%d\n",
                       (retX != 0) ? retX : retY,
                       anchor.x,
                       anchor.y,
                       currentTextPositionKnown ? currentX : -1,
                       currentTextPositionKnown ? currentY : -1);
                needRestart = true;
            }
        }
    }

    if (needRestart) {
        const int restartRet = rk_osd_restart();
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=osd_restart\n",
               restartRet);
    }
}

static void ApplyRkImageFlipConfig(const std::string& desiredMode)
{
    const std::string normalizedMode = NormalizeGbImageFlipMode(desiredMode);
    if (normalizedMode.empty()) {
        return;
    }

    std::string currentMode;
    const bool currentKnown = ReadRkImageFlipMode(currentMode);
    if (!currentKnown || NormalizeGbImageFlipMode(currentMode) != normalizedMode) {
        const int ret = rk_isp_set_image_flip(0, normalizedMode.c_str());
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=image_flip value=%s current=%s\n",
               ret,
               normalizedMode.c_str(),
               currentKnown ? currentMode.c_str() : "unknown");
    }
}


static const char* StreamRequestTypeName(StreamRequestType type)

{

    switch (type) {

    case kLiveStream:

        return "live";

    case kPlayback:

        return "playback";

    case kDownload:

        return "download";

    case kAudioStream:

        return "audio";

    case kStopStream:

        return "stop";

    default:

        return "unknown";

    }

}



static const char* QueryTypeName(QueryType type)

{

    switch (type) {

    case kDeviceInfoQuery:

        return "device_info";

    case kDeviceStatusQuery:

        return "device_status";

    case kDeviceCatalogQuery:

        return "device_catalog";

    case kDeviceRecordQuery:

        return "device_record";

    case kDeviceConfigQuery:

        return "device_config";

    case kDevicePresetQuery:

        return "device_preset";

    case kUnknowQuery:

    default:

        return "unknown";

    }

}



static const char* ControlTypeName(ControlType type)

{

    switch (type) {

    case kPtzControl:

        return "ptz";

    case kTeleBootControl:

        return "teleboot";

    case kDragZoomInControl:

        return "drag_zoom_in";

    case kDragZoomOutControl:

        return "drag_zoom_out";

    case kIFameControl:

        return "iframe";

    case kRecordControl:

        return "record";

    case kGurdControl:

        return "guard";

    case kAlarmResetControl:

        return "alarm_reset";

    case kDevConfigControl:

        return "config";

    case kDeviceUpgradeControl:

        return "device_upgrade";

    case kHomePositionControl:

        return "home_position";

    case kUnknowControl:

    default:

        return "unknown";

    }

}



static const char* PtzTypeName(PTZType type)

{

    switch (type) {

    case kPtzUp:

        return "up";

    case kPtzDown:

        return "down";

    case kPtzLeft:

        return "left";

    case kPtzRight:

        return "right";

    case kPtzUpLeft:

        return "up_left";

    case kPtzUpRight:

        return "up_right";

    case kPtzDownLeft:

        return "down_left";

    case kPtzDownRight:

        return "down_right";

    case kPtzPosAdd:

        return "preset_add";

    case kPtzPosCall:

        return "preset_call";

    case kPtzPosDel:

        return "preset_del";

    case kPtzStop:

        return "stop";

    case kPtzZoomIn:

        return "zoom_in";

    case kPtzZoomOut:

        return "zoom_out";

    case kPtzFocusNear:

        return "focus_near";

    case kPtzFocusFar:

        return "focus_far";

    case kPtzIRISOpen:

        return "iris_open";

    case kPtzIRISClose:

        return "iris_close";

    case kPtzCruAdd:

        return "cruise_add";

    case kPtzCruDel:

        return "cruise_del";

    case kPtzCruSpeed:

        return "cruise_speed";

    case kPtzCruTime:

        return "cruise_time";

    case kPtzCruStart:

        return "cruise_start";

    case kPtzScanStart:

        return "scan_start";

    case kPtzScanLeft:

        return "scan_left";

    case kPtzScanRight:

        return "scan_right";

    case kPtzScanSet:

        return "scan_set";

    case kPtzAidOpen:

        return "aid_open";

    case kPtzAidClose:

        return "aid_close";

    case kPtzUnkonw:

    default:

        return "unknown";

    }

}



static bool LoadPtzResumePosition(PtzPosition& position)

{

    memset(&position, 0, sizeof(position));



    CConfigTable table;

    if (!g_configManager.getConfig(getConfigName(CFG_PTZ_RESUME_POS), table)) {

        return false;

    }



    TExchangeAL<PtzPosition>::getConfig(table, position);

    return true;

}



static bool SavePtzResumePosition(const PtzPosition& position)

{

    CConfigTable table;

    TExchangeAL<PtzPosition>::setConfig(position, table);

    const int ret = g_configManager.setConfig(getConfigName(CFG_PTZ_RESUME_POS), table, 0, IConfigManager::applyOK);

    return ((ret & IConfigManager::applyFileError) == 0);

}



static bool UpdateGuardStatus(bool enable)

{

    CConfigTable table;

    if (!g_configManager.getConfig(getConfigName(CFG_AIONOFF), table)) {

        return false;

    }



    table["Status"] = enable ? 1 : 0;

    const int ret = g_configManager.setConfig(getConfigName(CFG_AIONOFF), table, 0, IConfigManager::applyOK);

    return ((ret & IConfigManager::applyFileError) == 0);

}



static void* GbRemoteRestartThread(void* arg)

{

    (void)arg;

    NormalRestart();

    return NULL;

}



static bool IsGbSdkSuccess(int ret)

{

    return (ret == 0 || ret == kGb28181Success);

}



static bool LooksLikeGbCode(const std::string& text)

{

    if (text.size() < 20) {

        return false;

    }



    for (size_t i = 0; i < text.size(); ++i) {

        if (text[i] < '0' || text[i] > '9') {

            return false;

        }

    }



    return true;

}



static std::string MaskSecret(const std::string& value)

{

    if (value.size() <= 2) {

        return "**";

    }



    return value.substr(0, 1) + "***" + value.substr(value.size() - 1, 1);

}



static void AppendConfigDiff(std::string& diff, const char* key, const std::string& oldValue, const std::string& newValue)

{

    if (oldValue == newValue) {

        return;

    }



    if (!diff.empty()) {

        diff += ";";

    }

    diff += key;

    diff += ":";

    diff += oldValue;

    diff += "->";

    diff += newValue;

}



static void AppendConfigDiff(std::string& diff, const char* key, int oldValue, int newValue)

{

    if (oldValue == newValue) {

        return;

    }



    char oldBuf[32] = {0};

    char newBuf[32] = {0};

    snprintf(oldBuf, sizeof(oldBuf), "%d", oldValue);

    snprintf(newBuf, sizeof(newBuf), "%d", newValue);

    AppendConfigDiff(diff, key, std::string(oldBuf), std::string(newBuf));

}



static std::string BuildConfigDiffSummary(const protocol::ProtocolExternalConfig& before,

                                          const protocol::ProtocolExternalConfig& after)

{

    std::string diff;

    AppendConfigDiff(diff, "version", before.version, after.version);

    AppendConfigDiff(diff, "gb.register.server_ip", before.gb_register.server_ip, after.gb_register.server_ip);

    AppendConfigDiff(diff, "gb.register.server_port", before.gb_register.server_port, after.gb_register.server_port);

    AppendConfigDiff(diff, "gb.register.device_id", before.gb_register.device_id, after.gb_register.device_id);

    AppendConfigDiff(diff, "gb.register.device_name", before.gb_register.device_name, after.gb_register.device_name);

    AppendConfigDiff(diff, "gb.register.expires_sec", before.gb_register.expires_sec, after.gb_register.expires_sec);

    AppendConfigDiff(diff, "gb.keepalive.interval_sec", before.gb_keepalive.interval_sec, after.gb_keepalive.interval_sec);

    AppendConfigDiff(diff, "gb.keepalive.timeout_sec", before.gb_keepalive.timeout_sec, after.gb_keepalive.timeout_sec);

    AppendConfigDiff(diff, "gb.keepalive.max_retry", before.gb_keepalive.max_retry, after.gb_keepalive.max_retry);

    AppendConfigDiff(diff, "gb.live.transport", before.gb_live.transport, after.gb_live.transport);

    AppendConfigDiff(diff, "gb.live.target_ip", before.gb_live.target_ip, after.gb_live.target_ip);

    AppendConfigDiff(diff, "gb.live.target_port", before.gb_live.target_port, after.gb_live.target_port);

    AppendConfigDiff(diff, "gb.live.video_stream_id", before.gb_live.video_stream_id, after.gb_live.video_stream_id);

    AppendConfigDiff(diff, "gb.live.video_codec", before.gb_live.video_codec, after.gb_live.video_codec);

    AppendConfigDiff(diff, "gb.live.audio_codec", before.gb_live.audio_codec, after.gb_live.audio_codec);

    AppendConfigDiff(diff, "gb.live.mtu", before.gb_live.mtu, after.gb_live.mtu);

    AppendConfigDiff(diff, "gb.live.payload_type", before.gb_live.payload_type, after.gb_live.payload_type);

    AppendConfigDiff(diff, "gb.live.ssrc", before.gb_live.ssrc, after.gb_live.ssrc);

    AppendConfigDiff(diff, "gb.live.clock_rate", before.gb_live.clock_rate, after.gb_live.clock_rate);

    AppendConfigDiff(diff, "gb.video.main_codec", before.gb_video.main_codec, after.gb_video.main_codec);

    AppendConfigDiff(diff, "gb.video.main_resolution", before.gb_video.main_resolution, after.gb_video.main_resolution);

    AppendConfigDiff(diff, "gb.video.main_fps", before.gb_video.main_fps, after.gb_video.main_fps);

    AppendConfigDiff(diff, "gb.video.main_bitrate_kbps", before.gb_video.main_bitrate_kbps, after.gb_video.main_bitrate_kbps);

    AppendConfigDiff(diff, "gb.video.sub_codec", before.gb_video.sub_codec, after.gb_video.sub_codec);

    AppendConfigDiff(diff, "gb.video.sub_resolution", before.gb_video.sub_resolution, after.gb_video.sub_resolution);

    AppendConfigDiff(diff, "gb.video.sub_fps", before.gb_video.sub_fps, after.gb_video.sub_fps);

    AppendConfigDiff(diff, "gb.video.sub_bitrate_kbps", before.gb_video.sub_bitrate_kbps, after.gb_video.sub_bitrate_kbps);

    AppendConfigDiff(diff, "gb.image.flip_mode", before.gb_image.flip_mode, after.gb_image.flip_mode);

    AppendConfigDiff(diff, "gb.osd.text_template", before.gb_osd.text_template, after.gb_osd.text_template);

    AppendConfigDiff(diff, "gb.osd.time_format", before.gb_osd.time_format, after.gb_osd.time_format);

    AppendConfigDiff(diff, "gb.osd.position", before.gb_osd.position, after.gb_osd.position);

    AppendConfigDiff(diff, "gb.osd.time_enabled", before.gb_osd.time_enabled, after.gb_osd.time_enabled);

    AppendConfigDiff(diff, "gb.osd.event_enabled", before.gb_osd.event_enabled, after.gb_osd.event_enabled);

    AppendConfigDiff(diff, "gb.osd.alert_enabled", before.gb_osd.alert_enabled, after.gb_osd.alert_enabled);

    AppendConfigDiff(diff, "gat.register.server_ip", before.gat_register.server_ip, after.gat_register.server_ip);

    AppendConfigDiff(diff, "gat.register.server_port", before.gat_register.server_port, after.gat_register.server_port);

    AppendConfigDiff(diff, "gat.register.device_id", before.gat_register.device_id, after.gat_register.device_id);

    AppendConfigDiff(diff, "gat.register.username", before.gat_register.username, after.gat_register.username);

    AppendConfigDiff(diff, "gat.register.password", before.gat_register.password, after.gat_register.password);

    AppendConfigDiff(diff, "gat.register.auth_method", before.gat_register.auth_method, after.gat_register.auth_method);

    AppendConfigDiff(diff, "gat.register.listen_port", before.gat_register.listen_port, after.gat_register.listen_port);

    AppendConfigDiff(diff, "gat.register.expires_sec", before.gat_register.expires_sec, after.gat_register.expires_sec);

    AppendConfigDiff(diff, "gat.register.keepalive_interval_sec", before.gat_register.keepalive_interval_sec, after.gat_register.keepalive_interval_sec);

    AppendConfigDiff(diff, "gat.register.max_retry", before.gat_register.max_retry, after.gat_register.max_retry);

    AppendConfigDiff(diff, "gat.upload.batch_size", before.gat_upload.batch_size, after.gat_upload.batch_size);

    AppendConfigDiff(diff, "gat.upload.flush_interval_ms", before.gat_upload.flush_interval_ms, after.gat_upload.flush_interval_ms);

    AppendConfigDiff(diff, "gat.upload.retry_policy", before.gat_upload.retry_policy, after.gat_upload.retry_policy);

    AppendConfigDiff(diff, "gat.capture.face_profile", before.gat_capture.face_profile, after.gat_capture.face_profile);

    AppendConfigDiff(diff, "gat.capture.nonmotor_profile", before.gat_capture.nonmotor_profile, after.gat_capture.nonmotor_profile);

    AppendConfigDiff(diff, "gat.capture.vehicle_profile", before.gat_capture.vehicle_profile, after.gat_capture.vehicle_profile);

    AppendConfigDiff(diff, "gat.capture.concurrent_limit", before.gat_capture.concurrent_limit, after.gat_capture.concurrent_limit);

    AppendConfigDiff(diff, "gb.broadcast.input_mode", before.gb_broadcast.input_mode, after.gb_broadcast.input_mode);

    AppendConfigDiff(diff, "gb.broadcast.codec", before.gb_broadcast.codec, after.gb_broadcast.codec);

    AppendConfigDiff(diff, "gb.broadcast.recv_port", before.gb_broadcast.recv_port, after.gb_broadcast.recv_port);

    AppendConfigDiff(diff, "gb.broadcast.file_cache_dir", before.gb_broadcast.file_cache_dir, after.gb_broadcast.file_cache_dir);

    AppendConfigDiff(diff, "gb.listen.transport", before.gb_listen.transport, after.gb_listen.transport);

    AppendConfigDiff(diff, "gb.listen.target_ip", before.gb_listen.target_ip, after.gb_listen.target_ip);

    AppendConfigDiff(diff, "gb.listen.target_port", before.gb_listen.target_port, after.gb_listen.target_port);

    AppendConfigDiff(diff, "gb.listen.codec", before.gb_listen.codec, after.gb_listen.codec);

    AppendConfigDiff(diff, "gb.listen.packet_ms", before.gb_listen.packet_ms, after.gb_listen.packet_ms);

    return diff;

}



static void FillGbTimeString(char* dst, size_t dstSize)

{

    if (dst == NULL || dstSize == 0) {

        return;

    }



    dst[0] = '\0';

    time_t now = time(NULL);

    if (now <= 0) {

        return;

    }



    struct tm tmNow;

    memset(&tmNow, 0, sizeof(tmNow));

#if defined(_WIN32)

    localtime_s(&tmNow, &now);

#else

    localtime_r(&now, &tmNow);

#endif

    snprintf(dst,

             dstSize,

             "%04d-%02d-%02dT%02d:%02d:%02d",

             tmNow.tm_year + 1900,

             tmNow.tm_mon + 1,

             tmNow.tm_mday,

             tmNow.tm_hour,

             tmNow.tm_min,

             tmNow.tm_sec);

}



static const char* GbSubscribeTypeToString(SubscribeType type)

{

    switch (type) {

    case kCatalogSubscribe:

        return "catalog";

    case kAlarmSubscribe:

        return "alarm";

    case kMobilePositionSubscribe:

        return "mobile_position";

    case kCatalogSubscribeExpire:

        return "catalog_expire";

    case kAlarmSubscribeExpire:

        return "alarm_expire";

    case kMobilePositionSubscribeExpire:

        return "mobile_position_expire";

    default:

        return "unknown";

    }

}



static bool IsGbSubscribeExpire(SubscribeType type)

{

    return type == kCatalogSubscribeExpire ||

           type == kAlarmSubscribeExpire ||

           type == kMobilePositionSubscribeExpire;

}



}



namespace protocol

{



ProtocolManager::ProtocolManager()

    : m_gb_receiver(this),

      m_gat_client(new GAT1400ClientService()),

      m_gb_client_sdk(NULL),

      m_gb_media_responder(NULL),

      m_gb_media_responder_user(NULL),

      m_gb_heartbeat_running(false),

      m_gb_client_started(false),

      m_gb_client_registered(false),

      m_gb_catalog_subscribe_handle(NULL),

      m_gb_alarm_subscribe_handle(NULL),

      m_gb_mobile_position_subscribe_handle(NULL),

      m_gb_live_capture_started(false),

      m_started(false),

      m_gb_device_name(""),

      m_gb_osd_time_enabled(true),

      m_gb_osd_event_enabled(false),

      m_gb_osd_alert_enabled(false)

{

}



ProtocolManager::~ProtocolManager()

{

    Stop();

    UnInit();

}



int ProtocolManager::Init(const std::string& configEndpoint)

{

    m_provider.reset(new LocalConfigProvider(configEndpoint));

    return ReloadExternalConfig();

}



void ProtocolManager::UnInit()

{

    HandleGbStopStreamRequest(NULL, "uninit");

    UnbindGbClientSdk();

    m_provider.reset();

}



int ProtocolManager::Start()

{

    if (m_started) {

        return 0;

    }



    int ret = m_rtp_ps_sender.Init(m_cfg.gb_live);

    if (ret != 0) {

        printf("[ProtocolManager] init RTP/PS sender failed: %d\n", ret);

        return ret;

    }



    ret = m_rtp_ps_sender.OpenSession();

    if (ret != 0) {

        printf("[ProtocolManager] open RTP/PS session failed: %d\n", ret);

        return ret;

    }



    ret = m_broadcast.StartSession(m_cfg.gb_broadcast);

    if (ret != 0) {

        printf("[ProtocolManager] start broadcast session failed: %d\n", ret);

        m_rtp_ps_sender.CloseSession();

        return ret;

    }



    ret = m_listen.StartSession(m_cfg.gb_listen);

    if (ret != 0) {

        printf("[ProtocolManager] start listen session failed: %d\n", ret);

        m_broadcast.StopSession();

        m_rtp_ps_sender.CloseSession();

        return ret;

    }



    ret = StartGbClientLifecycle();

    if (ret != 0) {

        printf("[ProtocolManager] start gb client lifecycle failed: %d\n", ret);

        m_listen.StopSession();

        m_broadcast.StopSession();

        m_rtp_ps_sender.CloseSession();

        return ret;

    }

    if (m_gat_client.get() != NULL) {
        ret = m_gat_client->Start(m_cfg, m_cfg.gb_register);
        if (ret != 0) {
            printf("[ProtocolManager] start gat1400 client failed: %d\n", ret);
            StopGbClientLifecycle();
            m_listen.StopSession();
            m_broadcast.StopSession();
            m_rtp_ps_sender.CloseSession();
            return ret;
        }
    }



    m_started = true;

    printf("[ProtocolManager] start success, config version=%s\n", m_cfg.version.c_str());

    return 0;

}



void ProtocolManager::Stop()

{

    HandleGbStopStreamRequest(NULL, "stop");

    if (m_gat_client.get() != NULL) {
        m_gat_client->Stop();
    }

    StopGbClientLifecycle();



    if (!m_started) {

        StopGbLiveCapture();

        return;

    }



    StopGbLiveCapture();

    m_listen.StopSession();

    m_broadcast.StopSession();

    m_rtp_ps_sender.CloseSession();



    m_started = false;

    printf("[ProtocolManager] stop success\n");

}



int ProtocolManager::ReloadExternalConfig()

{

    if (!m_provider.get()) {

        return -1;

    }



    printf("[ProtocolManager] module=config event=config_apply_start trace=manager error=0 stage=reload current_version=%s started=%d\n",

           m_cfg.version.c_str(),

           m_started ? 1 : 0);



    ProtocolExternalConfig latest;

    int ret = m_provider->PullLatest(latest);

    if (ret != 0) {

        printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=pull current_version=%s\n",

               ret,

               m_cfg.version.c_str());

        return ret;

    }



    ret = m_provider->Validate(latest);

    if (ret != 0) {

        printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=validate target_version=%s\n",

               ret,

               latest.version.c_str());

        return ret;

    }



    const bool restartBroadcast = (m_cfg.gb_broadcast.input_mode != latest.gb_broadcast.input_mode) ||

                                  (m_cfg.gb_broadcast.codec != latest.gb_broadcast.codec) ||

                                  (m_cfg.gb_broadcast.recv_port != latest.gb_broadcast.recv_port) ||

                                  (m_cfg.gb_broadcast.file_cache_dir != latest.gb_broadcast.file_cache_dir);

    const bool restartListen = (m_cfg.gb_listen.transport != latest.gb_listen.transport) ||

                               (m_cfg.gb_listen.target_ip != latest.gb_listen.target_ip) ||

                               (m_cfg.gb_listen.target_port != latest.gb_listen.target_port) ||

                               (m_cfg.gb_listen.codec != latest.gb_listen.codec) ||

                               (m_cfg.gb_listen.packet_ms != latest.gb_listen.packet_ms);

    const bool applyMediaConfig = (m_cfg.gb_video.main_codec != latest.gb_video.main_codec) ||

                                  (m_cfg.gb_video.main_resolution != latest.gb_video.main_resolution) ||

                                  (m_cfg.gb_video.main_fps != latest.gb_video.main_fps) ||

                                  (m_cfg.gb_video.main_bitrate_kbps != latest.gb_video.main_bitrate_kbps) ||

                                  (m_cfg.gb_video.sub_codec != latest.gb_video.sub_codec) ||

                                  (m_cfg.gb_video.sub_resolution != latest.gb_video.sub_resolution) ||

                                  (m_cfg.gb_video.sub_fps != latest.gb_video.sub_fps) ||

                                  (m_cfg.gb_video.sub_bitrate_kbps != latest.gb_video.sub_bitrate_kbps) ||

                                  (m_cfg.gb_image.flip_mode != latest.gb_image.flip_mode) ||

                                  (m_cfg.gb_osd.text_template != latest.gb_osd.text_template) ||

                                  (m_cfg.gb_osd.time_format != latest.gb_osd.time_format) ||

                                  (m_cfg.gb_osd.position != latest.gb_osd.position) ||

                                  (m_cfg.gb_osd.time_enabled != latest.gb_osd.time_enabled) ||

                                  (m_cfg.gb_osd.event_enabled != latest.gb_osd.event_enabled) ||

                                  (m_cfg.gb_osd.alert_enabled != latest.gb_osd.alert_enabled);

    const bool reloadGat = (m_cfg.gat_register.server_ip != latest.gat_register.server_ip) ||

                           (m_cfg.gat_register.server_port != latest.gat_register.server_port) ||

                           (m_cfg.gat_register.device_id != latest.gat_register.device_id) ||

                           (m_cfg.gat_register.username != latest.gat_register.username) ||

                           (m_cfg.gat_register.password != latest.gat_register.password) ||

                           (m_cfg.gat_register.auth_method != latest.gat_register.auth_method) ||

                           (m_cfg.gat_register.listen_port != latest.gat_register.listen_port) ||

                           (m_cfg.gat_register.expires_sec != latest.gat_register.expires_sec) ||

                           (m_cfg.gat_register.keepalive_interval_sec != latest.gat_register.keepalive_interval_sec) ||

                           (m_cfg.gat_register.max_retry != latest.gat_register.max_retry) ||

                           (m_cfg.gat_upload.batch_size != latest.gat_upload.batch_size) ||

                           (m_cfg.gat_upload.flush_interval_ms != latest.gat_upload.flush_interval_ms) ||

                           (m_cfg.gat_upload.retry_policy != latest.gat_upload.retry_policy) ||

                           (m_cfg.gat_capture.face_profile != latest.gat_capture.face_profile) ||

                           (m_cfg.gat_capture.nonmotor_profile != latest.gat_capture.nonmotor_profile) ||

                           (m_cfg.gat_capture.vehicle_profile != latest.gat_capture.vehicle_profile) ||

                           (m_cfg.gat_capture.concurrent_limit != latest.gat_capture.concurrent_limit);



    const std::string diffSummary = BuildConfigDiffSummary(m_cfg, latest);

    printf("[ProtocolManager] module=config event=config_diff trace=manager error=0 changed=%s\n",

           diffSummary.empty() ? "none" : diffSummary.c_str());



    m_cfg = latest;
    m_gb_device_name = m_cfg.gb_register.device_name;
    m_gb_osd_time_enabled = (m_cfg.gb_osd.time_enabled != 0);
    m_gb_osd_event_enabled = (m_cfg.gb_osd.event_enabled != 0);
    m_gb_osd_alert_enabled = (m_cfg.gb_osd.alert_enabled != 0);

    printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=reload version=%s changed=%s started=%d\n",

           m_cfg.version.c_str(),

           diffSummary.empty() ? "none" : diffSummary.c_str(),

           m_started ? 1 : 0);

    // if (applyMediaConfig) {
    //     ApplyRkVideoStreamConfig("main",
    //                              0,
    //                              m_cfg.gb_video.main_codec,
    //                              m_cfg.gb_video.main_resolution,
    //                              m_cfg.gb_video.main_fps,
    //                              m_cfg.gb_video.main_bitrate_kbps);
    //     ApplyRkVideoStreamConfig("sub",
    //                              1,
    //                              m_cfg.gb_video.sub_codec,
    //                              m_cfg.gb_video.sub_resolution,
    //                              m_cfg.gb_video.sub_fps,
    //                              m_cfg.gb_video.sub_bitrate_kbps);
    //     ApplyRkImageFlipConfig(m_cfg.gb_image.flip_mode);
    //     ApplyRkOsdConfig(m_cfg.gb_osd);
    // }



    if (m_started) {

        if (restartBroadcast) {

            const int broadcastRet = m_broadcast.StartSession(m_cfg.gb_broadcast);

            if (broadcastRet != 0) {

                printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_broadcast_restart version=%s\n",

                       broadcastRet,

                       m_cfg.version.c_str());

                return broadcastRet;

            }



            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_broadcast_restart version=%s\n",

                   m_cfg.version.c_str());

        }



        if (restartListen) {

            const int listenRet = m_listen.StartSession(m_cfg.gb_listen);

            if (listenRet != 0) {

                printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_listen_restart version=%s\n",

                       listenRet,

                       m_cfg.version.c_str());

                return listenRet;

            }



            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_listen_restart version=%s\n",

                   m_cfg.version.c_str());

        }



        const int regRet = RegisterGbClient(true);

        if (regRet != 0) {

            printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_reregister version=%s\n",

                   regRet,

                   m_cfg.version.c_str());

        } else {

            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_reregister version=%s\n",

                   m_cfg.version.c_str());

        }

        if (reloadGat && m_gat_client.get() != NULL) {

            const int gatRet = m_gat_client->Reload(m_cfg, m_cfg.gb_register);

            if (gatRet != 0) {

                printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gat_reload version=%s\n",

                       gatRet,

                       m_cfg.version.c_str());

                return gatRet;

            }

            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gat_reload version=%s\n",

                   m_cfg.version.c_str());

        }

    }



    return 0;

}



int ProtocolManager::StartGbClientLifecycle()

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    {

        std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

        if (m_gb_client_sdk == NULL) {

            printf("[ProtocolManager] gb client sdk not bound, skip start/register\n");

            return 0;

        }



        if (m_gb_client_started) {

            return 0;

        }



        std::string localGbCode = m_cfg.gb_register.device_id;

        if (localGbCode.empty()) {

            localGbCode = m_cfg.gb_register.username;

        }



        if (!LooksLikeGbCode(localGbCode)) {

            printf("[ProtocolManager] gb start failed, invalid local code=%s\n", localGbCode.c_str());

            return -101;

        }



        const std::string sipTransportCfg = ToLowerCopy(m_cfg.gb_live.transport);

        const TransPorco sipTransport = (sipTransportCfg == "tcp") ? kSipProcoOverTCP : kSipProcoOverUDP;



        uint16_t localSipPort = 5060;

        if (m_cfg.gb_register.server_port > 0 && m_cfg.gb_register.server_port <= 65535) {

            localSipPort = (uint16_t)m_cfg.gb_register.server_port;

        }



        const int startRet = m_gb_client_sdk->Start(kGB2016Version,

                                                     sipTransport,

                                                     localGbCode.c_str(),

                                                     "0.0.0.0",

                                                     localSipPort);

        if (!IsGbSdkSuccess(startRet)) {

            printf("[ProtocolManager] gb start failed ret=%d local=%s bind=0.0.0.0:%u\n",

                   startRet,

                   localGbCode.c_str(),

                   (unsigned int)localSipPort);

            return -102;

        }



        m_gb_client_started = true;

        m_gb_client_registered = false;

        printf("[ProtocolManager] gb start ok local=%s transport=%s bind=0.0.0.0:%u\n",

               localGbCode.c_str(),

               sipTransport == kSipProcoOverTCP ? "tcp" : "udp",

               (unsigned int)localSipPort);

    }



    int ret = RegisterGbClient(false);

    if (ret != 0) {

        StopGbClientLifecycle();

        return ret;

    }



    if (!m_gb_heartbeat_running.load()) {

        m_gb_heartbeat_running.store(true);

        if (m_gb_heartbeat_thread.joinable()) {

            m_gb_heartbeat_thread.join();

        }

        m_gb_heartbeat_thread = std::thread(&ProtocolManager::GbHeartbeatLoop, this);

    }



    return 0;

#else

    return 0;

#endif

}



void ProtocolManager::StopGbClientLifecycle()

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    m_gb_heartbeat_running.store(false);

    if (m_gb_heartbeat_thread.joinable()) {

        m_gb_heartbeat_thread.join();

    }



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        m_gb_client_started = false;

        m_gb_client_registered = false;

        return;

    }



    if (m_gb_client_registered) {

        const int logoutRet = m_gb_client_sdk->Logout();

        if (!IsGbSdkSuccess(logoutRet)) {

            printf("[ProtocolManager] gb logout failed ret=%d\n", logoutRet);

        } else {

            printf("[ProtocolManager] gb logout ok\n");

        }

    }



    if (m_gb_client_started) {

        m_gb_client_sdk->Stop();

        printf("[ProtocolManager] gb stop ok\n");

    }



    m_gb_client_started = false;

    m_gb_client_registered = false;

#else

    m_gb_heartbeat_running.store(false);

    m_gb_client_started = false;

    m_gb_client_registered = false;

#endif

}



int ProtocolManager::RegisterGbClient(bool force)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL || !m_gb_client_started) {

        return 0;

    }



    if (m_gb_client_registered && !force) {

        return 0;

    }



    if (m_cfg.gb_register.server_ip.empty() || m_cfg.gb_register.server_port <= 0 || m_cfg.gb_register.server_port > 65535) {

        printf("[ProtocolManager] gb register config invalid endpoint=%s:%d\n",

               m_cfg.gb_register.server_ip.c_str(),

               m_cfg.gb_register.server_port);

        m_gb_client_registered = false;

        return -111;

    }



    std::string localGbCode = m_cfg.gb_register.device_id;

    if (localGbCode.empty()) {

        localGbCode = m_cfg.gb_register.username;

    }



    if (!LooksLikeGbCode(localGbCode)) {

        printf("[ProtocolManager] gb register failed, invalid local code=%s\n", localGbCode.c_str());

        m_gb_client_registered = false;

        return -112;

    }



    std::string connectGbCode = localGbCode;

    if (LooksLikeGbCode(m_cfg.gb_register.username) && m_cfg.gb_register.username != localGbCode) {

        connectGbCode = m_cfg.gb_register.username;

    }



    std::string authUser = m_cfg.gb_register.username;

    if (authUser.empty() || authUser == connectGbCode) {

        authUser = localGbCode;

    }



    GBRegistParam registerParam;

    memset(&registerParam, 0, sizeof(registerParam));

    registerParam.expires = (m_cfg.gb_register.expires_sec > 0) ?

                                (uint32_t)m_cfg.gb_register.expires_sec :

                                (uint32_t)3600;

    CopyBounded(registerParam.username, sizeof(registerParam.username), authUser);

    CopyBounded(registerParam.password, sizeof(registerParam.password), m_cfg.gb_register.password);



    ConnectParam connectParam;

    memset(&connectParam, 0, sizeof(connectParam));

    CopyBounded(connectParam.ip, sizeof(connectParam.ip), m_cfg.gb_register.server_ip);

    connectParam.port = (uint16_t)m_cfg.gb_register.server_port;

    CopyBounded(connectParam.GBCode, sizeof(connectParam.GBCode), connectGbCode);



    const int regRet = m_gb_client_sdk->Register(&registerParam, &connectParam);

    if (!IsGbSdkSuccess(regRet)) {

        m_gb_client_registered = false;

        printf("[ProtocolManager] gb register failed ret=%d local=%s server=%s:%u connectGb=%s user=%s pass=%s\n",

               regRet,

               localGbCode.c_str(),

               connectParam.ip,

               (unsigned int)connectParam.port,

               connectGbCode.c_str(),

               authUser.c_str(),

               MaskSecret(m_cfg.gb_register.password).c_str());

        return -113;

    }



    m_gb_client_registered = true;

    const int pendingUpgradeRet = ReportPendingGbUpgradeResult();
    if (pendingUpgradeRet != 0) {
        printf("[ProtocolManager] gb pending upgrade report deferred ret=%d local=%s\n",
               pendingUpgradeRet,
               localGbCode.c_str());
    }

    printf("[ProtocolManager] gb register ok local=%s server=%s:%u connectGb=%s expires=%u user=%s\n",

           localGbCode.c_str(),

           connectParam.ip,

           (unsigned int)connectParam.port,

           connectGbCode.c_str(),

           (unsigned int)registerParam.expires,

           authUser.c_str());

    return 0;

#else

    (void)force;

    return 0;

#endif

}



int ProtocolManager::SendGbHeartbeatOnce()

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL || !m_gb_client_started || !m_gb_client_registered) {

        return 0;

    }



    KeepaliveInfo keepalive;

    memset(&keepalive, 0, sizeof(keepalive));

    keepalive.status = true;

    keepalive.Num = 0;

    keepalive.fault_device = NULL;



    const int hbRet = m_gb_client_sdk->SendHeartbeat(&keepalive);

    if (!IsGbSdkSuccess(hbRet)) {

        return hbRet;

    }



    return 0;

#else

    return 0;

#endif

}



void ProtocolManager::GbHeartbeatLoop()

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    int intervalSec = m_cfg.gb_keepalive.interval_sec;

    if (intervalSec <= 0) {

        intervalSec = 60;

    }



    int maxRetry = m_cfg.gb_keepalive.max_retry;

    if (maxRetry <= 0) {

        maxRetry = 3;

    }



    int failedCount = 0;

    while (m_gb_heartbeat_running.load()) {

        for (int i = 0; i < intervalSec && m_gb_heartbeat_running.load(); ++i) {

            std::this_thread::sleep_for(std::chrono::seconds(1));

        }



        if (!m_gb_heartbeat_running.load()) {

            break;

        }



        const int hbRet = SendGbHeartbeatOnce();

        if (hbRet == 0) {

            failedCount = 0;

            continue;

        }



        ++failedCount;

        printf("[ProtocolManager] gb heartbeat failed ret=%d fail=%d/%d\n",

               hbRet,

               failedCount,

               maxRetry);



        if (failedCount >= maxRetry) {

            const int regRet = RegisterGbClient(true);

            if (regRet == 0) {

                printf("[ProtocolManager] gb heartbeat recover by register\n");

                failedCount = 0;

            } else {

                printf("[ProtocolManager] gb heartbeat recover failed ret=%d\n", regRet);

            }

        }

    }

#else

    return;

#endif

}

const ProtocolExternalConfig& ProtocolManager::GetConfig() const

{

    return m_cfg;

}



int ProtocolManager::PushLiveVideoEsFrame(const uint8_t* data, size_t size, uint64_t pts90k, bool keyFrame)

{

    if (!m_started) {

        return -1;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (!m_gb_live_session.active || !m_gb_live_session.acked) {

            return 0;

        }

    }



    const int ret = m_rtp_ps_sender.SendVideoFrame(data, size, pts90k, keyFrame);

    if (ret == 0) {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {

            ++m_gb_live_session.sent_video_frames;

        }

    }

    return ret;

}



int ProtocolManager::PushLiveAudioEsFrame(const uint8_t* data, size_t size, uint64_t pts90k)

{

    if (!m_started) {

        return -1;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (!m_gb_live_session.active || !m_gb_live_session.acked) {

            return 0;

        }

    }



    const int ret = m_rtp_ps_sender.SendAudioFrame(data, size, pts90k);

    if (ret == 0) {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {

            ++m_gb_live_session.sent_audio_frames;

        }

    }

    return ret;

}



int ProtocolManager::PushListenAudioFrame(const uint8_t* data, size_t size, uint64_t timestamp)

{

    if (!m_started) {

        return -1;

    }



    return m_listen.PushAudioFrame(data, size, timestamp);

}



int ProtocolManager::ApplyGbBroadcastSdpOffer(const std::string& sdp, const std::string& remoteIpHint)

{

    return m_broadcast.ApplySdpOffer(sdp, remoteIpHint);

}



int ProtocolManager::ApplyGbBroadcastTransportHint(const std::string& remoteIp,

                                                   int remotePort,

                                                   int payloadType,

                                                   const std::string& codec,

                                                   int transportType)

{

    return m_broadcast.ApplyTransportHint(remoteIp, remotePort, payloadType, codec, transportType);

}



std::string ProtocolManager::BuildGbBroadcastAnswerSdp(const std::string& localIp) const

{

    return m_broadcast.BuildLocalAnswerSdp(localIp);

}



int ProtocolManager::BuildGbBroadcastMediaInfo(const std::string& localIp,

                                               const std::string& deviceId,

                                               MediaInfo& out,

                                               RtpMap& outMap) const

{

    memset(&out, 0, sizeof(out));

    memset(&outMap, 0, sizeof(outMap));



    std::string remoteIp;

    int remotePort = 0;

    int payloadType = -1;

    std::string codec;

    int localRecvPort = 0;

    int transportType = kRtpOverUdp;



    int ret = m_broadcast.GetNegotiatedTransport(remoteIp, remotePort, payloadType, codec, localRecvPort, transportType);

    if (ret != 0) {

        codec = NormalizeCodec(m_cfg.gb_broadcast.codec);

        payloadType = ResolvePayloadTypeByCodec(codec);

        localRecvPort = m_cfg.gb_broadcast.recv_port;

    }



    if (codec.empty()) {

        codec = "g711a";

    }



    if (payloadType < 0 || payloadType > 127) {

        payloadType = ResolvePayloadTypeByCodec(codec);

    }



    if (localRecvPort <= 0) {

        localRecvPort = m_cfg.gb_broadcast.recv_port;

    }



    if (localRecvPort <= 0) {

        return -1;

    }



    const std::string ip = localIp.empty() ? "0.0.0.0" : localIp;

    const std::string gbCode = deviceId.empty() ? m_cfg.gb_register.device_id : deviceId;



    out.RtpType = static_cast<NetTransType>(transportType);

    out.RequestType = kAudioStream;

    out.Port = static_cast<unsigned int>(localRecvPort);

    CopyBounded(out.DeviceID, sizeof(out.DeviceID), gbCode);

    CopyBounded(out.IP, sizeof(out.IP), ip);



    const unsigned int ssrcValue = (m_cfg.gb_live.ssrc > 0) ? static_cast<unsigned int>(m_cfg.gb_live.ssrc) : 1u;

    snprintf(out.Ssrc, sizeof(out.Ssrc), "%010u", ssrcValue);



    out.RtpDescri.type = kGBAudio;

    out.RtpDescri.DescriNum = 1;

    out.RtpDescri.mapDescri = &outMap;



    outMap.MediaFormat = static_cast<unsigned int>(payloadType);

    outMap.SampleRate = 8000;

    CopyBounded(outMap.MimeType, sizeof(outMap.MimeType), ResolveMimeByCodec(codec));



    return 0;

}



int ProtocolManager::HandleGbAudioStreamRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input)

{

    if (input == NULL) {

        return -10;

    }



    std::string remoteIp;

    int remotePort = 0;

    int payloadType = -1;

    std::string codec;

    if (!ExtractAudioTransportHint(input, remoteIp, remotePort, payloadType, codec)) {

        printf("[ProtocolManager] gb audio stream request parse failed gb=%s\n", gbCode != NULL ? gbCode : "");

        return -11;

    }



    int ret = ApplyGbBroadcastTransportHint(remoteIp, remotePort, payloadType, codec, input->RtpType);

    if (ret != 0) {

        printf("[ProtocolManager] gb audio stream apply transport failed=%d gb=%s\n", ret, gbCode != NULL ? gbCode : "");

        return -12;

    }



    ret = RespondGbMediaPlayInfo(handle, gbCode, kAudioStream, input);

    if (ret != 0) {

        return ret;

    }



    printf("[ProtocolManager] gb audio stream accepted gb=%s codec=%s pt=%d transport=%d remote=%s:%d\n",

           gbCode != NULL ? gbCode : "",

           codec.c_str(),

           payloadType,

           (int)input->RtpType,

           remoteIp.c_str(),

           remotePort);

    return 0;

}



int ProtocolManager::HandleGbLiveStreamRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input)

{

    const bool audioRequested = IsLiveAudioRequested(input);

    const bool audioEnabled = audioRequested;

    const int requestedStreamNum = ResolveGbStreamNumber(input, m_cfg);

    const bool preferSubStream = (requestedStreamNum > 0);



    int ret = ReconfigureGbLiveSender(input, gbCode, kLiveStream);

    if (ret != 0) {

        return ret;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        m_gb_live_session = GbLiveSession();

        m_gb_live_session.active = true;

        m_gb_live_session.stream_handle = handle;

        m_gb_live_session.gb_code = (gbCode != NULL) ? gbCode : "";

        m_gb_live_session.audio_requested = audioRequested;

        m_gb_live_session.audio_enabled = audioEnabled;

        m_gb_live_session.prefer_sub_stream = preferSubStream;

        m_gb_live_session.acked = false;

        printf("[ProtocolManager] gb live request gb=%s handle=%p stream_num=%d stream=%s audio_requested=%d\n",

               m_gb_live_session.gb_code.c_str(),

               handle,

               requestedStreamNum,

               preferSubStream ? "sub" : "main",

               audioRequested ? 1 : 0);

    }



    ret = StartGbLiveCapture();

    if (ret != 0) {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        m_gb_live_session = GbLiveSession();

        printf("[ProtocolManager] gb live capture start failed ret=%d gb=%s\n",

               ret,

               gbCode != NULL ? gbCode : "");

        return -25;

    }



    ret = RespondGbMediaPlayInfo(handle, gbCode, kLiveStream, input);

    if (ret != 0) {

        StopGbLiveCapture();

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        m_gb_live_session = GbLiveSession();

        return ret;

    }



    printf("[ProtocolManager] gb live stream accepted gb=%s handle=%p stream=%s audio_requested=%d audio_enabled=%d audio_codec=%s\n",

           gbCode != NULL ? gbCode : "",

           handle,

           preferSubStream ? "sub" : "main",

           audioRequested ? 1 : 0,

           audioEnabled ? 1 : 0,

           m_cfg.gb_live.audio_codec.c_str());

    return 0;

}



int ProtocolManager::HandleGbPlaybackRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input)

{

    return StartGbReplaySession(handle, gbCode, input, false);

}



int ProtocolManager::HandleGbDownloadRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input)

{

    return StartGbReplaySession(handle, gbCode, input, true);

}



int ProtocolManager::HandleGbStreamAck(StreamHandle handle)

{

    if (handle == NULL) {

        return -60;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        if (m_gb_replay_session.active &&

            m_gb_replay_session.stream_handle != NULL &&

            handle == m_gb_replay_session.stream_handle) {

            if (!m_gb_replay_session.acked) {

                const int ret = m_gb_replay_session.download

                                    ? Storage_Module_PauseDownload(m_gb_replay_session.storage_handle, false)

                                    : Storage_Module_PausePlaybackOnFile(m_gb_replay_session.storage_handle, false);

                if (ret != 0) {

                    printf("[ProtocolManager] gb replay ack resume failed ret=%d handle=%p storage=%d\n",

                           ret,

                           handle,

                           m_gb_replay_session.storage_handle);

                    return ret;

                }

                m_gb_replay_session.acked = true;

            }



            printf("[ProtocolManager] gb replay acked type=%s gb=%s handle=%p storage=%d\n",

                   m_gb_replay_session.download ? "download" : "playback",

                   m_gb_replay_session.gb_code.c_str(),

                   handle,

                   m_gb_replay_session.storage_handle);

            return 0;

        }

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active &&

            m_gb_live_session.stream_handle != NULL &&

            handle == m_gb_live_session.stream_handle) {

            m_gb_live_session.acked = true;

            printf("[ProtocolManager] gb live acked gb=%s handle=%p stream=%s audio_requested=%d audio_enabled=%d\n",

                   m_gb_live_session.gb_code.c_str(),

                   handle,

                   m_gb_live_session.prefer_sub_stream ? "sub" : "main",

                   m_gb_live_session.audio_requested ? 1 : 0,

                   m_gb_live_session.audio_enabled ? 1 : 0);

            return 0;

        }

    }



    printf("[ProtocolManager] gb stream ack ignored handle=%p\n", handle);

    return -61;

}



int ProtocolManager::HandleGbPlayControl(StreamHandle handle, const PlayCtrlCmd* cmd)

{

    if (cmd == NULL) {

        return -50;

    }



    GbReplaySession session;

    bool hasReplaySession = false;

    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        if (m_gb_replay_session.active) {

            if (handle != NULL && m_gb_replay_session.stream_handle != NULL && handle != m_gb_replay_session.stream_handle) {

                printf("[ProtocolManager] gb play control handle mismatch req=%p active=%p\n",

                       handle,

                       m_gb_replay_session.stream_handle);

                return -52;

            }



            session = m_gb_replay_session;

            hasReplaySession = true;

        }

    }



    if (!hasReplaySession) {

        if (cmd->Type == kPlayStart && handle != NULL) {

            return HandleGbStreamAck(handle);

        }



        printf("[ProtocolManager] gb play control ignored(no replay session) gb=%s handle=%p type=%d\n",

               cmd->GBCode,

               handle,

               (int)cmd->Type);

        return -51;

    }



    int ret = 0;

    const float speed = cmd->Scale;



    switch (cmd->Type) {

    case kPlayPause:

        ret = session.download ? Storage_Module_PauseDownload(session.storage_handle, true)

                               : Storage_Module_PausePlaybackOnFile(session.storage_handle, true);

        break;

    case kPlayStart:

        ret = session.download ? Storage_Module_PauseDownload(session.storage_handle, false)

                               : Storage_Module_PausePlaybackOnFile(session.storage_handle, false);

        break;

    case kPlayFast:

    case kPlaySlow:

        ret = -53;

        break;

    case kPlayDarg:

        if (session.download) {

            ret = -54;

        } else {

            ret = Storage_Module_SeekTime(session.storage_handle, (int)cmd->Npt);

        }

        break;

    default:

        ret = -55;

        break;

    }



    if (ret != 0) {

        printf("[ProtocolManager] gb play control failed ret=%d type=%d gb=%s handle=%p\n",

               ret,

               (int)cmd->Type,

               cmd->GBCode,

               handle);

    } else {

        printf("[ProtocolManager] gb play control success type=%d scale=%.2f npt=%u gb=%s handle=%p\n",

               (int)cmd->Type,

               speed,

               cmd->Npt,

               cmd->GBCode,

               handle);

    }



    return ret;

}



int ProtocolManager::HandleGbDeviceControl(ResponseHandle handle, const DevControlCmd* cmd)

{

    (void)handle;

    if (cmd == NULL) {

        return -68;

    }



    int ret = 0;

    switch (cmd->type) {

    case kPtzControl:

        ret = HandleGbPtzControl(cmd);

        break;

    case kGurdControl:

        ret = HandleGbGuardControl(cmd);

        break;

    case kAlarmResetControl:

        ret = HandleGbAlarmResetControl(cmd);

        break;

    case kDevConfigControl:

        ret = HandleGbConfigControl(cmd);

        break;

    case kDeviceUpgradeControl:

        ret = HandleGbDeviceUpgradeControl(cmd);

        break;

    case kHomePositionControl:

        ret = HandleGbHomePositionControl(cmd);

        break;

    case kTeleBootControl:

        ret = HandleGbTeleBootControl(cmd);

        break;

    case kDragZoomInControl:

    case kDragZoomOutControl:

    case kIFameControl:

    case kRecordControl:

        ret = 0;

        break;

    case kUnknowControl:

    default:

        ret = -69;

        break;

    }



    printf("[ProtocolManager] gb device control type=%s gb=%s ret=%d\n",

           ControlTypeName(cmd->type),

           cmd->GBCode,

           ret);

    return ret;

}



int ProtocolManager::HandleGbPtzControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -70;

    }



    const PtzCmd& ptzCmd = cmd->control_param.ptz_cmd;

    PtzPosition_s currentPosition;

    memset(&currentPosition, 0, sizeof(currentPosition));



    switch (ptzCmd.cmdType) {

    case kPtzStop:

        g_PtzHandle.DoMotorStop();

        return 0;

    case kPtzUp:

        g_PtzHandle.DoMotorMove(PTZ_UP);

        break;

    case kPtzDown:

        g_PtzHandle.DoMotorMove(PTZ_DOWN);

        break;

    case kPtzLeft:

        g_PtzHandle.DoMotorMove(PTZ_LEFT);

        break;

    case kPtzRight:

        g_PtzHandle.DoMotorMove(PTZ_RIGHT);

        break;

    case kPtzUpLeft:

        g_PtzHandle.DoMotorMove(PTZ_UP);

        g_PtzHandle.DoMotorMove(PTZ_LEFT);

        break;

    case kPtzUpRight:

        g_PtzHandle.DoMotorMove(PTZ_UP);

        g_PtzHandle.DoMotorMove(PTZ_RIGHT);

        break;

    case kPtzDownLeft:

        g_PtzHandle.DoMotorMove(PTZ_DOWN);

        g_PtzHandle.DoMotorMove(PTZ_LEFT);

        break;

    case kPtzDownRight:

        g_PtzHandle.DoMotorMove(PTZ_DOWN);

        g_PtzHandle.DoMotorMove(PTZ_RIGHT);

        break;

    case kPtzPosAdd: {

        if (g_PtzHandle.GetPosition(&currentPosition) != 0) {

            return -71;

        }



        PtzPosition savedPosition;

        if (!LoadPtzResumePosition(savedPosition)) {

            memset(&savedPosition, 0, sizeof(savedPosition));

        }

        savedPosition.Enable = true;

        savedPosition.x = currentPosition.x;

        savedPosition.y = currentPosition.y;

        savedPosition.privatex = currentPosition.x;

        savedPosition.privatey = currentPosition.y;

        return SavePtzResumePosition(savedPosition) ? 0 : -72;

    }

    case kPtzPosCall: {

        PtzPosition savedPosition;

        if (!LoadPtzResumePosition(savedPosition) || !savedPosition.Enable) {

            return -73;

        }



        return (g_PtzHandle.GotoPosition(savedPosition.x, savedPosition.y) == 0) ? 0 : -74;

    }

    case kPtzPosDel: {

        PtzPosition savedPosition;

        if (!LoadPtzResumePosition(savedPosition)) {

            memset(&savedPosition, 0, sizeof(savedPosition));

        }

        savedPosition.Enable = false;

        return SavePtzResumePosition(savedPosition) ? 0 : -75;

    }

    default:

        printf("[ProtocolManager] gb ptz cmd unsupported type=%s ctrl=%u gb=%s\n",

               PtzTypeName(ptzCmd.cmdType),

               ptzCmd.ctrlType,

               cmd->GBCode);

        return -76;

    }



    printf("[ProtocolManager] gb ptz cmd=%s ctrl=%u gb=%s\n",

           PtzTypeName(ptzCmd.cmdType),

           ptzCmd.ctrlType,

           cmd->GBCode);

    return 0;

}



int ProtocolManager::HandleGbGuardControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -77;

    }



    const bool enable = cmd->control_param.gurd_cmd;

    if (!UpdateGuardStatus(enable)) {

        return -78;

    }



    printf("[ProtocolManager] gb guard %s gb=%s\n",

           enable ? "enable" : "disable",

           cmd->GBCode);

    return 0;

}



int ProtocolManager::HandleGbAlarmResetControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -79;

    }



    const AlarmResetInfo& info = cmd->control_param.alarm_reset_cmd;

    if (info.AlarmMethod[0] == '\0') {

        return -80;

    }



    Storage_Module_ClearAlarmRecord();

    printf("[ProtocolManager] gb alarm reset method=%s type=%s gb=%s\n",

           info.AlarmMethod,

           info.AlarmType,

           cmd->GBCode);

    return 0;

}



int ProtocolManager::HandleGbConfigControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -81;

    }



    const ConfigSetParam& configParam = cmd->control_param.config_set_param;

    if (configParam.Num == 0 || configParam.arySetParam == NULL) {

        return -82;

    }



    bool handled = false;

    bool updated = false;

    bool restartLifecycle = false;

    bool applyOsdSwitch = false;

    bool applyImageFlip = false;

    int osdSwitch = -1;

    std::string imageFlipMode;

    for (unsigned int index = 0; index < configParam.Num; ++index) {

        const SettingParam& setting = configParam.arySetParam[index];

        if (setting.SetType == kBasicSetting) {

            handled = true;

            const BasicSetting& basic = setting.unionSetParam.Basic;

            if (basic.Expiration > 0 && m_cfg.gb_register.expires_sec != (int)basic.Expiration) {

                m_cfg.gb_register.expires_sec = (int)basic.Expiration;

                updated = true;

                restartLifecycle = true;

            }

            if (basic.HeartBeatInterval > 0 && m_cfg.gb_keepalive.interval_sec != (int)basic.HeartBeatInterval) {

                m_cfg.gb_keepalive.interval_sec = (int)basic.HeartBeatInterval;

                updated = true;

                restartLifecycle = true;

            }

            if (basic.HeartBeatcount > 0 && m_cfg.gb_keepalive.max_retry != (int)basic.HeartBeatcount) {

                m_cfg.gb_keepalive.max_retry = (int)basic.HeartBeatcount;

                updated = true;

                restartLifecycle = true;

            }

            if (basic.DeviceName[0] != '\0') {

                const std::string nextName = SafeStr(basic.DeviceName, sizeof(basic.DeviceName));

                if (!nextName.empty() && m_gb_device_name != nextName) {

                    m_gb_device_name = nextName;

                    m_cfg.gb_register.device_name = nextName;

                    updated = true;

                }

            }

            continue;

        }



        if (setting.SetType == kEncodeSetting || setting.SetType == kDecodeSetting) {

            handled = true;

            const SurveillanceParam* surveil = (setting.SetType == kEncodeSetting)

                ? &setting.unionSetParam.EncodeSetting.stuSurveilParam

                : &setting.unionSetParam.DecodeSetting.stuSurveilParam;

            const bool nextTimeEnabled = surveil->TimeShowFlag != 0;

            const bool nextEventEnabled = surveil->EventShowFlag != 0;

            const bool nextAlertEnabled = surveil->AlertShowFlag != 0;

            if (m_gb_osd_time_enabled != nextTimeEnabled) {

                m_gb_osd_time_enabled = nextTimeEnabled;

                m_cfg.gb_osd.time_enabled = nextTimeEnabled ? 1 : 0;

                updated = true;

            }

            if (m_gb_osd_event_enabled != nextEventEnabled) {

                m_gb_osd_event_enabled = nextEventEnabled;

                m_cfg.gb_osd.event_enabled = nextEventEnabled ? 1 : 0;

                updated = true;

            }

            if (m_gb_osd_alert_enabled != nextAlertEnabled) {

                m_gb_osd_alert_enabled = nextAlertEnabled;

                m_cfg.gb_osd.alert_enabled = nextAlertEnabled ? 1 : 0;

                updated = true;

            }

            osdSwitch = (nextTimeEnabled || nextEventEnabled || nextAlertEnabled) ? 1 : 0;

            applyOsdSwitch = true;

            printf("[ProtocolManager] gb config apply svac_%s time=%u event=%u alert=%u osd_switch=%d gb=%s\n",

                   (setting.SetType == kEncodeSetting) ? "encode" : "decode",

                   (unsigned int)surveil->TimeShowFlag,

                   (unsigned int)surveil->EventShowFlag,

                   (unsigned int)surveil->AlertShowFlag,

                   osdSwitch,

                   cmd->GBCode);

            continue;

        }



        if (setting.SetType == kImageSetting) {

            handled = true;

            const std::string nextMode = NormalizeGbImageFlipMode(
                SafeStr(setting.unionSetParam.Image.FlipMode, sizeof(setting.unionSetParam.Image.FlipMode)));

            if (!nextMode.empty()) {

                imageFlipMode = nextMode;

                applyImageFlip = true;

                if (m_cfg.gb_image.flip_mode != nextMode) {

                    m_cfg.gb_image.flip_mode = nextMode;

                    updated = true;

                }

                printf("[ProtocolManager] gb config apply image_flip=%s gb=%s\n",

                       nextMode.c_str(),

                       cmd->GBCode);

            }

            continue;

        }

        printf("[ProtocolManager] gb config ignore unsupported_set_type=%d gb=%s\n",

               (int)setting.SetType,

               cmd->GBCode);

    }



    if (!handled) {

        return -83;

    }



    if (applyOsdSwitch) {

        const int captureRet = CaptureSetOSDSwitch(osdSwitch);

        const int osdRet = rk_osd_set_enabled(kRkOsdDateTimeId, osdSwitch);

        printf("[ProtocolManager] gb config osd apply capture_ret=%d osd_ret=%d enabled=%d osd_id=%d gb=%s\n",

               captureRet,

               osdRet,

               osdSwitch,

               kRkOsdDateTimeId,

               cmd->GBCode);

    }



    if (applyImageFlip) {

        ApplyRkImageFlipConfig(imageFlipMode);

    }

    if (applyOsdSwitch) {

        ApplyRkOsdConfig(m_cfg.gb_osd);

    }

    if (updated && m_provider.get() != NULL) {

        const int pushRet = m_provider->PushApply(m_cfg);

        if (pushRet != 0) {

            printf("[ProtocolManager] gb config push apply failed ret=%d gb=%s\n",

                   pushRet,

                   cmd->GBCode);

        }

    }



    printf("[ProtocolManager] gb config apply device_name=%s expires=%d heartbeat=%d retry=%d osd(time=%d,event=%d,alert=%d) image_flip=%s gb=%s\n",

           ResolveGbDeviceName(m_cfg, m_gb_device_name).c_str(),

           m_cfg.gb_register.expires_sec,

           m_cfg.gb_keepalive.interval_sec,

           m_cfg.gb_keepalive.max_retry,

           m_gb_osd_time_enabled ? 1 : 0,

           m_gb_osd_event_enabled ? 1 : 0,

           m_gb_osd_alert_enabled ? 1 : 0,

           NormalizeGbImageFlipMode(m_cfg.gb_image.flip_mode).c_str(),

           cmd->GBCode);



    if (restartLifecycle && m_started) {

        StopGbClientLifecycle();

        return StartGbClientLifecycle();

    }



    return 0;

}



int ProtocolManager::HandleGbHomePositionControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -84;

    }



    const HomePositionCmd& home = cmd->control_param.homeposition_cmd;

    PtzPosition savedPosition;

    if (!LoadPtzResumePosition(savedPosition)) {

        memset(&savedPosition, 0, sizeof(savedPosition));

    }



    if (home.enable != 0) {

        PtzPosition_s currentPosition;

        memset(&currentPosition, 0, sizeof(currentPosition));

        if (g_PtzHandle.GetPosition(&currentPosition) != 0) {

            return -85;

        }



        savedPosition.Enable = true;

        savedPosition.x = currentPosition.x;

        savedPosition.y = currentPosition.y;

        savedPosition.privatex = currentPosition.x;

        savedPosition.privatey = currentPosition.y;

    } else {

        savedPosition.Enable = false;

    }



    if (!SavePtzResumePosition(savedPosition)) {

        return -86;

    }



    printf("[ProtocolManager] gb home position enable=%u reset=%u preset=%u gb=%s\n",

           home.enable,

           home.resetTime,

           home.presetIndex,

           cmd->GBCode);

    return 0;

}



int ProtocolManager::HandleGbTeleBootControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -87;

    }



    if (!CreateDetachedThread((char*)"gb_reboot", GbRemoteRestartThread, NULL, true)) {

        return -88;

    }



    printf("[ProtocolManager] gb teleboot scheduled gb=%s cooldown=%d\n",

           cmd->GBCode,

           m_cfg.gb_reboot.cooldown_sec);

    return 0;

}



int ProtocolManager::HandleGbDeviceUpgradeControl(const DevControlCmd* cmd)

{

    if (cmd == NULL) {

        return -101;

    }



    const DeviceUpgradeCmd& upgrade = cmd->control_param.device_upgrade_cmd;
    const std::string firmware = TrimWhitespaceCopy(SafeStr(upgrade.Firmware, sizeof(upgrade.Firmware)));
    const std::string fileUrl = TrimWhitespaceCopy(SafeStr(upgrade.FileURL, sizeof(upgrade.FileURL)));
    const std::string manufacturer = TrimWhitespaceCopy(SafeStr(upgrade.Manufacturer, sizeof(upgrade.Manufacturer)));
    const std::string sessionId = TrimWhitespaceCopy(SafeStr(upgrade.SessionID, sizeof(upgrade.SessionID)));
    const std::string expectedChecksum = NormalizeHexDigest(SafeStr(upgrade.Checksum, sizeof(upgrade.Checksum)));
    const unsigned long long expectedFileSize = static_cast<unsigned long long>(upgrade.FileSize);
    const bool forceUpgrade = (upgrade.ForceUpgrade != 0);
    bool packageInstalled = false;

    const auto fail = [&](int code, const char* reason) -> int {

        PersistGbUpgradePendingState(cmd->GBCode,
                                     sessionId.c_str(),
                                     firmware.c_str(),
                                     reason,
                                     false);

        NotifyGbUpgradeResult(cmd->GBCode,
                              sessionId.c_str(),
                              firmware.c_str(),
                              false,
                              reason);

        RemoveFileQuietly(kGbUpgradePackageTempPath);
        RemoveFileQuietly(kGbUpgradeTriggerFlag);
        if (packageInstalled) {

            RemoveFileQuietly(kGbUpgradePackagePath);

        }

        printf("[ProtocolManager] gb upgrade rejected ret=%d reason=%s firmware=%s manufacturer=%s session=%s force=%d url=%s gb=%s\n",

               code,

               (reason != NULL) ? reason : "unknown",

               firmware.c_str(),

               manufacturer.c_str(),

               sessionId.c_str(),

               forceUpgrade ? 1 : 0,

               fileUrl.c_str(),

               cmd->GBCode);

        return code;

    };



    if (fileUrl.empty() || !IsHttpUrl(fileUrl)) {

        return fail(-102, "invalid_url");

    }

    if (!IsValidUpgradeSessionId(sessionId)) {

        return fail(-103, "invalid_session");

    }

    if (!manufacturer.empty()) {

        const std::string localManufacturer = ToLowerCopy(TrimWhitespaceCopy(ResolveGbManufacturerName(m_cfg)));
        if (!localManufacturer.empty() &&
            ToLowerCopy(manufacturer) != localManufacturer) {

            return fail(-104, "manufacturer_mismatch");

        }

    }

    if (!IsUpgradeUrlAllowed(fileUrl, m_cfg.gb_upgrade.url_whitelist)) {

        return fail(-105, "url_not_allowed");

    }



    RemoveFileQuietly(kGbUpgradePackageTempPath);
    RemoveFileQuietly(kGbUpgradeTriggerFlag);

    const int downloadTimeout = (m_cfg.gb_upgrade.timeout_sec > 0) ? m_cfg.gb_upgrade.timeout_sec : 120;
    const int downloadRet = DownloadFileByCurl(fileUrl, kGbUpgradePackageTempPath, downloadTimeout);
    if (downloadRet != 0) {

        return fail(-106, "download_failed");

    }

    unsigned long long actualFileSize = 0;
    if (!ReadFileSizeBytes(kGbUpgradePackageTempPath, actualFileSize) || actualFileSize == 0) {

        return fail(-107, "empty_package");

    }

    if (m_cfg.gb_upgrade.max_package_mb > 0) {

        const unsigned long long maxPackageBytes =
            static_cast<unsigned long long>(m_cfg.gb_upgrade.max_package_mb) * 1024ULL * 1024ULL;
        if (actualFileSize > maxPackageBytes) {

            return fail(-108, "package_too_large");

        }

    }

    if (expectedFileSize > 0 && actualFileSize != expectedFileSize) {

        return fail(-109, "size_mismatch");

    }

    if (!expectedChecksum.empty()) {

        const std::string digestMode = ResolveUpgradeDigestMode(expectedChecksum, m_cfg);
        if ((digestMode == "md5" && expectedChecksum.size() != 32) ||
            (digestMode == "sha256" && expectedChecksum.size() != 64)) {

            return fail(-110, "invalid_checksum_format");

        }

        if (digestMode != "md5" && digestMode != "sha256") {

            return fail(-110, "unsupported_checksum_mode");

        }

        std::string actualChecksum;
        if (!ComputeFileDigest(kGbUpgradePackageTempPath, digestMode, actualChecksum)) {

            return fail(-111, "checksum_compute_failed");

        }

        if (actualChecksum != expectedChecksum) {

            return fail(-112, "checksum_mismatch");

        }

    }



    RemoveFileQuietly(kGbUpgradePackagePath);
    if (rename(kGbUpgradePackageTempPath, kGbUpgradePackagePath) != 0) {

        printf("[ProtocolManager] gb upgrade rename failed errno=%d temp=%s final=%s gb=%s\n",

               errno,

               kGbUpgradePackageTempPath,

               kGbUpgradePackagePath,

               cmd->GBCode);

        return fail(-113, "rename_failed");

    }

    packageInstalled = true;
    sync();

    if (!TouchFileMarker(kGbUpgradeTriggerFlag)) {

        printf("[ProtocolManager] gb upgrade trigger create failed errno=%d flag=%s gb=%s\n",

               errno,

               kGbUpgradeTriggerFlag,

               cmd->GBCode);

        return fail(-114, "trigger_failed");

    }

    sync();

    PersistGbUpgradePendingState(cmd->GBCode,
                                 sessionId.c_str(),
                                 firmware.c_str(),
                                 "accepted_for_upgrade",
                                 true);

    NotifyGbUpgradeResult(cmd->GBCode,
                          sessionId.c_str(),
                          firmware.c_str(),
                          true,
                          "accepted_for_upgrade");

    printf("[ProtocolManager] gb upgrade staged firmware=%s manufacturer=%s session=%s force=%d expected_size=%llu actual_size=%llu checksum=%s package=%s flag=%s gb=%s\n",

           firmware.c_str(),

           manufacturer.c_str(),

           sessionId.c_str(),

           forceUpgrade ? 1 : 0,

           expectedFileSize,

           actualFileSize,

           expectedChecksum.empty() ? "none" : expectedChecksum.c_str(),

           kGbUpgradePackagePath,

           kGbUpgradeTriggerFlag,

           cmd->GBCode);

    return 0;

}



int ProtocolManager::HandleGbSubscribe(SubscribeHandle handle, SubscribeType type, const char* gbCode, void* info)

{

    (void)info;



    if (handle == NULL &&

        type != kCatalogSubscribeExpire &&

        type != kAlarmSubscribeExpire &&

        type != kMobilePositionSubscribeExpire) {

        return -89;

    }



    bool notifyCatalog = false;

    {

        std::lock_guard<std::mutex> lock(m_gb_subscribe_mutex);

        switch (type) {

        case kCatalogSubscribe:

            m_gb_catalog_subscribe_handle = handle;

            notifyCatalog = true;

            break;

        case kAlarmSubscribe:

            m_gb_alarm_subscribe_handle = handle;

            break;

        case kMobilePositionSubscribe:

            m_gb_mobile_position_subscribe_handle = handle;

            break;

        case kCatalogSubscribeExpire:

            m_gb_catalog_subscribe_handle = NULL;

            break;

        case kAlarmSubscribeExpire:

            m_gb_alarm_subscribe_handle = NULL;

            break;

        case kMobilePositionSubscribeExpire:

            m_gb_mobile_position_subscribe_handle = NULL;

            break;

        default:

            return -90;

        }

    }



    int ret = 0;

    if (notifyCatalog) {

        ret = NotifyGbCatalog(gbCode);

    }



    printf("[ProtocolManager] module=gb28181 event=subscribe device=%s session=%p trace=manager error=%d type=%s action=%s catalog_snapshot=%s ret=%d\n",

           gbCode != NULL ? gbCode : "",

           handle,

           (ret == 0) ? 0 : ret,

           GbSubscribeTypeToString(type),

           IsGbSubscribeExpire(type) ? "unbind" : "bind",

           notifyCatalog ? "sent" : "skip",

           ret);

    return ret;

}



int ProtocolManager::NotifyGbCatalog(const char* gbCode)

{

    return NotifyGbCatalogInternal(gbCode);

}



int ProtocolManager::NotifyGbAlarm(const AlarmNotifyInfo* info)

{

    if (info == NULL) {

        return -91;

    }



    SubscribeHandle handle = NULL;

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    {

        std::lock_guard<std::mutex> lock(m_gb_subscribe_mutex);

        handle = m_gb_alarm_subscribe_handle;

    }

    if (handle == NULL) {

        return 0;

    }



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->NotifyDeviceAlarmForSub(handle, info);

    if (!IsGbSdkSuccess(ret)) {

        return ret;

    }

#endif



    printf("[ProtocolManager] module=gb28181 event=alarm_notify device=%s session=%p trace=manager error=0 alarm=%s priority=%d method=%d\n",

           info->DeviceID,

           handle,

           info->AlarmID,

           info->AlarmPriority,

           info->AlarmMethod);

    return 0;

}



int ProtocolManager::NotifyGbMobilePosition(const MobilePositionInfo* info)

{

    if (info == NULL) {

        return -92;

    }



    SubscribeHandle handle = NULL;

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    {

        std::lock_guard<std::mutex> lock(m_gb_subscribe_mutex);

        handle = m_gb_mobile_position_subscribe_handle;

    }

    if (handle == NULL) {

        return 0;

    }



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->NotifyMobilePositionForSub(handle, info);

    if (!IsGbSdkSuccess(ret)) {

        return ret;

    }

#endif



    printf("[ProtocolManager] module=gb28181 event=mobile_position_notify device=%s session=%p trace=manager error=0 lon=%.6f lat=%.6f speed=%.2f direction=%.2f\n",

           info->GBCode,

           handle,

           info->Longitude,

           info->Latitude,

           info->Speed,

           info->Direction);

    return 0;

}



int ProtocolManager::NotifyGbCatalogInternal(const char* gbCode)

{

    SubscribeHandle handle = NULL;

    const int catalogCount = 1;

    const char* manufacturer = "IPC";

    const char* model = "RC0240";

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    {

        std::lock_guard<std::mutex> lock(m_gb_subscribe_mutex);

        handle = m_gb_catalog_subscribe_handle;

    }

    if (handle == NULL) {

        return 0;

    }



    DeviceCatalogList catalogList;

    CatalogInfo catalogInfo;

    memset(&catalogList, 0, sizeof(catalogList));

    memset(&catalogInfo, 0, sizeof(catalogInfo));



    std::string deviceId = (gbCode != NULL && gbCode[0] != '\0') ? gbCode : m_cfg.gb_register.device_id;

    if (deviceId.empty()) {

        deviceId = "34020000001320000001";

    }



    CopyBounded(catalogList.GBCode, sizeof(catalogList.GBCode), deviceId);

    catalogList.sum = 1;

    catalogList.catalog = &catalogInfo;



    CopyBounded(catalogInfo.Event, sizeof(catalogInfo.Event), EVENT_ON);

    CopyBounded(catalogInfo.DeviceID, sizeof(catalogInfo.DeviceID), deviceId);

    CopyBounded(catalogInfo.Name, sizeof(catalogInfo.Name), deviceId);

    CopyBounded(catalogInfo.StreamNumberList, sizeof(catalogInfo.StreamNumberList), BuildGbStreamNumberList());

    CopyBounded(catalogInfo.Manufacturer, sizeof(catalogInfo.Manufacturer), manufacturer);

    CopyBounded(catalogInfo.Model, sizeof(catalogInfo.Model), model);

    catalogInfo.Parental = false;

    catalogInfo.Status = true;

    catalogInfo.port = 0;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->NotifyDeviceCatalogForSub(&catalogList);

    if (!IsGbSdkSuccess(ret)) {

        return ret;

    }

#endif



    printf("[ProtocolManager] module=gb28181 event=catalog_notify device=%s session=%p trace=manager error=0 catalog_count=%d manufacturer=%s model=%s\n",

           (gbCode != NULL && gbCode[0] != '\0') ? gbCode : m_cfg.gb_register.device_id.c_str(),

           handle,

           catalogCount,

           manufacturer,

           model);

    return 0;

}



int ProtocolManager::HandleGbQueryRequest(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -60;

    }



    int ret = 0;

    switch (param->type) {

    case kDeviceInfoQuery:

        ret = ResponseGbQueryDeviceInfo(handle, param);

        break;

    case kDeviceStatusQuery:

        ret = ResponseGbQueryDeviceStatus(handle, param);

        break;

    case kDeviceCatalogQuery:

        ret = ResponseGbQueryCatalog(handle, param);

        break;

    case kDeviceRecordQuery:

        ret = ResponseGbQueryRecord(handle, param);

        break;

    case kDeviceConfigQuery:

        ret = ResponseGbQueryConfig(handle, param);

        break;

    case kDevicePresetQuery:

        ret = ResponseGbQueryPreset(handle, param);

        break;

    case kUnknowQuery:

    default:

        ret = -61;

        break;

    }



    printf("[ProtocolManager] gb query type=%s gb=%s handle=%p ret=%d\n",

           QueryTypeName(param->type),

           param->GBCode,

           handle,

           ret);

    return ret;

}



int ProtocolManager::ResponseGbQueryDeviceInfo(ResponseHandle handle, const QueryParam* param)

{

    (void)handle;

    if (param == NULL) {

        return -62;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    DeviceInfo info;

    memset(&info, 0, sizeof(info));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    const std::string deviceName = ResolveGbDeviceName(m_cfg, m_gb_device_name);
    const std::string manufacturer = ResolveGbManufacturerName(m_cfg);
    const std::string model = ResolveGbModelName(m_cfg);
    const std::string deviceType = ResolveGbDeviceTypeName();
    const unsigned int videoChannelCount = ResolveGbVideoChannelCount();

    CopyBounded(info.GBCode, sizeof(info.GBCode), gbCode);
    CopyBounded(info.device_name, sizeof(info.device_name), deviceName);
    CopyBounded(info.device_type, sizeof(info.device_type), deviceType);
    CopyBounded(info.manufacturer, sizeof(info.manufacturer), manufacturer);
    CopyBounded(info.model, sizeof(info.model), model);
    CopyBounded(info.firmware, sizeof(info.firmware), m_cfg.version);
    info.video_channel_num = videoChannelCount;
    info.result = true;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    printf("[ProtocolManager] module=gb28181 event=device_info_response trace=manager error=0 gb=%s name=%s type=%s manufacturer=%s model=%s firmware=%s channel=%u\n",
           info.GBCode,
           info.device_name,
           info.device_type,
           info.manufacturer,
           info.model,
           info.firmware,
           info.video_channel_num);

    const int ret = m_gb_client_sdk->ResponseDeviceInfo(handle, &info);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



int ProtocolManager::ResponseGbQueryDeviceStatus(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -63;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    DeviceStatus status;

    memset(&status, 0, sizeof(status));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    CopyBounded(status.GBCode, sizeof(status.GBCode), gbCode);

    status.Result = true;

    status.Encode = true;

    status.Record = true;

    status.OnLine = true;

    status.StatusOK = true;

    status.AlarmNum = 0;

    status.Alarm_Status = NULL;



    time_t now = time(NULL);

    if (now > 0) {

        struct tm tmNow;

        memset(&tmNow, 0, sizeof(tmNow));

#if defined(_WIN32)

        localtime_s(&tmNow, &now);

#else

        localtime_r(&now, &tmNow);

#endif

        FillGbTimeString(status.DevDateTime, sizeof(status.DevDateTime));

    }



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseDeviceStatus(handle, &status);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



int ProtocolManager::ResponseGbQueryCatalog(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -64;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    DeviceCatalogList catalogList;

    CatalogInfo catalogInfo;

    memset(&catalogList, 0, sizeof(catalogList));

    memset(&catalogInfo, 0, sizeof(catalogInfo));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    CopyBounded(catalogList.GBCode, sizeof(catalogList.GBCode), gbCode);

    catalogList.sum = 1;

    catalogList.catalog = &catalogInfo;



    CopyBounded(catalogInfo.Event, sizeof(catalogInfo.Event), EVENT_ON);

    CopyBounded(catalogInfo.DeviceID, sizeof(catalogInfo.DeviceID), gbCode);

    CopyBounded(catalogInfo.Name, sizeof(catalogInfo.Name), gbCode);

    CopyBounded(catalogInfo.StreamNumberList, sizeof(catalogInfo.StreamNumberList), BuildGbStreamNumberList());

    CopyBounded(catalogInfo.Manufacturer, sizeof(catalogInfo.Manufacturer), "IPC");

    CopyBounded(catalogInfo.Model, sizeof(catalogInfo.Model), "RC0240");

    catalogInfo.Parental = false;

    catalogInfo.port = 0;



    const int transMode = (ToLowerCopy(m_cfg.gb_live.transport) == "tcp") ? 1 : 0;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseDeviceCatalog(handle, &catalogList, transMode);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



int ProtocolManager::ResponseGbQueryRecord(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -65;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    RecordIndex recordIndex;

    memset(&recordIndex, 0, sizeof(recordIndex));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    CopyBounded(recordIndex.GBCode, sizeof(recordIndex.GBCode), gbCode);

    recordIndex.Num = 0;

    recordIndex.record_list = NULL;



    const int transMode = (ToLowerCopy(m_cfg.gb_live.transport) == "tcp") ? 1 : 0;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseRecordIndex(handle, &recordIndex, transMode);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



int ProtocolManager::ResponseGbQueryConfig(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -66;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    DeviceConfigDownload deviceConfig;

    memset(&deviceConfig, 0, sizeof(deviceConfig));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    int runtimeOsdEnabled = (m_gb_osd_time_enabled || m_gb_osd_event_enabled || m_gb_osd_alert_enabled) ? 1 : 0;

    int osdEnabledValue = 0;

    if (ReadRkOsdEnabled(kRkOsdDateTimeId, &osdEnabledValue)) {

        runtimeOsdEnabled = osdEnabledValue;

    }



    std::string mainCodec = NormalizeRkVideoCodec(m_cfg.gb_video.main_codec);

    std::string subCodec = NormalizeRkVideoCodec(m_cfg.gb_video.sub_codec);

    std::string mainFrameRate = BuildFrameRateValue(m_cfg.gb_video.main_fps);

    std::string subFrameRate = BuildFrameRateValue(m_cfg.gb_video.sub_fps);

    int mainBitrate = m_cfg.gb_video.main_bitrate_kbps;

    int subBitrate = m_cfg.gb_video.sub_bitrate_kbps;

    int mainGop = 0;

    int subGop = 0;

    (void)ReadRkVideoOutputTypeString(0, mainCodec);

    (void)ReadRkVideoOutputTypeString(1, subCodec);

    (void)ReadRkVideoFrameRateString(0, mainFrameRate);

    (void)ReadRkVideoFrameRateString(1, subFrameRate);

    (void)rk_video_get_max_rate(0, &mainBitrate);

    (void)rk_video_get_max_rate(1, &subBitrate);

    (void)rk_video_get_gop(0, &mainGop);

    (void)rk_video_get_gop(1, &subGop);



    std::vector<ConfigParam> configList;

    if (IsConfigTypeRequested(param, kBasicParam)) {

        ConfigParam item;

        memset(&item, 0, sizeof(item));

        item.CfgType = kBasicParam;

        CopyBounded(item.UnionCfgParam.CfgBasic.DeviceName,

                    sizeof(item.UnionCfgParam.CfgBasic.DeviceName),

                    ResolveGbDeviceName(m_cfg, m_gb_device_name));

        item.UnionCfgParam.CfgBasic.Expiration = (unsigned int)((m_cfg.gb_register.expires_sec > 0) ? m_cfg.gb_register.expires_sec : 3600);

        item.UnionCfgParam.CfgBasic.HeartBeatInterval = (unsigned int)((m_cfg.gb_keepalive.interval_sec > 0) ? m_cfg.gb_keepalive.interval_sec : 60);

        item.UnionCfgParam.CfgBasic.HeartBeatcount = (unsigned int)((m_cfg.gb_keepalive.max_retry > 0) ? m_cfg.gb_keepalive.max_retry : 3);

        item.UnionCfgParam.CfgBasic.PosCapability = 0;

        item.UnionCfgParam.CfgBasic.Longitude = 0.0;

        item.UnionCfgParam.CfgBasic.Latitude = 0.0;

        configList.push_back(item);

    }



    if (IsConfigTypeRequested(param, kVideoParamOpt)) {

        ConfigParam item;

        memset(&item, 0, sizeof(item));

        item.CfgType = kVideoParamOpt;

        item.UnionCfgParam.CfgVideoOpt.DownloadSpeedOpt = 1;

        CopyBounded(item.UnionCfgParam.CfgVideoOpt.Resolution,

                    sizeof(item.UnionCfgParam.CfgVideoOpt.Resolution),

                    BuildGbVideoResolutionSummary());

        std::string imageFlip = NormalizeGbImageFlipMode(m_cfg.gb_image.flip_mode);

        std::string runtimeImageFlip;

        if (ReadRkImageFlipMode(runtimeImageFlip)) {

            imageFlip = NormalizeGbImageFlipMode(runtimeImageFlip);

        }

        CopyBounded(item.UnionCfgParam.CfgVideoOpt.ImageFlip,

                    sizeof(item.UnionCfgParam.CfgVideoOpt.ImageFlip),

                    imageFlip);

        configList.push_back(item);

    }



    if (IsConfigTypeRequested(param, kSVACEncodeConfig)) {

        ConfigParam item;

        memset(&item, 0, sizeof(item));

        item.CfgType = kSVACEncodeConfig;

        item.UnionCfgParam.CfgEncode.stuRoiParam.ROIFlag = 0;

        item.UnionCfgParam.CfgEncode.stuRoiParam.ROINum = 0;

        item.UnionCfgParam.CfgEncode.stuRoiParam.aryParam = NULL;

        item.UnionCfgParam.CfgEncode.stuSurveilParam.TimeShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.time_enabled : 0U;

        item.UnionCfgParam.CfgEncode.stuSurveilParam.EventShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.event_enabled : 0U;

        item.UnionCfgParam.CfgEncode.stuSurveilParam.AlertShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.alert_enabled : 0U;

        item.UnionCfgParam.CfgEncode.AudioRecognitionFlag.AudioRecognitionFlag = 0;

        configList.push_back(item);

    }



    if (IsConfigTypeRequested(param, kSVACDecodeConfig)) {

        ConfigParam item;

        memset(&item, 0, sizeof(item));

        item.CfgType = kSVACDecodeConfig;

        item.UnionCfgParam.CfgDecode.stuSurveilParam.TimeShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.time_enabled : 0U;

        item.UnionCfgParam.CfgDecode.stuSurveilParam.EventShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.event_enabled : 0U;

        item.UnionCfgParam.CfgDecode.stuSurveilParam.AlertShowFlag = runtimeOsdEnabled ? (unsigned int)m_cfg.gb_osd.alert_enabled : 0U;

        configList.push_back(item);

    }



    CopyBounded(deviceConfig.GBCode, sizeof(deviceConfig.GBCode), gbCode);

    deviceConfig.Result = true;

    deviceConfig.Num = (unsigned int)configList.size();

    deviceConfig.CfgParam = configList.empty() ? NULL : &configList[0];



    printf("[ProtocolManager] gb query config response num=%u main_codec=%s main_fps=%s main_bitrate=%d main_gop=%d sub_codec=%s sub_fps=%s sub_bitrate=%d sub_gop=%d osd(time=%d,event=%d,alert=%d,enabled=%d) gb=%s\n",

           deviceConfig.Num,

           mainCodec.empty() ? "unknown" : mainCodec.c_str(),

           mainFrameRate.empty() ? "unknown" : mainFrameRate.c_str(),

           mainBitrate,

           mainGop,

           subCodec.empty() ? "unknown" : subCodec.c_str(),

           subFrameRate.empty() ? "unknown" : subFrameRate.c_str(),

           subBitrate,

           subGop,

           m_cfg.gb_osd.time_enabled,

           m_cfg.gb_osd.event_enabled,

           m_cfg.gb_osd.alert_enabled,

           runtimeOsdEnabled,

           gbCode.c_str());



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseDeviceConfigQuery(handle, &deviceConfig);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



int ProtocolManager::ResponseGbQueryPreset(ResponseHandle handle, const QueryParam* param)

{

    if (param == NULL) {

        return -67;

    }



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    PresetInfo presetInfo;

    memset(&presetInfo, 0, sizeof(presetInfo));



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    CopyBounded(presetInfo.DeviceID, sizeof(presetInfo.DeviceID), gbCode);

    presetInfo.PersetListNum = 0;

    presetInfo.PresetList = NULL;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseDevicePresetInfo(handle, &presetInfo);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    return 0;

#endif

}



void ProtocolManager::HandleGbStopStreamRequest(StreamHandle handle, const char* gbCode)

{

    GbReplaySession replaySession;

    bool needStopReplay = false;

    GbLiveSession liveSession;

    bool needStopLive = false;



    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        if (m_gb_replay_session.active &&

            (handle == NULL ||

             m_gb_replay_session.stream_handle == NULL ||

             handle == m_gb_replay_session.stream_handle)) {

            replaySession = m_gb_replay_session;

            m_gb_replay_session = GbReplaySession();

            needStopReplay = true;

        }

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active &&

            (handle == NULL ||

             m_gb_live_session.stream_handle == NULL ||

             handle == m_gb_live_session.stream_handle)) {

            liveSession = m_gb_live_session;

            m_gb_live_session = GbLiveSession();

            needStopLive = true;

        }

    }



    if (needStopReplay) {

        if (replaySession.download) {

            Storage_Module_StopDownload(replaySession.storage_handle);

        } else {

            Storage_Module_StopPlayback(replaySession.storage_handle);

        }



        NotifyGbMediaStatus(replaySession.stream_handle,

                            replaySession.gb_code,

                            121,

                            "stop_stream");



        printf("[ProtocolManager] gb replay session stopped type=%s gb=%s handle=%p storage=%d\n",

               replaySession.download ? "download" : "playback",

               replaySession.gb_code.c_str(),

               replaySession.stream_handle,

               replaySession.storage_handle);

    }



    if (needStopLive) {

        StopGbLiveCapture();

        m_rtp_ps_sender.CloseSession();

        printf("[ProtocolManager] gb live session stopped gb=%s handle=%p stream=%s audio_requested=%d audio_enabled=%d sent_video=%u sent_audio=%u\n",

               liveSession.gb_code.c_str(),

               liveSession.stream_handle,

               liveSession.prefer_sub_stream ? "sub" : "main",

               liveSession.audio_requested ? 1 : 0,

               liveSession.audio_enabled ? 1 : 0,

               liveSession.sent_video_frames,

               liveSession.sent_audio_frames);

    }



    printf("[ProtocolManager] gb stop stream gb=%s handle=%p\n", gbCode != NULL ? gbCode : "", handle);

}



int ProtocolManager::ReconfigureGbLiveSender(const MediaInfo* input, const char* gbCode, StreamRequestType requestType)

{

    if (input == NULL) {

        return -20;

    }



    if (input->RtpType != kRtpOverUdp &&

        input->RtpType != kRtpOverTcp &&

        input->RtpType != kRtpOverTcpActive &&

        input->RtpType != kRtpOverTcpPassive) {

        printf("[ProtocolManager] gb %s stream unsupported transport gb=%s type=%d\n",

               StreamRequestTypeName(requestType),

               gbCode != NULL ? gbCode : "",

               (int)input->RtpType);

        return -21;

    }



    std::string remoteIp;

    int remotePort = 0;

    if (!ExtractStreamEndpoint(input, remoteIp, remotePort)) {

        printf("[ProtocolManager] gb %s stream parse endpoint failed gb=%s\n",

               StreamRequestTypeName(requestType),

               gbCode != NULL ? gbCode : "");

        return -22;

    }



    GbLiveParam runtimeParam = m_cfg.gb_live;

    runtimeParam.target_ip = remoteIp;

    runtimeParam.target_port = remotePort;

    runtimeParam.transport = (input->RtpType == kRtpOverUdp) ? "udp" : "tcp";



    m_rtp_ps_sender.CloseSession();



    int ret = m_rtp_ps_sender.Init(runtimeParam);

    if (ret != 0) {

        printf("[ProtocolManager] gb %s stream sender init failed ret=%d gb=%s\n",

               StreamRequestTypeName(requestType),

               ret,

               gbCode != NULL ? gbCode : "");

        return -23;

    }



    ret = m_rtp_ps_sender.OpenSession();

    if (ret != 0) {

        printf("[ProtocolManager] gb %s stream sender open failed ret=%d gb=%s remote=%s:%d\n",

               StreamRequestTypeName(requestType),

               ret,

               gbCode != NULL ? gbCode : "",

               remoteIp.c_str(),

               remotePort);



        m_rtp_ps_sender.Init(m_cfg.gb_live);

        m_rtp_ps_sender.OpenSession();

        return -24;

    }



    printf("[ProtocolManager] gb %s stream sender target=%s:%d gb=%s\n",

           StreamRequestTypeName(requestType),

           remoteIp.c_str(),

           remotePort,

           gbCode != NULL ? gbCode : "");

    return 0;

}



int ProtocolManager::BuildGbResponseMediaInfo(const char* gbCode,

                                              StreamRequestType requestType,

                                              const MediaInfo* input,

                                              MediaInfo& out,

                                              RtpMap& outMap) const

{

    if (requestType == kAudioStream) {

        return BuildGbBroadcastMediaInfo("", gbCode != NULL ? gbCode : "", out, outMap);

    }



    if (input == NULL) {

        return -1;

    }



    memset(&out, 0, sizeof(out));

    memset(&outMap, 0, sizeof(outMap));



    const int defaultPayload = (m_cfg.gb_live.payload_type >= 0 && m_cfg.gb_live.payload_type <= 127)

                                   ? m_cfg.gb_live.payload_type

                                   : 96;



    const std::string requestIp = SafeStr(input->IP, sizeof(input->IP));

    const std::string resolvedIp = requestIp.empty() ? m_cfg.gb_live.target_ip : requestIp;

    const unsigned int resolvedPort = (input->Port > 0)

                                          ? input->Port

                                          : (unsigned int)((m_cfg.gb_live.target_port > 0) ? m_cfg.gb_live.target_port : 0);



    if (input->RtpType == kRtpOverTcp ||

        input->RtpType == kRtpOverTcpActive ||

        input->RtpType == kRtpOverTcpPassive) {

        out.RtpType = input->RtpType;

    } else if (ToLowerCopy(m_cfg.gb_live.transport) == "tcp") {

        out.RtpType = kRtpOverTcp;

    } else {

        out.RtpType = kRtpOverUdp;

    }

    out.RequestType = requestType;

    out.StreamNum = ResolveGbStreamNumber(input, m_cfg);

    out.Port = resolvedPort;

    out.StartTime = input->StartTime;

    out.EndTime = input->EndTime;

    out.DownloadSpeed = input->DownloadSpeed;

    out.FileSize = input->FileSize;



    CopyBounded(out.DeviceID,

                sizeof(out.DeviceID),

                (gbCode != NULL && gbCode[0] != '\0') ? gbCode : m_cfg.gb_register.device_id);

    CopyBounded(out.IP, sizeof(out.IP), resolvedIp);



    const unsigned int ssrcValue = (m_cfg.gb_live.ssrc > 0) ? (unsigned int)m_cfg.gb_live.ssrc : 1u;

    snprintf(out.Ssrc, sizeof(out.Ssrc), "%010u", ssrcValue);



    out.RtpDescri.type = kGBVideo;

    out.RtpDescri.DescriNum = 1;

    out.RtpDescri.mapDescri = &outMap;



    if (input->RtpDescri.DescriNum > 0 && input->RtpDescri.mapDescri != NULL) {

        outMap = input->RtpDescri.mapDescri[0];

    }



    if (outMap.MediaFormat == 0 || outMap.MediaFormat > 127) {

        outMap.MediaFormat = (unsigned int)defaultPayload;

    }



    if (outMap.SampleRate == 0) {

        outMap.SampleRate = 90000;

    }



    if (outMap.MimeType[0] == '\0') {

        CopyBounded(outMap.MimeType, sizeof(outMap.MimeType), "PS");

    }



    return 0;

}



int ProtocolManager::RespondGbMediaPlayInfo(StreamHandle handle,

                                            const char* gbCode,

                                            StreamRequestType requestType,

                                            const MediaInfo* input)

{

    if (m_gb_media_responder == NULL) {

        printf("[ProtocolManager] gb media responder not bound, skip response gb=%s type=%s\n",

               gbCode != NULL ? gbCode : "",

               StreamRequestTypeName(requestType));

        return 0;

    }



    RtpMap map;

    MediaInfo answer;

    memset(&map, 0, sizeof(map));

    memset(&answer, 0, sizeof(answer));



    int ret = BuildGbResponseMediaInfo(gbCode, requestType, input, answer, map);

    if (ret != 0) {

        printf("[ProtocolManager] build gb media info failed ret=%d gb=%s type=%s\n",

               ret,

               gbCode != NULL ? gbCode : "",

               StreamRequestTypeName(requestType));

        return -31;

    }



    ret = m_gb_media_responder(handle, &answer, m_gb_media_responder_user);

    if (ret != 0) {

        printf("[ProtocolManager] gb media response callback failed ret=%d gb=%s type=%s\n",

               ret,

               gbCode != NULL ? gbCode : "",

               StreamRequestTypeName(requestType));

        return -32;

    }



    return 0;

}



int ProtocolManager::NotifyGbMediaStatus(StreamHandle handle,

                                         const std::string& gbCode,

                                         int notifyType,

                                         const char* reason)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const std::string code = gbCode.empty() ? m_cfg.gb_register.device_id : gbCode;

    if (code.empty()) {

        return -1;

    }



    const int ret = m_gb_client_sdk->NotifyMediaStatus(handle, code.c_str(), notifyType);

    if (!IsGbSdkSuccess(ret)) {

        printf("[ProtocolManager] gb notify media status failed ret=%d notify=%d gb=%s handle=%p reason=%s\n",

               ret,

               notifyType,

               code.c_str(),

               handle,

               reason != NULL ? reason : "");

        return ret;

    }



    printf("[ProtocolManager] gb notify media status success notify=%d gb=%s handle=%p reason=%s\n",

           notifyType,

           code.c_str(),

           handle,

           reason != NULL ? reason : "");

#else

    (void)handle;

    (void)gbCode;

    (void)notifyType;

    (void)reason;

#endif

    return 0;

}



int ProtocolManager::NotifyGbUpgradeResult(const char* gbCode,
                                           const char* sessionId,
                                           const char* firmware,
                                           bool result,
                                           const char* description)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    if (m_gb_client_sdk == NULL) {

        return -1;

    }

    const std::string code = TrimWhitespaceCopy((gbCode != NULL) ? gbCode : "");
    if (code.empty()) {

        return -2;

    }

    const int ret = m_gb_client_sdk->NotifyDeviceUpgradeResult(code.c_str(), sessionId, firmware, result, description);
    if (!IsGbSdkSuccess(ret)) {

        printf("[ProtocolManager] gb notify upgrade result failed ret=%d result=%d gb=%s session=%s firmware=%s description=%s\n",
               ret,
               result ? 1 : 0,
               code.c_str(),
               (sessionId != NULL) ? sessionId : "",
               (firmware != NULL) ? firmware : "",
               (description != NULL) ? description : "");
        return ret;

    }

    printf("[ProtocolManager] gb notify upgrade result success result=%d gb=%s session=%s firmware=%s description=%s\n",
           result ? 1 : 0,
           code.c_str(),
           (sessionId != NULL) ? sessionId : "",
           (firmware != NULL) ? firmware : "",
           (description != NULL) ? description : "");

#else

    (void)gbCode;
    (void)sessionId;
    (void)firmware;
    (void)result;
    (void)description;

#endif

    return 0;

}



int ProtocolManager::PersistGbUpgradePendingState(const char* gbCode,
                                             const char* sessionId,
                                             const char* firmware,
                                             const char* description,
                                             bool pending)

{

    CConfigTable table;
    g_configManager.getConfig(getConfigName(CFG_UPDATE), table);

    table["Status"] = pending ? 0 : table.get("Status", 0).asInt();
    table[kGbUpgradeConfigPendingKey] = pending ? 1 : 0;
    table[kGbUpgradeConfigDeviceKey] = (gbCode != NULL) ? gbCode : "";
    table[kGbUpgradeConfigSessionKey] = (sessionId != NULL) ? sessionId : "";
    table[kGbUpgradeConfigFirmwareKey] = (firmware != NULL) ? firmware : "";
    table[kGbUpgradeConfigDescriptionKey] = (description != NULL) ? description : "";

    const int ret = g_configManager.setConfig(getConfigName(CFG_UPDATE), table, 0, IConfigManager::applyOK);
    if (ret != IConfigManager::applyOK) {
        printf("[ProtocolManager] gb upgrade persist state failed ret=%d pending=%d gb=%s session=%s firmware=%s description=%s\n",
               ret,
               pending ? 1 : 0,
               (gbCode != NULL) ? gbCode : "",
               (sessionId != NULL) ? sessionId : "",
               (firmware != NULL) ? firmware : "",
               (description != NULL) ? description : "");
        return ret;
    }

    printf("[ProtocolManager] gb upgrade persist state success pending=%d gb=%s session=%s firmware=%s description=%s\n",
           pending ? 1 : 0,
           (gbCode != NULL) ? gbCode : "",
           (sessionId != NULL) ? sessionId : "",
           (firmware != NULL) ? firmware : "",
           (description != NULL) ? description : "");
    return 0;

}



int ProtocolManager::ReportPendingGbUpgradeResult()

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_UPDATE), table)) {
        return -3;
    }

    if (table.get(kGbUpgradeConfigPendingKey, 0).asInt() == 0) {
        return 0;
    }

    const std::string gbCode = TrimWhitespaceCopy(table.get(kGbUpgradeConfigDeviceKey, "").asString());
    const std::string sessionId = TrimWhitespaceCopy(table.get(kGbUpgradeConfigSessionKey, "").asString());
    const std::string firmware = TrimWhitespaceCopy(table.get(kGbUpgradeConfigFirmwareKey, "").asString());

    std::string description = TrimWhitespaceCopy(table.get(kGbUpgradeConfigDescriptionKey, "").asString());
    if (description.empty()) {
        description = "upgrade_completed_after_reboot";
    }

    const int ret = NotifyGbUpgradeResult(gbCode.c_str(),
                                          sessionId.c_str(),
                                          firmware.c_str(),
                                          true,
                                          description.c_str());
    if (ret != 0) {
        return ret;
    }

    table["Status"] = 1;
    table[kGbUpgradeConfigPendingKey] = 0;
    table[kGbUpgradeConfigDescriptionKey] = description;
    const int saveRet = g_configManager.setConfig(getConfigName(CFG_UPDATE), table, 0, IConfigManager::applyOK);
    if (saveRet != IConfigManager::applyOK) {
        printf("[ProtocolManager] gb upgrade clear pending state failed ret=%d gb=%s session=%s firmware=%s\n",
               saveRet,
               gbCode.c_str(),
               sessionId.c_str(),
               firmware.c_str());
        return saveRet;
    }

    printf("[ProtocolManager] gb upgrade final result reported after reboot gb=%s session=%s firmware=%s description=%s\n",
           gbCode.c_str(),
           sessionId.c_str(),
           firmware.c_str(),
           description.c_str());

#else

    return 0;

#endif

    return 0;

}



int ProtocolManager::StartGbReplaySession(StreamHandle handle, const char* gbCode, const MediaInfo* input, bool download)

{

    if (input == NULL) {

        return -40;

    }



    if (input->StartTime == 0 || input->EndTime == 0 || input->StartTime >= input->EndTime) {

        printf("[ProtocolManager] gb %s invalid time range start=%llu end=%llu gb=%s\n",

               download ? "download" : "playback",

               (unsigned long long)input->StartTime,

               (unsigned long long)input->EndTime,

               gbCode != NULL ? gbCode : "");

        return -41;

    }



    int ret = ReconfigureGbLiveSender(input, gbCode, download ? kDownload : kPlayback);

    if (ret != 0) {

        return ret;

    }



    HandleGbStopStreamRequest(NULL, gbCode);



    const int startSec = (int)input->StartTime;

    const int endSec = (int)input->EndTime;



    const int storageHandle = download

                                  ? Storage_Module_StartDownload(startSec, endSec, &ProtocolManager::OnGbReplayStorageFrame, this)

                                  : Storage_Module_StartPlayback(startSec, endSec, &ProtocolManager::OnGbReplayStorageFrame, this);

    if (storageHandle < 0) {

        printf("[ProtocolManager] gb %s start storage failed ret=%d gb=%s range=[%d,%d]\n",

               download ? "download" : "playback",

               storageHandle,

               gbCode != NULL ? gbCode : "",

               startSec,

               endSec);

        return -42;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        m_gb_replay_session.active = true;

        m_gb_replay_session.download = download;

        m_gb_replay_session.stream_handle = handle;

        m_gb_replay_session.storage_handle = storageHandle;

        m_gb_replay_session.gb_code = (gbCode != NULL) ? gbCode : "";

        m_gb_replay_session.start_time = input->StartTime;

        m_gb_replay_session.end_time = input->EndTime;

        m_gb_replay_session.last_pts90k = 0;

        m_gb_replay_session.acked = false;

        m_gb_replay_session.sent_video_frames = 0;

        m_gb_replay_session.sent_audio_frames = 0;

    }



    ret = download ? Storage_Module_PauseDownload(storageHandle, true)
                   : Storage_Module_PausePlaybackOnFile(storageHandle, true);

    if (ret != 0) {

        printf("[ProtocolManager] gb %s initial pause failed ret=%d gb=%s storage=%d\n",

               download ? "download" : "playback",

               ret,

               gbCode != NULL ? gbCode : "",

               storageHandle);

        if (download) {

            Storage_Module_StopDownload(storageHandle);

        } else {

            Storage_Module_StopPlayback(storageHandle);

        }

        ClearGbReplaySessionState();

        return -43;

    }



    ret = RespondGbMediaPlayInfo(handle, gbCode, download ? kDownload : kPlayback, input);

    if (ret != 0) {

        if (download) {

            Storage_Module_StopDownload(storageHandle);

        } else {

            Storage_Module_StopPlayback(storageHandle);

        }



        ClearGbReplaySessionState();

        return ret;

    }



    printf("[ProtocolManager] gb %s session started gb=%s handle=%p storage=%d range=[%d,%d]\n",

           download ? "download" : "playback",

           gbCode != NULL ? gbCode : "",

           handle,

           storageHandle,

           startSec,

           endSec);

    return 0;

}



void ProtocolManager::OnGbReplayStorageFrame(unsigned char* data,

                                             int size,

                                             Mp4DemuxerFrameInfo_s* frameInfo,

                                             void* userData)

{

    ProtocolManager* self = (ProtocolManager*)userData;

    if (self == NULL) {

        return;

    }



    self->HandleGbReplayStorageFrame(data, size, frameInfo);

}



void ProtocolManager::HandleGbReplayStorageFrame(unsigned char* data, int size, Mp4DemuxerFrameInfo_s* frameInfo)

{

    if (data == NULL && size == 0 && frameInfo == NULL) {

        GbReplaySession ended;

        bool hasSession = false;



        {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            if (m_gb_replay_session.active) {

                ended = m_gb_replay_session;

                m_gb_replay_session = GbReplaySession();

                hasSession = true;

            }

        }



        if (hasSession) {

            NotifyGbMediaStatus(ended.stream_handle,

                                ended.gb_code,

                                121,

                                "eos");



            printf("[ProtocolManager] gb %s eos gb=%s handle=%p storage=%d video=%u audio=%u\n",

                   ended.download ? "download" : "playback",

                   ended.gb_code.c_str(),

                   ended.stream_handle,

                   ended.storage_handle,

                   ended.sent_video_frames,

                   ended.sent_audio_frames);

        }



        return;

    }



    if (data == NULL || size <= 0 || frameInfo == NULL) {

        return;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        if (!m_gb_replay_session.active || !m_gb_replay_session.acked) {

            return;

        }

    }



    const uint64_t pts90k = TimestampMsToPts90k(frameInfo->ullTimestamp);

    int ret = 0;



    if (frameInfo->iStreamType == 1) {

        const bool keyFrame = (frameInfo->iFrameType == 1);

        ret = m_rtp_ps_sender.SendVideoFrame((const uint8_t*)data, (size_t)size, pts90k, keyFrame);



        if (ret == 0) {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            if (m_gb_replay_session.active) {

                m_gb_replay_session.last_pts90k = pts90k;

                ++m_gb_replay_session.sent_video_frames;

            }

        }

    } else if (frameInfo->iStreamType == 2) {

        const std::string audioCodec = NormalizeCodec(m_cfg.gb_live.audio_codec);

        if (audioCodec != "pcm") {

            bool firstSkipLog = false;

            {

                std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

                if (m_gb_replay_session.active) {

                    firstSkipLog = (m_gb_replay_session.sent_audio_frames == 0);

                    ++m_gb_replay_session.sent_audio_frames;

                }

            }



            if (firstSkipLog) {

                printf("[ProtocolManager] gb replay audio skip, only pcm passthrough supported current=%s\n",

                       m_cfg.gb_live.audio_codec.c_str());

            }

            return;

        }



        ret = m_rtp_ps_sender.SendAudioFrame((const uint8_t*)data, (size_t)size, pts90k);

        if (ret == 0) {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            if (m_gb_replay_session.active) {

                m_gb_replay_session.last_pts90k = pts90k;

                ++m_gb_replay_session.sent_audio_frames;

            }

        }

    } else {

        return;

    }



    if (ret != 0) {

        printf("[ProtocolManager] gb replay send failed ret=%d streamType=%d size=%d pts90k=%llu\n",

               ret,

               frameInfo->iStreamType,

               size,

               (unsigned long long)pts90k);

    }

}



int ProtocolManager::OnGbLiveCapture(int media_chn,

                                     int media_type,

                                     int media_subtype,

                                     unsigned long long frame_pts,

                                     unsigned char* frame_data,

                                     int frame_len,

                                     int frame_end_flag)

{

    ProtocolManager* self = g_gb_live_audio_manager;

    if (self == NULL) {

        return 0;

    }



    return self->HandleGbLiveCaptureInternal(media_chn,

                                             media_type,

                                             media_subtype,

                                             frame_pts,

                                             frame_data,

                                             frame_len,

                                             frame_end_flag);

}



int ProtocolManager::StartGbLiveCapture()

{

    if (m_gb_live_capture_started) {

        return 0;

    }



    if (g_gb_live_audio_manager != NULL && g_gb_live_audio_manager != this) {

        return -1;

    }



    int mediaType = DMC_MEDIA_TYPE_H264 | DMC_MEDIA_TYPE_H265;

    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.audio_enabled) {

            mediaType |= DMC_MEDIA_TYPE_AUDIO;

        }

    }



    const int ret = CaptureSetStreamCallBack((char*)kGbLiveDmcModuleName,

                                             mediaType,

                                             &ProtocolManager::OnGbLiveCapture);

    if (ret != 0) {

        return -2;

    }



    g_gb_live_audio_manager = this;

    m_gb_live_capture_started = true;

    return 0;

}



void ProtocolManager::StopGbLiveCapture()

{

    if (!m_gb_live_capture_started) {

        return;

    }



    CaptureSetStreamCallBack((char*)kGbLiveDmcModuleName,

                             DMC_MEDIA_TYPE_H264 | DMC_MEDIA_TYPE_H265 | DMC_MEDIA_TYPE_AUDIO,

                             NULL);

    if (g_gb_live_audio_manager == this) {

        g_gb_live_audio_manager = NULL;

    }

    m_gb_live_capture_started = false;

}



int ProtocolManager::HandleGbLiveCaptureInternal(int media_chn,

                                                 int media_type,

                                                 int media_subtype,

                                                 unsigned long long frame_pts,

                                                 unsigned char* frame_data,

                                                 int frame_len,

                                                 int frame_end_flag)

{

    (void)frame_end_flag;



    if (!m_started || frame_data == NULL || frame_len <= 0) {

        return 0;

    }



    GbLiveSession session;

    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        session = m_gb_live_session;

    }



    if (!session.active) {

        return 0;

    }



    const uint64_t timestampMs = (frame_pts > 0) ? static_cast<uint64_t>(frame_pts) : GetNowMs();

    const uint64_t pts90k = TimestampMsToPts90k(timestampMs);



    if (media_chn == DMC_MEDIA_VIDEO_MAIN_STREAM || media_chn == DMC_MEDIA_VIDEO_SUB_STREAM) {

        const bool needSubStream = session.prefer_sub_stream;

        const bool isSubStream = (media_chn == DMC_MEDIA_VIDEO_SUB_STREAM);

        if (needSubStream != isSubStream) {

            return 0;

        }



        if (media_type != DMC_MEDIA_TYPE_H264 && media_type != DMC_MEDIA_TYPE_H265) {

            return 0;

        }



        const bool keyFrame = (media_subtype == DMC_MEDIA_SUBTYPE_IFRAME);

        return PushLiveVideoEsFrame(frame_data, static_cast<size_t>(frame_len), pts90k, keyFrame);

    }



    if (!session.audio_enabled || media_type != DMC_MEDIA_TYPE_AUDIO || media_chn != DMC_MEDIA_AUDIO_FRIST_STREAM) {

        return 0;

    }



    const std::string codec = NormalizeCodec(m_cfg.gb_live.audio_codec);

    if (codec == "g711u") {

        if (media_subtype == DMC_MEDIA_SUBTYPE_ULAW) {

            return PushLiveAudioEsFrame(frame_data, static_cast<size_t>(frame_len), pts90k);

        }



        if (media_subtype == DMC_MEDIA_SUBTYPE_PCM) {

            std::vector<uint8_t> encoded(static_cast<size_t>(frame_len) + 16);

            const int outLen = DG_encode_g711u((char*)frame_data, (char*)&encoded[0], frame_len);

            if (outLen > 0) {

                return PushLiveAudioEsFrame(&encoded[0], static_cast<size_t>(outLen), pts90k);

            }

        }

        return 0;

    }



    if (codec == "g711a") {

        if (media_subtype == DMC_MEDIA_SUBTYPE_ALAW) {

            return PushLiveAudioEsFrame(frame_data, static_cast<size_t>(frame_len), pts90k);

        }



        if (media_subtype == DMC_MEDIA_SUBTYPE_PCM) {

            std::vector<uint8_t> encoded(static_cast<size_t>(frame_len) + 16);

            const int outLen = DG_encode_g711a((char*)frame_data, (char*)&encoded[0], frame_len);

            if (outLen > 0) {

                return PushLiveAudioEsFrame(&encoded[0], static_cast<size_t>(outLen), pts90k);

            }

            return 0;

        }



        if (media_subtype == DMC_MEDIA_SUBTYPE_ULAW) {

            std::vector<uint8_t> pcm(static_cast<size_t>(frame_len) * 2 + 16);

            const int pcmLen = DG_decode_g711u((char*)frame_data, (char*)&pcm[0], frame_len);

            if (pcmLen <= 0) {

                return 0;

            }



            std::vector<uint8_t> encoded(static_cast<size_t>(pcmLen) + 16);

            const int outLen = DG_encode_g711a((char*)&pcm[0], (char*)&encoded[0], pcmLen);

            if (outLen > 0) {

                return PushLiveAudioEsFrame(&encoded[0], static_cast<size_t>(outLen), pts90k);

            }

        }

        return 0;

    }



    if (codec == "pcm") {

        return 0;

    }



    return 0;

}



void ProtocolManager::ClearGbReplaySessionState()

{

    std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

    m_gb_replay_session = GbReplaySession();

}



void ProtocolManager::SetGbMediaPlayInfoResponder(GbMediaPlayInfoResponder responder, void* userData)

{

    m_gb_media_responder = responder;

    m_gb_media_responder_user = userData;

}



void ProtocolManager::SetGbBroadcastPcmCallback(GB28181BroadcastBridge::PcmFrameCallback cb, void* userData)

{

    m_broadcast.SetPcmFrameCallback(cb, userData);

}



void ProtocolManager::BindGbClientSdk(GB28181ClientSDK* sdk)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    GB28181ClientSDK* oldSdk = NULL;

    {

        std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

        oldSdk = m_gb_client_sdk;

    }



    if (oldSdk != NULL && oldSdk != sdk) {

        StopGbClientLifecycle();

        oldSdk->SetReceiver(NULL);

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

        m_gb_client_sdk = sdk;

    }



    if (sdk == NULL) {

        SetGbMediaPlayInfoResponder(NULL, NULL);

        printf("[ProtocolManager] bind gb sdk null, receiver unbound\n");

        return;

    }



    sdk->SetReceiver(GetGbClientReceiver());

    SetGbMediaPlayInfoResponder(&ProtocolManager::OnGbMediaPlayInfoRespond, this);

    printf("[ProtocolManager] bind gb sdk success, receiver attached\n");



    if (m_started) {

        const int ret = StartGbClientLifecycle();

        if (ret != 0) {

            printf("[ProtocolManager] bind gb sdk lifecycle start failed: %d\n", ret);

        }

    }

#else

    (void)sdk;

    m_gb_client_sdk = NULL;

    SetGbMediaPlayInfoResponder(NULL, NULL);

    printf("[ProtocolManager] bind gb sdk skipped, PROTOCOL_HAS_GB28181_CLIENT_SDK=0\n");

#endif

}



void ProtocolManager::UnbindGbClientSdk()

{

    StopGbClientLifecycle();



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    GB28181ClientSDK* oldSdk = NULL;

    {

        std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

        oldSdk = m_gb_client_sdk;

        m_gb_client_sdk = NULL;

    }

    if (oldSdk != NULL) {

        oldSdk->SetReceiver(NULL);

    }

#else

    m_gb_client_sdk = NULL;

#endif



    SetGbMediaPlayInfoResponder(NULL, NULL);



    std::lock_guard<std::mutex> subscribeLock(m_gb_subscribe_mutex);

    m_gb_catalog_subscribe_handle = NULL;

    m_gb_alarm_subscribe_handle = NULL;

    m_gb_mobile_position_subscribe_handle = NULL;

}



GB28181ClientReceiverAdapter* ProtocolManager::GetGbClientReceiver()

{

    return &m_gb_receiver;

}



const GB28181ClientReceiverAdapter* ProtocolManager::GetGbClientReceiver() const

{

    return &m_gb_receiver;

}

GAT1400ClientService* ProtocolManager::GetGatClientService()

{

    return m_gat_client.get();

}

const GAT1400ClientService* ProtocolManager::GetGatClientService() const

{

    return m_gat_client.get();

}



int ProtocolManager::OnGbMediaPlayInfoRespond(StreamHandle handle, const MediaInfo* info, void* userData)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    ProtocolManager* self = (ProtocolManager*)userData;

    if (self == NULL || info == NULL) {

        return -1;

    }



    std::lock_guard<std::mutex> lock(self->m_gb_lifecycle_mutex);

    if (self->m_gb_client_sdk == NULL) {

        return -1;

    }



    const int ret = self->m_gb_client_sdk->ResponseMediaPlayInfo(handle, info);

    return IsGbSdkSuccess(ret) ? 0 : ret;

#else

    (void)handle;

    (void)info;

    (void)userData;

    return -1;

#endif

}





}









































































