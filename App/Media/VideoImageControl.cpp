#include "VideoImageControl.h"

#include <stdio.h>

extern "C"
{
int rk_isp_get_image_flip(int cam_id, const char **value);
int rk_isp_set_image_flip(int cam_id, const char *value);
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

static std::string NormalizeVideoImageFlipMode(const std::string& modeIn)
{
    const std::string mode = ToLowerCopy(modeIn);
    if (mode.empty() || mode == "0" || mode == "none" || mode == "close" || mode == "off" ||
        mode == "restore" || mode == "reset") {
        return "close";
    }

    if (mode == "1" || mode == "mirror" || mode == "horizontal" || mode == "hflip" ||
        mode == "leftright" || mode == "left_right") {
        return "mirror";
    }

    if (mode == "2" || mode == "flip" || mode == "vertical" || mode == "vflip" ||
        mode == "updown" || mode == "up_down") {
        return "flip";
    }

    if (mode == "3" || mode == "centrosymmetric" || mode == "center" ||
        mode == "both" || mode == "all") {
        return "centrosymmetric";
    }

    return modeIn;
}

} // namespace

namespace media
{

bool QueryVideoImageFlipMode(std::string* value)
{
    if (value == NULL) {
        return false;
    }

    const char* raw = NULL;
    if (rk_isp_get_image_flip(0, &raw) != 0 || raw == NULL || raw[0] == '\0') {
        return false;
    }

    const std::string normalized = NormalizeVideoImageFlipMode(raw);
    *value = normalized.empty() ? std::string(raw) : normalized;
    return !value->empty();
}

int ApplyVideoImageFlipMode(const std::string& desiredMode)
{
    const std::string normalizedMode = NormalizeVideoImageFlipMode(desiredMode);
    if (normalizedMode != "close" &&
        normalizedMode != "mirror" &&
        normalizedMode != "flip" &&
        normalizedMode != "centrosymmetric") {
        return -1;
    }

    std::string currentMode;
    const bool currentKnown = QueryVideoImageFlipMode(&currentMode);
    if (currentKnown && currentMode == normalizedMode) {
        return 0;
    }

    const int ret = rk_isp_set_image_flip(0, normalizedMode.c_str());
    printf("[VideoImageControl] apply image_flip ret=%d value=%s current=%s\n",
           ret,
           normalizedMode.c_str(),
           currentKnown ? currentMode.c_str() : "unknown");
    return ret;
}

} // namespace media
