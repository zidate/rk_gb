#include "VideoOsdControl.h"

#include <stdio.h>
#include <string.h>

#include "PAL/Capture.h"


#include "ExchangeAL/CameraExchange.h"
#include "ExchangeAL/Exchange.h"
#include "ExchangeAL/MediaExchange.h"
#include "ExchangeAL/ExchangeKind.h"
#include "Manager/ConfigManager.h"


namespace
{

static const int kVideoOsdDateTimeId = 1;
static const int kVideoOsdCustomTextId = 2;
static const int kVideoOsdCoordinateMax = 10000;
static const int kDefaultMainStreamWidth = 2560;
static const int kDefaultMainStreamHeight = 1440;
static int g_cached_master_switch = -1;
static int g_cached_event_switch = -1;
static int g_cached_alert_switch = -1;
static bool g_has_cached_protocol_state = false;
static media::VideoOsdState g_cached_protocol_state;

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

static std::string TrimWhitespaceCopy(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }

    return text.substr(begin, end - begin);
}

static bool ContainsToken(const std::string& text, const char* token)
{
    return token != NULL && text.find(token) != std::string::npos;
}

static std::string NormalizeVideoOsdTextTemplate(const std::string& textIn)
{
    const std::string text = TrimWhitespaceCopy(textIn);
    return (ToLowerCopy(text) == "null") ? "" : text;
}

static std::string NormalizeVideoOsdDateStyle(const std::string& formatIn)
{
    const std::string format = ToLowerCopy(formatIn);
    if (format.empty()) {
        return "CHR-YYYY-MM-DD";
    }

    if (ContainsToken(format, "yyyy.mm.dd")) {
        return "CHR-YYYY.MM.DD";
    }
    if (ContainsToken(format, "yyyy/mm/dd")) {
        return "CHR-YYYY/MM/DD";
    }
    if (format.find("年") != std::string::npos ||
        format.find("月") != std::string::npos ||
        format.find("日") != std::string::npos) {
        return "YYYY-MM-DD";
    }

    return "CHR-YYYY-MM-DD";
}

static std::string NormalizeVideoOsdTimeStyle(const std::string& formatIn)
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

static std::string BuildVideoOsdTimeFormatFromStyles(const std::string& dateStyleIn,
                                                     const std::string& timeStyleIn)
{
    std::string datePart = "yyyy-MM-dd";
    const std::string dateStyle = ToLowerCopy(TrimWhitespaceCopy(dateStyleIn));
    if (ContainsToken(dateStyle, "yyyy.mm.dd")) {
        datePart = "yyyy.MM.dd";
    } else if (ContainsToken(dateStyle, "yyyy/mm/dd")) {
        datePart = "yyyy/MM/dd";
    } else if (dateStyle.find("年") != std::string::npos ||
               dateStyle.find("月") != std::string::npos ||
               dateStyle.find("日") != std::string::npos) {
        datePart = "yyyy年MM月dd日";
    }

    std::string timePart = "HH:mm:ss";
    const std::string timeStyle = ToLowerCopy(TrimWhitespaceCopy(timeStyleIn));
    if (ContainsToken(timeStyle, "12hour")) {
        timePart = "hh:mm:ss tt";
    }

    return datePart + " " + timePart;
}

static bool QueryMainStreamResolution(int& width, int& height)
{
    width = 0;
    height = 0;
    return CaptureGetResolution(0, &width, &height) == 0 && width > 0 && height > 0;
}

static void QueryMainStreamResolutionOrDefault(int& width, int& height)
{
    //if (!QueryMainStreamResolution(width, height)) {
        width = kDefaultMainStreamWidth;
        height = kDefaultMainStreamHeight;
    //}
}

static int ClampVideoOsdCoordinate(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > kVideoOsdCoordinateMax) {
        return kVideoOsdCoordinateMax;
    }
    return value;
}

static int ScaleVideoOsdXToDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolutionOrDefault(width, height);
    return (ClampVideoOsdCoordinate(value) * width + kVideoOsdCoordinateMax / 2) /
        kVideoOsdCoordinateMax;
}

static int ScaleVideoOsdYToDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolutionOrDefault(width, height);
    return (ClampVideoOsdCoordinate(value) * height + kVideoOsdCoordinateMax / 2) /
        kVideoOsdCoordinateMax;
}

static int ScaleVideoOsdXFromDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolutionOrDefault(width, height);
    if (value <= 0 || width <= 0) {
        return 0;
    }
    return ClampVideoOsdCoordinate(
        (value * kVideoOsdCoordinateMax + width / 2) / width);
}

static int ScaleVideoOsdYFromDevice(int value)
{
    int width = 0;
    int height = 0;
    QueryMainStreamResolutionOrDefault(width, height);
    if (value <= 0 || height <= 0) {
        return 0;
    }
    return ClampVideoOsdCoordinate(
        (value * kVideoOsdCoordinateMax + height / 2) / height);
}

static int NormalizeVideoOsdDateType(const std::string& formatIn)
{
    const std::string format = ToLowerCopy(TrimWhitespaceCopy(formatIn));
    if (ContainsToken(format, "yyyy.mm.dd")) {
        return 1;
    }
    if (ContainsToken(format, "yyyy/mm/dd")) {
        return 2;
    }
    if (format.find("年") != std::string::npos ||
        format.find("月") != std::string::npos ||
        format.find("日") != std::string::npos) {
        return 3;
    }
    return 0;
}

static int NormalizeVideoOsdTimeType(const std::string& formatIn)
{
    return (NormalizeVideoOsdTimeStyle(formatIn) == "12hour") ? 1 : 0;
}

static std::string BuildVideoOsdDateStyleFromType(int dateType)
{
    switch (dateType) {
    case 1:
        return "CHR-YYYY.MM.DD";
    case 2:
        return "CHR-YYYY/MM/DD";
    case 3:
        return "YYYY-MM-DD";
    case 0:
    default:
        return "CHR-YYYY-MM-DD";
    }
}

static std::string BuildVideoOsdTimeFormatFromConfig(int dateType, int timeType)
{
    std::string datePart = "yyyy-MM-dd";
    switch (dateType) {
    case 1:
        datePart = "yyyy.MM.dd";
        break;
    case 2:
        datePart = "yyyy/MM/dd";
        break;
    case 3:
        datePart = "yyyy年MM月dd日";
        break;
    case 0:
    default:
        break;
    }

    return datePart + ((timeType == 1) ? " hh:mm:ss tt" : " HH:mm:ss");
}

static bool NormalizeVideoOsdFontSize(int value, int* out)
{
    if (out == NULL) {
        return false;
    }
    if (value == 16 || value == 32 || value == 64) {
        *out = value;
        return true;
    }
    return false;
}

static bool NormalizeVideoOsdFontColorMode(const std::string& modeIn, std::string* out)
{
    if (out == NULL) {
        return false;
    }
    const std::string mode = ToLowerCopy(TrimWhitespaceCopy(modeIn));
    if (mode.empty() || mode == "custom" || mode == "customize" || mode == "manual" || mode == "2") {
        *out = "customize";
        return true;
    }
    if (mode == "auto" || mode == "black_white_auto" || mode == "blackwhite" || mode == "1") {
        *out = "auto";
        return true;
    }
    return false;
}

static bool IsHexDigit(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool NormalizeVideoOsdFontColor(const std::string& colorIn, std::string* out)
{
    if (out == NULL) {
        return false;
    }
    std::string color = TrimWhitespaceCopy(colorIn);
    if (color.size() >= 2 && color[0] == '0' && (color[1] == 'x' || color[1] == 'X')) {
        color = color.substr(2);
    }
    if (!color.empty() && color[0] == '#') {
        color = color.substr(1);
    }
    if (color.size() != 6) {
        return false;
    }
    for (size_t index = 0; index < color.size(); ++index) {
        if (!IsHexDigit(color[index])) {
            return false;
        }
        if (color[index] >= 'A' && color[index] <= 'F') {
            color[index] = static_cast<char>(color[index] - 'A' + 'a');
        }
    }
    *out = "#" + color;
    return true;
}

static int NormalizeVideoOsdAlignmentValue(const std::string& alignmentIn)
{
    const std::string alignment = ToLowerCopy(TrimWhitespaceCopy(alignmentIn));
    if (alignment == "right" || alignment == "right_align" || alignment == "right-aligned" || alignment == "1") {
        return 1;
    }
    return 0;
}

static std::string BuildVideoOsdAlignmentValue(int alignment)
{
    return (alignment == 1) ? "right" : "left";
}

static int MergeError(int current, int candidate)
{
    return (current != 0) ? current : candidate;
}

static void NormalizeVideoOsdTextItems(media::VideoOsdState* state)
{
    if (state == NULL) {
        return;
    }

    if (!state->has_text_items) {
        state->text_items.clear();
        return;
    }

    size_t writeIndex = 0;
    for (size_t index = 0; index < state->text_items.size(); ++index) {
        media::VideoOsdTextItem item = state->text_items[index];
        if (item.has_text) {
            item.text = NormalizeVideoOsdTextTemplate(item.text);
            item.has_text = !item.text.empty();
        }
        if (item.has_alignment) {
            item.alignment = BuildVideoOsdAlignmentValue(
                NormalizeVideoOsdAlignmentValue(item.alignment));
        }
        if (!item.has_text && !item.has_position && !item.has_alignment) {
            continue;
        }
        if (writeIndex != index) {
            state->text_items[writeIndex] = item;
        }
        ++writeIndex;
    }

    state->text_items.resize(writeIndex);
    state->has_text_items = !state->text_items.empty();
}

static media::VideoOsdTextItem* EnsurePrimaryVideoOsdTextItem(media::VideoOsdState* state)
{
    if (state == NULL) {
        return NULL;
    }

    if (!state->has_text_items) {
        state->has_text_items = true;
        state->text_items.clear();
    }
    if (state->text_items.empty()) {
        state->text_items.push_back(media::VideoOsdTextItem());
    }

    return &state->text_items[0];
}

static const media::VideoOsdTextItem* GetPrimaryVideoOsdTextItem(const media::VideoOsdState& state)
{
    if (!state.has_text_items || state.text_items.empty()) {
        return NULL;
    }

    return &state.text_items[0];
}

static std::string GetPrimaryVideoOsdTextTemplate(const media::VideoOsdState& state)
{
    const media::VideoOsdTextItem* item = GetPrimaryVideoOsdTextItem(state);
    if (item == NULL || !item->has_text) {
        return "";
    }

    return NormalizeVideoOsdTextTemplate(item->text);
}

static bool GetVideoOsdTimePosition(const media::VideoOsdState& state, int* x, int* y)
{
    if (x == NULL || y == NULL ||
        !state.has_time_position ||
        state.time_x < 0 ||
        state.time_y < 0) {
        return false;
    }

    *x = state.time_x;
    *y = state.time_y;
    return true;
}

static bool GetPrimaryVideoOsdTextPosition(const media::VideoOsdState& state, int* x, int* y)
{
    if (x == NULL || y == NULL) {
        return false;
    }

    const media::VideoOsdTextItem* item = GetPrimaryVideoOsdTextItem(state);
    if (item == NULL || !item->has_position || item->x < 0 || item->y < 0) {
        return false;
    }

    *x = item->x;
    *y = item->y;
    return true;
}

static void CacheVideoOsdProtocolState(const media::VideoOsdState& desired)
{
    g_cached_protocol_state = desired;
    NormalizeVideoOsdTextItems(&g_cached_protocol_state);
    if (!g_cached_protocol_state.has_time_format &&
        (g_cached_protocol_state.has_date_style ||
         g_cached_protocol_state.has_time_style)) {
        g_cached_protocol_state.has_time_format = true;
        g_cached_protocol_state.time_format = BuildVideoOsdTimeFormatFromStyles(
            g_cached_protocol_state.date_style,
            g_cached_protocol_state.time_style);
    }
    g_has_cached_protocol_state = true;
}

} // namespace

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

using namespace std;

std::string strToHexAscii(const std::string &input) {
    std::stringstream ss;
    
    for (char c : input) {
        // 转两位大写十六进制
        ss << hex << uppercase << setw(2) << setfill('0') << (unsigned int)(unsigned char)c;
    }
    
    return ss.str();
}
std::string hexToStr(const std::string &hexStr) {
    std::string result;
    for (int i = 0; i < hexStr.size(); i += 2) {
        // 每两位截取
        string byteHex = hexStr.substr(i, 2);
        // 转成一个字节
        char c = (char)strtol(byteHex.c_str(), NULL, 16);
        result += c;
    }
    return result;
}


namespace media
{

bool ResolveVideoOsdAnchor(const std::string& positionIn, int* x, int* y)
{
    if (x == NULL || y == NULL) {
        return false;
    }

    *x = 0;
    *y = 0;

    const std::string position = ToLowerCopy(positionIn);
    if (position.empty()) {
        return false;
    }

    if ((sscanf(position.c_str(), "%d,%d", x, y) == 2 ||
         sscanf(position.c_str(), "%d:%d", x, y) == 2) &&
        *x >= 0 &&
        *y >= 0) {
        *x = ClampVideoOsdCoordinate(*x);
        *y = ClampVideoOsdCoordinate(*y);
        return true;
    }

    if (position == "top_left" || position == "left_top" || position == "top-left") {
        *x = 0;
        *y = 0;
        return true;
    }

    if (position == "top_right" || position == "right_top" || position == "top-right") {
        *x = kVideoOsdCoordinateMax;
        *y = 0;
        return true;
    }

    if (position == "bottom_left" || position == "left_bottom" || position == "bottom-left") {
        *x = 0;
        *y = kVideoOsdCoordinateMax;
        return true;
    }

    if (position == "bottom_right" || position == "right_bottom" || position == "bottom-right") {
        *x = kVideoOsdCoordinateMax;
        *y = kVideoOsdCoordinateMax;
        return true;
    }

    if (position == "center" || position == "middle") {
        *x = kVideoOsdCoordinateMax / 2;
        *y = kVideoOsdCoordinateMax / 2;
        return true;
    }

    return false;
}

int ApplyVideoOsdConfig(const VideoOsdState& desired)
{
    VideoOsdState normalizedDesired = desired;
    NormalizeVideoOsdTextItems(&normalizedDesired);

	int normalizedFontSize = 0;
	if (normalizedDesired.has_font_size &&
	    !NormalizeVideoOsdFontSize(normalizedDesired.font_size, &normalizedFontSize)) {
		return -1;
	}

	std::string normalizedFontColorMode;
	if (normalizedDesired.has_font_color_mode &&
	    !NormalizeVideoOsdFontColorMode(normalizedDesired.font_color_mode, &normalizedFontColorMode)) {
		return -1;
	}

	std::string normalizedFontColor;
	if (normalizedDesired.has_font_color &&
	    !NormalizeVideoOsdFontColor(normalizedDesired.font_color, &normalizedFontColor)) {
		return -1;
	}

	CConfigTable table;
	OSDTimeConf_S curOSDTimeConfig;
	OSDTimeConf_S newOSDTimeConfig;
	g_configManager.getConfig(getConfigName(CFG_OSD_TIME), table);
    TExchangeAL<OSDTimeConf_S>::getConfig(table, curOSDTimeConfig);
	newOSDTimeConfig = curOSDTimeConfig;

	if (normalizedDesired.has_time_enabled)
	{
		newOSDTimeConfig.show = normalizedDesired.time_enabled;
	}
	if (normalizedDesired.has_time_position)
	{
		newOSDTimeConfig.x = ScaleVideoOsdXToDevice(normalizedDesired.time_x);
		newOSDTimeConfig.y = ScaleVideoOsdYToDevice(normalizedDesired.time_y);
	}
	if (normalizedDesired.has_time_format)
	{
		newOSDTimeConfig.date_type = NormalizeVideoOsdDateType(normalizedDesired.time_format);
		newOSDTimeConfig.time_type = NormalizeVideoOsdTimeType(normalizedDesired.time_format);
	}
	if (normalizedDesired.has_date_style)
	{
		newOSDTimeConfig.date_type = NormalizeVideoOsdDateType(normalizedDesired.date_style);
	}
	if (normalizedDesired.has_time_style)
	{
		newOSDTimeConfig.time_type = NormalizeVideoOsdTimeType(normalizedDesired.time_style);
	}
	if (normalizedDesired.has_time_display_week_enabled)
	{
		newOSDTimeConfig.display_week_enabled =
			(normalizedDesired.time_display_week_enabled != 0) ? 1 : 0;
	}
	if (normalizedDesired.has_time_alignment)
	{
		newOSDTimeConfig.alignment =
			NormalizeVideoOsdAlignmentValue(normalizedDesired.time_alignment);
	}
	if (normalizedDesired.has_font_size)
	{
		newOSDTimeConfig.font_size = normalizedFontSize;
	}
	if (normalizedDesired.has_font_color_mode)
	{
		newOSDTimeConfig.font_color_mode = normalizedFontColorMode;
	}
	if (normalizedDesired.has_font_color)
	{
		newOSDTimeConfig.font_color = normalizedFontColor;
	}

	if (newOSDTimeConfig.date_type != curOSDTimeConfig.date_type ||
		newOSDTimeConfig.time_type != curOSDTimeConfig.time_type ||
		newOSDTimeConfig.display_week_enabled != curOSDTimeConfig.display_week_enabled ||
		newOSDTimeConfig.x != curOSDTimeConfig.x ||
		newOSDTimeConfig.y != curOSDTimeConfig.y ||
		newOSDTimeConfig.show != curOSDTimeConfig.show ||
		newOSDTimeConfig.alignment != curOSDTimeConfig.alignment ||
		newOSDTimeConfig.font_size != curOSDTimeConfig.font_size ||
		newOSDTimeConfig.font_color_mode != curOSDTimeConfig.font_color_mode ||
		newOSDTimeConfig.font_color != curOSDTimeConfig.font_color)
	{
		table.clear();
		TExchangeAL<OSDTimeConf_S>::setConfig(newOSDTimeConfig, table);
		g_configManager.setConfig(getConfigName(CFG_OSD_TIME), table, 0, IConfigManager::applyOK);
	}

	OSDTextAllConf_S curOSDTextAllConfig;
	OSDTextAllConf_S newOSDTextAllConfig;
	g_configManager.getConfig(getConfigName(CFG_OSD_TEXT), table);
    TExchangeAL<OSDTextAllConf_S>::getConfig(table, curOSDTextAllConfig);
	newOSDTextAllConfig = curOSDTextAllConfig;

	if (normalizedDesired.has_text_items)
	{
		int i;
		for (i = 0; i < OSD_TEXT_MAX && i < (int)normalizedDesired.text_items.size(); i++)
		{
			media::VideoOsdTextItem &item = normalizedDesired.text_items[i];
			if (item.has_text)
			{
				newOSDTextAllConfig.osd_text[i].text = strToHexAscii(item.text);
			}
			if (item.has_position)
			{
				newOSDTextAllConfig.osd_text[i].x = ScaleVideoOsdXToDevice(item.x);
				newOSDTextAllConfig.osd_text[i].y = ScaleVideoOsdYToDevice(item.y);
			}
			if (item.has_alignment)
			{
				newOSDTextAllConfig.osd_text[i].alignment =
					NormalizeVideoOsdAlignmentValue(item.alignment);
			}
			const bool hasText =
				!newOSDTextAllConfig.osd_text[i].text.empty();
			newOSDTextAllConfig.osd_text[i].show =
				((!normalizedDesired.has_text_enabled || normalizedDesired.text_enabled != 0) &&
				 hasText) ? 1 : 0;
		}
		for (; i < OSD_TEXT_MAX; i++)
		{
			newOSDTextAllConfig.osd_text[i].show = 0;
		}
	}
	else if (normalizedDesired.has_text_enabled)
	{
		for (int i = 0; i < OSD_TEXT_MAX; i++)
		{
			newOSDTextAllConfig.osd_text[i].show =
				(normalizedDesired.text_enabled != 0 &&
				 !newOSDTextAllConfig.osd_text[i].text.empty()) ? 1 : 0;
		}
	}
	for (int i = 0; i < OSD_TEXT_MAX; i++)
	{
		if (newOSDTextAllConfig.osd_text[i].text != curOSDTextAllConfig.osd_text[i].text ||
			newOSDTextAllConfig.osd_text[i].x != curOSDTextAllConfig.osd_text[i].x ||
			newOSDTextAllConfig.osd_text[i].y != curOSDTextAllConfig.osd_text[i].y ||
			newOSDTextAllConfig.osd_text[i].show != curOSDTextAllConfig.osd_text[i].show ||
			newOSDTextAllConfig.osd_text[i].alignment != curOSDTextAllConfig.osd_text[i].alignment)
		{
			table.clear();
			TExchangeAL<OSDTextAllConf_S>::setConfig(newOSDTextAllConfig, table);
			g_configManager.setConfig(getConfigName(CFG_OSD_TEXT), table, 0, IConfigManager::applyOK);
			break;
		}
	}

    return 0;
}

bool QueryVideoOsdState(VideoOsdState* state)
{
    if (state == NULL) {
        return false;
    }

    *state = VideoOsdState();
//    if (g_has_cached_protocol_state) {
//        *state = g_cached_protocol_state;
//        NormalizeVideoOsdTextItems(state);
//    }
//
//    if (g_cached_master_switch >= 0) {
//        state->has_master_enabled = true;
//        state->master_enabled = g_cached_master_switch;
//    }
//
//    if (g_cached_event_switch >= 0) {
//        state->has_event_enabled = true;
//        state->event_enabled = g_cached_event_switch;
//    }
//
//    if (g_cached_alert_switch >= 0) {
//        state->has_alert_enabled = true;
//        state->alert_enabled = g_cached_alert_switch;
//    }

	CConfigTable table;
	OSDTimeConf_S curOSDTimeConfig;
	g_configManager.getConfig(getConfigName(CFG_OSD_TIME), table);
    TExchangeAL<OSDTimeConf_S>::getConfig(table, curOSDTimeConfig);

	OSDTextAllConf_S curOSDTextAllConfig;
	g_configManager.getConfig(getConfigName(CFG_OSD_TEXT), table);
    TExchangeAL<OSDTextAllConf_S>::getConfig(table, curOSDTextAllConfig);

	state->has_time_enabled = true;
	state->time_enabled = curOSDTimeConfig.show;

	state->has_font_size = true;
	state->font_size = curOSDTimeConfig.font_size;
	state->has_font_color_mode = true;
	state->font_color_mode = curOSDTimeConfig.font_color_mode;
	state->has_font_color = true;
	state->font_color = curOSDTimeConfig.font_color;

	state->has_text_enabled = true;
	state->text_enabled = 0;
	for (int i = 0; i < OSD_TEXT_MAX; i++)
	{
		if (curOSDTextAllConfig.osd_text[i].show)
		{
			state->text_enabled = 1;
			break;
		}
	}

	state->has_time_format = true;
	state->time_format = BuildVideoOsdTimeFormatFromConfig(
		curOSDTimeConfig.date_type, curOSDTimeConfig.time_type);

	state->has_date_style = true;
	state->date_style = BuildVideoOsdDateStyleFromType(curOSDTimeConfig.date_type);
	state->has_time_style = true;
	state->time_style = (curOSDTimeConfig.time_type == 1) ? "12hour" : "24hour";

	state->has_time_position = true;
	state->time_x = ScaleVideoOsdXFromDevice(curOSDTimeConfig.x);
	state->time_y = ScaleVideoOsdYFromDevice(curOSDTimeConfig.y);
	state->has_time_display_week_enabled = true;
	state->time_display_week_enabled = curOSDTimeConfig.display_week_enabled;
	state->has_time_alignment = true;
	state->time_alignment = BuildVideoOsdAlignmentValue(curOSDTimeConfig.alignment);

	state->has_text_items = true;
	for (int i = 0; i < OSD_TEXT_MAX; i++)
	{
		if (curOSDTextAllConfig.osd_text[i].show)
		{
			media::VideoOsdTextItem item;
			item.has_text = true;
			item.text = hexToStr(curOSDTextAllConfig.osd_text[i].text);
			item.has_position = true;
			item.x = ScaleVideoOsdXFromDevice(curOSDTextAllConfig.osd_text[i].x);
			item.y = ScaleVideoOsdYFromDevice(curOSDTextAllConfig.osd_text[i].y);
			item.has_alignment = true;
			item.alignment = BuildVideoOsdAlignmentValue(curOSDTextAllConfig.osd_text[i].alignment);
			state->text_items.push_back(item);
		}
	}

    NormalizeVideoOsdTextItems(state);

	if (!state->has_master_enabled &&
	    (state->has_time_enabled || state->has_event_enabled ||
	     state->has_alert_enabled || state->has_text_enabled)) {
	    state->has_master_enabled = true;
	    state->master_enabled = ((state->has_time_enabled && state->time_enabled != 0) ||
	                             (state->has_event_enabled && state->event_enabled != 0) ||
	                             (state->has_alert_enabled && state->alert_enabled != 0) ||
	                             (state->has_text_enabled && state->text_enabled != 0)) ? 1 : 0;
	}

	return state->has_master_enabled ||
	       state->has_event_enabled ||
	       state->has_alert_enabled ||
	       state->has_font_size ||
	       state->has_font_color_mode ||
	       state->has_font_color ||
	       state->has_time_enabled ||
	       state->has_text_enabled ||
	       state->has_time_format ||
	       state->has_date_style ||
	       state->has_time_style ||
	       state->has_time_position ||
	       state->has_time_display_week_enabled ||
	       state->has_time_alignment ||
	       state->has_text_items;
}

} // namespace media
