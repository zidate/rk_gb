/* 保存并应用 cmiot 独立 OSD 配置，不复用国标 OSD 中间态。 */
#include "CmiotOsdControl.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "Common.h"
#include "Manager/CloudPlatformControl.h"
#include "Media/AVManager.h"
#include "Media/NormalizedOsdControl.h"

namespace
{

const int kCoordinateMax = 10000;
const int kDeviceTextMax = 7;
const cmiot_uint8_t kCmiotOsdModeCustom = 1;
const cmiot_uint8_t kCmiotOsdModeGb = 2;

struct StoredCmiotOsd
{
    bool valid;
    cmiot_bool_t osdSwitch;
    cmiot_uint8_t mode;
    cmiotOsdDateInfo_t date;
    cmiot_uint32_t fontSize;
    char fontColor[16];
    cmiot_uint32_t customCount;
    cmiotOsdTextInfo_t customText[CMIOT_APP_OSD_TEXT_MAX];
    cmiot_uint32_t districtCount;
    cmiotGBOsdTextInfo_t districtText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdPosInfo_t districtPos;
    cmiot_uint32_t additionCount;
    cmiotGBOsdTextInfo_t additionText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdPosInfo_t additionPos;

    StoredCmiotOsd()
        : valid(false),
          osdSwitch(CMIOT_FALSE),
          mode(kCmiotOsdModeCustom),
          fontSize(32),
          customCount(0),
          districtCount(0),
          additionCount(0)
    {
        memset(&date, 0, sizeof(date));
        memset(fontColor, 0, sizeof(fontColor));
        memset(customText, 0, sizeof(customText));
        memset(districtText, 0, sizeof(districtText));
        memset(&districtPos, 0, sizeof(districtPos));
        memset(additionText, 0, sizeof(additionText));
        memset(&additionPos, 0, sizeof(additionPos));
    }
};

pthread_mutex_t g_cmiot_osd_mutex = PTHREAD_MUTEX_INITIALIZER;
StoredCmiotOsd g_cmiot_osd;
bool g_cmiot_osd_loaded = false;

size_t BoundedStringLength(const char* text, size_t maxLen)
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

std::string SafeString(const char* text, size_t maxLen)
{
    return std::string(text ? text : "", BoundedStringLength(text, maxLen));
}

void CopyString(char* dst, size_t dstSize, const std::string& src)
{
    if (dst == NULL || dstSize == 0) {
        return;
    }
    snprintf(dst, dstSize, "%s", src.c_str());
}

std::string ToLowerCopy(const std::string& text)
{
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
        }
    }
    return out;
}

bool IsHexDigit(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

bool IsFiniteNonNegative(float value)
{
    return value == value && value >= 0.0f &&
           value <= static_cast<float>(kCoordinateMax);
}

bool IsPositionValid(const cmiotOsdPosInfo_t& pos)
{
    return pos.x <= static_cast<cmiot_uint32_t>(kCoordinateMax) &&
           pos.y <= static_cast<cmiot_uint32_t>(kCoordinateMax) &&
           pos.alignSet >= CMIOT_OSD_POS_ALIGN_NOT_SET &&
           pos.alignSet <= CMIOT_OSD_POS_ALIGN_RIGHT &&
           IsFiniteNonNegative(pos.alignPos);
}

bool IsGbPositionValid(const cmiotGBOsdPosInfo_t& pos)
{
    return IsFiniteNonNegative(pos.sideSpace) &&
           IsFiniteNonNegative(pos.vertSpace) &&
           IsFiniteNonNegative(pos.lineSpace);
}

bool IsFontSizeValid(cmiot_uint32_t fontSize)
{
    return fontSize >= 16 && fontSize <= 128 && (fontSize % 8) == 0;
}

bool IsFontColorValid(const char* colorText)
{
    const std::string color = SafeString(colorText, 16);
    const std::string lower = ToLowerCopy(color);
    if (lower == "black" || lower == "white" || lower == "auto") {
        return true;
    }
    if (color.size() != 7 || color[0] != '#') {
        return false;
    }
    for (size_t i = 1; i < color.size(); ++i) {
        if (!IsHexDigit(color[i])) {
            return false;
        }
    }
    return true;
}

bool IsDateFormatValid(const char* dateFormat)
{
    const std::string value = SafeString(dateFormat, 24);
    return value == "YYYY-MM-DD" || value == "YYYY.MM.DD" ||
           value == "YYYY/MM/DD" || value == "YYYY年MM月DD日";
}

bool IsDateStatusValid(const char* status)
{
    const std::string value = ToLowerCopy(SafeString(status, 4));
    return value == "on" || value == "off";
}

int ValidateInput(const cmiotOSDInfo_t& info)
{
    if (info.osdSwitch == 0) {
        return 0;
    }
    if (info.osdSwitch != 1 ||
        (info.mode != kCmiotOsdModeCustom && info.mode != kCmiotOsdModeGb) ||
        !IsDateStatusValid(info.date.status) ||
        !IsDateFormatValid(info.date.dateFormat) ||
        (info.date.timeFormat != 12 && info.date.timeFormat != 24) ||
        info.date.weekday > 1 ||
        !IsFontSizeValid(info.fontSize) ||
        !IsFontColorValid(info.fontColor)) {
        return -1;
    }

    if ((info.mode == kCmiotOsdModeCustom && !IsPositionValid(info.date.pos)) ||
        (info.mode == kCmiotOsdModeGb && !IsGbPositionValid(info.date.gbPos))) {
        return -1;
    }

    if (info.mode == kCmiotOsdModeCustom) {
        if (info.osdText.customText.textNum > CMIOT_APP_OSD_TEXT_MAX ||
            (info.osdText.customText.textNum > 0 &&
             info.osdText.customText.text == NULL)) {
            return -1;
        }
        for (cmiot_uint32_t i = 0; i < info.osdText.customText.textNum; ++i) {
            const cmiotOsdTextInfo_t& text = info.osdText.customText.text[i];
            if (BoundedStringLength(text.content, sizeof(text.content)) >= sizeof(text.content) ||
                !IsPositionValid(text.pos)) {
                return -1;
            }
        }
        return 0;
    }

    const cmiot_uint32_t district = info.osdText.gbText.districtText.textNum;
    const cmiot_uint32_t addition = info.osdText.gbText.additionText.textNum;
    if (district > CMIOT_APP_OSD_TEXT_MAX ||
        addition > CMIOT_APP_OSD_TEXT_MAX ||
        district > CMIOT_APP_OSD_TEXT_MAX - addition ||
        (district > 0 && info.osdText.gbText.districtText.text == NULL) ||
        (addition > 0 && info.osdText.gbText.additionText.text == NULL) ||
        !IsGbPositionValid(info.osdText.gbText.districtText.textPos) ||
        !IsGbPositionValid(info.osdText.gbText.additionText.textPos)) {
        return -1;
    }
    for (cmiot_uint32_t i = 0; i < district; ++i) {
        const cmiotGBOsdTextInfo_t& text = info.osdText.gbText.districtText.text[i];
        if (BoundedStringLength(text.content, sizeof(text.content)) >= sizeof(text.content)) {
            return -1;
        }
    }
    for (cmiot_uint32_t i = 0; i < addition; ++i) {
        const cmiotGBOsdTextInfo_t& text = info.osdText.gbText.additionText.text[i];
        if (BoundedStringLength(text.content, sizeof(text.content)) >= sizeof(text.content)) {
            return -1;
        }
    }
    return 0;
}

void CopyInput(const cmiotOSDInfo_t& info, StoredCmiotOsd* out)
{
    if (out == NULL) {
        return;
    }
    if (info.osdSwitch == 0) {
        out->valid = true;
        out->osdSwitch = CMIOT_FALSE;
        return;
    }

    StoredCmiotOsd next;
    next.valid = true;
    next.osdSwitch = CMIOT_TRUE;
    next.mode = info.mode;
    next.date = info.date;
    next.date.status[sizeof(next.date.status) - 1] = '\0';
    next.date.dateFormat[sizeof(next.date.dateFormat) - 1] = '\0';
    next.fontSize = info.fontSize;
    CopyString(next.fontColor, sizeof(next.fontColor),
               SafeString(info.fontColor, sizeof(info.fontColor)));

    if (info.mode == kCmiotOsdModeCustom) {
        next.customCount = info.osdText.customText.textNum;
        for (cmiot_uint32_t i = 0; i < next.customCount; ++i) {
            next.customText[i] = info.osdText.customText.text[i];
            next.customText[i].content[sizeof(next.customText[i].content) - 1] = '\0';
        }
    } else {
        next.districtCount = info.osdText.gbText.districtText.textNum;
        next.districtPos = info.osdText.gbText.districtText.textPos;
        for (cmiot_uint32_t i = 0; i < next.districtCount; ++i) {
            next.districtText[i] = info.osdText.gbText.districtText.text[i];
            next.districtText[i].content[sizeof(next.districtText[i].content) - 1] = '\0';
        }
        next.additionCount = info.osdText.gbText.additionText.textNum;
        next.additionPos = info.osdText.gbText.additionText.textPos;
        for (cmiot_uint32_t i = 0; i < next.additionCount; ++i) {
            next.additionText[i] = info.osdText.gbText.additionText.text[i];
            next.additionText[i].content[sizeof(next.additionText[i].content) - 1] = '\0';
        }
    }
    *out = next;
}

void PositionToJson(const cmiotOsdPosInfo_t& pos, Json::Value* value)
{
    (*value)["x"] = pos.x;
    (*value)["y"] = pos.y;
    (*value)["align_set"] = static_cast<int>(pos.alignSet);
    (*value)["align_pos"] = pos.alignPos;
}

void GbPositionToJson(const cmiotGBOsdPosInfo_t& pos, Json::Value* value)
{
    (*value)["side_space"] = pos.sideSpace;
    (*value)["vert_space"] = pos.vertSpace;
    (*value)["line_space"] = pos.lineSpace;
}

void PositionFromJson(const Json::Value& value, cmiotOsdPosInfo_t* pos)
{
    pos->x = value["x"].asUInt();
    pos->y = value["y"].asUInt();
    pos->alignSet = static_cast<cmiotOsdPosAlignSetting_e>(value["align_set"].asInt());
    pos->alignPos = static_cast<float>(value["align_pos"].asDouble());
}

void GbPositionFromJson(const Json::Value& value, cmiotGBOsdPosInfo_t* pos)
{
    pos->sideSpace = static_cast<float>(value["side_space"].asDouble());
    pos->vertSpace = static_cast<float>(value["vert_space"].asDouble());
    pos->lineSpace = static_cast<float>(value["line_space"].asDouble());
}

int SaveStoredConfig(const StoredCmiotOsd& config)
{
    CConfigTable table;
    table["valid"] = config.valid;
    table["osd_switch"] = static_cast<int>(config.osdSwitch);
    table["mode"] = config.mode;
    table["date"]["status"] = SafeString(config.date.status, sizeof(config.date.status));
    table["date"]["date_format"] = SafeString(config.date.dateFormat,
                                                sizeof(config.date.dateFormat));
    table["date"]["time_format"] = config.date.timeFormat;
    table["date"]["weekday"] = config.date.weekday;
    PositionToJson(config.date.pos, &table["date"]["pos"]);
    GbPositionToJson(config.date.gbPos, &table["date"]["gb_pos"]);
    table["font_size"] = config.fontSize;
    table["font_color"] = SafeString(config.fontColor, sizeof(config.fontColor));

    table["custom_text"] = Json::arrayValue;
    for (cmiot_uint32_t i = 0; i < config.customCount; ++i) {
        Json::Value item;
        item["content"] = SafeString(config.customText[i].content,
                                      sizeof(config.customText[i].content));
        PositionToJson(config.customText[i].pos, &item["pos"]);
        table["custom_text"].append(item);
    }

    table["district_text"] = Json::arrayValue;
    for (cmiot_uint32_t i = 0; i < config.districtCount; ++i) {
        Json::Value item;
        item["content"] = SafeString(config.districtText[i].content,
                                      sizeof(config.districtText[i].content));
        table["district_text"].append(item);
    }
    GbPositionToJson(config.districtPos, &table["district_pos"]);

    table["addition_text"] = Json::arrayValue;
    for (cmiot_uint32_t i = 0; i < config.additionCount; ++i) {
        Json::Value item;
        item["content"] = SafeString(config.additionText[i].content,
                                      sizeof(config.additionText[i].content));
        table["addition_text"].append(item);
    }
    GbPositionToJson(config.additionPos, &table["addition_pos"]);

    const int ret = g_configManager.setConfig(getConfigName(CFG_CMIOT_OSD),
                                              table, 0, IConfigManager::applyOK);
    return ret == IConfigManager::applyOK ? 0 : ret;
}

int LoadStoredConfig(StoredCmiotOsd* config)
{
    if (config == NULL) {
        return -1;
    }
    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_CMIOT_OSD), table)) {
        return -1;
    }

    StoredCmiotOsd loaded;
    loaded.valid = table["valid"].asBool();
    loaded.osdSwitch = table["osd_switch"].asInt() ? CMIOT_TRUE : CMIOT_FALSE;
    loaded.mode = static_cast<cmiot_uint8_t>(table["mode"].asUInt());
    if (!loaded.valid) {
        *config = loaded;
        return 0;
    }

    CopyString(loaded.date.status, sizeof(loaded.date.status),
               table["date"]["status"].asString());
    CopyString(loaded.date.dateFormat, sizeof(loaded.date.dateFormat),
               table["date"]["date_format"].asString());
    loaded.date.timeFormat = static_cast<cmiot_uint16_t>(
        table["date"]["time_format"].asUInt());
    loaded.date.weekday = static_cast<cmiot_uint16_t>(table["date"]["weekday"].asUInt());
    PositionFromJson(table["date"]["pos"], &loaded.date.pos);
    GbPositionFromJson(table["date"]["gb_pos"], &loaded.date.gbPos);
    loaded.fontSize = table["font_size"].asUInt();
    CopyString(loaded.fontColor, sizeof(loaded.fontColor), table["font_color"].asString());

    const Json::Value& custom = table["custom_text"];
    const Json::Value& district = table["district_text"];
    const Json::Value& addition = table["addition_text"];
    if (custom.size() > CMIOT_APP_OSD_TEXT_MAX ||
        district.size() > CMIOT_APP_OSD_TEXT_MAX ||
        addition.size() > CMIOT_APP_OSD_TEXT_MAX ||
        district.size() > CMIOT_APP_OSD_TEXT_MAX - addition.size()) {
        return -1;
    }
    loaded.customCount = static_cast<cmiot_uint32_t>(custom.size());
    for (cmiot_uint32_t i = 0; i < loaded.customCount; ++i) {
        CopyString(loaded.customText[i].content, sizeof(loaded.customText[i].content),
                   custom[i]["content"].asString());
        PositionFromJson(custom[i]["pos"], &loaded.customText[i].pos);
    }
    loaded.districtCount = static_cast<cmiot_uint32_t>(district.size());
    for (cmiot_uint32_t i = 0; i < loaded.districtCount; ++i) {
        CopyString(loaded.districtText[i].content, sizeof(loaded.districtText[i].content),
                   district[i]["content"].asString());
    }
    GbPositionFromJson(table["district_pos"], &loaded.districtPos);
    loaded.additionCount = static_cast<cmiot_uint32_t>(addition.size());
    for (cmiot_uint32_t i = 0; i < loaded.additionCount; ++i) {
        CopyString(loaded.additionText[i].content, sizeof(loaded.additionText[i].content),
                   addition[i]["content"].asString());
    }
    GbPositionFromJson(table["addition_pos"], &loaded.additionPos);

    if (loaded.osdSwitch) {
        cmiotOSDInfo_t view;
        memset(&view, 0, sizeof(view));
        view.osdSwitch = loaded.osdSwitch;
        view.mode = loaded.mode;
        view.date = loaded.date;
        view.fontSize = loaded.fontSize;
        CopyString(view.fontColor, sizeof(view.fontColor), loaded.fontColor);
        if (loaded.mode == kCmiotOsdModeCustom) {
            view.osdText.customText.textNum = loaded.customCount;
            view.osdText.customText.text = loaded.customText;
        } else {
            view.osdText.gbText.districtText.textNum = loaded.districtCount;
            view.osdText.gbText.districtText.text = loaded.districtText;
            view.osdText.gbText.districtText.textPos = loaded.districtPos;
            view.osdText.gbText.additionText.textNum = loaded.additionCount;
            view.osdText.gbText.additionText.text = loaded.additionText;
            view.osdText.gbText.additionText.textPos = loaded.additionPos;
        }
        if (ValidateInput(view) != 0) {
            return -1;
        }
    }

    *config = loaded;
    return 0;
}

int EnsureLoadedLocked()
{
    if (g_cmiot_osd_loaded) {
        return 0;
    }
    const int ret = LoadStoredConfig(&g_cmiot_osd);
    if (ret == 0) {
        g_cmiot_osd_loaded = true;
    }
    return ret;
}

int DateTypeFromConfig(const char* dateFormat)
{
    const std::string format = SafeString(dateFormat, 24);
    if (format == "YYYY.MM.DD") {
        return 1;
    }
    if (format == "YYYY/MM/DD") {
        return 2;
    }
    if (format == "YYYY年MM月DD日") {
        return 3;
    }
    return 0;
}

bool IsStatusOn(const char* status)
{
    return ToLowerCopy(SafeString(status, 4)) == "on";
}

int ClampCoordinate(int value)
{
    if (value < 0) {
        return 0;
    }
    return value > kCoordinateMax ? kCoordinateMax : value;
}

int PositionX(const cmiotOsdPosInfo_t& pos, int fontSize, int* alignment)
{
    if (pos.alignSet == CMIOT_OSD_POS_ALIGN_LEFT) {
        *alignment = 0;
        return ClampCoordinate(OsdCharacterMarginToNormalized(pos.alignPos, fontSize, 1));
    }
    if (pos.alignSet == CMIOT_OSD_POS_ALIGN_RIGHT) {
        *alignment = 1;
        const int margin = OsdCharacterMarginToNormalized(pos.alignPos, fontSize, 1);
        return ClampCoordinate(kCoordinateMax - margin);
    }
    *alignment = 0;
    return static_cast<int>(pos.x);
}

void BuildColor(const StoredCmiotOsd& config,
                std::string* mode, std::string* color)
{
    const std::string raw = SafeString(config.fontColor, sizeof(config.fontColor));
    const std::string lower = ToLowerCopy(raw);
    if (lower == "auto") {
        *mode = "auto";
        *color = "#ffffff";
    } else if (lower == "black") {
        *mode = "customize";
        *color = "#000000";
    } else if (lower == "white") {
        *mode = "customize";
        *color = "#ffffff";
    } else {
        *mode = "customize";
        *color = raw;
    }
}

int ApplyTextItem(int index, const char* content, int x, int y, int alignment)
{
    const std::string text = SafeString(content, CMIOT_MAX_OSD_CONTENT_LEN + 1);
    return ApplyNormalizedOsdText(index, text.c_str(), x, y,
                                  text.empty() ? 0 : 1, alignment);
}

int ApplyStoredCmiotOsd(const StoredCmiotOsd& config)
{
    std::string colorMode;
    std::string color;
    BuildColor(config, &colorMode, &color);
    int ret = ApplyNormalizedOsdCommon(static_cast<int>(config.fontSize),
                                       colorMode.c_str(), color.c_str());
    if (ret != 0) {
        return ret;
    }

    int timeX = 0;
    int timeY = 0;
    int timeAlignment = 0;
    if (config.mode == kCmiotOsdModeCustom) {
        timeX = PositionX(config.date.pos, static_cast<int>(config.fontSize),
                          &timeAlignment);
        timeY = static_cast<int>(config.date.pos.y);
    } else {
        const int side = OsdCharacterMarginToNormalized(
            config.date.gbPos.sideSpace, static_cast<int>(config.fontSize), 1);
        const int top = OsdCharacterMarginToNormalized(
            config.date.gbPos.vertSpace, static_cast<int>(config.fontSize), 0);
        timeX = ClampCoordinate(kCoordinateMax - side);
        timeY = ClampCoordinate(top);
        timeAlignment = 1;
    }
    ret = ApplyNormalizedOsdTime(DateTypeFromConfig(config.date.dateFormat),
                                  config.date.timeFormat == 12 ? 1 : 0,
                                  config.date.weekday ? 1 : 0,
                                  timeX, timeY,
                                  IsStatusOn(config.date.status) ? 1 : 0,
                                  timeAlignment);
    if (ret != 0) {
        return ret;
    }

    int textIndex = 0;
    if (config.mode == kCmiotOsdModeCustom) {
        for (cmiot_uint32_t i = 0; i < config.customCount; ++i) {
            int alignment = 0;
            const int x = PositionX(config.customText[i].pos,
                                    static_cast<int>(config.fontSize), &alignment);
            ret = ApplyTextItem(textIndex++, config.customText[i].content,
                                x, static_cast<int>(config.customText[i].pos.y),
                                alignment);
            if (ret != 0) {
                return ret;
            }
        }
    } else {
        const int districtSide = OsdCharacterMarginToNormalized(
            config.districtPos.sideSpace, static_cast<int>(config.fontSize), 1);
        const int districtBottom = OsdCharacterMarginToNormalized(
            config.districtPos.vertSpace, static_cast<int>(config.fontSize), 0);
        const int districtStep = OsdCharacterMarginToNormalized(
            1.0f + config.districtPos.lineSpace,
            static_cast<int>(config.fontSize), 0);
        int y = ClampCoordinate(kCoordinateMax - districtBottom -
                                districtStep * static_cast<int>(config.districtCount));
        for (cmiot_uint32_t i = 0; i < config.districtCount; ++i) {
            ret = ApplyTextItem(textIndex++, config.districtText[i].content,
                                ClampCoordinate(kCoordinateMax - districtSide),
                                ClampCoordinate(y + districtStep * static_cast<int>(i)), 1);
            if (ret != 0) {
                return ret;
            }
        }

        const int additionSide = OsdCharacterMarginToNormalized(
            config.additionPos.sideSpace, static_cast<int>(config.fontSize), 1);
        const int additionBottom = OsdCharacterMarginToNormalized(
            config.additionPos.vertSpace, static_cast<int>(config.fontSize), 0);
        const int additionStep = OsdCharacterMarginToNormalized(
            1.0f + config.additionPos.lineSpace,
            static_cast<int>(config.fontSize), 0);
        y = ClampCoordinate(kCoordinateMax - additionBottom -
                            additionStep * static_cast<int>(config.additionCount));
        for (cmiot_uint32_t i = 0; i < config.additionCount; ++i) {
            ret = ApplyTextItem(textIndex++, config.additionText[i].content,
                                ClampCoordinate(additionSide),
                                ClampCoordinate(y + additionStep * static_cast<int>(i)), 0);
            if (ret != 0) {
                return ret;
            }
        }
    }

    while (textIndex < kDeviceTextMax) {
        ret = ApplyNormalizedOsdText(textIndex++, "", 0, 0, 0, 0);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

int ApplyEffectiveConfigLocked(const StoredCmiotOsd& config)
{
    if (GetCloudPlatform() != CLOUD_PLATFORM_CMIOT) {
        return 0;
    }
    return config.osdSwitch ? ApplyStoredCmiotOsd(config)
                            : g_AVManager.ApplyLocalOsdConfig();
}

cmiot_uint32_t CopyCustomText(cmiotOsdTextInfo_t* dst, cmiot_uint32_t capacity,
                              const StoredCmiotOsd& stored)
{
    cmiot_uint32_t copied = 0;
    while (dst != NULL && copied < capacity && copied < stored.customCount) {
        dst[copied] = stored.customText[copied];
        ++copied;
    }
    return copied;
}

cmiot_uint32_t CopyGbText(cmiotGBOsdTextInfo_t* dst, cmiot_uint32_t capacity,
                          const cmiotGBOsdTextInfo_t* src, cmiot_uint32_t count)
{
    cmiot_uint32_t copied = 0;
    while (dst != NULL && copied < capacity && copied < count) {
        dst[copied] = src[copied];
        ++copied;
    }
    return copied;
}

} // namespace

extern "C" int cmiot_osd_get_text_capacity(void)
{
    return CMIOT_APP_OSD_TEXT_MAX;
}

extern "C" int cmiot_osd_initialize(void)
{
    pthread_mutex_lock(&g_cmiot_osd_mutex);
    const int loadRet = EnsureLoadedLocked();
    const StoredCmiotOsd current = g_cmiot_osd;
    pthread_mutex_unlock(&g_cmiot_osd_mutex);
    if (loadRet != 0) {
        return loadRet;
    }
    if (GetCloudPlatform() != CLOUD_PLATFORM_CMIOT ||
        !current.valid || !current.osdSwitch) {
        return 0;
    }
    return ApplyStoredCmiotOsd(current);
}

extern "C" int cmiot_osd_is_override_active(void)
{
    if (GetCloudPlatform() != CLOUD_PLATFORM_CMIOT) {
        return 0;
    }
    pthread_mutex_lock(&g_cmiot_osd_mutex);
    const int loadRet = EnsureLoadedLocked();
    const bool active = loadRet == 0 && g_cmiot_osd.valid && g_cmiot_osd.osdSwitch;
    pthread_mutex_unlock(&g_cmiot_osd_mutex);
    return active ? 1 : 0;
}

extern "C" int cmiot_osd_set_config(const cmiotOSDInfo_t* info)
{
    if (info == NULL || ValidateInput(*info) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_cmiot_osd_mutex);
    StoredCmiotOsd next;
    if (EnsureLoadedLocked() == 0) {
        next = g_cmiot_osd;
    }
    CopyInput(*info, &next);
    const int saveRet = SaveStoredConfig(next);
    int applyRet = 0;
    if (saveRet == 0) {
        g_cmiot_osd = next;
        g_cmiot_osd_loaded = true;
        applyRet = ApplyEffectiveConfigLocked(next);
    }
    pthread_mutex_unlock(&g_cmiot_osd_mutex);
    if (saveRet != 0) {
        return saveRet;
    }
    return applyRet;
}

extern "C" int cmiot_osd_get_config(cmiotOSDInfo_t* info)
{
    if (info == NULL) {
        return -1;
    }

    const cmiot_uint8_t requestedMode = info->mode;
    cmiotOsdTextInfo_t* customOut = NULL;
    cmiot_uint32_t customCapacity = 0;
    cmiotGBOsdTextInfo_t* districtOut = NULL;
    cmiot_uint32_t districtCapacity = 0;
    cmiotGBOsdTextInfo_t* additionOut = NULL;
    cmiot_uint32_t additionCapacity = 0;
    if (requestedMode == kCmiotOsdModeCustom) {
        customOut = info->osdText.customText.text;
        customCapacity = info->osdText.customText.textNum;
    } else if (requestedMode == kCmiotOsdModeGb) {
        districtOut = info->osdText.gbText.districtText.text;
        districtCapacity = info->osdText.gbText.districtText.textNum;
        additionOut = info->osdText.gbText.additionText.text;
        additionCapacity = info->osdText.gbText.additionText.textNum;
    }

    pthread_mutex_lock(&g_cmiot_osd_mutex);
    const int loadRet = EnsureLoadedLocked();
    const StoredCmiotOsd stored = g_cmiot_osd;
    pthread_mutex_unlock(&g_cmiot_osd_mutex);
    if (loadRet != 0) {
        return loadRet;
    }

    memset(info, 0, sizeof(*info));
    info->osdSwitch = stored.osdSwitch;
    info->mode = stored.mode;
    info->date = stored.date;
    info->fontSize = stored.fontSize;
    CopyString(info->fontColor, sizeof(info->fontColor), stored.fontColor);
    if (!stored.valid) {
        info->mode = kCmiotOsdModeCustom;
        return 0;
    }

    if (stored.mode == kCmiotOsdModeCustom) {
        const cmiot_uint32_t copied = CopyCustomText(customOut, customCapacity, stored);
        info->osdText.customText.text = customOut;
        info->osdText.customText.textNum =
            customOut == NULL || customCapacity == 0 ? stored.customCount : copied;
        return customOut != NULL && copied < stored.customCount ? -2 : 0;
    }

    const cmiot_uint32_t districtCopied = CopyGbText(
        districtOut, districtCapacity, stored.districtText, stored.districtCount);
    const cmiot_uint32_t additionCopied = CopyGbText(
        additionOut, additionCapacity, stored.additionText, stored.additionCount);
    info->osdText.gbText.districtText.text = districtOut;
    info->osdText.gbText.districtText.textNum =
        districtOut == NULL || districtCapacity == 0 ? stored.districtCount : districtCopied;
    info->osdText.gbText.districtText.textPos = stored.districtPos;
    info->osdText.gbText.additionText.text = additionOut;
    info->osdText.gbText.additionText.textNum =
        additionOut == NULL || additionCapacity == 0 ? stored.additionCount : additionCopied;
    info->osdText.gbText.additionText.textPos = stored.additionPos;
    return (districtOut != NULL && districtCopied < stored.districtCount) ||
           (additionOut != NULL && additionCopied < stored.additionCount) ? -2 : 0;
}
