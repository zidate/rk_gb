/* 在调用 RV1106 RK OSD 接口前完成坐标和字符边距换算。 */
#include "NormalizedOsdControl.h"

#include "PAL/Capture.h"

extern "C" {
int gb_rkipc_osd_common_set(int font_size,
                            const char* font_color_mode,
                            const char* font_color);
int gb_rkipc_osd_time_set(int date_type, int time_type,
                          int display_week_enabled, int x, int y,
                          int show, int alignment);
int gb_rkipc_osd_text_set(int index, const char* text,
                          int x, int y, int show, int alignment);
}

namespace
{

const int kCoordinateMax = 10000;
const int kDefaultWidth = 1920;
const int kDefaultHeight = 1080;

int ScaleCoordinate(int value, int canvas)
{
    if (value < 0 || value > kCoordinateMax || canvas <= 0) {
        return -1;
    }
    return (value * canvas + kCoordinateMax / 2) / kCoordinateMax;
}

void QueryCanvas(int* width, int* height)
{
    if (CaptureGetResolution(0, width, height) != 0 ||
        *width <= 0 || *height <= 0) {
        *width = kDefaultWidth;
        *height = kDefaultHeight;
    }
}

} // namespace

int ApplyNormalizedOsdCommon(int fontSize,
                             const char* fontColorMode,
                             const char* fontColor)
{
    return gb_rkipc_osd_common_set(fontSize, fontColorMode, fontColor);
}

int ApplyNormalizedOsdTime(int dateType, int timeType, int displayWeek,
                           int x, int y, int show, int alignment)
{
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int deviceX = ScaleCoordinate(x, width);
    const int deviceY = ScaleCoordinate(y, height);
    if (deviceX < 0 || deviceY < 0) {
        return -1;
    }
    return gb_rkipc_osd_time_set(dateType, timeType, displayWeek,
                                 deviceX, deviceY, show, alignment);
}

int ApplyNormalizedOsdText(int index, const char* text,
                           int x, int y, int show, int alignment)
{
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int deviceX = ScaleCoordinate(x, width);
    const int deviceY = ScaleCoordinate(y, height);
    if (deviceX < 0 || deviceY < 0) {
        return -1;
    }
    return gb_rkipc_osd_text_set(index, text ? text : "",
                                 deviceX, deviceY, show, alignment);
}

int OsdCharacterMarginToNormalized(float characters, int fontSize,
                                   int horizontal)
{
    if (characters < 0.0f || fontSize <= 0) {
        return -1;
    }
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int canvas = horizontal ? width : height;
    const float pixels = characters * static_cast<float>(fontSize);
    const int normalized = static_cast<int>(
        pixels * static_cast<float>(kCoordinateMax) / canvas + 0.5f);
    return normalized > kCoordinateMax ? kCoordinateMax : normalized;
}
