/* 将外部 0-10000 OSD 坐标在最终媒体边界转换为 RK 设备坐标。 */
#ifndef NORMALIZED_OSD_CONTROL_H_
#define NORMALIZED_OSD_CONTROL_H_

int ApplyNormalizedOsdCommon(int fontSize,
                             const char* fontColorMode,
                             const char* fontColor);
int ApplyNormalizedOsdTime(int dateType, int timeType, int displayWeek,
                           int x, int y, int show, int alignment);
int ApplyNormalizedOsdText(int index, const char* text,
                           int x, int y, int show, int alignment);
int OsdCharacterMarginToNormalized(float characters, int fontSize,
                                   int horizontal);

#endif
