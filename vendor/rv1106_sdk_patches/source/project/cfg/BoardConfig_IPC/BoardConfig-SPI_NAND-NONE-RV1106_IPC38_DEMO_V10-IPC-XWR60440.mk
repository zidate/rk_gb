#!/bin/bash

# Target arch
export RK_ARCH=arm

# Target CHIP
export RK_CHIP=rv1106

# Target Toolchain Cross Compile
export RK_TOOLCHAIN_CROSS=arm-rockchip830-linux-uclibcgnueabihf

# Target boot medium: emmc/spi_nor/spi_nand
export RK_BOOT_MEDIUM=spi_nand

# Uboot defconfig
export RK_UBOOT_DEFCONFIG=rv1106-XWR60440_defconfig

# Uboot defconfig fragment
export RK_UBOOT_DEFCONFIG_FRAGMENT=rk-sfc.config

# Kernel defconfig
export RK_KERNEL_DEFCONFIG=rv1106_defconfig

# Kernel defconfig fragment
#export RK_KERNEL_DEFCONFIG_FRAGMENT="rv1106-ipc.config rv1106-XWR60440-nand.config"
export RK_KERNEL_DEFCONFIG_FRAGMENT=rv1106-ipc-XWR60440.config

# Kernel dts
export RK_KERNEL_DTS=rv1106g-38x38-ipc-XWR60440-v10-spi-nand.dts

#misc image
export RK_MISC=wipe_all-misc.img

# Config sensor IQ files
# RK_CAMERA_SENSOR_IQFILES format:
#     "iqfile1 iqfile2 iqfile3 ..."
# ./build.sh media and copy <SDK root dir>/output/out/media_out/isp_iqfiles/$RK_CAMERA_SENSOR_IQFILES
export RK_CAMERA_SENSOR_IQFILES="cv4005_CMK-OT2274-V10_28IRC-F20.json"

# Config sensor lens CAC calibrattion bin files
#export RK_CAMERA_SENSOR_CAC_BIN="CAC_sc4336_OT01_40IRC_F16 CAC_sc530ai_CMK-OT2115-PC1_30IRC-F16"

# Config CMA size in environment
export RK_BOOTARGS_CMA_SIZE="66M"

# config partition in environment
# RK_PARTITION_CMD_IN_ENV format:
#     <partdef>[,<partdef>]
#       <partdef> := <size>[@<offset>](part-name)
# Note:
#   If the first partition offset is not 0x0, it must be added. Otherwise, it needn't adding.
export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),1M(uboot),4M(boot_a),4M(boot_b),10M(rootfs_a),10M(rootfs_b),32M(oem_a),32M(oem_b),1792K(reserved),256K(misc),-(userdata)"
#export RK_PARTITION_CMD_IN_ENV="64K(env),128K@64K(idblock),192K(uboot),2M(boot),2M(rootfs),8M(oem),-(userdata)"

# config partition's filesystem type (squashfs is readonly)
# emmc:    squashfs/ext4
# nand:    squashfs/ubifs
# spi nor: squashfs/jffs2
# RK_PARTITION_FS_TYPE_CFG format:
#     AAAA:/BBBB/CCCC@ext4
#         AAAA ----------> partition name
#         /BBBB/CCCC ----> partition mount point
#         ext4 ----------> partition filesystem type
export RK_PARTITION_FS_TYPE_CFG=rootfs_a@IGNORE@squashfs,oem_a@/oem@squashfs,userdata@/userdata@ubifs

# config filesystem compress (Just for squashfs or ubifs)
# squashfs: lz4/lzo/lzma/xz/gzip, default xz
# ubifs:    lzo/zlib, default lzo
export RK_SQUASHFS_COMP=xz
export RK_UBIFS_COMP=lzo

# app config
export RK_APP_TYPE=RKIPC_RV1106_DUAL_IPC

# build ipc web backend
export RK_APP_IPCWEB_BACKEND=n

# enable install app to oem partition
export RK_BUILD_APP_TO_OEM_PARTITION=y

# disable build gdb
export RK_ENABLE_GDB=n

# disable build adb
export RK_ENABLE_ADBD=n

# enable rockchip test
export RK_ENABLE_ROCKCHIP_TEST=n

export RK_ENABLE_WIFI=y
export RK_ENABLE_WIFI_CHIP=RTL8188FTV
# config AUDIO model
export RK_AUDIO_MODEL=NONE

# config AI-ISP model
export RK_AIISP_MODEL=NONE

# config NPU model
export RK_NPU_MODEL="object_detection_pfp_896x512.data"


# Config SPI NAND or SLC NAND
# RK_NAND_BLOCK_SIZE: block size (default 128 KB)
# RK_NAND_PAGE_SIZE: page size   (default 2 KB)
export RK_NAND_BLOCK_SIZE=0x20000
export RK_NAND_PAGE_SIZE=2048
