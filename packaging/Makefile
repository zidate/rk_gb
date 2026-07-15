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

.PHONY: all images factory artifacts clean

all: factory artifacts

images:
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

artifacts: images
	$(Q)mkdir -p $(RELEASE_DIR)/sd
	$(Q)rm -f $(RELEASE_DIR)/ota_ab.tar $(RELEASE_DIR)/sd/*.img
	$(Q)cp $(IMAGE_DIR)/uboot.img $(IMAGE_DIR)/boot.img $(IMAGE_DIR)/rootfs.img $(IMAGE_DIR)/oem.img $(RELEASE_DIR)/sd/
	$(Q)tar -C $(IMAGE_DIR) -cf $(RELEASE_DIR)/ota_ab.tar boot.img rootfs.img oem.img

clean:
	$(Q)rm -rf $(PACKAGING)/userdata $(PACKAGING)/rootfs $(PACKAGING)/oem
	$(Q)rm -f $(RELEASE_DIR)/ota_ab.tar
	$(Q)rm -rf $(RELEASE_DIR)/sd
	$(Q)$(MAKE) clean -C $(IMAGE_DIR)
