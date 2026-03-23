#include "ProtocolManager.h"
#include "gat1400/GAT1400ClientService.h"
#include "ProtocolLog.h"



#include <stdio.h>

#include <string.h>

#include <algorithm>
#include <chrono>

#include <time.h>

#include <vector>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>



#ifndef PROTOCOL_HAS_GB28181_CLIENT_SDK

#define PROTOCOL_HAS_GB28181_CLIENT_SDK 0

#endif



#if PROTOCOL_HAS_GB28181_CLIENT_SDK

#include "GB28181ClientSDK.h"

#endif



#include "Storage_api.h"
#include "Manager/ConfigManager.h"
#include "Manager/EventManager.h"
#include "ExchangeAL/CameraExchange.h"
#include "ExchangeAL/ExchangeKind.h"
#include "Update/update.h"

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

#define printf protocol::ProtocolPrintf



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
static const char* kGbUpgradeConfigPendingKey = "GbPending";
static const char* kGbUpgradeConfigSessionKey = "GbSessionID";
static const char* kGbUpgradeConfigFirmwareKey = "GbFirmware";
static const char* kGbUpgradeConfigDeviceKey = "GbDeviceID";
static const char* kGbUpgradeConfigDescriptionKey = "GbDescription";
static const char* kGbUpgradeReleaseEvent = "UpgradeReleaseResource";

struct GbUpgradeThreadContext
{
    protocol::ProtocolManager* manager;

    GbUpgradeThreadContext() : manager(NULL) {}
};



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

static unsigned int GenerateGbMediaSsrc(int remotePort)
{
    unsigned int seed = static_cast<unsigned int>(time(NULL));
    seed ^= static_cast<unsigned int>(getpid() & 0xFFFF);
    seed ^= static_cast<unsigned int>(remotePort & 0xFFFF);
    seed ^= static_cast<unsigned int>(rand() & 0x7FFFFFFF);
    seed &= 0x3FFFFFFF;
    return (seed == 0U) ? 1U : seed;
}

enum GbVideoStartState
{
    kGbVideoStartNone = 0,
    kGbVideoStartConfig = 1,
    kGbVideoStartIdr = 2
};

static bool FindAnnexBStartCode(const uint8_t* data,
                                size_t size,
                                size_t offset,
                                size_t* startOffset,
                                size_t* prefixSize)
{
    if (data == NULL || startOffset == NULL || prefixSize == NULL || offset >= size) {
        return false;
    }

    for (size_t i = offset; i + 3 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                *startOffset = i;
                *prefixSize = 3;
                return true;
            }

            if (i + 4 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                *startOffset = i;
                *prefixSize = 4;
                return true;
            }
        }
    }

    return false;
}

static GbVideoStartState DetectH264GbVideoStartState(const uint8_t* data, size_t size)
{
    bool sawConfig = false;
    bool sawStartCode = false;
    size_t offset = 0;
    size_t startOffset = 0;
    size_t prefixSize = 0;

    while (FindAnnexBStartCode(data, size, offset, &startOffset, &prefixSize)) {
        sawStartCode = true;
        const size_t nalOffset = startOffset + prefixSize;
        if (nalOffset >= size) {
            break;
        }

        const uint8_t nalType = data[nalOffset] & 0x1F;
        if (nalType == 5) {
            return kGbVideoStartIdr;
        }

        if (nalType == 7 || nalType == 8) {
            sawConfig = true;
        }

        offset = nalOffset + 1;
    }

    if (!sawStartCode && size > 0) {
        const uint8_t nalType = data[0] & 0x1F;
        if (nalType == 5) {
            return kGbVideoStartIdr;
        }

        if (nalType == 7 || nalType == 8) {
            return kGbVideoStartConfig;
        }
    }

    return sawConfig ? kGbVideoStartConfig : kGbVideoStartNone;
}

static GbVideoStartState DetectH265GbVideoStartState(const uint8_t* data, size_t size)
{
    bool sawConfig = false;
    bool sawStartCode = false;
    size_t offset = 0;
    size_t startOffset = 0;
    size_t prefixSize = 0;

    while (FindAnnexBStartCode(data, size, offset, &startOffset, &prefixSize)) {
        sawStartCode = true;
        const size_t nalOffset = startOffset + prefixSize;
        if (nalOffset >= size) {
            break;
        }

        const uint8_t nalType = static_cast<uint8_t>((data[nalOffset] >> 1) & 0x3F);
        if (nalType == 19 || nalType == 20) {
            return kGbVideoStartIdr;
        }

        if (nalType == 32 || nalType == 33 || nalType == 34) {
            sawConfig = true;
        }

        offset = nalOffset + 1;
    }

    if (!sawStartCode && size > 1) {
        const uint8_t nalType = static_cast<uint8_t>((data[0] >> 1) & 0x3F);
        if (nalType == 19 || nalType == 20) {
            return kGbVideoStartIdr;
        }

        if (nalType == 32 || nalType == 33 || nalType == 34) {
            return kGbVideoStartConfig;
        }
    }

    return sawConfig ? kGbVideoStartConfig : kGbVideoStartNone;
}

static GbVideoStartState DetectGbVideoStartState(int mediaType, const uint8_t* data, size_t size)
{
    if (data == NULL || size == 0) {
        return kGbVideoStartNone;
    }

    if (mediaType == DMC_MEDIA_TYPE_H264) {
        return DetectH264GbVideoStartState(data, size);
    }

    if (mediaType == DMC_MEDIA_TYPE_H265) {
        return DetectH265GbVideoStartState(data, size);
    }

    return kGbVideoStartNone;
}

static const char* GbVideoStartStateName(GbVideoStartState state)
{
    switch (state) {
        case kGbVideoStartConfig:
            return "config";
        case kGbVideoStartIdr:
            return "idr";
        default:
            return "none";
    }
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
    if (path != NULL && path[0] != '\0') {
        remove(path);
    }
}

static bool ReadFileSizeBytes(const char* path, unsigned long long& outSize)
{
    outSize = 0;
    if (path == NULL || path[0] == '\0') {
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
    if (url.empty() || outputPath == NULL || outputPath[0] == '\0') {
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
    if (path == NULL || path[0] == '\0') {
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
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }

    fclose(fp);
    return true;
}

static bool ResolveAudioCodecFromPayload(const RtpMap& map,

                                         int& payloadType,

                                         std::string& codec,

                                         unsigned int& sampleRate)

{

    payloadType = (int)map.MediaFormat;

    codec = NormalizeCodec(SafeStr(map.MimeType, sizeof(map.MimeType)));

    sampleRate = map.SampleRate;



    if (codec.empty() && payloadType == 8) {

        codec = "g711a";

    }



    if (codec.empty() && payloadType == 0) {

        codec = "g711u";

    }



    if (payloadType < 0 || payloadType > 127 || codec.empty()) {

        return false;

    }



    if (sampleRate == 0) {

        sampleRate = 8000;

    }



    return true;

}



static bool ExtractAudioTransportHintWithPreference(const MediaInfo* input,

                                                    const std::string& preferredCodec,

                                                    std::string& remoteIp,

                                                    int& remotePort,

                                                    int& payloadType,

                                                    std::string& codec,

                                                    unsigned int& sampleRate)

{

    if (input == NULL) {

        return false;

    }



    remoteIp = SafeStr(input->IP, sizeof(input->IP));

    remotePort = (int)input->Port;

    payloadType = -1;

    codec.clear();

    sampleRate = 0;



    const std::string normalizedPreferred = NormalizeCodec(preferredCodec);



    if (input->RtpDescri.DescriNum > 0 && input->RtpDescri.mapDescri != NULL) {

        for (unsigned int i = 0; i < input->RtpDescri.DescriNum; ++i) {

            int candidatePayloadType = -1;

            std::string candidateCodec;

            unsigned int candidateSampleRate = 0;

            if (!ResolveAudioCodecFromPayload(input->RtpDescri.mapDescri[i],
                                              candidatePayloadType,
                                              candidateCodec,
                                              candidateSampleRate)) {

                continue;

            }



            if (payloadType < 0) {

                payloadType = candidatePayloadType;

                codec = candidateCodec;

                sampleRate = candidateSampleRate;

            }



            if (!normalizedPreferred.empty() && candidateCodec == normalizedPreferred) {

                payloadType = candidatePayloadType;

                codec = candidateCodec;

                sampleRate = candidateSampleRate;

                break;

            }

        }

    }



    if (remotePort <= 0 || payloadType < 0 || payloadType > 127 || codec.empty()) {

        return false;

    }



    if (sampleRate == 0) {

        sampleRate = 8000;

    }



    return true;

}



static bool ExtractAudioTransportHint(const MediaInfo* input,

                                      std::string& remoteIp,

                                      int& remotePort,

                                      int& payloadType,

                                      std::string& codec)

{

    unsigned int sampleRate = 0;

    return ExtractAudioTransportHintWithPreference(input,
                                                   "",
                                                   remoteIp,
                                                   remotePort,
                                                   payloadType,
                                                   codec,
                                                   sampleRate);

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

static bool ResolveGbResponseLocalIp(const std::string& remoteIp,

                                     int remotePort,

                                     std::string& localIp)

{

    if (remoteIp.empty() || remotePort <= 0 || remotePort > 65535) {

        return false;

    }



    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {

        return false;

    }



    sockaddr_in remoteAddr;

    memset(&remoteAddr, 0, sizeof(remoteAddr));

    remoteAddr.sin_family = AF_INET;

    remoteAddr.sin_port = htons((uint16_t)remotePort);

    if (inet_pton(AF_INET, remoteIp.c_str(), &remoteAddr.sin_addr) != 1) {

        close(sockfd);

        return false;

    }



    if (connect(sockfd, (sockaddr*)&remoteAddr, sizeof(remoteAddr)) != 0) {

        close(sockfd);

        return false;

    }



    sockaddr_in localAddr;

    memset(&localAddr, 0, sizeof(localAddr));

    socklen_t localAddrLen = sizeof(localAddr);

    if (getsockname(sockfd, (sockaddr*)&localAddr, &localAddrLen) != 0) {

        close(sockfd);

        return false;

    }



    char ipbuf[INET_ADDRSTRLEN] = {0};

    const char* ipstr = inet_ntop(AF_INET, &localAddr.sin_addr, ipbuf, sizeof(ipbuf));

    close(sockfd);

    if (ipstr == NULL || ipstr[0] == '\0' || strcmp(ipstr, "0.0.0.0") == 0) {

        return false;

    }



    localIp = ipstr;

    return true;

}



static const char* GbNetTransportName(NetTransType type)

{

    switch (type) {

    case kRtpOverUdp:

        return "udp";

    case kRtpOverTcp:

        return "tcp";

    case kRtpOverTcpActive:

        return "tcp-active";

    case kRtpOverTcpPassive:

        return "tcp-passive";

    default:

        return "unknown";

    }

}

static NetTransType ResolveGbAnswerTransportType(NetTransType requestType,

                                                 const std::string& defaultTransport)

{

    switch (requestType) {

    case kRtpOverTcpActive:

        return kRtpOverTcpPassive;

    case kRtpOverTcpPassive:

    case kRtpOverTcp:

        return kRtpOverTcpActive;

    case kRtpOverUdp:

        return kRtpOverUdp;

    default:

        break;

    }



    return (ToLowerCopy(defaultTransport) == "tcp") ? kRtpOverTcpActive : kRtpOverUdp;

}

static bool IsGbTalkListenSupportedTransport(NetTransType transportType)
{
    return transportType == kRtpOverUdp ||
           transportType == kRtpOverTcp ||
           transportType == kRtpOverTcpActive;
}

static protocol::GbBroadcastParam BuildGbTalkBridgeParam(const protocol::ProtocolExternalConfig& cfg)
{
    protocol::GbBroadcastParam param;
    param.input_mode = "stream";
    param.codec = NormalizeCodec(cfg.gb_talk.codec);
    if (param.codec.empty()) {
        param.codec = "g711a";
    }
    param.recv_port = cfg.gb_talk.recv_port;
    param.file_cache_dir = cfg.gb_broadcast.file_cache_dir.empty() ? "/tmp" : cfg.gb_broadcast.file_cache_dir;
    return param;
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

static bool IsLoopbackIp(const std::string& ip)
{
    const std::string normalized = ToLowerCopy(ip);
    return normalized == "127.0.0.1" || normalized == "localhost";
}

static bool ShouldAutoStartGbListen(const protocol::GbListenParam& param)
{
    if (param.target_ip.empty() || param.target_port <= 0) {
        return false;
    }

    return !IsLoopbackIp(param.target_ip);
}

static uint64_t GetSteadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static uint32_t ResolveGbRegisterRefreshLeadSec(uint32_t expiresSec)
{
    if (expiresSec <= 120U) {
        return expiresSec > 10U ? (expiresSec / 2U) : 5U;
    }

    return 60U;
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

static bool IsPsMuxMimeType(const std::string& mime)

{

    return ToLowerCopy(mime) == "ps";

}

static std::string NormalizeGbLiveVideoCodec(const std::string& codecIn)

{

    const std::string codec = ToLowerCopy(codecIn);

    if (codec == "h264" || codec == "h.264" || codec == "avc") {

        return "h264";

    }



    if (codec == "h265" || codec == "h.265" || codec == "hevc") {

        return "h265";

    }



    return codec;

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

static bool HasLivePsMuxOffer(const MediaInfo* input)

{

    if (input == NULL || input->RtpDescri.DescriNum == 0 || input->RtpDescri.mapDescri == NULL) {

        return false;

    }



    for (unsigned int i = 0; i < input->RtpDescri.DescriNum; ++i) {

        const RtpMap& map = input->RtpDescri.mapDescri[i];

        const std::string mime = SafeStr(map.MimeType, sizeof(map.MimeType));

        if (IsPsMuxMimeType(mime)) {

            return true;

        }

    }



    return false;

}

static bool ShouldForceGbLiveAudio(const protocol::ProtocolExternalConfig& cfg)

{

    const std::string codec = NormalizeCodec(cfg.gb_live.audio_codec);

    return codec == "g711a" ||

           codec == "g711u" ||

           codec == "aac" ||

           codec == "g722";

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
    CConfigTable table;
    CameraParamAll config;
    memset(&config, 0, sizeof(config));
    if (!g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table)) {
        return false;
    }

    TExchangeAL<CameraParamAll>::getConfigV2(table, config, 1);
    value = (config.vCameraParamAll[0].rotateAttr == RA_NONE) ? "close" : "centrosymmetric";
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

    const int desiredRotate = (normalizedMode == "close") ? RA_NONE : RA_180;
    CConfigTable table;
    CameraParamAll config;
    memset(&config, 0, sizeof(config));
    if (!g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table)) {
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=-1 target=image_flip value=%s current=unknown\n",
               normalizedMode.c_str());
        return;
    }

    TExchangeAL<CameraParamAll>::getConfigV2(table, config, 1);
    const int currentRotate = config.vCameraParamAll[0].rotateAttr;
    if (currentRotate != desiredRotate) {
        config.vCameraParamAll[0].rotateAttr = desiredRotate;
        TExchangeAL<CameraParamAll>::setConfigV2(config, table, 1);
        const int ret = g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table, 0, IConfigManager::applyOK);
        printf("[ProtocolManager] module=config event=media_apply trace=manager error=%d target=image_flip value=%s current=%d desired=%d\n",
               ret,
               normalizedMode.c_str(),
               currentRotate,
               desiredRotate);
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

static void LoadGbUpgradePendingIdentity(std::string& gbCode,
                                         std::string& sessionId,
                                         std::string& firmware)
{
    gbCode.clear();
    sessionId.clear();
    firmware.clear();

    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_UPDATE), table)) {
        return;
    }

    gbCode = TrimWhitespaceCopy(table.get(kGbUpgradeConfigDeviceKey, "").asString());
    sessionId = TrimWhitespaceCopy(table.get(kGbUpgradeConfigSessionKey, "").asString());
    firmware = TrimWhitespaceCopy(table.get(kGbUpgradeConfigFirmwareKey, "").asString());
}

static void ClearGbUpgradePendingState(protocol::ProtocolManager* manager, const char* description)
{
    std::string gbCode;
    std::string sessionId;
    std::string firmware;
    LoadGbUpgradePendingIdentity(gbCode, sessionId, firmware);

    CConfigTable table;
    g_configManager.getConfig(getConfigName(CFG_UPDATE), table);
    table["Status"] = table.get("Status", 0).asInt();
    table[kGbUpgradeConfigPendingKey] = 0;
    table[kGbUpgradeConfigDeviceKey] = gbCode;
    table[kGbUpgradeConfigSessionKey] = sessionId;
    table[kGbUpgradeConfigFirmwareKey] = firmware;
    table[kGbUpgradeConfigDescriptionKey] = (description != NULL) ? description : "";
    g_configManager.setConfig(getConfigName(CFG_UPDATE), table, 0, IConfigManager::applyOK);
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
    AppendConfigDiff(diff, "gb.register.enabled", before.gb_register.enabled, after.gb_register.enabled);

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

    AppendConfigDiff(diff, "gb.talk.codec", before.gb_talk.codec, after.gb_talk.codec);

    AppendConfigDiff(diff, "gb.talk.recv_port", before.gb_talk.recv_port, after.gb_talk.recv_port);

    AppendConfigDiff(diff, "gb.talk.sample_rate", before.gb_talk.sample_rate, after.gb_talk.sample_rate);

    AppendConfigDiff(diff, "gb.talk.jitter_buffer_ms", before.gb_talk.jitter_buffer_ms, after.gb_talk.jitter_buffer_ms);

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

    AppendConfigDiff(diff, "gat.register.scheme", before.gat_register.scheme, after.gat_register.scheme);

    AppendConfigDiff(diff, "gat.register.server_port", before.gat_register.server_port, after.gat_register.server_port);

    AppendConfigDiff(diff, "gat.register.base_path", before.gat_register.base_path, after.gat_register.base_path);

    AppendConfigDiff(diff, "gat.register.device_id", before.gat_register.device_id, after.gat_register.device_id);

    AppendConfigDiff(diff, "gat.register.username", before.gat_register.username, after.gat_register.username);

    AppendConfigDiff(diff, "gat.register.password", before.gat_register.password, after.gat_register.password);

    AppendConfigDiff(diff, "gat.register.auth_method", before.gat_register.auth_method, after.gat_register.auth_method);

    AppendConfigDiff(diff, "gat.register.listen_port", before.gat_register.listen_port, after.gat_register.listen_port);

    AppendConfigDiff(diff, "gat.register.expires_sec", before.gat_register.expires_sec, after.gat_register.expires_sec);

    AppendConfigDiff(diff, "gat.register.keepalive_interval_sec", before.gat_register.keepalive_interval_sec, after.gat_register.keepalive_interval_sec);

    AppendConfigDiff(diff, "gat.register.max_retry", before.gat_register.max_retry, after.gat_register.max_retry);

    AppendConfigDiff(diff, "gat.register.request_timeout_ms", before.gat_register.request_timeout_ms, after.gat_register.request_timeout_ms);

    AppendConfigDiff(diff, "gat.register.retry_backoff_policy", before.gat_register.retry_backoff_policy, after.gat_register.retry_backoff_policy);

    AppendConfigDiff(diff, "gat.upload.batch_size", before.gat_upload.batch_size, after.gat_upload.batch_size);

    AppendConfigDiff(diff, "gat.upload.flush_interval_ms", before.gat_upload.flush_interval_ms, after.gat_upload.flush_interval_ms);

    AppendConfigDiff(diff, "gat.upload.retry_policy", before.gat_upload.retry_policy, after.gat_upload.retry_policy);

    AppendConfigDiff(diff, "gat.upload.queue_dir", before.gat_upload.queue_dir, after.gat_upload.queue_dir);

    AppendConfigDiff(diff, "gat.upload.max_pending_count", before.gat_upload.max_pending_count, after.gat_upload.max_pending_count);

    AppendConfigDiff(diff, "gat.upload.replay_interval_sec", before.gat_upload.replay_interval_sec, after.gat_upload.replay_interval_sec);

    AppendConfigDiff(diff, "gat.upload.enable_apes_post_compat", before.gat_upload.enable_apes_post_compat, after.gat_upload.enable_apes_post_compat);

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

    AppendConfigDiff(diff, "gb.listen.sample_rate", before.gb_listen.sample_rate, after.gb_listen.sample_rate);

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

static void FormatEpochIsoTime(time_t epochSec, bool utc, char* dst, size_t dstSize)
{
    if (dst == NULL || dstSize == 0) {
        return;
    }

    dst[0] = '\0';
    if (epochSec <= 0) {
        return;
    }

    struct tm tmValue;
    memset(&tmValue, 0, sizeof(tmValue));
#if defined(_WIN32)
    if (utc) {
        gmtime_s(&tmValue, &epochSec);
    } else {
        localtime_s(&tmValue, &epochSec);
    }
#else
    if (utc) {
        gmtime_r(&epochSec, &tmValue);
    } else {
        localtime_r(&epochSec, &tmValue);
    }
#endif

    snprintf(dst,
             dstSize,
             "%04d-%02d-%02dT%02d:%02d:%02d",
             tmValue.tm_year + 1900,
             tmValue.tm_mon + 1,
             tmValue.tm_mday,
             tmValue.tm_hour,
             tmValue.tm_min,
             tmValue.tm_sec);
}

static bool ParseGbHttpDate(const std::string& rawDate, time_t* outEpochSec)
{
    if (outEpochSec == NULL || rawDate.empty()) {
        return false;
    }

    struct tm tmValue;
    memset(&tmValue, 0, sizeof(tmValue));
    char* end = strptime(rawDate.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tmValue);
    if (end != NULL && *end == '\0') {
        tmValue.tm_isdst = 0;
        const time_t epochSec = timegm(&tmValue);
        if (epochSec <= 0) {
            return false;
        }

        *outEpochSec = epochSec;
        return true;
    }

    memset(&tmValue, 0, sizeof(tmValue));
    end = strptime(rawDate.c_str(), "%Y-%m-%dT%H:%M:%S", &tmValue);
    if (end == NULL) {
        return false;
    }

    if (*end == '.') {
        ++end;
        while (*end >= '0' && *end <= '9') {
            ++end;
        }
    }

    bool useUtc = false;
    int tzOffsetSec = 0;
    if (*end == '\0') {
        tmValue.tm_isdst = -1;
        const time_t epochSec = mktime(&tmValue);
        if (epochSec <= 0) {
            return false;
        }

        *outEpochSec = epochSec;
        return true;
    }

    if (*end == 'Z') {
        useUtc = true;
        ++end;
    } else if (*end == '+' || *end == '-') {
        const int sign = (*end == '-') ? -1 : 1;
        ++end;

        if (end[0] < '0' || end[0] > '9' || end[1] < '0' || end[1] > '9') {
            return false;
        }

        const int tzHour = (end[0] - '0') * 10 + (end[1] - '0');
        end += 2;

        int tzMinute = 0;
        if (*end == ':') {
            ++end;
        }

        if (*end != '\0') {
            if (end[0] < '0' || end[0] > '9' || end[1] < '0' || end[1] > '9') {
                return false;
            }

            tzMinute = (end[0] - '0') * 10 + (end[1] - '0');
            end += 2;
        }

        tzOffsetSec = sign * (tzHour * 3600 + tzMinute * 60);
        useUtc = true;
    }

    if (*end != '\0') {
        return false;
    }

    tmValue.tm_isdst = 0;
    time_t epochSec = useUtc ? timegm(&tmValue) : mktime(&tmValue);
    if (epochSec <= 0) {
        return false;
    }

    if (useUtc && tzOffsetSec != 0) {
        epochSec -= tzOffsetSec;
    }

    *outEpochSec = epochSec;
    return true;
}

static bool ParseGbCompactDateTime(const std::string& text, time_t* outEpochSec)
{
    if (outEpochSec == NULL || text.size() != 14) {
        return false;
    }

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }

    struct tm tmValue;
    memset(&tmValue, 0, sizeof(tmValue));
    tmValue.tm_year = atoi(text.substr(0, 4).c_str()) - 1900;
    tmValue.tm_mon = atoi(text.substr(4, 2).c_str()) - 1;
    tmValue.tm_mday = atoi(text.substr(6, 2).c_str());
    tmValue.tm_hour = atoi(text.substr(8, 2).c_str());
    tmValue.tm_min = atoi(text.substr(10, 2).c_str());
    tmValue.tm_sec = atoi(text.substr(12, 2).c_str());
    tmValue.tm_isdst = -1;

    const time_t epochSec = mktime(&tmValue);
    if (epochSec == static_cast<time_t>(-1) || epochSec <= 0) {
        return false;
    }

    *outEpochSec = epochSec;
    return true;
}

static bool ParseGbQueryDateTime(const char* text, time_t* outEpochSec)
{
    if (outEpochSec == NULL || text == NULL || text[0] == '\0') {
        return false;
    }

    const std::string raw(text);
    return ParseGbHttpDate(raw, outEpochSec) || ParseGbCompactDateTime(raw, outEpochSec);
}

static void FormatGbRecordTime(time_t epochSec, char* dst, size_t dstSize)
{
    FormatEpochIsoTime(epochSec, false, dst, dstSize);
}

static bool IsGbAlarmRecordType(int recType)
{
    return recType == 1;
}

static bool MatchGbRecordQueryType(const std::string& queryType, int recType)
{
    if (queryType.empty() || queryType == "all") {
        return true;
    }

    if (queryType == "alarm") {
        return IsGbAlarmRecordType(recType);
    }

    if (queryType == "time") {
        return !IsGbAlarmRecordType(recType);
    }

    return false;
}

static const char* ResolveGbRecordTypeText(int recType)
{
    return IsGbAlarmRecordType(recType) ? "alarm" : "time";
}

static bool BuildGbRecordFilePath(int startEpochSec,
                                  int endEpochSec,
                                  int recType,
                                  char* buffer,
                                  size_t bufferSize)
{
    if (buffer == NULL || bufferSize == 0 || startEpochSec <= 0 || endEpochSec <= 0) {
        return false;
    }

    time_t startTime = static_cast<time_t>(startEpochSec);
    time_t endTime = static_cast<time_t>(endEpochSec);
    struct tm startTm;
    struct tm endTm;
    memset(&startTm, 0, sizeof(startTm));
    memset(&endTm, 0, sizeof(endTm));
    if (localtime_r(&startTime, &startTm) == NULL || localtime_r(&endTime, &endTm) == NULL) {
        return false;
    }

    const char* suffix = IsGbAlarmRecordType(recType) ? "_ALARM" : "";
    const int written = snprintf(buffer,
                                 bufferSize,
                                 __STORAGE_SD_MOUNT_PATH__ "/DCIM/%04d/%02d/%02d/"
                                 "%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d%s.mp4",
                                 startTm.tm_year + 1900,
                                 startTm.tm_mon + 1,
                                 startTm.tm_mday,
                                 startTm.tm_year + 1900,
                                 startTm.tm_mon + 1,
                                 startTm.tm_mday,
                                 startTm.tm_hour,
                                 startTm.tm_min,
                                 startTm.tm_sec,
                                 endTm.tm_year + 1900,
                                 endTm.tm_mon + 1,
                                 endTm.tm_mday,
                                 endTm.tm_hour,
                                 endTm.tm_min,
                                 endTm.tm_sec,
                                 suffix);
    if (written <= 0 || static_cast<size_t>(written) >= bufferSize) {
        if (bufferSize > 0) {
            buffer[0] = '\0';
        }
        return false;
    }

    return true;
}

static std::string GetGbRecordFileName(const std::string& filePath)
{
    if (filePath.empty()) {
        return "";
    }

    const size_t pos = filePath.find_last_of('/');
    if (pos == std::string::npos) {
        return filePath;
    }

    return filePath.substr(pos + 1);
}

static void AppendGbRecordEntriesFromIndexFile(const char* indexPath,
                                               time_t queryStart,
                                               time_t queryEnd,
                                               const std::string& queryType,
                                               std::vector<record_file_info_s>* entries)
{
    if (indexPath == NULL || entries == NULL) {
        return;
    }

    FILE* fp = fopen(indexPath, "rb");
    if (fp == NULL) {
        return;
    }

    record_file_info_s item;
    while (fread(&item, sizeof(item), 1, fp) == 1) {
        if (item.iStartTime <= 0 || item.iEndTime <= 0) {
            continue;
        }

        if (!MatchGbRecordQueryType(queryType, item.iRecType)) {
            continue;
        }

        if (static_cast<time_t>(item.iEndTime) < queryStart ||
            static_cast<time_t>(item.iStartTime) > queryEnd) {
            continue;
        }

        entries->push_back(item);
    }

    fclose(fp);
}

static void CollectGbRecordEntries(time_t queryStart,
                                   time_t queryEnd,
                                   const std::string& queryType,
                                   std::vector<record_file_info_s>* entries)
{
    if (entries == NULL || queryStart <= 0 || queryEnd < queryStart) {
        return;
    }

    entries->clear();

    struct tm tmCursor;
    memset(&tmCursor, 0, sizeof(tmCursor));
    time_t cursor = queryStart;
    if (localtime_r(&cursor, &tmCursor) == NULL) {
        return;
    }
    tmCursor.tm_hour = 0;
    tmCursor.tm_min = 0;
    tmCursor.tm_sec = 0;
    time_t dayStart = mktime(&tmCursor);
    if (dayStart == static_cast<time_t>(-1)) {
        return;
    }

    while (dayStart <= queryEnd) {
        if (localtime_r(&dayStart, &tmCursor) == NULL) {
            break;
        }

        char indexPath[128] = {0};
        snprintf(indexPath,
                 sizeof(indexPath),
                 __STORAGE_SD_MOUNT_PATH__ "/DCIM/%04d/%02d/%02d/index",
                 tmCursor.tm_year + 1900,
                 tmCursor.tm_mon + 1,
                 tmCursor.tm_mday);
        AppendGbRecordEntriesFromIndexFile(indexPath, queryStart, queryEnd, queryType, entries);

        if (queryType.empty() || queryType == "all" || queryType == "alarm") {
            char alarmIndexPath[128] = {0};
            snprintf(alarmIndexPath,
                     sizeof(alarmIndexPath),
                     __STORAGE_SD_MOUNT_PATH__ "/DCIM/%04d/%02d/%02d/alarm_index",
                     tmCursor.tm_year + 1900,
                     tmCursor.tm_mon + 1,
                     tmCursor.tm_mday);
            AppendGbRecordEntriesFromIndexFile(alarmIndexPath, queryStart, queryEnd, queryType, entries);
        }

        ++tmCursor.tm_mday;
        tmCursor.tm_hour = 0;
        tmCursor.tm_min = 0;
        tmCursor.tm_sec = 0;
        dayStart = mktime(&tmCursor);
        if (dayStart == static_cast<time_t>(-1)) {
            break;
        }
    }

    std::sort(entries->begin(),
              entries->end(),
              [](const record_file_info_s& lhs, const record_file_info_s& rhs) {
                  if (lhs.iStartTime != rhs.iStartTime) {
                      return lhs.iStartTime < rhs.iStartTime;
                  }
                  if (lhs.iEndTime != rhs.iEndTime) {
                      return lhs.iEndTime < rhs.iEndTime;
                  }
                  return lhs.iRecType < rhs.iRecType;
              });
}

static int ApplyGbRegisterTimeSyncLocally(time_t epochSec)
{
    if (epochSec <= 0) {
        return -1;
    }

    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec = epochSec;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) != 0) {
        return (errno != 0) ? -errno : -2;
    }

    const int saveRet = Storage_Module_SaveSystemTime(true);
    if (saveRet != 0) {
        printf("[ProtocolManager] gb register time sync persist failed ret=%d epoch=%lld\n",
               saveRet,
               (long long)epochSec);
    }

    return 0;
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



void* ProtocolManager::GbUpgradeApplyThread(void* arg)
{
    std::unique_ptr<GbUpgradeThreadContext> ctx(static_cast<GbUpgradeThreadContext*>(arg));
    ProtocolManager* manager = (ctx.get() != NULL) ? ctx->manager : NULL;
    if (manager == NULL) {
        return NULL;
    }

    usleep(500 * 1000);

    printf("[ProtocolManager] gb upgrade apply start package=%s\n",
           kGbUpgradePackagePath);

    IEventManager::instance()->notify(kGbUpgradeReleaseEvent, 0, appEventPulse, NULL, NULL, NULL);
    usleep(500 * 1000);

    const int updateRet = DG_update((char*)kGbUpgradePackagePath);
    printf("[ProtocolManager] gb upgrade apply finish package=%s ret=%d\n",
           kGbUpgradePackagePath,
           updateRet);
    sync();

    if (RunShellCommand("reboot -f") != 0) {
        printf("[ProtocolManager] gb upgrade reboot command failed package=%s\n",
               kGbUpgradePackagePath);
        ClearGbUpgradePendingState(manager, "reboot_command_failed");
        manager->m_gb_upgrade_running.store(false);
        return NULL;
    }

    manager->m_gb_upgrade_running.store(false);
    return NULL;
}


ProtocolManager::ProtocolManager()

    : m_gb_receiver(this),

      m_gat_client(new GAT1400ClientService()),

      m_gb_client_sdk(NULL),

      m_gb_media_responder(NULL),

      m_gb_media_responder_user(NULL),

      m_gb_time_sync_hook(NULL),

      m_gb_time_sync_user(NULL),

      m_gb_heartbeat_running(false),
      m_gb_upgrade_running(false),

      m_gb_client_started(false),

      m_gb_client_registered(false),
      m_gb_last_teleboot_ms(0),
      m_gb_register_success_ms(0),
      m_gb_register_expires_sec(0),

      m_gb_catalog_subscribe_handle(NULL),

      m_gb_alarm_subscribe_handle(NULL),

      m_gb_mobile_position_subscribe_handle(NULL),

      m_gb_current_media_ssrc(0),
      m_gb_current_media_port(0),

      m_gb_replay_generation(0),

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

    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        for (std::vector<GbReplayCallbackContext*>::iterator it = m_gb_replay_callback_contexts.begin();
             it != m_gb_replay_callback_contexts.end();
             ++it) {

            delete *it;

        }

        m_gb_replay_callback_contexts.clear();

    }

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

    ret = m_talk_broadcast.StartSession(BuildGbTalkBridgeParam(m_cfg));

    if (ret != 0) {

        printf("[ProtocolManager] start talk session failed: %d\n", ret);

        m_broadcast.StopSession();

        m_rtp_ps_sender.CloseSession();

        return ret;

    }



    if (ShouldAutoStartGbListen(m_cfg.gb_listen)) {
        ret = m_listen.StartSession(m_cfg.gb_listen);

        if (ret != 0) {

            printf("[ProtocolManager] start listen session failed: %d\n", ret);

            m_talk_broadcast.StopSession();

            m_broadcast.StopSession();

            m_rtp_ps_sender.CloseSession();

            return ret;

        }
    } else {
        printf("[ProtocolManager] skip listen session auto start target=%s:%d\n",
               m_cfg.gb_listen.target_ip.c_str(),
               m_cfg.gb_listen.target_port);
    }

    ApplyRkImageFlipConfig(m_cfg.gb_image.flip_mode);


    if (m_cfg.gb_register.enabled != 0) {
        ret = StartGbClientLifecycle();

        if (ret != 0) {

            printf("[ProtocolManager] start gb client lifecycle failed: %d\n", ret);

            m_listen.StopSession();

            m_talk_broadcast.StopSession();

            m_broadcast.StopSession();

            m_rtp_ps_sender.CloseSession();

            return ret;

        }
    } else {
        printf("[ProtocolManager] skip gb client lifecycle because config disabled\n");
    }

    if (m_gat_client.get() != NULL) {
        ret = m_gat_client->Start(m_cfg, m_cfg.gb_register);
        if (ret != 0) {
            printf("[ProtocolManager] start gat1400 client failed: %d\n", ret);
            StopGbClientLifecycle();
            m_talk_broadcast.StopSession();
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

    m_talk_broadcast.StopSession();

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

    const bool restartTalk = (m_cfg.gb_talk.codec != latest.gb_talk.codec) ||

                             (m_cfg.gb_talk.recv_port != latest.gb_talk.recv_port) ||

                             (m_cfg.gb_talk.sample_rate != latest.gb_talk.sample_rate) ||

                             (m_cfg.gb_talk.jitter_buffer_ms != latest.gb_talk.jitter_buffer_ms);

    const bool restartListen = (m_cfg.gb_listen.transport != latest.gb_listen.transport) ||

                               (m_cfg.gb_listen.target_ip != latest.gb_listen.target_ip) ||

                               (m_cfg.gb_listen.target_port != latest.gb_listen.target_port) ||

                               (m_cfg.gb_listen.codec != latest.gb_listen.codec) ||

                               (m_cfg.gb_listen.packet_ms != latest.gb_listen.packet_ms) ||

                               (m_cfg.gb_listen.sample_rate != latest.gb_listen.sample_rate);

    const bool reloadGbLifecycle = (m_cfg.gb_register.enabled != latest.gb_register.enabled) ||

                                   (m_cfg.gb_register.server_ip != latest.gb_register.server_ip) ||

                                   (m_cfg.gb_register.server_port != latest.gb_register.server_port) ||

                                   (m_cfg.gb_register.device_id != latest.gb_register.device_id) ||

                                   (m_cfg.gb_register.username != latest.gb_register.username) ||

                                   (m_cfg.gb_register.password != latest.gb_register.password);

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
    const bool applyImageFlipConfig = (m_cfg.gb_image.flip_mode != latest.gb_image.flip_mode);

    const bool reloadGat = (m_cfg.gat_register.scheme != latest.gat_register.scheme) ||

                           (m_cfg.gat_register.server_ip != latest.gat_register.server_ip) ||

                           (m_cfg.gat_register.server_port != latest.gat_register.server_port) ||

                           (m_cfg.gat_register.base_path != latest.gat_register.base_path) ||

                           (m_cfg.gat_register.device_id != latest.gat_register.device_id) ||

                           (m_cfg.gat_register.username != latest.gat_register.username) ||

                           (m_cfg.gat_register.password != latest.gat_register.password) ||

                           (m_cfg.gat_register.auth_method != latest.gat_register.auth_method) ||

                           (m_cfg.gat_register.listen_port != latest.gat_register.listen_port) ||

                           (m_cfg.gat_register.expires_sec != latest.gat_register.expires_sec) ||

                           (m_cfg.gat_register.keepalive_interval_sec != latest.gat_register.keepalive_interval_sec) ||

                           (m_cfg.gat_register.max_retry != latest.gat_register.max_retry) ||

                           (m_cfg.gat_register.request_timeout_ms != latest.gat_register.request_timeout_ms) ||

                           (m_cfg.gat_register.retry_backoff_policy != latest.gat_register.retry_backoff_policy) ||

                           (m_cfg.gat_upload.batch_size != latest.gat_upload.batch_size) ||

                           (m_cfg.gat_upload.flush_interval_ms != latest.gat_upload.flush_interval_ms) ||

                           (m_cfg.gat_upload.retry_policy != latest.gat_upload.retry_policy) ||

                           (m_cfg.gat_upload.queue_dir != latest.gat_upload.queue_dir) ||

                           (m_cfg.gat_upload.max_pending_count != latest.gat_upload.max_pending_count) ||

                           (m_cfg.gat_upload.replay_interval_sec != latest.gat_upload.replay_interval_sec) ||

                           (m_cfg.gat_upload.enable_apes_post_compat != latest.gat_upload.enable_apes_post_compat) ||

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

        if (restartTalk) {

            const int talkRet = RestartGbTalkBridge("config_reload");

            if (talkRet != 0) {

                printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_talk_restart version=%s\n",

                       talkRet,

                       m_cfg.version.c_str());

                return talkRet;

            }

            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_talk_restart version=%s\n",

                   m_cfg.version.c_str());

        }



        if (restartListen) {

            if (ShouldAutoStartGbListen(m_cfg.gb_listen)) {
                const int listenRet = m_listen.StartSession(m_cfg.gb_listen);

                if (listenRet != 0) {

                    printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_listen_restart version=%s\n",

                           listenRet,

                           m_cfg.version.c_str());

                    return listenRet;

                }

                printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_listen_restart version=%s\n",

                       m_cfg.version.c_str());
            } else {
                m_listen.StopSession();
                printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_listen_skip version=%s target=%s:%d\n",
                       m_cfg.version.c_str(),
                       m_cfg.gb_listen.target_ip.c_str(),
                       m_cfg.gb_listen.target_port);
            }

        }

        if (applyImageFlipConfig) {
            ApplyRkImageFlipConfig(m_cfg.gb_image.flip_mode);
        }

        if (reloadGbLifecycle) {
            StopGbClientLifecycle();
            if (m_cfg.gb_register.enabled != 0) {
                const int regRet = StartGbClientLifecycle();

                if (regRet != 0) {

                    printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_restart version=%s\n",

                           regRet,

                           m_cfg.version.c_str());

                    return regRet;

                }

                printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_restart version=%s\n",

                       m_cfg.version.c_str());
            } else {
                printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_disable version=%s\n",
                       m_cfg.version.c_str());
            }
        } else if (m_cfg.gb_register.enabled != 0) {
            const int regRet = RegisterGbClient(true);

            if (regRet != 0) {

                printf("[ProtocolManager] module=config event=config_apply_fail trace=manager error=%d stage=gb_reregister version=%s\n",

                       regRet,

                       m_cfg.version.c_str());

            } else {

                printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_reregister version=%s\n",

                       m_cfg.version.c_str());

            }
        } else {
            printf("[ProtocolManager] module=config event=config_apply_success trace=manager error=0 stage=gb_skip version=%s\n",
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

    if (m_cfg.gb_register.enabled == 0) {
        printf("[ProtocolManager] gb client lifecycle disabled by config\n");
        return 0;
    }

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
        m_gb_register_success_ms = 0;
        m_gb_register_expires_sec = 0;

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
    m_gb_register_success_ms = 0;
    m_gb_register_expires_sec = 0;

#else

    m_gb_heartbeat_running.store(false);

    m_gb_client_started = false;

    m_gb_client_registered = false;
    m_gb_register_success_ms = 0;
    m_gb_register_expires_sec = 0;

#endif

}

int ProtocolManager::GBManager_Start()
{
    printf("[ProtocolManager] GBManager_Start called\n");
    return Start();
}

int ProtocolManager::GBManager_Stop()
{
    printf("[ProtocolManager] GBManager_Stop called\n");
    Stop();
    return 0;
}

void ProtocolManager::OnGbConfigChanged()
{
    printf("[ProtocolManager] OnGbConfigChanged, reloading config and restarting GB...\n");

    if (!m_provider.get()) {
        printf("[ProtocolManager] OnGbConfigChanged failed, no provider\n");
        return;
    }

    ProtocolExternalConfig latest;
    int ret = m_provider->PullLatest(latest);
    if (ret != 0) {
        printf("[ProtocolManager] OnGbConfigChanged failed to pull config ret=%d\n", ret);
        return;
    }

    ret = m_provider->Validate(latest);
    if (ret != 0) {
        printf("[ProtocolManager] OnGbConfigChanged failed to validate config ret=%d\n", ret);
        return;
    }

    m_cfg = latest;
    m_gb_device_name = m_cfg.gb_register.device_name;

    Stop();
    Start();
}

int ProtocolManager::RegisterGbClient(bool force)

{

#if PROTOCOL_HAS_GB28181_CLIENT_SDK

    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_cfg.gb_register.enabled == 0) {
        printf("[ProtocolManager] gb register skipped because config disabled\n");
        m_gb_client_registered = false;
        m_gb_register_success_ms = 0;
        m_gb_register_expires_sec = 0;
        return 0;
    }

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
        m_gb_register_success_ms = 0;
        m_gb_register_expires_sec = 0;

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
    m_gb_register_success_ms = GetSteadyNowMs();
    m_gb_register_expires_sec = registerParam.expires;

    const int pendingUpgradeRet = ReportPendingGbUpgradeResult();
    if (pendingUpgradeRet != 0) {
        printf("[ProtocolManager] gb pending upgrade report deferred ret=%d local=%s\n",
               pendingUpgradeRet,
               localGbCode.c_str());
    }

    printf("[ProtocolManager] gb register ok local=%s server=%s:%u connectGb=%s expires=%u user=%s refresh_at_ms=%llu\n",

           localGbCode.c_str(),

           connectParam.ip,

           (unsigned int)connectParam.port,

           connectGbCode.c_str(),

           (unsigned int)registerParam.expires,

           authUser.c_str(),
           (unsigned long long)(m_gb_register_success_ms +
               (uint64_t)((registerParam.expires > ResolveGbRegisterRefreshLeadSec(registerParam.expires))
                              ? (registerParam.expires - ResolveGbRegisterRefreshLeadSec(registerParam.expires))
                              : 1U) * 1000ULL));

    std::string registerDate;
    const int dateRet = QueryGbRegisterDate(registerDate);
    printf("[ProtocolManager] gb register date header ret=%d raw=%s\n",
           dateRet,
           (dateRet == 0) ? registerDate.c_str() : "");
    if (dateRet == 0) {
        PrepareGbRegisterTimeSync(registerDate);
    }

    return 0;

#else

    (void)force;

    return 0;

#endif

}

int ProtocolManager::QueryGbRegisterDate(std::string& outDate) const
{
    outDate.clear();

#if PROTOCOL_HAS_GB28181_CLIENT_SDK
    if (m_gb_client_sdk == NULL || !m_gb_client_started) {
        return -1;
    }

    char* rawDate = m_gb_client_sdk->GetTime();
    if (rawDate == NULL) {
        return -2;
    }

    outDate.assign(rawDate);
    delete[] rawDate;
    rawDate = NULL;

    return outDate.empty() ? -3 : 0;
#else
    return -4;
#endif
}

int ProtocolManager::PrepareGbRegisterTimeSync(const std::string& rawDate)
{
    if (rawDate.empty()) {
        printf("[ProtocolManager] gb register time sync skip reason=empty_date_header dry_run=0\n");
        return -1;
    }

    time_t epochSec = 0;
    if (!ParseGbHttpDate(rawDate, &epochSec)) {
        printf("[ProtocolManager] gb register time sync parse failed raw=%s dry_run=0\n",
               rawDate.c_str());
        return -2;
    }

    char utcTime[32] = {0};
    char localTime[32] = {0};
    FormatEpochIsoTime(epochSec, true, utcTime, sizeof(utcTime));
    FormatEpochIsoTime(epochSec, false, localTime, sizeof(localTime));

    GbTimeSyncInfo info;
    info.raw_date = rawDate;
    info.utc_time = utcTime;
    info.local_time = localTime;
    info.epoch_sec = static_cast<int64_t>(epochSec);
    info.dry_run = false;

    printf("[ProtocolManager] gb register time sync prepared raw=%s utc=%s local=%s epoch=%lld action=settimeofday dry_run=0\n",
           rawDate.c_str(),
           utcTime,
           localTime,
           (long long)epochSec);

    if (m_gb_time_sync_hook != NULL) {
        const int hookRet = m_gb_time_sync_hook(info, m_gb_time_sync_user);
        printf("[ProtocolManager] gb register time sync hook ret=%d dry_run=%d\n",
               hookRet,
               info.dry_run ? 1 : 0);
        return hookRet;
    } else {
        const int applyRet = ApplyGbRegisterTimeSyncLocally(epochSec);
        printf("[ProtocolManager] gb register time sync local_apply ret=%d dry_run=%d\n",
               applyRet,
               info.dry_run ? 1 : 0);
        return applyRet;
    }
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
    int elapsedSec = 0;

    while (m_gb_heartbeat_running.load()) {

        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!m_gb_heartbeat_running.load()) {
            break;
        }

        ++elapsedSec;

        if (m_gb_client_registered && m_gb_register_success_ms > 0 && m_gb_register_expires_sec > 0) {
            const uint32_t leadSec = ResolveGbRegisterRefreshLeadSec(m_gb_register_expires_sec);
            const uint32_t refreshAfterSec = (m_gb_register_expires_sec > leadSec) ?
                                             (m_gb_register_expires_sec - leadSec) : 1U;
            const uint64_t nextRefreshMs = m_gb_register_success_ms + (uint64_t)refreshAfterSec * 1000ULL;
            if (GetSteadyNowMs() >= nextRefreshMs) {
                const int refreshRet = RegisterGbClient(true);
                if (refreshRet == 0) {
                    printf("[ProtocolManager] gb register refresh ok expires=%u lead=%u\n",
                           m_gb_register_expires_sec,
                           leadSec);
                } else {
                    printf("[ProtocolManager] gb register refresh failed ret=%d\n", refreshRet);
                }
            }
        }

        if (elapsedSec < intervalSec) {
            continue;
        }

        elapsedSec = 0;

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

int ProtocolManager::SetGbRegisterConfig(const GbRegisterParam& param)
{
    m_cfg.gb_register = param;
    return 0;
}

GbRegisterParam ProtocolManager::GetGbRegisterConfig() const
{
    return m_cfg.gb_register;
}



int ProtocolManager::PushLiveVideoEsFrame(const uint8_t* data,
                                         size_t size,
                                         uint64_t pts90k,
                                         int mediaType,
                                         bool keyFrame)

{

    if (!m_started) {

        return -1;

    }



    bool active = false;
    bool acked = false;
    bool logFrame = false;
    StreamHandle handle = NULL;
    uint32_t recvVideo = 0;
    uint32_t recvAudio = 0;
    uint32_t sentVideo = 0;
    uint32_t sentAudio = 0;
    const uint64_t nowMs = GetNowMs();
    const GbVideoStartState startState = DetectGbVideoStartState(mediaType, data, size);
    bool waitingFirstIdr = false;
    bool passConfigBeforeIdr = false;
    bool firstIdrAccepted = false;
    bool dropBeforeIdr = false;
    bool logDropBeforeIdr = false;

    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {
            active = true;
            acked = m_gb_live_session.acked;
            handle = m_gb_live_session.stream_handle;
            ++m_gb_live_session.recv_video_frames;
            recvVideo = m_gb_live_session.recv_video_frames;
            recvAudio = m_gb_live_session.recv_audio_frames;
            sentVideo = m_gb_live_session.sent_video_frames;
            sentAudio = m_gb_live_session.sent_audio_frames;
            waitingFirstIdr = m_gb_live_session.wait_first_idr;
            if (m_gb_live_session.wait_first_idr) {
                if (startState == kGbVideoStartIdr) {
                    m_gb_live_session.wait_first_idr = false;
                    firstIdrAccepted = true;
                } else if (startState == kGbVideoStartConfig) {
                    passConfigBeforeIdr = true;
                } else {
                    dropBeforeIdr = true;
                    if (m_gb_live_session.last_wait_idr_log_ms == 0 ||
                        nowMs >= m_gb_live_session.last_wait_idr_log_ms + 1000ULL) {
                        m_gb_live_session.last_wait_idr_log_ms = nowMs;
                        logDropBeforeIdr = true;
                    }
                }
            }
            if (m_gb_live_session.last_capture_log_ms == 0 ||
                nowMs >= m_gb_live_session.last_capture_log_ms + 1000ULL) {
                m_gb_live_session.last_capture_log_ms = nowMs;
                logFrame = true;
            }
        }

    }

    if (logFrame) {
        printf("[ProtocolManager] gb live video es handle=%p acked=%d wait_first_idr=%d start=%s recv_video=%u recv_audio=%u sent_video=%u sent_audio=%u size=%lu key=%d pts90k=%llu\n",
               handle,
               acked ? 1 : 0,
               waitingFirstIdr ? 1 : 0,
               GbVideoStartStateName(startState),
               recvVideo,
               recvAudio,
               sentVideo,
               sentAudio,
               (unsigned long)size,
               keyFrame ? 1 : 0,
               (unsigned long long)pts90k);
    }

    if (!active || !acked) {
        return 0;
    }

    if (dropBeforeIdr) {
        if (logDropBeforeIdr) {
            printf("[ProtocolManager] gb live drop pre-idr frame handle=%p start=%s size=%lu pts90k=%llu\n",
                   handle,
                   GbVideoStartStateName(startState),
                   (unsigned long)size,
                   (unsigned long long)pts90k);
        }
        return 0;
    }

    if (passConfigBeforeIdr) {
        printf("[ProtocolManager] gb live pass config before first idr handle=%p start=%s size=%lu pts90k=%llu\n",
               handle,
               GbVideoStartStateName(startState),
               (unsigned long)size,
               (unsigned long long)pts90k);
    } else if (firstIdrAccepted) {
        printf("[ProtocolManager] gb live first idr accepted handle=%p size=%lu pts90k=%llu\n",
               handle,
               (unsigned long)size,
               (unsigned long long)pts90k);
    }

    const int ret = m_rtp_ps_sender.SendVideoFrame(data,
                                                   size,
                                                   pts90k,
                                                   keyFrame || startState == kGbVideoStartIdr);

    if (ret == 0) {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {

            ++m_gb_live_session.sent_video_frames;

        }

    } else {

        GbLiveSession session;
        bool needStop = false;

        {
            std::lock_guard<std::mutex> lock(m_gb_live_mutex);
            if (m_gb_live_session.active) {
                session = m_gb_live_session;
                needStop = true;
            }
        }

        if (needStop) {
            printf("[ProtocolManager] gb live send failed ret=%d media=video gb=%s handle=%p size=%lu pts90k=%llu, stop session\n",
                   ret,
                   session.gb_code.c_str(),
                   session.stream_handle,
                   static_cast<unsigned long>(size),
                   static_cast<unsigned long long>(pts90k));
            HandleGbStopStreamRequest(session.stream_handle, session.gb_code.c_str());
        }

    }

    return ret;

}



int ProtocolManager::PushLiveAudioEsFrame(const uint8_t* data, size_t size, uint64_t pts90k)

{

    if (!m_started) {

        return -1;
    }



    bool active = false;
    bool acked = false;
    StreamHandle handle = NULL;

    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {
            active = true;
            acked = m_gb_live_session.acked;
            handle = m_gb_live_session.stream_handle;
            ++m_gb_live_session.recv_audio_frames;
        }

    }

    if (!active || !acked) {
        return 0;
    }



    const int ret = m_rtp_ps_sender.SendAudioFrame(data, size, pts90k);

    if (ret == 0) {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active) {

            ++m_gb_live_session.sent_audio_frames;

        }

    } else {

        GbLiveSession session;
        bool needStop = false;

        {
            std::lock_guard<std::mutex> lock(m_gb_live_mutex);
            if (m_gb_live_session.active) {
                session = m_gb_live_session;
                needStop = true;
            }
        }

        if (needStop) {
            printf("[ProtocolManager] gb live send failed ret=%d media=audio gb=%s handle=%p size=%lu pts90k=%llu, stop session\n",
                   ret,
                   session.gb_code.c_str(),
                   session.stream_handle,
                   static_cast<unsigned long>(size),
                   static_cast<unsigned long long>(pts90k));
            HandleGbStopStreamRequest(session.stream_handle, session.gb_code.c_str());
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

        const std::string& broadcastTransport = m_cfg.gb_broadcast.transport;

        transportType = (ToLowerCopy(broadcastTransport) == "tcp") ? kRtpOverTcpActive : kRtpOverUdp;

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



int ProtocolManager::HandleGbBroadcastNotify(const char* gbCode, const BroadcastInfo* info)

{

    if (info == NULL) {

        return -1;

    }



    const std::string sourceId = SafeStr(info->SourceID, sizeof(info->SourceID));

    const std::string targetId = SafeStr(info->TargetID, sizeof(info->TargetID));

    const std::string deviceId = m_cfg.gb_register.device_id;
    const std::string resolvedGbCode = (gbCode != NULL && gbCode[0] != '\0')
                                           ? gbCode
                                           : (!targetId.empty() ? targetId : deviceId);

    if (!targetId.empty() && !deviceId.empty() && targetId != deviceId) {

        printf("[ProtocolManager] gb broadcast notify rejected gb=%s source=%s target=%s local_device=%s\n",
               resolvedGbCode.c_str(),
               sourceId.c_str(),
               targetId.c_str(),
               deviceId.c_str());

        return -2;

    }



    std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);

    m_gb_broadcast_session.gb_code = resolvedGbCode;

    m_gb_broadcast_session.source_id = sourceId;

    m_gb_broadcast_session.target_id = targetId;

    m_gb_broadcast_session.notify_ts_ms = GetNowMs();



    printf("[ProtocolManager] gb broadcast notify accepted gb=%s source=%s target=%s active=%d handle=%p\n",
           m_gb_broadcast_session.gb_code.c_str(),
           m_gb_broadcast_session.source_id.c_str(),
           m_gb_broadcast_session.target_id.c_str(),
           m_gb_broadcast_session.active ? 1 : 0,
           m_gb_broadcast_session.stream_handle);

    return 0;

}

int ProtocolManager::BuildGbTalkMediaInfo(const char* gbCode,

                                         const MediaInfo* input,

                                         const std::string& localIp,

                                         MediaInfo& out,

                                         RtpMap& outMap) const

{

    memset(&out, 0, sizeof(out));

    memset(&outMap, 0, sizeof(outMap));



    const std::string preferredCodec = NormalizeCodec(m_cfg.gb_talk.codec);

    std::string remoteIp;

    int remotePort = 0;

    int payloadType = -1;

    std::string codec;

    unsigned int sampleRate = 0;

    if (!ExtractAudioTransportHintWithPreference(input,
                                                 preferredCodec,
                                                 remoteIp,
                                                 remotePort,
                                                 payloadType,
                                                 codec,
                                                 sampleRate)) {

        codec = preferredCodec.empty() ? "g711a" : preferredCodec;

        payloadType = ResolvePayloadTypeByCodec(codec);

        sampleRate = (m_cfg.gb_talk.sample_rate > 0) ? (unsigned int)m_cfg.gb_talk.sample_rate : 8000U;

    }



    if (sampleRate == 0) {

        sampleRate = (m_cfg.gb_talk.sample_rate > 0) ? (unsigned int)m_cfg.gb_talk.sample_rate : 8000U;

    }



    const NetTransType answerTransport = (input != NULL)
                                             ? ResolveGbAnswerTransportType(input->RtpType, "udp")
                                             : kRtpOverUdp;
    const std::string ip = localIp.empty() ? "0.0.0.0" : localIp;
    const std::string deviceId = (gbCode != NULL && gbCode[0] != '\0') ? gbCode : m_cfg.gb_register.device_id;
    const int localRecvPort = (m_cfg.gb_talk.recv_port > 0) ? m_cfg.gb_talk.recv_port : 0;

    if (localRecvPort <= 0) {

        return -1;

    }



    out.RtpType = answerTransport;

    out.RequestType = kAudioStream;

    out.Port = static_cast<unsigned int>(localRecvPort);

    CopyBounded(out.DeviceID, sizeof(out.DeviceID), deviceId);

    CopyBounded(out.IP, sizeof(out.IP), ip);



    const unsigned int ssrcValue = (m_cfg.gb_live.ssrc > 0) ? static_cast<unsigned int>(m_cfg.gb_live.ssrc) : 1u;

    snprintf(out.Ssrc, sizeof(out.Ssrc), "%010u", ssrcValue);



    out.RtpDescri.type = kGBAudio;

    out.RtpDescri.DescriNum = 1;

    out.RtpDescri.mapDescri = &outMap;



    outMap.MediaFormat = static_cast<unsigned int>(payloadType);

    outMap.SampleRate = sampleRate;

    CopyBounded(outMap.MimeType, sizeof(outMap.MimeType), ResolveMimeByCodec(codec));



    return 0;

}

int ProtocolManager::HandleGbBroadcastNotifyResponse(const char* gbCode, const BroadcastInfo* info, bool ok)

{

#if !PROTOCOL_HAS_GB28181_CLIENT_SDK
    (void)gbCode;
    (void)info;
    (void)ok;
    return -1;
#else
    if (!ok || info == NULL) {

        printf("[ProtocolManager] gb broadcast notify response unavailable gb=%s ok=%d\n",
               gbCode != NULL ? gbCode : "",
               ok ? 1 : 0);

        return -70;

    }


    const std::string sourceId = SafeStr(info->SourceID, sizeof(info->SourceID));

    const std::string targetId = SafeStr(info->TargetID, sizeof(info->TargetID));

    const std::string deviceId = m_cfg.gb_register.device_id;
    const std::string resolvedGbCode = (gbCode != NULL && gbCode[0] != '\0')
                                           ? gbCode
                                           : (!targetId.empty() ? targetId : deviceId);


    GB28181ClientSDK* sdk = NULL;
    {
        std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);
        sdk = m_gb_client_sdk;
    }

    if (sdk == NULL) {

        printf("[ProtocolManager] gb broadcast active invite skipped no sdk gb=%s source=%s target=%s\n",
               resolvedGbCode.c_str(),
               sourceId.c_str(),
               targetId.c_str());

        return -71;

    }


    std::string localIp;
    if (!ResolveGbResponseLocalIp(m_cfg.gb_register.server_ip, m_cfg.gb_register.server_port, localIp)) {
        std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);
        localIp = m_gb_broadcast_session.local_ip;
    }

    if (localIp.empty()) {

        printf("[ProtocolManager] gb broadcast active invite local ip unavailable server=%s:%d gb=%s\n",
               m_cfg.gb_register.server_ip.c_str(),
               m_cfg.gb_register.server_port,
               resolvedGbCode.c_str());

        return -72;

    }


    RtpMap requestMap;
    MediaInfo request;
    memset(&requestMap, 0, sizeof(requestMap));
    memset(&request, 0, sizeof(request));

    const std::string mediaDeviceId = sourceId.empty() ? resolvedGbCode : sourceId;
    const std::string inviteTargetId = mediaDeviceId;

    int ret = BuildGbBroadcastMediaInfo(localIp, mediaDeviceId, request, requestMap);
    if (ret != 0) {

        printf("[ProtocolManager] gb broadcast active invite build request failed ret=%d gb=%s invite_target=%s\n",
               ret,
               resolvedGbCode.c_str(),
               inviteTargetId.c_str());

        return -73;

    }

    request.StreamNum = 0;


    MediaInfo answer;
    memset(&answer, 0, sizeof(answer));

    StreamHandle handle = NULL;
    printf("[ProtocolManager] gb broadcast active invite start gb=%s source=%s target=%s invite_target=%s media_device=%s transport=%s local=%s:%u codec=%s pt=%u\n",
           resolvedGbCode.c_str(),
           sourceId.c_str(),
           targetId.c_str(),
           inviteTargetId.c_str(),
           mediaDeviceId.c_str(),
           GbNetTransportName(request.RtpType),
           request.IP,
           request.Port,
           requestMap.MimeType,
           requestMap.MediaFormat);

    ret = sdk->StartBroadcastStreamRequest(inviteTargetId.c_str(), &request, &answer, &handle);

    if (!IsGbSdkSuccess(ret) || handle == NULL) {

        printf("[ProtocolManager] gb broadcast active invite failed ret=%d gb=%s source=%s target=%s invite_target=%s\n",
               ret,
               resolvedGbCode.c_str(),
               sourceId.c_str(),
               targetId.c_str(),
               inviteTargetId.c_str());

        return -74;

    }


    std::string remoteIp;
    int remotePort = 0;
    int payloadType = -1;
    std::string codec;
    if (!ExtractAudioTransportHint(&answer, remoteIp, remotePort, payloadType, codec)) {

        const char* mime = "";
        int mediaFormat = -1;
        unsigned int sampleRate = 0;
        if (answer.RtpDescri.mapDescri != NULL && answer.RtpDescri.DescriNum > 0) {
            mime = answer.RtpDescri.mapDescri[0].MimeType;
            mediaFormat = (int)answer.RtpDescri.mapDescri[0].MediaFormat;
            sampleRate = answer.RtpDescri.mapDescri[0].SampleRate;
        }

        printf("[ProtocolManager] gb broadcast active invite parse answer failed gb=%s handle=%p ip=%s port=%u transport=%s desc_num=%u mime=%s pt=%d rate=%u\n",
               resolvedGbCode.c_str(),
               handle,
               answer.IP,
               answer.Port,
               GbNetTransportName(answer.RtpType),
               answer.RtpDescri.DescriNum,
               mime,
               mediaFormat,
               sampleRate);

        sdk->StopStreamRequest(handle);
        if (answer.RtpDescri.mapDescri != NULL) {
            free(answer.RtpDescri.mapDescri);
            answer.RtpDescri.mapDescri = NULL;
            answer.RtpDescri.DescriNum = 0;
        }
        return -75;

    }


    ret = ApplyGbBroadcastTransportHint(remoteIp, remotePort, payloadType, codec, answer.RtpType);
    if (ret != 0) {

        printf("[ProtocolManager] gb broadcast active invite apply answer failed ret=%d gb=%s remote=%s:%d handle=%p\n",
               ret,
               resolvedGbCode.c_str(),
               remoteIp.c_str(),
               remotePort,
               handle);

        sdk->StopStreamRequest(handle);
        if (answer.RtpDescri.mapDescri != NULL) {
            free(answer.RtpDescri.mapDescri);
            answer.RtpDescri.mapDescri = NULL;
            answer.RtpDescri.DescriNum = 0;
        }
        return -76;

    }


    {
        std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);
        const uint64_t notifyTsMs = m_gb_broadcast_session.notify_ts_ms;
        m_gb_broadcast_session = GbBroadcastSession();
        m_gb_broadcast_session.active = true;
        m_gb_broadcast_session.acked = true;
        m_gb_broadcast_session.stream_handle = handle;
        m_gb_broadcast_session.gb_code = resolvedGbCode;
        m_gb_broadcast_session.source_id = sourceId;
        m_gb_broadcast_session.target_id = targetId;
        m_gb_broadcast_session.remote_ip = remoteIp;
        m_gb_broadcast_session.remote_port = remotePort;
        m_gb_broadcast_session.local_ip = request.IP;
        m_gb_broadcast_session.local_port = (int)request.Port;
        m_gb_broadcast_session.payload_type = payloadType;
        m_gb_broadcast_session.transport_type = (int)answer.RtpType;
        m_gb_broadcast_session.codec = codec;
        m_gb_broadcast_session.notify_ts_ms = notifyTsMs;
        m_gb_broadcast_session.invite_ts_ms = GetNowMs();
    }


    printf("[ProtocolManager] gb broadcast active invite established gb=%s handle=%p remote=%s:%d local=%s:%u codec=%s pt=%d transport=%s\n",
           resolvedGbCode.c_str(),
           handle,
           remoteIp.c_str(),
           remotePort,
           request.IP,
           request.Port,
           codec.c_str(),
           payloadType,
           GbNetTransportName(answer.RtpType));

    if (answer.RtpDescri.mapDescri != NULL) {
        free(answer.RtpDescri.mapDescri);
        answer.RtpDescri.mapDescri = NULL;
        answer.RtpDescri.DescriNum = 0;
    }

    return 0;
#endif

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

    unsigned int sampleRate = 0;

    GbTalkSession previousSession;

    bool needResetPrevious = false;

    const std::string preferredCodec = NormalizeCodec(m_cfg.gb_talk.codec);

    const int localRecvPort = (m_cfg.gb_talk.recv_port > 0) ? m_cfg.gb_talk.recv_port : 0;

    const NetTransType answerTransport = ResolveGbAnswerTransportType(input->RtpType, "udp");

    if (!ExtractAudioTransportHintWithPreference(input,
                                                 preferredCodec,
                                                 remoteIp,
                                                 remotePort,
                                                 payloadType,
                                                 codec,
                                                 sampleRate)) {

        printf("[ProtocolManager] gb talk invite parse failed gb=%s\n", gbCode != NULL ? gbCode : "");

        return -11;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_talk_mutex);

        if (m_gb_talk_session.active &&
            m_gb_talk_session.stream_handle != NULL &&
            m_gb_talk_session.stream_handle != handle) {

            previousSession = m_gb_talk_session;

            m_gb_talk_session = GbTalkSession();

            needResetPrevious = true;

        }

    }

    if (needResetPrevious) {

        printf("[ProtocolManager] gb talk session replaced old_handle=%p old_remote=%s:%d new_handle=%p gb=%s\n",
               previousSession.stream_handle,
               previousSession.remote_ip.c_str(),
               previousSession.remote_port,
               handle,
               gbCode != NULL ? gbCode : "");

        const int resetRet = RestartGbTalkBridge("replace_audio_stream");
        if (resetRet != 0) {
            return resetRet;
        }

    }



    std::string localIp;

    if (!ResolveGbResponseLocalIp(remoteIp, remotePort, localIp)) {

        printf("[ProtocolManager] gb talk invite local ip unresolved remote=%s:%d gb=%s\n",
               remoteIp.c_str(),
               remotePort,
               gbCode != NULL ? gbCode : "");

    }



    int ret = RespondGbMediaPlayInfo(handle, gbCode, kAudioStream, input);

    if (ret != 0) {

        return ret;

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_talk_mutex);

        m_gb_talk_session = GbTalkSession();

        m_gb_talk_session.active = true;

        m_gb_talk_session.stream_handle = handle;

        m_gb_talk_session.gb_code = (gbCode != NULL) ? gbCode : "";

        m_gb_talk_session.invite_ts_ms = GetNowMs();

        m_gb_talk_session.remote_ip = remoteIp;

        m_gb_talk_session.remote_port = remotePort;

        m_gb_talk_session.local_ip = localIp;

        m_gb_talk_session.local_port = localRecvPort;

        m_gb_talk_session.payload_type = payloadType;

        m_gb_talk_session.request_transport_type = (int)input->RtpType;

        m_gb_talk_session.answer_transport_type = (int)answerTransport;

        m_gb_talk_session.codec = codec;

        m_gb_talk_session.sample_rate = (sampleRate > 0) ? (int)sampleRate : m_cfg.gb_talk.sample_rate;

    }



    printf("[ProtocolManager] gb talk invite accepted gb=%s codec=%s pt=%d req_transport=%d answer_transport=%d remote=%s:%d local=%s:%d sample_rate=%u ack_wait=1\n",

           gbCode != NULL ? gbCode : "",

           codec.c_str(),

           payloadType,

           (int)input->RtpType,

           (int)answerTransport,

           remoteIp.c_str(),

           remotePort,

           localIp.c_str(),

           localRecvPort,

           sampleRate);

    return 0;

}



int ProtocolManager::HandleGbLiveStreamRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input)

{

    std::string remoteIp;

    int remotePort = 0;

    const bool endpointKnown = ExtractStreamEndpoint(input, remoteIp, remotePort);

    const bool audioRequested = IsLiveAudioRequested(input);

    const bool psMuxOffer = HasLivePsMuxOffer(input);

    const bool forceAudio = ShouldForceGbLiveAudio(m_cfg);

    const bool audioEnabled = audioRequested || psMuxOffer || forceAudio;

    const int requestedStreamNum = ResolveGbStreamNumber(input, m_cfg);

    const bool preferSubStream = (requestedStreamNum > 0);



    printf("[ProtocolManager] gb live request enter gb=%s handle=%p offered_transport=%s remote=%s:%d stream_num=%d audio_requested=%d ps_mux_offer=%d force_audio=%d audio_enabled=%d\n",

           gbCode != NULL ? gbCode : "",

           handle,

           (input != NULL) ? GbNetTransportName(input->RtpType) : "null",

           endpointKnown ? remoteIp.c_str() : "",

           endpointKnown ? remotePort : 0,

           requestedStreamNum,

           audioRequested ? 1 : 0,

           psMuxOffer ? 1 : 0,

           forceAudio ? 1 : 0,

           audioEnabled ? 1 : 0);



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

        printf("[ProtocolManager] gb live request gb=%s handle=%p stream_num=%d stream=%s audio_requested=%d force_audio=%d audio_enabled=%d\n",

               m_gb_live_session.gb_code.c_str(),

               handle,

               requestedStreamNum,

               preferSubStream ? "sub" : "main",

               audioRequested ? 1 : 0,

               forceAudio ? 1 : 0,

               audioEnabled ? 1 : 0);

    }



    ret = StartGbLiveCapture();

    if (ret != 0) {
        m_rtp_ps_sender.CloseSession();
        m_gb_current_media_ssrc = 0;
        m_gb_current_media_port = 0;

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
        m_rtp_ps_sender.CloseSession();
        m_gb_current_media_ssrc = 0;
        m_gb_current_media_port = 0;

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        m_gb_live_session = GbLiveSession();

        return ret;

    }



    printf("[ProtocolManager] gb live stream accepted gb=%s handle=%p stream=%s audio_requested=%d ps_mux_offer=%d force_audio=%d audio_enabled=%d audio_codec=%s\n",

           gbCode != NULL ? gbCode : "",

           handle,

           preferSubStream ? "sub" : "main",

           audioRequested ? 1 : 0,

           psMuxOffer ? 1 : 0,

           forceAudio ? 1 : 0,

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



    std::string gbCode;
    bool preferSubStream = false;
    bool audioRequested = false;
    bool audioEnabled = false;
    int forceIdrChannel = -1;

    {

        std::lock_guard<std::mutex> lock(m_gb_live_mutex);

        if (m_gb_live_session.active &&

            m_gb_live_session.stream_handle != NULL &&

            handle == m_gb_live_session.stream_handle) {

            if (!m_rtp_ps_sender.IsOpened()) {

                const int openRet = m_rtp_ps_sender.OpenSession();

                if (openRet != 0) {

                    printf("[ProtocolManager] gb live ack open sender failed ret=%d gb=%s handle=%p\n",

                           openRet,

                           m_gb_live_session.gb_code.c_str(),

                           handle);

                    return openRet;

                }

            }

            m_gb_live_session.acked = true;
            m_gb_live_session.wait_first_idr = true;
            m_gb_live_session.last_wait_idr_log_ms = 0;
            gbCode = m_gb_live_session.gb_code;
            preferSubStream = m_gb_live_session.prefer_sub_stream;
            audioRequested = m_gb_live_session.audio_requested;
            audioEnabled = m_gb_live_session.audio_enabled;
            forceIdrChannel = m_gb_live_session.prefer_sub_stream ? 1 : 0;

        } else {
            forceIdrChannel = -1;

        }

    }

    if (forceIdrChannel >= 0) {
        const int forceRet = CaptureForceIFrame(forceIdrChannel, 0);
        printf("[ProtocolManager] gb live acked gb=%s handle=%p stream=%s audio_requested=%d audio_enabled=%d force_idr_channel=%d force_idr_ret=%d\n",
               gbCode.c_str(),
               handle,
               preferSubStream ? "sub" : "main",
               audioRequested ? 1 : 0,
               audioEnabled ? 1 : 0,
               forceIdrChannel,
               forceRet);
        return 0;
    }

    GbTalkSession talkSession;
    bool hasTalkSession = false;

    {
        std::lock_guard<std::mutex> lock(m_gb_talk_mutex);
        if (m_gb_talk_session.active &&
            m_gb_talk_session.stream_handle != NULL &&
            handle == m_gb_talk_session.stream_handle) {
            m_gb_talk_session.acked = true;
            m_gb_talk_session.ack_ts_ms = GetNowMs();
            talkSession = m_gb_talk_session;
            hasTalkSession = true;
        }
    }

    if (hasTalkSession) {
        const int applyRet = m_talk_broadcast.ApplyTransportHint(talkSession.remote_ip,
                                                                 talkSession.remote_port,
                                                                 talkSession.payload_type,
                                                                 talkSession.codec,
                                                                 talkSession.answer_transport_type);
        if (applyRet != 0) {
            printf("[ProtocolManager] gb talk ack apply transport failed ret=%d gb=%s handle=%p remote=%s:%d local=%s:%d codec=%s pt=%d transport=%d\n",
                   applyRet,
                   talkSession.gb_code.c_str(),
                   handle,
                   talkSession.remote_ip.c_str(),
                   talkSession.remote_port,
                   talkSession.local_ip.c_str(),
                   talkSession.local_port,
                   talkSession.codec.c_str(),
                   talkSession.payload_type,
                   talkSession.answer_transport_type);
            HandleGbStopStreamRequest(handle, talkSession.gb_code.c_str());
            return applyRet;
        }

        int uplinkRet = 0;
        bool uplinkStarted = false;
        if (IsGbTalkListenSupportedTransport((NetTransType)talkSession.answer_transport_type)) {
            GbListenParam listenParam = m_cfg.gb_listen;
            listenParam.transport = ((NetTransType)talkSession.answer_transport_type == kRtpOverUdp) ? "udp" : "tcp";
            listenParam.target_ip = talkSession.remote_ip;
            listenParam.target_port = talkSession.remote_port;
            listenParam.codec = talkSession.codec;
            listenParam.sample_rate = (talkSession.sample_rate > 0) ? talkSession.sample_rate : m_cfg.gb_talk.sample_rate;
            m_listen.StopSession();
            uplinkRet = m_listen.StartSession(listenParam);
            uplinkStarted = (uplinkRet == 0);
        } else {
            uplinkRet = -1;
            printf("[ProtocolManager] gb talk uplink skipped unsupported transport gb=%s handle=%p transport=%d\n",
                   talkSession.gb_code.c_str(),
                   handle,
                   talkSession.answer_transport_type);
        }

        {
            std::lock_guard<std::mutex> lock(m_gb_talk_mutex);
            if (m_gb_talk_session.active &&
                m_gb_talk_session.stream_handle == handle) {
                m_gb_talk_session.uplink_started = uplinkStarted;
            }
        }

        printf("[ProtocolManager] gb talk acked gb=%s handle=%p remote=%s:%d local=%s:%d codec=%s pt=%d transport=%d sample_rate=%d uplink_started=%d uplink_ret=%d\n",
               talkSession.gb_code.c_str(),
               handle,
               talkSession.remote_ip.c_str(),
               talkSession.remote_port,
               talkSession.local_ip.c_str(),
               talkSession.local_port,
               talkSession.codec.c_str(),
               talkSession.payload_type,
               talkSession.answer_transport_type,
               talkSession.sample_rate,
               uplinkStarted ? 1 : 0,
               uplinkRet);
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);
        if (m_gb_broadcast_session.active &&
            m_gb_broadcast_session.stream_handle != NULL &&
            handle == m_gb_broadcast_session.stream_handle) {
            m_gb_broadcast_session.acked = true;
            printf("[ProtocolManager] gb broadcast acked gb=%s handle=%p remote=%s:%d local=%s:%d codec=%s pt=%d transport=%d\n",
                   m_gb_broadcast_session.gb_code.c_str(),
                   handle,
                   m_gb_broadcast_session.remote_ip.c_str(),
                   m_gb_broadcast_session.remote_port,
                   m_gb_broadcast_session.local_ip.c_str(),
                   m_gb_broadcast_session.local_port,
                   m_gb_broadcast_session.codec.c_str(),
                   m_gb_broadcast_session.payload_type,
                   m_gb_broadcast_session.transport_type);
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

        if (cmd->Type == kPlayStop) {

            HandleGbStopStreamRequest(handle, NULL);

            return 0;

        }



        printf("[ProtocolManager] gb play control ignored(no replay session) gb=%s handle=%p type=%d\n",

               cmd->GBCode,

               handle,

               (int)cmd->Type);

        return -51;

    }



    int ret = 0;

    const float speed = cmd->Scale;

    auto markReplayAcked = [&]() {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        if (m_gb_replay_session.active &&

            m_gb_replay_session.stream_handle != NULL &&

            (handle == NULL || handle == m_gb_replay_session.stream_handle)) {

            m_gb_replay_session.acked = true;

            printf("[ProtocolManager] gb replay control acked type=%s gb=%s handle=%p storage=%d cmd=%d\n",

                   m_gb_replay_session.download ? "download" : "playback",

                   m_gb_replay_session.gb_code.c_str(),

                   m_gb_replay_session.stream_handle,

                   m_gb_replay_session.storage_handle,

                   (int)cmd->Type);

        }

    };

    auto resumeReplay = [&]() -> int {

        const int resumeRet = session.download ? Storage_Module_PauseDownload(session.storage_handle, false)

                                               : Storage_Module_PausePlaybackOnFile(session.storage_handle, false);

        if (resumeRet == 0) {

            markReplayAcked();

        }

        return resumeRet;

    };



    switch (cmd->Type) {

    case kPlayPause:

        ret = session.download ? Storage_Module_PauseDownload(session.storage_handle, true)

                               : Storage_Module_PausePlaybackOnFile(session.storage_handle, true);

        break;

    case kPlayStart:

        if (!session.download && speed > 0.0f) {

            ret = Storage_Module_SetPlaybackSpeed(session.storage_handle, speed);

            if (ret != 0) {

                break;

            }

        }

        ret = resumeReplay();

        break;

    case kPlayFast:

    case kPlaySlow:

        if (session.download || speed <= 0.0f) {

            ret = -53;

            break;

        }

        ret = Storage_Module_SetPlaybackSpeed(session.storage_handle, speed);

        if (ret == 0) {

            ret = resumeReplay();

        }

        break;

    case kPlayDarg:

        if (session.download) {

            ret = -54;

        } else {

            const int tz_offset_sec = 8 * 3600;
            ret = Storage_Module_SeekTime(session.storage_handle, (int)cmd->Npt + tz_offset_sec);

            if (ret == 0) {

                ret = resumeReplay();

            }

        }

        break;

    case kPlayStop:

        HandleGbStopStreamRequest(handle, session.gb_code.c_str());

        ret = 0;

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



    const uint64_t nowMs = GetSteadyNowMs();
    const uint64_t lastMs = m_gb_last_teleboot_ms.load();
    const uint64_t cooldownMs = (m_cfg.gb_reboot.cooldown_sec > 0)
        ? (static_cast<uint64_t>(m_cfg.gb_reboot.cooldown_sec) * 1000ULL)
        : 0ULL;
    if (cooldownMs > 0 && lastMs > 0 && nowMs > lastMs && (nowMs - lastMs) < cooldownMs) {
        printf("[ProtocolManager] gb teleboot rejected gb=%s cooldown=%d last_ms=%llu now_ms=%llu\n",
               cmd->GBCode,
               m_cfg.gb_reboot.cooldown_sec,
               (unsigned long long)lastMs,
               (unsigned long long)nowMs);
        return -88;
    }

    m_gb_last_teleboot_ms.store(nowMs);

    if (!CreateDetachedThread((char*)"gb_reboot", GbRemoteRestartThread, NULL, true)) {

        m_gb_last_teleboot_ms.store(lastMs);
        return -89;

    }



    printf("[ProtocolManager] gb teleboot scheduled gb=%s cooldown=%d require_auth_level=%d\n",

           cmd->GBCode,

           m_cfg.gb_reboot.cooldown_sec,
           m_cfg.gb_reboot.require_auth_level);

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

    if (m_gb_upgrade_running.load()) {
        printf("[ProtocolManager] gb upgrade rejected ret=%d reason=%s firmware=%s manufacturer=%s session=%s force=%d url=%s gb=%s\n",
               -115,
               "upgrade_busy",
               firmware.c_str(),
               manufacturer.c_str(),
               sessionId.c_str(),
               forceUpgrade ? 1 : 0,
               fileUrl.c_str(),
               cmd->GBCode);
        return -115;
    }

    const auto fail = [&](int code, const char* reason) -> int {

        PersistGbUpgradePendingState(cmd->GBCode,
                                     sessionId.c_str(),
                                     firmware.c_str(),
                                     reason,
                                     false);

        RemoveFileQuietly(kGbUpgradePackageTempPath);
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

    PersistGbUpgradePendingState(cmd->GBCode,
                                 sessionId.c_str(),
                                 firmware.c_str(),
                                 "",
                                 true);

    std::unique_ptr<GbUpgradeThreadContext> threadCtx(new GbUpgradeThreadContext());
    threadCtx->manager = this;
    m_gb_upgrade_running.store(true);
    if (!CreateDetachedThread((char*)"gb_upgrade_apply", ProtocolManager::GbUpgradeApplyThread, threadCtx.get(), true)) {
        m_gb_upgrade_running.store(false);
        return fail(-114, "apply_thread_failed");
    }
    threadCtx.release();

    printf("[ProtocolManager] gb upgrade staged firmware=%s manufacturer=%s session=%s force=%d expected_size=%llu actual_size=%llu checksum=%s package=%s gb=%s\n",

           firmware.c_str(),

           manufacturer.c_str(),

           sessionId.c_str(),

           forceUpgrade ? 1 : 0,

           expectedFileSize,

           actualFileSize,

           expectedChecksum.empty() ? "none" : expectedChecksum.c_str(),

           kGbUpgradePackagePath,

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



int ProtocolManager::NotifyGbAlarm(AlarmNotifyInfo* info)

{

    if (info == NULL) {

        return -91;

    }
        strncpy(info->DeviceID, m_cfg.gb_register.device_id.c_str(), GB_ID_LEN - 1);

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

    std::vector<RecordParam> records;



    std::string gbCode = SafeStr(param->GBCode, sizeof(param->GBCode));

    if (gbCode.empty()) {

        gbCode = m_cfg.gb_register.device_id;

    }

    if (gbCode.empty()) {

        gbCode = "34020000001320000001";

    }



    CopyBounded(recordIndex.GBCode, sizeof(recordIndex.GBCode), gbCode);
    const std::string deviceName = ResolveGbDeviceName(m_cfg, m_gb_device_name);
    const std::string queryType = ToLowerCopy(
        SafeStr(param->query_descri.record_index.Type, sizeof(param->query_descri.record_index.Type)));
    const std::string startText = SafeStr(param->query_descri.record_index.StartTime,
                                          sizeof(param->query_descri.record_index.StartTime));
    const std::string endText = SafeStr(param->query_descri.record_index.EndTime,
                                        sizeof(param->query_descri.record_index.EndTime));

    time_t startEpoch = 0;
    time_t endEpoch = 0;
    if (!ParseGbQueryDateTime(param->query_descri.record_index.StartTime, &startEpoch) ||
        !ParseGbQueryDateTime(param->query_descri.record_index.EndTime, &endEpoch)) {
        printf("[ProtocolManager] gb record query invalid time gb=%s start=%s end=%s type=%s\n",
               gbCode.c_str(),
               startText.c_str(),
               endText.c_str(),
               queryType.c_str());
        return -67;
    }

    if (endEpoch < startEpoch) {
        printf("[ProtocolManager] gb record query invalid range gb=%s start=%s end=%s\n",
               gbCode.c_str(),
               startText.c_str(),
               endText.c_str());
        return -68;
    }

    std::string recordAddress;
    ResolveGbResponseLocalIp(m_cfg.gb_register.server_ip, m_cfg.gb_register.server_port, recordAddress);

    std::vector<record_file_info_s> recordEntries;
    CollectGbRecordEntries(startEpoch, endEpoch, queryType, &recordEntries);

    records.reserve(recordEntries.size());
    for (std::vector<record_file_info_s>::const_iterator it = recordEntries.begin();
         it != recordEntries.end();
         ++it) {
        RecordParam item;
        memset(&item, 0, sizeof(item));

        char startBuf[32] = {0};
        char endBuf[32] = {0};
        FormatGbRecordTime((time_t)it->iStartTime, startBuf, sizeof(startBuf));
        FormatGbRecordTime((time_t)it->iEndTime, endBuf, sizeof(endBuf));

        char filePath[ROUTE_LEN] = {0};
        BuildGbRecordFilePath(it->iStartTime, it->iEndTime, it->iRecType, filePath, sizeof(filePath));
        const std::string filePathText = filePath;
        const std::string fileName = GetGbRecordFileName(filePathText);

        CopyBounded(item.DeviceID, sizeof(item.DeviceID), gbCode);
        CopyBounded(item.StartTime, sizeof(item.StartTime), startBuf);
        CopyBounded(item.EndTime, sizeof(item.EndTime), endBuf);
        CopyBounded(item.FilePath, sizeof(item.FilePath), filePathText);
        CopyBounded(item.Address, sizeof(item.Address), recordAddress);
        CopyBounded(item.Type, sizeof(item.Type), ResolveGbRecordTypeText(it->iRecType));
        CopyBounded(item.Name, sizeof(item.Name), fileName.empty() ? deviceName : fileName);
        item.Secrecy = 0;
        records.push_back(item);

        printf("[ProtocolManager] gb record query item start=%s end=%s type=%s path=%s\n",
               startBuf,
               endBuf,
               ResolveGbRecordTypeText(it->iRecType),
               filePathText.c_str());
    }

    recordIndex.Num = static_cast<unsigned int>(records.size());
    recordIndex.record_list = records.empty() ? NULL : &records[0];

    printf("[ProtocolManager] gb record query gb=%s start=%s end=%s type=%s result=%u addr=%s\n",
           gbCode.c_str(),
           startText.c_str(),
           endText.c_str(),
           queryType.c_str(),
           recordIndex.Num,
           recordAddress.c_str());



    const int transMode = (ToLowerCopy(m_cfg.gb_live.transport) == "tcp") ? 1 : 0;



    std::lock_guard<std::mutex> lock(m_gb_lifecycle_mutex);

    if (m_gb_client_sdk == NULL) {

        return 0;

    }



    const int ret = m_gb_client_sdk->ResponseRecordIndex(handle, &recordIndex, transMode);

    printf("[ProtocolManager] gb record query ResponseRecordIndex raw_ret=%d success=%d\n",
           ret,
           IsGbSdkSuccess(ret) ? 1 : 0);

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

    GbBroadcastSession broadcastSession;

    bool needStopBroadcast = false;

    GbTalkSession talkSession;

    bool needStopTalk = false;



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



    {

        std::lock_guard<std::mutex> lock(m_gb_talk_mutex);

        if (m_gb_talk_session.active &&

            (handle == NULL ||

             m_gb_talk_session.stream_handle == NULL ||

             handle == m_gb_talk_session.stream_handle)) {

            talkSession = m_gb_talk_session;

            m_gb_talk_session = GbTalkSession();

            needStopTalk = true;

        }

    }



    {

        std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);

        if (m_gb_broadcast_session.active &&

            (handle == NULL ||

             m_gb_broadcast_session.stream_handle == NULL ||

             handle == m_gb_broadcast_session.stream_handle)) {

            broadcastSession = m_gb_broadcast_session;

            m_gb_broadcast_session = GbBroadcastSession();

            needStopBroadcast = true;

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



    if (needStopBroadcast) {

        m_broadcast.StopSession();

        printf("[ProtocolManager] gb broadcast session stopped gb=%s handle=%p source=%s target=%s remote=%s:%d local=%s:%d codec=%s pt=%d transport=%d acked=%d\n",

               broadcastSession.gb_code.c_str(),

               broadcastSession.stream_handle,

               broadcastSession.source_id.c_str(),

               broadcastSession.target_id.c_str(),

               broadcastSession.remote_ip.c_str(),

               broadcastSession.remote_port,

               broadcastSession.local_ip.c_str(),

               broadcastSession.local_port,

               broadcastSession.codec.c_str(),

               broadcastSession.payload_type,

               broadcastSession.transport_type,

               broadcastSession.acked ? 1 : 0);



        if (m_started && handle != NULL) {

            const int restartRet = RestartGbBroadcastBridge("stop_stream");

            if (restartRet != 0) {

                printf("[ProtocolManager] gb broadcast bridge restart failed ret=%d reason=stop_stream\n", restartRet);

            }

        }

    }

    if (needStopTalk) {

        m_listen.StopSession();

        m_talk_broadcast.StopSession();

        printf("[ProtocolManager] gb talk session stopped gb=%s handle=%p remote=%s:%d local=%s:%d codec=%s pt=%d req_transport=%d answer_transport=%d acked=%d uplink_started=%d\n",

               talkSession.gb_code.c_str(),

               talkSession.stream_handle,

               talkSession.remote_ip.c_str(),

               talkSession.remote_port,

               talkSession.local_ip.c_str(),

               talkSession.local_port,

               talkSession.codec.c_str(),

               talkSession.payload_type,

               talkSession.request_transport_type,

               talkSession.answer_transport_type,

               talkSession.acked ? 1 : 0,

               talkSession.uplink_started ? 1 : 0);



        if (m_started && handle != NULL) {

            const int restartRet = RestartGbTalkBridge("stop_stream");

            if (restartRet != 0) {

                printf("[ProtocolManager] gb talk bridge restart failed ret=%d reason=stop_stream\n", restartRet);

            }

        }

    }



    if (needStopReplay || needStopLive) {
        m_gb_current_media_ssrc = 0;
        m_gb_current_media_port = 0;
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



    const int requestedStreamNum = ResolveGbStreamNumber(input, m_cfg);

    GbLiveParam runtimeParam = m_cfg.gb_live;

    runtimeParam.target_ip = remoteIp;

    runtimeParam.target_port = remotePort;
    runtimeParam.local_port = 0;
    runtimeParam.video_stream_id = (requestedStreamNum > 0) ? "sub" : "main";

    runtimeParam.transport = (input->RtpType == kRtpOverUdp) ? "udp" : "tcp";
    if (runtimeParam.transport == "tcp" && m_cfg.gb_live.local_port > 0) {
        runtimeParam.local_port = m_cfg.gb_live.local_port;
    }
    runtimeParam.ssrc = (m_cfg.gb_live.ssrc > 0)
                            ? m_cfg.gb_live.ssrc
                            : static_cast<int>(GenerateGbMediaSsrc(remotePort));

    std::string runtimeVideoCodec;
    if (ReadRkVideoOutputTypeString(requestedStreamNum, runtimeVideoCodec)) {
        const std::string normalizedRuntimeCodec = NormalizeGbLiveVideoCodec(runtimeVideoCodec);
        if (!normalizedRuntimeCodec.empty()) {
            runtimeParam.video_codec = normalizedRuntimeCodec;
        }
    } else {
        runtimeParam.video_codec = NormalizeGbLiveVideoCodec(runtimeParam.video_codec);
    }

    printf("[ProtocolManager] gb %s stream runtime codec stream=%s codec=%s remote=%s:%d\n",
           StreamRequestTypeName(requestType),
           runtimeParam.video_stream_id.c_str(),
           runtimeParam.video_codec.c_str(),
           runtimeParam.target_ip.c_str(),
           runtimeParam.target_port);

    m_gb_current_media_ssrc = static_cast<uint32_t>(runtimeParam.ssrc);
    m_gb_current_media_port = 0;



    m_rtp_ps_sender.CloseSession();



    int ret = m_rtp_ps_sender.Init(runtimeParam);

    if (ret != 0) {

        printf("[ProtocolManager] gb %s stream sender init failed ret=%d gb=%s\n",

               StreamRequestTypeName(requestType),

               ret,

               gbCode != NULL ? gbCode : "");

        return -23;

    }



    if (requestType == kLiveStream && runtimeParam.transport == "tcp") {
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

        m_gb_current_media_port = m_rtp_ps_sender.GetLocalPort();
        printf("[ProtocolManager] gb %s stream sender prepared target=%s:%d local_port=%d gb=%s transport=%s open=pre_response\n",
               StreamRequestTypeName(requestType),
               remoteIp.c_str(),
               remotePort,
               m_gb_current_media_port,
               gbCode != NULL ? gbCode : "",
               runtimeParam.transport.c_str());
        return 0;
    }

    if (requestType == kLiveStream) {

        printf("[ProtocolManager] gb %s stream sender prepared target=%s:%d local_port=%d gb=%s transport=%s open=deferred_until_ack\n",

               StreamRequestTypeName(requestType),

               remoteIp.c_str(),

               remotePort,

               runtimeParam.local_port,

               gbCode != NULL ? gbCode : "",

               runtimeParam.transport.c_str());

        return 0;

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



    m_gb_current_media_port = m_rtp_ps_sender.GetLocalPort();

    printf("[ProtocolManager] gb %s stream sender target=%s:%d local_port=%d gb=%s\n",

           StreamRequestTypeName(requestType),

           remoteIp.c_str(),

           remotePort,

           m_gb_current_media_port,

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

        std::string localIp;
        if (input != NULL) {
            const std::string requestIp = SafeStr(input->IP, sizeof(input->IP));
            if (!ResolveGbResponseLocalIp(requestIp, (int)input->Port, localIp)) {
                printf("[ProtocolManager] gb talk resolve local ip failed remote=%s:%u gb=%s\n",
                       requestIp.c_str(),
                       input->Port,
                       gbCode != NULL ? gbCode : "");
            }
        }

        if (localIp.empty()) {
            std::lock_guard<std::mutex> lock(m_gb_talk_mutex);
            localIp = m_gb_talk_session.local_ip;
        }

        return BuildGbTalkMediaInfo(gbCode, input, localIp, out, outMap);

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

    std::string resolvedIp = requestIp.empty() ? m_cfg.gb_live.target_ip : requestIp;

    unsigned int resolvedPort = (input->Port > 0)

                                    ? input->Port

                                    : (unsigned int)((m_cfg.gb_live.target_port > 0) ? m_cfg.gb_live.target_port : 0);



    if (input->RtpType == kRtpOverTcp ||

        input->RtpType == kRtpOverTcpActive ||

        input->RtpType == kRtpOverTcpPassive) {

        out.RtpType = ResolveGbAnswerTransportType(input->RtpType, m_cfg.gb_live.transport);

    } else if (ToLowerCopy(m_cfg.gb_live.transport) == "tcp") {

        out.RtpType = kRtpOverTcpActive;

    } else {

        out.RtpType = kRtpOverUdp;

    }

    out.RequestType = requestType;

    out.StreamNum = ResolveGbStreamNumber(input, m_cfg);

    out.StartTime = input->StartTime;

    out.EndTime = input->EndTime;

    out.DownloadSpeed = input->DownloadSpeed;

    out.FileSize = input->FileSize;
    CopyBounded(out.MediaF, sizeof(out.MediaF), input->MediaF);



    // For downstream media answers, advertise the device-side address rather than

    // echoing the platform offer endpoint back in the 200 OK SDP.

    if (requestType == kLiveStream || requestType == kPlayback || requestType == kDownload) {

        std::string localIp;

        if (ResolveGbResponseLocalIp(requestIp, (int)input->Port, localIp)) {

            resolvedIp = localIp;

        }

        if (out.RtpType == kRtpOverTcpActive || out.RtpType == kRtpOverTcp) {
            if (m_gb_current_media_port > 0) {
                resolvedPort = static_cast<unsigned int>(m_gb_current_media_port);
            } else if (m_cfg.gb_live.local_port > 0) {
                resolvedPort = static_cast<unsigned int>(m_cfg.gb_live.local_port);
            }
        }

    }



    CopyBounded(out.DeviceID,

                sizeof(out.DeviceID),

                (gbCode != NULL && gbCode[0] != '\0') ? gbCode : m_cfg.gb_register.device_id);

    out.Port = resolvedPort;

    CopyBounded(out.IP, sizeof(out.IP), resolvedIp);



    const unsigned int ssrcValue = (m_gb_current_media_ssrc > 0)
                                       ? m_gb_current_media_ssrc
                                       : ((m_cfg.gb_live.ssrc > 0) ? (unsigned int)m_cfg.gb_live.ssrc : 1u);

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

    printf("[ProtocolManager] gb media response prepared gb=%s type=%s handle=%p answer_transport=%s answer_ip=%s answer_port=%u answer_ssrc=%s req_transport=%s req_ip=%s req_port=%u\n",
           gbCode != NULL ? gbCode : "",
           StreamRequestTypeName(requestType),
           handle,
           GbNetTransportName(answer.RtpType),
           answer.IP,
           answer.Port,
           answer.Ssrc,
           input != NULL ? GbNetTransportName(input->RtpType) : "null",
           input != NULL ? input->IP : "",
           input != NULL ? input->Port : 0U);



    ret = m_gb_media_responder(handle, &answer, m_gb_media_responder_user);

    if (ret != 0) {

        printf("[ProtocolManager] gb media response callback failed ret=%d gb=%s type=%s\n",

               ret,

               gbCode != NULL ? gbCode : "",

               StreamRequestTypeName(requestType));

        return -32;

    }



    printf("[ProtocolManager] gb media response sent gb=%s type=%s answer_transport=%s answer_ip=%s answer_port=%u ssrc=%s\n",

           gbCode != NULL ? gbCode : "",

           StreamRequestTypeName(requestType),

           GbNetTransportName(answer.RtpType),

           answer.IP,

           answer.Port,

           answer.Ssrc);



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



    printf("[ProtocolManager] gb %s request enter gb=%s handle=%p offered_transport=%s remote=%s:%d range=[%llu,%llu], stop existing sessions first\n",
           download ? "download" : "playback",
           gbCode != NULL ? gbCode : "",
           handle,
           GbNetTransportName(input->RtpType),
           input->IP,
           input->Port,
           (unsigned long long)input->StartTime,
           (unsigned long long)input->EndTime);

    HandleGbStopStreamRequest(NULL, gbCode);

    int ret = ReconfigureGbLiveSender(input, gbCode, download ? kDownload : kPlayback);

    if (ret != 0) {

        return ret;

    }
    const int tz_offset_sec = 8 * 3600;
    const int startSec = (int)input->StartTime + tz_offset_sec;
    const int endSec = (int)input->EndTime + tz_offset_sec;



    GbReplayCallbackContext* callbackContext = new GbReplayCallbackContext();
    if (callbackContext == NULL) {

        return -44;

    }

    callbackContext->manager = this;
    callbackContext->download = download;

    {

        std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

        callbackContext->generation = ++m_gb_replay_generation;
        m_gb_replay_callback_contexts.push_back(callbackContext);

    }

    const int storageHandle = download

                                  ? Storage_Module_StartDownload(startSec, endSec, &ProtocolManager::OnGbReplayStorageFrame, callbackContext)

                                  : Storage_Module_StartPlayback(startSec, endSec, &ProtocolManager::OnGbReplayStorageFrame, callbackContext);

    if (storageHandle < 0) {

        printf("[ProtocolManager] gb %s start storage failed ret=%d gb=%s range=[%d,%d]\n",

               download ? "download" : "playback",

               storageHandle,

               gbCode != NULL ? gbCode : "",

               startSec,

               endSec);

        {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            for (std::vector<GbReplayCallbackContext*>::iterator it = m_gb_replay_callback_contexts.begin();
                 it != m_gb_replay_callback_contexts.end();
                 ++it) {

                if (*it == callbackContext) {

                    delete *it;
                    m_gb_replay_callback_contexts.erase(it);
                    break;

                }

            }

        }

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

        m_gb_replay_session.generation = callbackContext->generation;

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

    GbReplayCallbackContext* context = (GbReplayCallbackContext*)userData;

    ProtocolManager* self = (context != NULL) ? context->manager : NULL;

    if (self == NULL) {

        return;

    }



    self->HandleGbReplayStorageFrame(data, size, frameInfo, context);

}



void ProtocolManager::HandleGbReplayStorageFrame(unsigned char* data,
                                                 int size,
                                                 Mp4DemuxerFrameInfo_s* frameInfo,
                                                 const GbReplayCallbackContext* context)

{

    const uint64_t callbackGeneration = (context != NULL) ? context->generation : 0;

    if (data == NULL && size == 0 && frameInfo == NULL) {

        GbReplaySession ended;

        bool hasSession = false;

        bool currentActive = false;

        uint64_t currentGeneration = 0;
        GbReplayCallbackContext* ownedContext = NULL;



        {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            if (m_gb_replay_session.active &&
                callbackGeneration != 0 &&
                callbackGeneration == m_gb_replay_session.generation) {

                ended = m_gb_replay_session;

                m_gb_replay_session = GbReplaySession();

                hasSession = true;

            }

            currentActive = m_gb_replay_session.active;
            currentGeneration = m_gb_replay_session.generation;

            for (std::vector<GbReplayCallbackContext*>::iterator it = m_gb_replay_callback_contexts.begin();
                 it != m_gb_replay_callback_contexts.end();
                 ++it) {

                if (*it == context) {

                    ownedContext = *it;
                    m_gb_replay_callback_contexts.erase(it);
                    break;

                }

            }

        }

        if (!hasSession && callbackGeneration != 0) {

            printf("[ProtocolManager] gb replay stale eos ignored generation=%llu active=%d current=%llu\n",
                   (unsigned long long)callbackGeneration,
                   currentActive ? 1 : 0,
                   (unsigned long long)currentGeneration);

        }

        if (ownedContext != NULL) {

            delete ownedContext;

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



    bool waitLogged = false;
    while (true) {

        bool active = false;
        bool acked = false;

        {

            std::lock_guard<std::mutex> lock(m_gb_replay_mutex);

            active = (m_gb_replay_session.active &&
                      callbackGeneration != 0 &&
                      callbackGeneration == m_gb_replay_session.generation);
            acked = active && m_gb_replay_session.acked;

        }

        if (!active) {

            return;

        }

        if (acked) {

            break;

        }

        if (!waitLogged) {

            printf("[ProtocolManager] gb replay frame gated before play-start streamType=%d size=%d ts_ms=%llu\n",

                   frameInfo->iStreamType,

                   size,

                   (unsigned long long)frameInfo->ullTimestamp);

            waitLogged = true;

        }

        usleep(10 * 1000);

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

        const uint8_t* audioData = (const uint8_t*)data;

        size_t audioSize = (size_t)size;

        std::vector<uint8_t> encodedAudio;

        if (audioCodec == "g711a") {

            encodedAudio.resize(audioSize + 16U);

            const int outLen = DG_encode_g711a((char*)data, (char*)&encodedAudio[0], size);

            if (outLen <= 0) {

                printf("[ProtocolManager] gb replay audio encode failed codec=%s size=%d\n",

                       m_cfg.gb_live.audio_codec.c_str(),

                       size);

                return;

            }

            encodedAudio.resize((size_t)outLen);

            audioData = &encodedAudio[0];

            audioSize = encodedAudio.size();

        } else if (audioCodec == "g711u") {

            encodedAudio.resize(audioSize + 16U);

            const int outLen = DG_encode_g711u((char*)data, (char*)&encodedAudio[0], size);

            if (outLen <= 0) {

                printf("[ProtocolManager] gb replay audio encode failed codec=%s size=%d\n",

                       m_cfg.gb_live.audio_codec.c_str(),

                       size);

                return;

            }

            encodedAudio.resize((size_t)outLen);

            audioData = &encodedAudio[0];

            audioSize = encodedAudio.size();

        } else {

            printf("[ProtocolManager] gb replay audio unsupported codec=%s, expect g711a/g711u by sdp\n",

                   m_cfg.gb_live.audio_codec.c_str());

            return;

        }



        ret = m_rtp_ps_sender.SendAudioFrame(audioData, audioSize, pts90k);

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

        return PushLiveVideoEsFrame(frame_data,
                                    static_cast<size_t>(frame_len),
                                    pts90k,
                                    media_type,
                                    keyFrame);

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



void ProtocolManager::ClearGbBroadcastSessionState()

{

    std::lock_guard<std::mutex> lock(m_gb_broadcast_mutex);

    m_gb_broadcast_session = GbBroadcastSession();

}

void ProtocolManager::ClearGbTalkSessionState()

{

    std::lock_guard<std::mutex> lock(m_gb_talk_mutex);

    m_gb_talk_session = GbTalkSession();

}



int ProtocolManager::RestartGbBroadcastBridge(const char* reason)

{

    m_broadcast.StopSession();

    if (!m_started) {
        return 0;
    }

    const int ret = m_broadcast.StartSession(m_cfg.gb_broadcast);
    printf("[ProtocolManager] gb broadcast bridge restart reason=%s ret=%d mode=%s codec=%s recv_port=%d\n",
           reason != NULL ? reason : "",
           ret,
           m_cfg.gb_broadcast.input_mode.c_str(),
           m_cfg.gb_broadcast.codec.c_str(),
           m_cfg.gb_broadcast.recv_port);
    return ret;

}

int ProtocolManager::RestartGbTalkBridge(const char* reason)

{

    m_listen.StopSession();

    m_talk_broadcast.StopSession();

    ClearGbTalkSessionState();

    if (!m_started) {
        return 0;
    }

    const GbBroadcastParam talkParam = BuildGbTalkBridgeParam(m_cfg);
    const int ret = m_talk_broadcast.StartSession(talkParam);
    printf("[ProtocolManager] gb talk bridge restart reason=%s ret=%d codec=%s recv_port=%d sample_rate=%d jitter_buffer_ms=%d\n",
           reason != NULL ? reason : "",
           ret,
           m_cfg.gb_talk.codec.c_str(),
           m_cfg.gb_talk.recv_port,
           m_cfg.gb_talk.sample_rate,
           m_cfg.gb_talk.jitter_buffer_ms);

    if (m_started && ShouldAutoStartGbListen(m_cfg.gb_listen)) {
        const int listenRet = m_listen.StartSession(m_cfg.gb_listen);
        printf("[ProtocolManager] gb talk bridge restore listen reason=%s ret=%d target=%s:%d transport=%s\n",
               reason != NULL ? reason : "",
               listenRet,
               m_cfg.gb_listen.target_ip.c_str(),
               m_cfg.gb_listen.target_port,
               m_cfg.gb_listen.transport.c_str());
    }
    return ret;

}



void ProtocolManager::SetGbMediaPlayInfoResponder(GbMediaPlayInfoResponder responder, void* userData)

{

    m_gb_media_responder = responder;

    m_gb_media_responder_user = userData;

}

void ProtocolManager::SetGbTimeSyncHook(GbTimeSyncHook hook, void* userData)
{

    m_gb_time_sync_hook = hook;

    m_gb_time_sync_user = userData;

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
