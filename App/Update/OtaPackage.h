#ifndef APP_UPDATE_OTA_PACKAGE_H
#define APP_UPDATE_OTA_PACKAGE_H

#include <stddef.h>

/* 校验 RV1106 ota.bin，并生成仅供 rk_ota 使用的临时 tar。 */
int OtaPackageCreateTar(const char *package_path, char *tar_path, size_t tar_path_size);

#endif
