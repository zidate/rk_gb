#ifndef APP_UPDATE_AB_UPDATE_H
#define APP_UPDATE_AB_UPDATE_H

/* 校验并应用包含 boot/rootfs/oem 的 RV1106 ota.bin。 */
int AbUpdateApply(const char *package_path, bool reboot_after_success);

#endif
