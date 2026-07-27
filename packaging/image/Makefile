.PHONY: all clean
.PHONY: rebuild-raw

all: rebuild-raw
	$(Q)./packaging-raw ./partition.ini ./raw.bin

rebuild-raw:
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-raw clean all
	$(Q)cp ./tools-sourcecode/packaging-raw/packaging-raw ./packaging-raw

clean:
	$(Q)rm -f ./oem.img
	$(Q)rm -f ./rootfs.img
	$(Q)rm -f ./linux.bin
	$(Q)rm -f ./upgrade.bin
	$(Q)rm -f ./raw.bin
	$(Q)rm -f ./conf.jffs2
	$(Q)$(MAKE) -C ./tools-sourcecode/packaging-raw clean
