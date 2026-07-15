#ifndef CMIOT_OSD_CONTROL_H_
#define CMIOT_OSD_CONTROL_H_

#include "cmiot_define.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 文本索引 0..4 映射硬件 RGN 1..5，RGN 6/7 保留。 */
#define CMIOT_APP_OSD_TEXT_MAX 5

/* 返回 CMIOT_APP_OSD_TEXT_MAX。 */
int cmiot_osd_get_text_capacity(void);

/* 加载持久化配置，并仅在当前平台为 CMIOT 时恢复启用的水印。 */
int cmiot_osd_initialize(void);

/* 当前平台为 CMIOT 且持久化 osdSwitch=1 时返回 1。 */
int cmiot_osd_is_override_active(void);

/* 独立保存并应用 cmiotOSDInfo_t，失败返回负值或媒体层错误码。 */
int cmiot_osd_set_config(const cmiotOSDInfo_t *info);

/*
 * 读取持久化的 cmiot OSD。调用方通过 mode、text 指针和 textNum 容量提供
 * 输出缓冲区；缓冲区不足时复制可容纳部分并返回 -2。
 */
int cmiot_osd_get_config(cmiotOSDInfo_t *info);

#ifdef __cplusplus
}
#endif

#endif
