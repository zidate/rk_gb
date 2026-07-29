.PHONY: all clean rebuild-raw rebuild-update

all: rebuild-raw rebuild-update
	$(Q)./packaging-raw ./partition.ini ./raw.bin
	$(Q)./packaging-update ./upgrade.ini ./ota.bin

rebuild-raw:
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-raw clean all
	$(Q)cp ./tools-sourcecode/packaging-raw/packaging-raw ./packaging-raw

rebuild-update:
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-update clean all
	$(Q)cp ./tools-sourcecode/packaging-update/packaging-update ./packaging-update

clean:
	$(Q)rm -f ./oem.img
	$(Q)rm -f ./rootfs.img
	$(Q)rm -f ./linux.bin
	$(Q)rm -f ./upgrade.bin
	$(Q)rm -f ./ota.bin
	$(Q)rm -f ./raw.bin
	$(Q)rm -f ./conf.jffs2
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-raw clean
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-update clean
	$(Q)rm -f ./packaging-raw ./packaging-update
