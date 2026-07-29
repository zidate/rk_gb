#ifndef APP_UPDATE_OTA_DOWNLOAD_H
#define APP_UPDATE_OTA_DOWNLOAD_H

#include <stdint.h>

/* 下载文件、校验 32 位十六进制 MD5，并原子发布到 final_path。 */
int OtaDownloadFile(const char *url,
                    const char *temporary_path,
                    const char *final_path,
                    const char *expected_md5,
                    uint32_t *file_size);

#endif
