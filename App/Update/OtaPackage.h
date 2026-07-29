#ifndef APP_UPDATE_OTA_PACKAGE_H
#define APP_UPDATE_OTA_PACKAGE_H

/* 校验 RV1106 ota.bin，并把三镜像事务性解析到 output_dir。 */
int OtaPackageExtractImages(const char *package_path, const char *output_dir);

/* 清理 output_dir 中固定的 boot/rootfs/oem 镜像。 */
void OtaPackageRemoveImages(const char *output_dir);

#endif
