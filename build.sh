#!/bin/bash

ROOT=$(cd $(dirname $0) && pwd )
BUILD_DIR=$ROOT/cmake-build
BUILD_MID_DIR=$ROOT/Middleware/cmake-build
DAEMON_DIR=$ROOT/daemon
BURNTOOL_DIR=$ROOT/burntool

echo $ROOT
echo $DAEMON_DIR
echo $SDUPDATE_DIR

CHIP_TYPE=RV1106_DUAL_IPC
#BOARD_TYPE=RV1103_DUAL_IPC
#励国安保灯
# BOARD_TYPE=RC0240_LGV10
# BLE_TYPE=ATBM6012B
#励国安保灯

#双目枪
BOARD_TYPE=RC0240
BLE_TYPE=ATBM6062
#双目枪


PACKAGING=$ROOT/packaging
SD_IMAGE_NAMES=(env.img idblock.img uboot.img boot.img rootfs.img oem.img)
PACKAGING_ARCHIVE=$ROOT/packaging.7z
PACKAGING_FIXED_IMAGE_MEMBERS=(
	packaging/image/env.img
	packaging/image/idblock.img
	packaging/image/uboot.img
	packaging/image/boot.img
)

# BLE_TYPE=ATBM6132
#BLE_TYPE=AIC8800DL
#DMODE=debug
DMODE=release
if [ -z $CROSS ];then
	source $ROOT/envsetup.sh
	echo "CROSS = $CROSS"
else
	echo "CROSS = $CROSS"
fi

function daemon-clean()
{
	make clean -C $DAEMON_DIR;
}

function daemon()
{
	make -C $DAEMON_DIR;
}
#----------------------------
function burntool-clean()
{
	make clean -C $BURNTOOL_DIR;
}
#----------------------------
function burntool()
{
	make -C $BURNTOOL_DIR;
}
#----------------------------
function clean_mid()
{
	rm -rf $BUILD_MID_DIR
	rm -rf $ROOT/Middleware/Lib
}
#----------------------------
function make_mid()
{
	if [ ! -d $BUILD_MID_DIR ]; then
		mkdir $BUILD_MID_DIR
	fi
	cd $BUILD_MID_DIR
	cmake .. -D$CHIP_TYPE=ON -D$BOARD_TYPE=ON -D$DMODE=ON -DRK_ENABLE_IPV6_SOCKET=ON
	make -j4
	cd $ROOT
}
#----------------------------
function clean_app()
{
	rm -rf $BUILD_DIR
	rm -rf $ROOT/Bin
	rm -rf $ROOT/Lib/Package
	daemon-clean
	burntool-clean
}
#----------------------------
function make_app()
{
	if [ ! -d $BUILD_DIR ]; then
		mkdir $BUILD_DIR
	fi
	cd $BUILD_DIR
	cmake .. -D$CHIP_TYPE=ON -D$BOARD_TYPE=ON -D$BLE_TYPE=ON -D$DMODE=ON -DRK_ENABLE_IPV6_SOCKET=ON -DRK_ENABLE_GB_IPV6_SOCKET=OFF
	make -j4
	cd $ROOT
	daemon
	burntool
}
#----------------------------
function clean_all()
{
	clean_mid
	clean_app
}
#----------------------------
function make_all()
{
	make_mid
	make_app
}
#----------------------------
function image()
{
	OSD_FONT_SRC=$ROOT/Middleware/libmpp/rkipc/common/osd/noto_serif_sc_gb2312.otf
	OSD_FONT_DST=$PACKAGING/oem_ipc/usr/share/noto_serif_sc_gb2312.otf
	OTA_PACKAGE=$ROOT/Release/ota.bin
	if [ ! -f "$PACKAGING_ARCHIVE" ]; then
		echo "packaging archive not found: $PACKAGING_ARCHIVE"
		return 1
	fi
	for fixed_image_member in "${PACKAGING_FIXED_IMAGE_MEMBERS[@]}"; do
		rm -f "$ROOT/$fixed_image_member" || return 1
	done
	python3 -m py7zr x "$PACKAGING_ARCHIVE" "$ROOT" \
		--files "${PACKAGING_FIXED_IMAGE_MEMBERS[@]}" || return 1
	for fixed_image_member in "${PACKAGING_FIXED_IMAGE_MEMBERS[@]}"; do
		if [ ! -f "$ROOT/$fixed_image_member" ]; then
			echo "fixed image missing from packaging.7z: $fixed_image_member"
			return 1
		fi
	done
	if ! python3 "$ROOT/tools/verify_uboot_env.py" "$PACKAGING/image/env.img"; then
		echo "packaging.7z env.img is not a valid A/B environment image"
		return 1
	fi
	if [ "$(stat -c %s "$PACKAGING/image/idblock.img")" -le 0 ] ||
	   [ "$(stat -c %s "$PACKAGING/image/idblock.img")" -gt $((1024 * 1024)) ] ||
	   [ "$(stat -c %s "$PACKAGING/image/uboot.img")" -le 0 ] ||
	   [ "$(stat -c %s "$PACKAGING/image/uboot.img")" -gt $((1024 * 1024)) ] ||
	   [ "$(stat -c %s "$PACKAGING/image/boot.img")" -le 0 ] ||
	   [ "$(stat -c %s "$PACKAGING/image/boot.img")" -gt $((4 * 1024 * 1024)) ]; then
		echo "packaging.7z fixed image size validation failed"
		return 1
	fi
	if [ ! -f "$OSD_FONT_SRC" ]; then
		echo "OSD font not found: $OSD_FONT_SRC"
		return 1
	fi
	mkdir -p "$(dirname "$OSD_FONT_DST")"
	cp -f "$OSD_FONT_SRC" "$OSD_FONT_DST" || return 1
	echo "make image ..."
	make -C "$PACKAGING" || return 1
	for image_name in "${SD_IMAGE_NAMES[@]}" ota.bin; do
		if [ ! -f "$PACKAGING/image/$image_name" ]; then
			echo "OTA image not found: $PACKAGING/image/$image_name"
			return 1
		fi
	done
	if [ ! -f "$OTA_PACKAGE" ]; then
		echo "OTA package not found: $OTA_PACKAGE"
		return 1
	fi
	echo "OTA package: $OTA_PACKAGE"
	echo "make image $PACKAGING $BOARD_TYPE $BLE_TYPE end ..."
}
#----------------------------
function image-clean()
{
	echo "clean image ..."
	make clean -C $PACKAGING;
	rm -f "$ROOT/Release/ota.bin"
	echo "clean image $PACKAGING $BOARD_TYPE $BLE_TYPE end ..."
}
#----------------------------
function usage()
{
	echo ""
	echo "Usage: build.sh [Options] [Project]"
	echo ""

	echo "Available Options :"
	echo "all             -build all [app image]"
	echo "clean           -build clean all"
	echo "app             -build app"
	
	# echo "app-clean     -build clean Middleware"
	# echo "mw            -build Middleware"
	# echo "mw-clean      -build clean Middleware"
	# echo "pkg           -build Package"
	# echo "pkg-clean     -build clean Package"
	# echo "image         -build packaging image"
	# echo "image-clean   -build packaging image clean"

	echo ""
	echo "Available Project :"
	echo "----------------------------------------------------------------"
	echo "         1: RC0240(cob atbm6062/rtl8818ftv + gc2063 )"
	echo "----------------------------------------------------------------"
	echo "         2: RC0240V30(cob aic8800dl + gc2063 )"
	echo "----------------------------------------------------------------"
	echo "         3: RC0240V40(cob atbm6132  + gc2063 )"
	echo "----------------------------------------------------------------"
	echo "         4: RC0240V20(cob atbm6062  + cv2003 )"
	echo "----------------------------------------------------------------"
	echo ""

}
#----------------------------
function check()
{
	usage
	echo 'Which project would you like?:[Select 1~3]'
	read aNum
	case $aNum in
		1)  echo 'Select 1: RC0240(cob atbm6062/rtl8818ftv + gc2063 )'
		BOARD_TYPE=RC0240
		BLE_TYPE=ATBM6062
		CHIP_TYPE=RV1106_DUAL_IPC
		;;
		2)  echo 'Select 2: RC0240V30(cob aic8800dl + gc2063 )'
		BOARD_TYPE=RC0240V30
		BLE_TYPE=AIC8800DL
		CHIP_TYPE=RV1106_DUAL_IPC
		;;
		3)  echo 'Select 3: RC0240V40(cob atbm6132 + gc2063 )'
		BOARD_TYPE=RC0240V40
		BLE_TYPE=ATBM6132
		CHIP_TYPE=RV1106_DUAL_IPC
		;;
		4)  echo 'Select 4: RC0240V20(cob atbm6062  + cv2003 )'
		BOARD_TYPE=RC0240V20
		BLE_TYPE=ATBM6062
		CHIP_TYPE=RV1106_DUAL_IPC
		;;		
	esac

	echo "CHIP_TYPE=$CHIP_TYPE"
	echo "BOARD_TYPE=$BOARD_TYPE"
	echo "BLE_TYPE=$BLE_TYPE"
	PACKAGING=$ROOT/packaging-$BOARD_TYPE
	echo "PACKAGING=$PACKAGING"
	
	sleep 3
}

if [ "$1" == "clean" ];then
#	check $2
	clean_all
	image-clean
elif [ "$1" == "all" ];then
	DMODE=release
#	check $2
	clean_all
	make_all
	echo "BOARD_TYPE=$BOARD_TYPE"
	image
elif [ "$1" == "debug" ];then
	DMODE=debug
#	check $2
#	clean_all
	make_all
	echo "BOARD_TYPE=$BOARD_TYPE"
	image
elif [ "$1" == "app" ];then
#	check $2
	make_app
elif [ "$1" == "app-clean" ];then
#	check $2
	clean_app
elif [ "$1" == "mid" ];then
#	check $2
	make_mid
elif [ "$1" == "mid-clean" ];then
#	check $2
	clean_mid
elif [ "$1" == "image" ];then
#	check $2
	image
elif [ "$1" == "image-clean" ];then
#	check $2
	image-clean
else
	usage
fi
