#include "VideoEncodeControl.h"

#include <stdio.h>
#include <string.h>

#include "PAL/Capture.h"

extern "C"
{
int rk_video_get_gop(int stream_id, int *value);
int rk_video_set_gop(int stream_id, int value);
int rk_video_get_max_rate(int stream_id, int *value);
int rk_video_set_max_rate(int stream_id, int value);
int rk_video_get_RC_mode(int stream_id, const char **value);
int rk_video_set_RC_mode(int stream_id, const char *value);
int rk_video_get_output_data_type(int stream_id, const char **value);
int rk_video_set_output_data_type(int stream_id, const char *value);
int rk_video_get_frame_rate(int stream_id, char **value);
int rk_video_set_frame_rate(int stream_id, const char *value);
int rk_video_set_resolution(int stream_id, const char *value);
}

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

static const char* ResolveStreamName(int streamId)
{
    switch (streamId) {
        case 0:
            return "main";
        case 1:
            return "sub";
        default:
            return "unknown";
    }
}

static std::string NormalizeVideoEncodeCodec(const std::string& codecIn)
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

static std::string NormalizeVideoBitrateType(const std::string& modeIn)
{
    const std::string mode = ToLowerCopy(modeIn);
    if (mode.empty()) {
        return "";
    }

    if (mode == "cbr" || mode == "constant" || mode == "fixed") {
        return "CBR";
    }

    if (mode == "vbr" || mode == "avbr" || mode == "cvbr" || mode == "qvbr" ||
        mode == "smtrc" || mode == "variable" || mode == "fixqp" || mode == "cqp") {
        return "VBR";
    }

    return modeIn;
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

static std::string BuildResolutionValue(int width, int height, char separator)
{
    if (width <= 0 || height <= 0) {
        return "";
    }

    char buffer[32] = {0};
    snprintf(buffer, sizeof(buffer), "%d%c%d", width, separator, height);
    return buffer;
}

static bool ReadVideoEncodeFrameRateString(int streamId, std::string& value)
{
    char buffer[32] = {0};
    char* out = buffer;
    if (rk_video_get_frame_rate(streamId, &out) != 0 || buffer[0] == '\0') {
        return false;
    }

    value = buffer;
    return true;
}

static bool ReadVideoEncodeCodecString(int streamId, std::string& value)
{
    const char* out = NULL;
    if (rk_video_get_output_data_type(streamId, &out) != 0 || out == NULL || out[0] == '\0') {
        return false;
    }

    value = NormalizeVideoEncodeCodec(out);
    return true;
}

static bool ReadVideoBitrateTypeString(int streamId, std::string& value)
{
    const char* out = NULL;
    if (rk_video_get_RC_mode(streamId, &out) != 0 || out == NULL || out[0] == '\0') {
        return false;
    }

    value = NormalizeVideoBitrateType(out);
    return !value.empty();
}

static bool ReadVideoEncodeResolution(int streamId, int* width, int* height)
{
    if (width == NULL || height == NULL) {
        return false;
    }

    *width = 0;
    *height = 0;
    return CaptureGetResolution(streamId, width, height) == 0 && *width > 0 && *height > 0;
}

static int MergeError(int current, int candidate)
{
    return (current != 0) ? current : candidate;
}

static std::string BuildResolutionSummary(const media::VideoEncodeState& state)
{
    const bool hasMain = state.main_stream.has_resolution && state.main_stream.width > 0 && state.main_stream.height > 0;
    const bool hasSub = state.sub_stream.has_resolution && state.sub_stream.width > 0 && state.sub_stream.height > 0;

    if (hasMain && hasSub) {
        return state.main_stream.resolution + "/" + state.sub_stream.resolution;
    }

    if (hasMain) {
        return state.main_stream.resolution;
    }

    if (hasSub) {
        return state.sub_stream.resolution;
    }

    return "";
}

} // namespace

namespace media
{

bool QueryVideoEncodeStreamState(int streamId, VideoEncodeStreamState* state)
{
    if (state == NULL) {
        return false;
    }

    *state = VideoEncodeStreamState();

    std::string value;
    if (ReadVideoEncodeCodecString(streamId, value)) {
        state->has_codec = true;
        state->codec = value;
    }

    int width = 0;
    int height = 0;
    if (ReadVideoEncodeResolution(streamId, &width, &height)) {
        state->has_resolution = true;
        state->width = width;
        state->height = height;
        state->resolution = BuildResolutionValue(width, height, 'x');
    }

    if (ReadVideoEncodeFrameRateString(streamId, value)) {
        state->has_frame_rate = true;
        state->frame_rate = value;
    }

    int bitrate = 0;
    if (rk_video_get_max_rate(streamId, &bitrate) == 0) {
        state->has_bitrate = true;
        state->bitrate_kbps = bitrate;
    }

    if (ReadVideoBitrateTypeString(streamId, value)) {
        state->has_bitrate_type = true;
        state->bitrate_type = value;
    }

    int gop = 0;
    if (rk_video_get_gop(streamId, &gop) == 0) {
        state->has_gop = true;
        state->gop = gop;
    }

    return state->has_codec ||
           state->has_resolution ||
           state->has_frame_rate ||
           state->has_bitrate ||
           state->has_bitrate_type ||
           state->has_gop;
}

bool QueryVideoEncodeState(VideoEncodeState* state)
{
    if (state == NULL) {
        return false;
    }

    *state = VideoEncodeState();

    const bool hasMain = QueryVideoEncodeStreamState(0, &state->main_stream);
    const bool hasSub = QueryVideoEncodeStreamState(1, &state->sub_stream);

    state->resolution_summary = BuildResolutionSummary(*state);
    return hasMain || hasSub || !state->resolution_summary.empty();
}

int ApplyVideoEncodeStreamConfig(int streamId, const VideoEncodeStreamConfig& desired)
{
    int finalRet = 0;
    const char* streamName = ResolveStreamName(streamId);

    const std::string normalizedCodec = NormalizeVideoEncodeCodec(desired.codec);
    if (!normalizedCodec.empty()) {
        std::string currentCodec;
        const bool codecKnown = ReadVideoEncodeCodecString(streamId, currentCodec);
        if (!codecKnown || currentCodec != normalizedCodec) {
            const int ret = rk_video_set_output_data_type(streamId, normalizedCodec.c_str());
            printf("[VideoEncodeControl] apply codec ret=%d stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   normalizedCodec.c_str(),
                   codecKnown ? currentCodec.c_str() : "unknown");
            finalRet = MergeError(finalRet, ret);
        }
    }

    const std::string frameRateValue = BuildFrameRateValue(desired.fps);
    if (!frameRateValue.empty()) {
        std::string currentFrameRate;
        const bool fpsKnown = ReadVideoEncodeFrameRateString(streamId, currentFrameRate);
        if (!fpsKnown || currentFrameRate != frameRateValue) {
            const int ret = rk_video_set_frame_rate(streamId, frameRateValue.c_str());
            printf("[VideoEncodeControl] apply fps ret=%d stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   frameRateValue.c_str(),
                   fpsKnown ? currentFrameRate.c_str() : "unknown");
            finalRet = MergeError(finalRet, ret);
        }
    }

    if (desired.bitrate_kbps > 0) {
        int currentBitrate = 0;
        const bool bitrateKnown = (rk_video_get_max_rate(streamId, &currentBitrate) == 0);
        if (!bitrateKnown || currentBitrate != desired.bitrate_kbps) {
            const int ret = rk_video_set_max_rate(streamId, desired.bitrate_kbps);
            printf("[VideoEncodeControl] apply bitrate ret=%d stream=%s stream_id=%d value=%d current=%d\n",
                   ret,
                   streamName,
                   streamId,
                   desired.bitrate_kbps,
                   bitrateKnown ? currentBitrate : -1);
            finalRet = MergeError(finalRet, ret);
        }
    }

    const std::string normalizedBitrateType = NormalizeVideoBitrateType(desired.bitrate_type);
    if (!normalizedBitrateType.empty()) {
        std::string currentBitrateType;
        const bool bitrateTypeKnown = ReadVideoBitrateTypeString(streamId, currentBitrateType);
        if (!bitrateTypeKnown || currentBitrateType != normalizedBitrateType) {
            const int ret = rk_video_set_RC_mode(streamId, normalizedBitrateType.c_str());
            printf("[VideoEncodeControl] apply bitrate_type ret=%d stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   normalizedBitrateType.c_str(),
                   bitrateTypeKnown ? currentBitrateType.c_str() : "unknown");
            finalRet = MergeError(finalRet, ret);
        }
    }

    const std::string normalizedResolution = NormalizeResolutionValue(desired.resolution, '*');
    if (!normalizedResolution.empty()) {
        int currentWidth = 0;
        int currentHeight = 0;
        std::string currentResolution;
        const bool resolutionKnown = ReadVideoEncodeResolution(streamId, &currentWidth, &currentHeight);
        if (resolutionKnown) {
            currentResolution = BuildResolutionValue(currentWidth, currentHeight, '*');
        }

        if (!resolutionKnown || currentResolution != normalizedResolution) {
            const int ret = rk_video_set_resolution(streamId, normalizedResolution.c_str());
            printf("[VideoEncodeControl] apply resolution ret=%d stream=%s stream_id=%d value=%s current=%s\n",
                   ret,
                   streamName,
                   streamId,
                   normalizedResolution.c_str(),
                   resolutionKnown ? currentResolution.c_str() : "unknown");
            finalRet = MergeError(finalRet, ret);
        }
    }

    return finalRet;
}

} // namespace media
