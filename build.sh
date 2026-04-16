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
	cmake .. -D$CHIP_TYPE=ON -D$BOARD_TYPE=ON -D$DMODE=ON
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
	cmake .. -D$CHIP_TYPE=ON -D$BOARD_TYPE=ON -D$BLE_TYPE=ON -D$DMODE=ON
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
	echo "make image ..."
	make -C $PACKAGING;
	echo "make image $PACKAGING $BOARD_TYPE $BLE_TYPE end ..."
}
#----------------------------
function image-clean()
{
	echo "clean image ..."
	make clean -C $PACKAGING;
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
