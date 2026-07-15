#include "CmiotOsdControl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "ExchangeAL/ExchangeKind.h"
#include "ExchangeAL/MediaExchange.h"
#include "Manager/ConfigManager.h"
#include "Media/VideoOsdControl.h"
#include "PAL/Capture.h"

namespace
{

static const int kCoordinateMax = 10000;
static const int kDefaultMainStreamWidth = 1920;
static const int kDefaultMainStreamHeight = 1080;
static const cmiot_uint8_t kCmiotOsdModeCustom = 1;
static const cmiot_uint8_t kCmiotOsdModeGb = 2;

struct CachedCmiotOsd
{
    bool valid;
    cmiotOSDInfo_t info;
    cmiotOsdTextInfo_t customText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdTextInfo_t districtText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdTextInfo_t additionText[CMIOT_APP_OSD_TEXT_MAX];

    CachedCmiotOsd()
        : valid(false)
    {
        memset(&info, 0, sizeof(info));
        memset(customText, 0, sizeof(customText));
        memset(districtText, 0, sizeof(districtText));
        memset(additionText, 0, sizeof(additionText));
    }
};

static pthread_mutex_t g_cmiot_osd_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static CachedCmiotOsd g_cmiot_osd_cache;

static int ClampInt(int value, int minValue, int maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static cmiot_uint32_t ClampCoordinate(cmiot_uint32_t value)
{
    return (value > (cmiot_uint32_t)kCoordinateMax) ? (cmiot_uint32_t)kCoordinateMax : value;
}

static size_t BoundedStringLength(const char* text, size_t maxLen)
{
    if (text == NULL) {
        return 0;
    }
    size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return len;
}

static std::string SafeString(const char* text, size_t maxLen)
{
    return std::string(text ? text : "", BoundedStringLength(text, maxLen));
}

static void CopyString(char* dst, size_t dstSize, const std::string& src)
{
    if (dst == NULL || dstSize == 0) {
        return;
    }
    snprintf(dst, dstSize, "%s", src.c_str());
}

static std::string HexEncodeString(const std::string& input)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size() * 2);
    for (size_t i = 0; i < input.size(); ++i) {
        const unsigned char value = (unsigned char)input[i];
        output.push_back(kHex[(value >> 4) & 0x0f]);
        output.push_back(kHex[value & 0x0f]);
    }
    return output;
}

static std::string HexDecodeString(const std::string& input)
{
    std::string output;
    for (size_t i = 0; i + 1 < input.size(); i += 2) {
        char byteText[3] = {input[i], input[i + 1], '\0'};
        output.push_back((char)strtol(byteText, NULL, 16));
    }
    return output;
}

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

static std::string TrimCopy(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' ||
            text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }

    return text.substr(begin, end - begin);
}

static bool IsHexDigit(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool NormalizeRgbColor(const std::string& input, std::string* out)
{
    if (out == NULL) {
        return false;
    }

    std::string color = TrimCopy(input);
    if (color.size() >= 2 && color[0] == '0' && (color[1] == 'x' || color[1] == 'X')) {
        color = color.substr(2);
    }
    if (!color.empty() && color[0] == '#') {
        color = color.substr(1);
    }
    if (color.size() != 6) {
        return false;
    }
    for (size_t i = 0; i < color.size(); ++i) {
        if (!IsHexDigit(color[i])) {
            return false;
        }
        if (color[i] >= 'A' && color[i] <= 'F') {
            color[i] = static_cast<char>(color[i] - 'A' + 'a');
        }
    }

    *out = "#" + color;
    return true;
}

static int NormalizeFontSize(int value)
{
    if (value <= 0) {
        return 32;
    }
    if (value <= 24) {
        return 16;
    }
    if (value <= 48) {
        return 32;
    }
    return 64;
}

static bool NormalizeFontColor(const char* colorText,
                               std::string* modeOut,
                               std::string* colorOut,
                               std::string* cmiotColorOut)
{
    if (modeOut == NULL || colorOut == NULL || cmiotColorOut == NULL) {
        return false;
    }

    const std::string raw = TrimCopy(SafeString(colorText, 16));
    const std::string lower = ToLowerCopy(raw);
    if (lower.empty() || lower == "white") {
        *modeOut = "customize";
        *colorOut = "#ffffff";
        *cmiotColorOut = "white";
        return true;
    }
    if (lower == "black") {
        *modeOut = "customize";
        *colorOut = "#000000";
        *cmiotColorOut = "black";
        return true;
    }
    if (lower == "auto") {
        *modeOut = "auto";
        *colorOut = "#ffffff";
        *cmiotColorOut = "auto";
        return true;
    }

    std::string normalizedColor;
    if (!NormalizeRgbColor(raw, &normalizedColor)) {
        return false;
    }
    *modeOut = "customize";
    *colorOut = normalizedColor;
    *cmiotColorOut = normalizedColor;
    return true;
}

static bool IsStatusOn(const cmiot_char_t status[4])
{
    const std::string value = ToLowerCopy(TrimCopy(SafeString(status, 4)));
    return value == "on" || value == "1" || value == "true";
}

static void SetStatus(cmiot_char_t status[4], bool enabled)
{
    CopyString(status, 4, enabled ? "On" : "Off");
}

static std::string BuildTimeFormat(const cmiotOsdDateInfo_t& date)
{
    std::string dateFormat = TrimCopy(SafeString(date.dateFormat, sizeof(date.dateFormat)));
    if (dateFormat.empty()) {
        dateFormat = "YYYY-MM-DD";
    }
    return dateFormat + ((date.timeFormat == 12) ? " 12hour" : " 24hour");
}

static std::string BuildDateFormatFromState(const media::VideoOsdState& state)
{
    if (state.has_date_style) {
        const std::string style = ToLowerCopy(state.date_style);
        if (style.find("yyyy.mm.dd") != std::string::npos) {
            return "YYYY.MM.DD";
        }
        if (style.find("yyyy/mm/dd") != std::string::npos) {
            return "YYYY/MM/DD";
        }
    }
    return "YYYY-MM-DD";
}

static cmiot_uint16_t BuildTimeFormatFromState(const media::VideoOsdState& state)
{
    return (state.has_time_style && ToLowerCopy(state.time_style).find("12") != std::string::npos) ? 12 : 24;
}

static void QueryMainStreamResolution(int* width, int* height)
{
    int streamWidth = 0;
    int streamHeight = 0;
    if (CaptureGetResolution(0, &streamWidth, &streamHeight) != 0 ||
        streamWidth <= 0 || streamHeight <= 0) {
        streamWidth = kDefaultMainStreamWidth;
        streamHeight = kDefaultMainStreamHeight;
    }

    if (width != NULL) {
        *width = streamWidth;
    }
    if (height != NULL) {
        *height = streamHeight;
    }
}

static int CharMarginToCoordinate(float chars, int fontSize, int canvasSize)
{
    if (chars <= 0.0f || fontSize <= 0 || canvasSize <= 0) {
        return 0;
    }
    const float pixels = chars * (float)fontSize;
    return ClampInt((int)(pixels * (float)kCoordinateMax / (float)canvasSize + 0.5f),
                    0,
                    kCoordinateMax);
}

static int ScaleCoordinateXToDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolution(&width, &height);
    return (ClampInt(value, 0, kCoordinateMax) * width + kCoordinateMax / 2) /
        kCoordinateMax;
}

static int ScaleCoordinateYToDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolution(&width, &height);
    return (ClampInt(value, 0, kCoordinateMax) * height + kCoordinateMax / 2) /
        kCoordinateMax;
}

static int ScaleCoordinateXFromDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolution(&width, &height);
    if (value <= 0 || width <= 0) {
        return 0;
    }
    return ClampInt((value * kCoordinateMax + width / 2) / width, 0, kCoordinateMax);
}

static int ScaleCoordinateYFromDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolution(&width, &height);
    if (value <= 0 || height <= 0) {
        return 0;
    }
    return ClampInt((value * kCoordinateMax + height / 2) / height, 0, kCoordinateMax);
}

static int LineStepToCoordinate(float lineSpace, int fontSize, int canvasHeight)
{
    const float gap = (lineSpace > 0.0f) ? lineSpace : 0.0f;
    return CharMarginToCoordinate(1.0f + gap, fontSize, canvasHeight);
}

static std::string AlignmentFromCmiot(cmiotOsdPosAlignSetting_e alignSet)
{
    return (alignSet == CMIOT_OSD_POS_ALIGN_RIGHT) ? "right" : "left";
}

static int XFromCmiotPosition(const cmiotOsdPosInfo_t& pos, int fontSize, int canvasWidth)
{
    if (pos.alignSet == CMIOT_OSD_POS_ALIGN_LEFT) {
        return CharMarginToCoordinate(pos.alignPos, fontSize, canvasWidth);
    }
    if (pos.alignSet == CMIOT_OSD_POS_ALIGN_RIGHT) {
        return kCoordinateMax - CharMarginToCoordinate(pos.alignPos, fontSize, canvasWidth);
    }
    return (int)ClampCoordinate(pos.x);
}

static void FillTextItemPosition(media::VideoOsdTextItem* item,
                                 const cmiotOsdPosInfo_t& pos,
                                 int fontSize)
{
    if (item == NULL) {
        return;
    }

    int canvasWidth = 0;
    int canvasHeight = 0;
    QueryMainStreamResolution(&canvasWidth, &canvasHeight);

    item->has_position = true;
    item->x = XFromCmiotPosition(pos, fontSize, canvasWidth);
    item->y = (int)ClampCoordinate(pos.y);
    item->has_alignment = true;
    item->alignment = AlignmentFromCmiot(pos.alignSet);
}

static void FillCmiotPositionFromTextItem(cmiotOsdPosInfo_t* pos,
                                          const media::VideoOsdTextItem& item)
{
    if (pos == NULL) {
        return;
    }
    memset(pos, 0, sizeof(*pos));
    pos->x = item.has_position ? (cmiot_uint32_t)ClampInt(item.x, 0, kCoordinateMax) : 0;
    pos->y = item.has_position ? (cmiot_uint32_t)ClampInt(item.y, 0, kCoordinateMax) : 0;
    if (item.has_alignment && ToLowerCopy(item.alignment) == "right") {
        pos->alignSet = CMIOT_OSD_POS_ALIGN_RIGHT;
    } else {
        pos->alignSet = CMIOT_OSD_POS_ALIGN_NOT_SET;
    }
    pos->alignPos = 0.0f;
}

static int AppendTextItem(media::VideoOsdState* state,
                          const std::string& text,
                          const cmiotOsdPosInfo_t& pos,
                          int fontSize)
{
    if (state == NULL || text.empty()) {
        return 0;
    }
    if (state->text_items.size() >= CMIOT_APP_OSD_TEXT_MAX) {
        return 0;
    }

    media::VideoOsdTextItem item;
    item.has_text = true;
    item.text = text;
    FillTextItemPosition(&item, pos, fontSize);
    state->text_items.push_back(item);
    state->has_text_items = true;
    return 1;
}

static void BuildGbDatePosition(const cmiotGBOsdPosInfo_t& gbPos,
                                int fontSize,
                                int* x,
                                int* y)
{
    int canvasWidth = 0;
    int canvasHeight = 0;
    QueryMainStreamResolution(&canvasWidth, &canvasHeight);
    *x = kCoordinateMax - CharMarginToCoordinate(gbPos.sideSpace, fontSize, canvasWidth);
    *y = CharMarginToCoordinate(gbPos.vertSpace, fontSize, canvasHeight);
}

static void AppendGbTextList(media::VideoOsdState* state,
                             const cmiotOsdGBTextInfoList_t& list,
                             bool rightAlign,
                             int fontSize)
{
    if (state == NULL || list.text == NULL || list.textNum == 0) {
        return;
    }

    int canvasWidth = 0;
    int canvasHeight = 0;
    QueryMainStreamResolution(&canvasWidth, &canvasHeight);

    const cmiot_uint32_t remaining =
        (state->text_items.size() >= CMIOT_APP_OSD_TEXT_MAX) ?
            0U :
            (cmiot_uint32_t)(CMIOT_APP_OSD_TEXT_MAX - state->text_items.size());
    const cmiot_uint32_t count = std::min(list.textNum, remaining);
    const int xMargin = CharMarginToCoordinate(list.textPos.sideSpace, fontSize, canvasWidth);
    const int yBottomMargin = CharMarginToCoordinate(list.textPos.vertSpace, fontSize, canvasHeight);
    const int lineStep = std::max(1, LineStepToCoordinate(list.textPos.lineSpace, fontSize, canvasHeight));
    const int blockHeight = (count > 0) ? (int)(lineStep * (int)count) : 0;
    const int firstY = ClampInt(kCoordinateMax - yBottomMargin - blockHeight, 0, kCoordinateMax);

    for (cmiot_uint32_t i = 0; i < count; ++i) {
        const std::string text = TrimCopy(SafeString(list.text[i].content, sizeof(list.text[i].content)));
        if (text.empty()) {
            continue;
        }

        media::VideoOsdTextItem item;
        item.has_text = true;
        item.text = text;
        item.has_position = true;
        item.x = rightAlign ? (kCoordinateMax - xMargin) : xMargin;
        item.y = ClampInt(firstY + (int)i * lineStep, 0, kCoordinateMax);
        item.has_alignment = true;
        item.alignment = rightAlign ? "right" : "left";
        state->text_items.push_back(item);
        state->has_text_items = true;
        if (state->text_items.size() >= CMIOT_APP_OSD_TEXT_MAX) {
            break;
        }
    }
}

static int BuildMediaStateFromCmiot(const cmiotOSDInfo_t& info,
                                    media::VideoOsdState* state,
                                    CachedCmiotOsd* cache)
{
    if (state == NULL || cache == NULL) {
        return -1;
    }

    *state = media::VideoOsdState();
    *cache = CachedCmiotOsd();

    const bool osdEnabled = (info.osdSwitch != 0);
    if (info.mode != kCmiotOsdModeCustom && info.mode != kCmiotOsdModeGb) {
        return -1;
    }
    const int fontSize = NormalizeFontSize((int)info.fontSize);
    std::string fontColorMode;
    std::string fontColor;
    std::string cmiotFontColor;
    if (!NormalizeFontColor(info.fontColor, &fontColorMode, &fontColor, &cmiotFontColor)) {
        return -1;
    }

    state->has_font_size = true;
    state->font_size = fontSize;
    state->has_font_color_mode = true;
    state->font_color_mode = fontColorMode;
    state->has_font_color = true;
    state->font_color = fontColor;
    state->has_time_enabled = true;
    state->time_enabled = osdEnabled && IsStatusOn(info.date.status) ? 1 : 0;
    state->has_text_enabled = true;
    state->text_enabled = 0;
    state->has_time_format = true;
    state->time_format = BuildTimeFormat(info.date);
    state->has_time_display_week_enabled = true;
    state->time_display_week_enabled = (info.date.weekday != 0) ? 1 : 0;

    cache->valid = true;
    cache->info.osdSwitch = osdEnabled ? CMIOT_TRUE : CMIOT_FALSE;
    cache->info.mode = (info.mode == kCmiotOsdModeGb) ? kCmiotOsdModeGb : kCmiotOsdModeCustom;
    cache->info.date = info.date;
    cache->info.fontSize = (cmiot_uint32_t)fontSize;
    CopyString(cache->info.fontColor, sizeof(cache->info.fontColor), cmiotFontColor);

    if (!osdEnabled) {
        state->text_items.clear();
        state->has_text_items = true;
        state->text_enabled = 0;
        if (cache->info.mode == kCmiotOsdModeGb) {
            cache->info.osdText.gbText.districtText.text = cache->districtText;
            cache->info.osdText.gbText.districtText.textNum = 0;
            cache->info.osdText.gbText.additionText.text = cache->additionText;
            cache->info.osdText.gbText.additionText.textNum = 0;
        } else {
            cache->info.osdText.customText.text = cache->customText;
            cache->info.osdText.customText.textNum = 0;
        }
        return 0;
    }

    if (info.mode == kCmiotOsdModeGb) {
        const cmiot_uint32_t districtCount = std::min(info.osdText.gbText.districtText.textNum,
                                                      (cmiot_uint32_t)CMIOT_APP_OSD_TEXT_MAX);
        cmiot_uint32_t additionCapacity = CMIOT_APP_OSD_TEXT_MAX - districtCount;
        const cmiot_uint32_t additionCount = std::min(info.osdText.gbText.additionText.textNum,
                                                      additionCapacity);
        if ((districtCount > 0 && info.osdText.gbText.districtText.text == NULL) ||
            (additionCount > 0 && info.osdText.gbText.additionText.text == NULL)) {
            return -1;
        }

        int timeX = kCoordinateMax;
        int timeY = 0;
        BuildGbDatePosition(info.date.gbPos, fontSize, &timeX, &timeY);
        state->has_time_position = true;
        state->time_x = timeX;
        state->time_y = timeY;
        state->has_time_alignment = true;
        state->time_alignment = "right";

        AppendGbTextList(state, info.osdText.gbText.districtText, true, fontSize);
        AppendGbTextList(state, info.osdText.gbText.additionText, false, fontSize);
        state->text_enabled = state->text_items.empty() ? 0 : 1;

        for (cmiot_uint32_t i = 0; i < districtCount; ++i) {
            cache->districtText[i] = info.osdText.gbText.districtText.text[i];
        }
        for (cmiot_uint32_t i = 0; i < additionCount; ++i) {
            cache->additionText[i] = info.osdText.gbText.additionText.text[i];
        }
        cache->info.osdText.gbText.districtText.textNum = districtCount;
        cache->info.osdText.gbText.districtText.text = cache->districtText;
        cache->info.osdText.gbText.districtText.textPos = info.osdText.gbText.districtText.textPos;
        cache->info.osdText.gbText.additionText.textNum = additionCount;
        cache->info.osdText.gbText.additionText.text = cache->additionText;
        cache->info.osdText.gbText.additionText.textPos = info.osdText.gbText.additionText.textPos;
        return 0;
    }

    state->has_time_position = true;
    int canvasWidth = 0;
    QueryMainStreamResolution(&canvasWidth, NULL);
    state->time_x = XFromCmiotPosition(info.date.pos, fontSize, canvasWidth);
    state->time_y = (int)ClampCoordinate(info.date.pos.y);
    state->has_time_alignment = true;
    state->time_alignment = AlignmentFromCmiot(info.date.pos.alignSet);

    if (info.osdText.customText.textNum > 0 && info.osdText.customText.text == NULL) {
        return -1;
    }

    const cmiot_uint32_t textCount =
        std::min(info.osdText.customText.textNum, (cmiot_uint32_t)CMIOT_APP_OSD_TEXT_MAX);
    cmiot_uint32_t appliedCount = 0;
    for (cmiot_uint32_t i = 0; i < textCount; ++i) {
        const cmiotOsdTextInfo_t& textInfo = info.osdText.customText.text[i];
        cache->customText[appliedCount] = textInfo;
        const std::string text = TrimCopy(SafeString(textInfo.content, sizeof(textInfo.content)));
        if (AppendTextItem(state, text, textInfo.pos, fontSize) > 0) {
            ++appliedCount;
        }
    }
    state->text_enabled = state->text_items.empty() ? 0 : 1;
    cache->info.osdText.customText.textNum = appliedCount;
    cache->info.osdText.customText.text = cache->customText;
    return 0;
}

static int NormalizeAlignmentValue(const std::string& alignment)
{
    return (ToLowerCopy(TrimCopy(alignment)) == "right") ? 1 : 0;
}

static bool IsCmiotTextConfigChanged(const OSDTextAllConf_S& left,
                                     const OSDTextAllConf_S& right)
{
    for (int i = 0; i < CMIOT_APP_OSD_TEXT_MAX; ++i) {
        if (left.osd_text[i].text != right.osd_text[i].text ||
            left.osd_text[i].x != right.osd_text[i].x ||
            left.osd_text[i].y != right.osd_text[i].y ||
            left.osd_text[i].show != right.osd_text[i].show ||
            left.osd_text[i].alignment != right.osd_text[i].alignment) {
            return true;
        }
    }
    return false;
}

static int ApplyCmiotTextConfig(const media::VideoOsdState& state)
{
    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_OSD_TEXT), table)) {
        return -1;
    }

    OSDTextAllConf_S curTextConfig;
    OSDTextAllConf_S nextTextConfig;
    TExchangeAL<OSDTextAllConf_S>::getConfig(table, curTextConfig);
    nextTextConfig = curTextConfig;

    if (state.has_text_items) {
        int i = 0;
        for (i = 0;
             i < CMIOT_APP_OSD_TEXT_MAX && i < (int)state.text_items.size();
             ++i) {
            const media::VideoOsdTextItem& item = state.text_items[i];
            if (item.has_text) {
                nextTextConfig.osd_text[i].text = HexEncodeString(item.text);
            }
            if (item.has_position) {
                nextTextConfig.osd_text[i].x = ScaleCoordinateXToDevice(item.x);
                nextTextConfig.osd_text[i].y = ScaleCoordinateYToDevice(item.y);
            }
            if (item.has_alignment) {
                nextTextConfig.osd_text[i].alignment = NormalizeAlignmentValue(item.alignment);
            }
            const bool hasText = !nextTextConfig.osd_text[i].text.empty();
            nextTextConfig.osd_text[i].show =
                ((!state.has_text_enabled || state.text_enabled != 0) && hasText) ? 1 : 0;
        }
        for (; i < CMIOT_APP_OSD_TEXT_MAX; ++i) {
            nextTextConfig.osd_text[i].show = 0;
        }
    } else if (state.has_text_enabled) {
        for (int i = 0; i < CMIOT_APP_OSD_TEXT_MAX; ++i) {
            nextTextConfig.osd_text[i].show =
                (state.text_enabled != 0 && !nextTextConfig.osd_text[i].text.empty()) ? 1 : 0;
        }
    }

    if (!IsCmiotTextConfigChanged(curTextConfig, nextTextConfig)) {
        return 0;
    }

    table.clear();
    TExchangeAL<OSDTextAllConf_S>::setConfig(nextTextConfig, table);
    const int ret = g_configManager.setConfig(getConfigName(CFG_OSD_TEXT),
                                              table,
                                              0,
                                              IConfigManager::applyOK);
    return (ret == IConfigManager::applyOK) ? 0 : ret;
}

static cmiot_uint32_t FillCmiotCustomTextFromConfig(cmiotOSDInfo_t* out,
                                                    cmiotOsdTextInfo_t* textOut,
                                                    cmiot_uint32_t textCapacity)
{
    if (out == NULL) {
        return 0;
    }

    cmiot_uint32_t availableCount = 0;
    cmiot_uint32_t copiedCount = 0;
    CConfigTable table;
    OSDTextAllConf_S curTextConfig;
    if (g_configManager.getConfig(getConfigName(CFG_OSD_TEXT), table)) {
        TExchangeAL<OSDTextAllConf_S>::getConfig(table, curTextConfig);
        for (int i = 0; i < CMIOT_APP_OSD_TEXT_MAX; ++i) {
            if (!curTextConfig.osd_text[i].show) {
                continue;
            }

            if (textOut != NULL && copiedCount < textCapacity) {
                cmiotOsdTextInfo_t& item = textOut[copiedCount];
                memset(&item, 0, sizeof(item));
                CopyString(item.content,
                           sizeof(item.content),
                           HexDecodeString(curTextConfig.osd_text[i].text));
                item.pos.x = (cmiot_uint32_t)ScaleCoordinateXFromDevice(curTextConfig.osd_text[i].x);
                item.pos.y = (cmiot_uint32_t)ScaleCoordinateYFromDevice(curTextConfig.osd_text[i].y);
                item.pos.alignSet =
                    (curTextConfig.osd_text[i].alignment == 1) ?
                        CMIOT_OSD_POS_ALIGN_RIGHT :
                        CMIOT_OSD_POS_ALIGN_NOT_SET;
                item.pos.alignPos = 0.0f;
                ++copiedCount;
            }
            ++availableCount;
        }
    }

    out->osdText.customText.text = textOut;
    out->osdText.customText.textNum =
        (textOut == NULL || textCapacity == 0) ? availableCount : copiedCount;
    return availableCount;
}

static void StoreCache(const CachedCmiotOsd& cache)
{
    pthread_mutex_lock(&g_cmiot_osd_cache_mutex);
    g_cmiot_osd_cache = cache;
    if (g_cmiot_osd_cache.info.mode == kCmiotOsdModeGb) {
        g_cmiot_osd_cache.info.osdText.gbText.districtText.text = g_cmiot_osd_cache.districtText;
        g_cmiot_osd_cache.info.osdText.gbText.additionText.text = g_cmiot_osd_cache.additionText;
    } else {
        g_cmiot_osd_cache.info.osdText.customText.text = g_cmiot_osd_cache.customText;
    }
    pthread_mutex_unlock(&g_cmiot_osd_cache_mutex);
}

static cmiot_uint32_t CopyCustomTextList(cmiotOsdTextInfo_t* dst,
                                         cmiot_uint32_t capacity,
                                         const cmiotOsdTextInfo_t* src,
                                         cmiot_uint32_t count)
{
    if (dst == NULL || capacity == 0 || src == NULL || count == 0) {
        return 0;
    }
    const cmiot_uint32_t copyCount = std::min(capacity, count);
    for (cmiot_uint32_t i = 0; i < copyCount; ++i) {
        dst[i] = src[i];
    }
    return copyCount;
}

static cmiot_uint32_t CopyGbTextList(cmiotGBOsdTextInfo_t* dst,
                                     cmiot_uint32_t capacity,
                                     const cmiotGBOsdTextInfo_t* src,
                                     cmiot_uint32_t count)
{
    if (dst == NULL || capacity == 0 || src == NULL || count == 0) {
        return 0;
    }
    const cmiot_uint32_t copyCount = std::min(capacity, count);
    for (cmiot_uint32_t i = 0; i < copyCount; ++i) {
        dst[i] = src[i];
    }
    return copyCount;
}

static int CopyCacheToOutput(const CachedCmiotOsd& cache, cmiotOSDInfo_t* out)
{
    if (out == NULL || !cache.valid) {
        return -1;
    }

    const cmiot_uint8_t requestedMode = out->mode;
    cmiotOsdTextInfo_t* customTextOut = NULL;
    cmiot_uint32_t customCapacity = 0;
    cmiotGBOsdTextInfo_t* districtTextOut = NULL;
    cmiot_uint32_t districtCapacity = 0;
    cmiotGBOsdTextInfo_t* additionTextOut = NULL;
    cmiot_uint32_t additionCapacity = 0;

    if (requestedMode == kCmiotOsdModeGb) {
        districtTextOut = out->osdText.gbText.districtText.text;
        districtCapacity = out->osdText.gbText.districtText.textNum;
        additionTextOut = out->osdText.gbText.additionText.text;
        additionCapacity = out->osdText.gbText.additionText.textNum;
    } else if (requestedMode == kCmiotOsdModeCustom) {
        customTextOut = out->osdText.customText.text;
        customCapacity = out->osdText.customText.textNum;
    }

    *out = cache.info;

    if (cache.info.mode == kCmiotOsdModeGb) {
        const cmiot_uint32_t districtCount = cache.info.osdText.gbText.districtText.textNum;
        const cmiot_uint32_t additionCount = cache.info.osdText.gbText.additionText.textNum;
        const cmiot_uint32_t districtCopied =
            CopyGbTextList(districtTextOut, districtCapacity, cache.districtText, districtCount);
        const cmiot_uint32_t additionCopied =
            CopyGbTextList(additionTextOut, additionCapacity, cache.additionText, additionCount);
        out->osdText.gbText.districtText.text = districtTextOut;
        out->osdText.gbText.districtText.textNum =
            (districtTextOut == NULL || districtCapacity == 0) ? districtCount : districtCopied;
        out->osdText.gbText.additionText.text = additionTextOut;
        out->osdText.gbText.additionText.textNum =
            (additionTextOut == NULL || additionCapacity == 0) ? additionCount : additionCopied;
        return ((districtTextOut != NULL && districtCopied < districtCount) ||
                (additionTextOut != NULL && additionCopied < additionCount)) ? -2 : 0;
    }

    const cmiot_uint32_t customCount = cache.info.osdText.customText.textNum;
    const cmiot_uint32_t customCopied =
        CopyCustomTextList(customTextOut, customCapacity, cache.customText, customCount);
    out->osdText.customText.text = customTextOut;
    out->osdText.customText.textNum =
        (customTextOut == NULL || customCapacity == 0) ? customCount : customCopied;
    return (customTextOut != NULL && customCopied < customCount) ? -2 : 0;
}

static void FillCmiotFromMediaState(cmiotOSDInfo_t* out,
                                    const media::VideoOsdState& state,
                                    cmiotOsdTextInfo_t* textOut,
                                    cmiot_uint32_t textCapacity)
{
    memset(out, 0, sizeof(*out));
    out->osdSwitch =
        ((state.has_time_enabled && state.time_enabled != 0) ||
         (state.has_text_enabled && state.text_enabled != 0)) ? CMIOT_TRUE : CMIOT_FALSE;
    out->mode = kCmiotOsdModeCustom;
    SetStatus(out->date.status, state.has_time_enabled && state.time_enabled != 0);
    CopyString(out->date.dateFormat, sizeof(out->date.dateFormat), BuildDateFormatFromState(state));
    out->date.timeFormat = BuildTimeFormatFromState(state);
    out->date.weekday =
        (state.has_time_display_week_enabled && state.time_display_week_enabled != 0) ? 1 : 0;
    out->date.pos.x = state.has_time_position ? (cmiot_uint32_t)ClampInt(state.time_x, 0, kCoordinateMax) : 0;
    out->date.pos.y = state.has_time_position ? (cmiot_uint32_t)ClampInt(state.time_y, 0, kCoordinateMax) : 0;
    out->date.pos.alignSet =
        (state.has_time_alignment && ToLowerCopy(state.time_alignment) == "right") ?
            CMIOT_OSD_POS_ALIGN_RIGHT :
            CMIOT_OSD_POS_ALIGN_NOT_SET;
    out->fontSize = state.has_font_size ? (cmiot_uint32_t)state.font_size : 32;
    if (state.has_font_color_mode && ToLowerCopy(state.font_color_mode) == "auto") {
        CopyString(out->fontColor, sizeof(out->fontColor), "auto");
    } else if (state.has_font_color && ToLowerCopy(state.font_color) == "#000000") {
        CopyString(out->fontColor, sizeof(out->fontColor), "black");
    } else if (state.has_font_color && ToLowerCopy(state.font_color) == "#ffffff") {
        CopyString(out->fontColor, sizeof(out->fontColor), "white");
    } else if (state.has_font_color) {
        CopyString(out->fontColor, sizeof(out->fontColor), state.font_color);
    } else {
        CopyString(out->fontColor, sizeof(out->fontColor), "white");
    }

    const cmiot_uint32_t availableCount =
        (state.has_text_items && state.has_text_enabled && state.text_enabled != 0) ?
            (cmiot_uint32_t)std::min(state.text_items.size(), (size_t)CMIOT_APP_OSD_TEXT_MAX) :
            0U;
    const cmiot_uint32_t copyCount = std::min(availableCount, textCapacity);
    const cmiot_uint32_t safeCopyCount = (textOut == NULL) ? 0 : copyCount;
    for (cmiot_uint32_t i = 0; i < safeCopyCount; ++i) {
        memset(&textOut[i], 0, sizeof(textOut[i]));
        CopyString(textOut[i].content, sizeof(textOut[i].content), state.text_items[i].text);
        FillCmiotPositionFromTextItem(&textOut[i].pos, state.text_items[i]);
    }
    out->osdText.customText.text = textOut;
    out->osdText.customText.textNum =
        (textOut == NULL || textCapacity == 0) ? availableCount : safeCopyCount;
}

} // namespace

extern "C" int cmiot_osd_get_text_capacity(void)
{
    return CMIOT_APP_OSD_TEXT_MAX;
}

extern "C" int cmiot_osd_set_config(const cmiotOSDInfo_t *info)
{
    if (info == NULL) {
        return -1;
    }

    media::VideoOsdState state;
    CachedCmiotOsd cache;
    const int buildRet = BuildMediaStateFromCmiot(*info, &state, &cache);
    if (buildRet != 0) {
        return buildRet;
    }

    media::VideoOsdState timeState = state;
    timeState.has_text_enabled = false;
    timeState.has_text_items = false;
    timeState.text_items.clear();

    const int applyRet = media::ApplyVideoOsdConfig(timeState);
    if (applyRet != 0) {
        return applyRet;
    }

    const int textRet = ApplyCmiotTextConfig(state);
    if (textRet != 0) {
        return textRet;
    }

    StoreCache(cache);
    return 0;
}

extern "C" int cmiot_osd_get_config(cmiotOSDInfo_t *info)
{
    if (info == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_cmiot_osd_cache_mutex);
    const CachedCmiotOsd cache = g_cmiot_osd_cache;
    pthread_mutex_unlock(&g_cmiot_osd_cache_mutex);

    if (cache.valid) {
        return CopyCacheToOutput(cache, info);
    }

    const cmiot_uint8_t requestedMode = info->mode;
    cmiotOsdTextInfo_t* customTextOut = NULL;
    cmiot_uint32_t customCapacity = 0;
    if (requestedMode == kCmiotOsdModeCustom) {
        customTextOut = info->osdText.customText.text;
        customCapacity = info->osdText.customText.textNum;
    }

    media::VideoOsdState state;
    if (!media::QueryVideoOsdState(&state)) {
        return -1;
    }
    FillCmiotFromMediaState(info, state, NULL, 0);
    const cmiot_uint32_t availableCount =
        FillCmiotCustomTextFromConfig(info, customTextOut, customCapacity);
    return (customTextOut != NULL &&
            info->osdText.customText.textNum < availableCount) ? -2 : 0;
}
