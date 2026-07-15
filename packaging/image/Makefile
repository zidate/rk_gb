all:
	$(Q)./packaging-update ./merge.ini ./linux.bin
	$(Q)./packaging-raw ./partition.ini ./raw.bin

clean:
	$(Q)rm -f ./oem.img
	$(Q)rm -f ./rootfs.img
	$(Q)rm -f ./linux.bin
	$(Q)rm -f ./upgrade.bin
	$(Q)rm -f ./raw.bin
	$(Q)rm -f ./conf.jffs2
