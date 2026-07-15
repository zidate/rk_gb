#ifndef CMIOT_OSD_CONTROL_H_
#define CMIOT_OSD_CONTROL_H_

#include "cmiot_define.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Text OSD index 0..4 maps to hardware RGN 1..5. RGN 6/7 are reserved. */
#define CMIOT_APP_OSD_TEXT_MAX 5

/* Returns CMIOT_APP_OSD_TEXT_MAX. */
int cmiot_osd_get_text_capacity(void);

/*
 * Applies cmiotOSDInfo_t through the existing media OSD path.
 * Returns 0 on success, -1 for invalid input, or the media apply error.
 */
int cmiot_osd_set_config(const cmiotOSDInfo_t *info);

/*
 * Reads the last cmiot-applied OSD config, or converts current media OSD state
 * to custom mode when cmiot has not set one yet.
 *
 * To read text content, caller should set info->mode and provide the matching
 * text pointer plus textNum as capacity before calling. If no buffer is
 * supplied, textNum returns the required item count. Returns -2 when the
 * supplied buffer is too small and partial data was copied.
 */
int cmiot_osd_get_config(cmiotOSDInfo_t *info);

#ifdef __cplusplus
}
#endif

#endif
