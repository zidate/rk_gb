#ifndef APP_UPDATE_AB_UPDATE_H
#define APP_UPDATE_AB_UPDATE_H

/* Apply a boot/rootfs/oem tar package through the board's rk_ota utility. */
int AbUpdateApply(const char *package_path, bool reboot_after_success);

#endif
