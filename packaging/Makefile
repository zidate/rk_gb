ROOT := ..
PACKAGING := .
IMAGE_DIR := $(PACKAGING)/image
ROOTFS_DIR := $(PACKAGING)/rootfs_pub
OEM_DIR := $(PACKAGING)/oem_ipc
CONF_DIR := $(PACKAGING)/data
RELEASE_DIR := $(ROOT)/Release

ROOTFS_PART_SIZE := 10*0x100000
OEM_PART_SIZE := 32*0x100000
USERDATA_PART_SIZE := 0x1FC0000
SD_IMAGES := env.img idblock.img uboot.img boot.img rootfs.img oem.img
SD_IMAGE_PATHS := $(addprefix $(IMAGE_DIR)/,$(SD_IMAGES))

.PHONY: all images factory artifacts clean

all: artifacts

images:
	$(Q)test -x $(OEM_DIR)/usr/bin/rk_ota
	$(Q)rm -f $(OEM_DIR)/usr/bin/dgiot
	$(Q)rm -f $(ROOTFS_DIR)/usr/bin/daemon
	$(Q)rm -f $(ROOTFS_DIR)/usr/bin/burntool
	$(Q)cp $(ROOT)/Bin/dgiot $(OEM_DIR)/usr/bin/dgiot
	$(Q)cp $(ROOT)/daemon/daemon $(ROOTFS_DIR)/usr/bin/daemon
	$(Q)cp $(ROOT)/burntool/burntool $(ROOTFS_DIR)/usr/bin/burntool
	$(Q)$(CROSS)strip $(OEM_DIR)/usr/bin/dgiot
	$(Q)$(CROSS)strip $(ROOTFS_DIR)/usr/bin/daemon
	$(Q)$(CROSS)strip $(ROOTFS_DIR)/usr/bin/burntool
	$(Q)rm -rf $(PACKAGING)/userdata $(PACKAGING)/rootfs $(PACKAGING)/oem
	$(Q)./mkfs_ubi.sh $(CONF_DIR) $(PACKAGING)/userdata $(USERDATA_PART_SIZE) userdata ubifs
	$(Q)./mkfs_ubi.sh $(ROOTFS_DIR) $(PACKAGING)/rootfs $(ROOTFS_PART_SIZE) rootfs squashfs
	$(Q)./mkfs_ubi.sh $(OEM_DIR) $(PACKAGING)/oem $(OEM_PART_SIZE) oem squashfs
	$(Q)cp $(PACKAGING)/userdata/userdata.img $(IMAGE_DIR)/userdata.img
	$(Q)cp $(PACKAGING)/rootfs/rootfs.img $(IMAGE_DIR)/rootfs.img
	$(Q)cp $(PACKAGING)/oem/oem.img $(IMAGE_DIR)/oem.img
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/userdata.img) -le $$(( $(USERDATA_PART_SIZE) ))
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/rootfs.img) -le $$(( $(ROOTFS_PART_SIZE) ))
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/oem.img) -le $$(( $(OEM_PART_SIZE) ))

factory: images
	$(Q)$(MAKE) -C $(IMAGE_DIR)

artifacts: factory
	$(Q)mkdir -p $(RELEASE_DIR)/sd
	$(Q)rm -f $(RELEASE_DIR)/ota.bin $(RELEASE_DIR)/sd/*.img
	$(Q)test -f $(IMAGE_DIR)/env.img
	$(Q)test -f $(IMAGE_DIR)/idblock.img
	$(Q)test -f $(IMAGE_DIR)/uboot.img
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/env.img) -eq $$((256 * 1024))
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/idblock.img) -gt 0
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/idblock.img) -le $$((1024 * 1024))
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/uboot.img) -gt 0
	$(Q)test $$(stat -c %s $(IMAGE_DIR)/uboot.img) -le $$((1024 * 1024))
	$(Q)cp $(SD_IMAGE_PATHS) $(RELEASE_DIR)/sd/
	$(Q)cp $(IMAGE_DIR)/ota.bin $(RELEASE_DIR)/ota.bin

clean:
	$(Q)rm -rf $(PACKAGING)/userdata $(PACKAGING)/rootfs $(PACKAGING)/oem
	$(Q)rm -f $(RELEASE_DIR)/ota.bin
	$(Q)rm -rf $(RELEASE_DIR)/sd
	$(Q)$(MAKE) clean -C $(IMAGE_DIR)
