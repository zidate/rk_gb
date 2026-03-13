if (debug) 
	add_definitions(-DDDBUG)
endif()

add_definitions(-DPROTOCOL_HAS_GB28181_CLIENT_SDK=1)
	
message("compile for RV1106_DUAL_IPC")

if (RC0240) 
	add_definitions(-DRC0240)
elseif (RC0240_LGV10)
	add_definitions(-DRC0240_LGV10)
endif()

#指定头文件
include_directories(
	${PROJECT_SOURCE_DIR}/Middleware/Include/
	${CMAKE_CURRENT_SOURCE_DIR}/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/ffmpeg/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/fdk-aac/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/libevent/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/Ble/
	${CMAKE_CURRENT_SOURCE_DIR}/../Include/TestModuleV2/
	${CMAKE_CURRENT_SOURCE_DIR}/Manager/
	${CMAKE_CURRENT_SOURCE_DIR}/Media/
	${CMAKE_CURRENT_SOURCE_DIR}/Net/
	${CMAKE_CURRENT_SOURCE_DIR}/Onvif/
	${CMAKE_CURRENT_SOURCE_DIR}/Rtsp/
	${CMAKE_CURRENT_SOURCE_DIR}/RtspServer/src/
	${CMAKE_CURRENT_SOURCE_DIR}/RtspServer/src/3rdpart/
	${CMAKE_CURRENT_SOURCE_DIR}/RtspServer/src/net/
	${CMAKE_CURRENT_SOURCE_DIR}/RtspServer/src/xop/
	${CMAKE_CURRENT_SOURCE_DIR}/Pcba/
	${CMAKE_CURRENT_SOURCE_DIR}/ProduceNew/
	${CMAKE_CURRENT_SOURCE_DIR}/HttpServer/
	${CMAKE_CURRENT_SOURCE_DIR}/Ptz/
	${CMAKE_CURRENT_SOURCE_DIR}/QrCode/
	${CMAKE_CURRENT_SOURCE_DIR}/tuya/
	${CMAKE_CURRENT_SOURCE_DIR}/Update/
	${CMAKE_CURRENT_SOURCE_DIR}/Protocol/
	${CMAKE_CURRENT_SOURCE_DIR}/Protocol/config/
	${CMAKE_CURRENT_SOURCE_DIR}/Protocol/gb28181/
	${PROJECT_SOURCE_DIR}/third_party/media-server/libmpeg/include/
	${PROJECT_SOURCE_DIR}/third_party/media-server/librtp/include/
    ${CMAKE_CURRENT_SOURCE_DIR}/Encryption/
    ${CMAKE_CURRENT_SOURCE_DIR}/ProductCof/
)

if (RC0240) 
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Storage/common)
elseif (RC0240_LGV10)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Storage/lg)
endif()

#指定库头文件 ble 配网库
if (ATBM6062) #根据宏开关判断逻辑
    message("compile for ATBM6062")
	link_directories(${PROJECT_SOURCE_DIR}/Lib/Device/Ble/atbm6062)
	add_definitions(-DBLE_ATBM6062)
elseif (ATBM6012B)
	message("compile for ATBM6012B")
	link_directories(${PROJECT_SOURCE_DIR}/Lib/Device/Ble/atbm6012b)
	add_definitions(-DBLE_ATBM6012B)
elseif (ATBM6132)
	message("compile for ATBM6132")
	link_directories(${PROJECT_SOURCE_DIR}/Lib/Device/Ble/atbm6132)
	add_definitions(-DBLE_ATBM6132)
elseif (AIC8800DL)
	message("compile for AIC8800DL")
	link_directories(${PROJECT_SOURCE_DIR}/Lib/Device/Ble/aic8800dl)
	add_definitions(-DBLE_AIC8800DL)
else()
	message(WARNING  "none compile BLE WIFI")
endif()

message("compile for RV1106_DUAL_IPC LIB")
message("compile for RC0240 LIB")
link_directories(${PROJECT_SOURCE_DIR}/Lib/Device/MultitekSDK/RV1106_DUAL_IPC/RC0240)

link_directories(
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/alsa
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/curl
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/ffmpeg
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/iconv
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/json
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/rtsp
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/zxing
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/zbar
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/fdk-aac
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/libevent
	${PROJECT_SOURCE_DIR}/Lib/Device/ThirdPartyLibs/TestModuleV2
	${PROJECT_SOURCE_DIR}/Lib/Device/Tuya
	${PROJECT_SOURCE_DIR}/Lib/Package
	${PROJECT_SOURCE_DIR}/Middleware/Lib
)

#指定源文件
#media 模块
set(Media_SRC 
	Media/AVManager.cpp
	Media/Storage.cpp
	Media/MediaDefaultConfig.cpp
	Media/Record.cpp
	Media/Alarm.cpp
	Media/Camera.cpp
	Media/AudioPrompt.cpp
	Media/IndicatorLight.cpp
	Media/Siren.cpp
	Media/AudioManager.cpp
	Media/CameraFilter.cpp
)

#set(Media_SRC ${Media_SRC} Media/AnBaoLightManager.cpp)

#Manager模块
set(Manager_SRC Manager/ManagerDefaultConfig.cpp)
#Tuya模块
set(Tuya_SRC tuya/Tuya.cpp
	tuya/Tuya_common.cpp
	tuya/tuya_ipc_cloud_storage.cpp
	tuya/tuya_ipc_motion_detect.cpp
	tuya/tuya_ipc_dp_handler.cpp
	tuya/tuya_ipc_dp_utils.cpp
	tuya/tuya_ipc_media_handler.cpp
	tuya/tuya_ipc_p2p.cpp
	tuya/tuya_ipc_sd.cpp
	tuya/tuya_ipc_system_control.cpp
	tuya/tuya_ipc_wifi.cpp
	tuya/tuya_ipc_wired.cpp
	tuya/tuya_main.cpp
	tuya/tuya_ipc_sdk_upgrade_demo.cpp
	tuya/tuya_ipc_sdk_simple_start.cpp
	tuya/tuya_ipc_doorbell.cpp
)
#Net模块
set(Net_SRC Net/NetConfigHook.cpp)
#QrCode模块
set(QrCode_SRC QrCode/QrCode.cpp)
#Http模块
set(HttpServer_SRC HttpServer/HttpServer.cpp)
#产测模块
#set(Produce_SRC ProduceNew/Produce.cpp ProduceNew/NetWifi.cpp)
set(Produce_SRC ProduceNew/NetWifi.cpp)
#新产测模块
set(TestModuleV2_SRC TestModuleV2/TestModuleV2_Demo.cpp TestModuleV2/TestMonitor.cpp)
#存储模块
if (RC0240) 
	set(Storage_SRC Storage/common/DiskManager.cpp
		Storage/common/Mp4_Demuxer.cpp
		Storage/common/mp4_muxing.cpp
		Storage/common/Storage_api.cpp
		Storage/common/StorageManager.cpp
		Storage/common/RingBuffer.cpp
	)
elseif (RC0240_LGV10)
	set(Storage_SRC Storage/lg/DiskManager.cpp
		Storage/lg/Mp4_Demuxer.cpp
		Storage/lg/mp4_muxing.cpp
		Storage/lg/Storage_api.cpp
		Storage/lg/StorageManager.cpp
		Storage/lg/RingBuffer.cpp
	)
endif()

#云台模块
set(Ptz_SRC Ptz/Ptz.cpp)
#Pcba测试模块
set(Pcba_SRC Pcba/Pcba.cpp)
#wdt模块
set(Wdt_SRC Wdt/Wdt.cpp)
#Rtsp serer模块
# set(Rtsp_SRC Rtsp/VideoRTSPMan.cpp
# 		Rtsp/StreamSource.cpp
# 		Rtsp/live_rtsp.cpp
# )
set(Rtsp_SRC Rtsp/live_rtsp.cpp)

aux_source_directory(RtspServer/src/net RtspServer_net_SRC)
aux_source_directory(RtspServer/src/xop RtspServer_xop_SRC)

#message("RtspServer_net_SRC=${RtspServer_net_SRC}")
#message("RtspServer_xop_SRC=${RtspServer_xop_SRC}")

set(Onvif Onvif/Onvif.cpp)
#升级烧录模块
set(Update Update/update.cpp)
set(Protocol_SRC
	Protocol/ProtocolManager.cpp
	Protocol/config/LocalConfigProvider.cpp
	Protocol/gb28181/GB28181BroadcastBridge.cpp
	Protocol/gb28181/GB28181ListenBridge.cpp
	Protocol/gb28181/GB28181RtpPsSender.cpp
	Protocol/gb28181/GB28181ClientReceiverAdapter.cpp
)
#加密和产测配置 模块
set(Encryption_SRC Encryption/Encryption.cpp)
set(ProductCof_SRC ProductCof/ProductCof.cpp)


set(PLATFORM_PORT_ROOT ${PROJECT_SOURCE_DIR}/third_party/platform_sdk_port)
set(GB_SIP_ROOT ${PROJECT_SOURCE_DIR}/third_party/gb_sip/install)

set(GB_SHARE_SHIM_SRC
	Protocol/gb28181/sdk_port/ShareSdkShim.cpp
)

set(GB_NET_SOCKET_SHIM_SRC
	Protocol/gb28181/sdk_port/NetSocketSdkShim.cpp
)

set(SIP_SDK_PORT_SRC
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/Lock.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/Interlocked.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/PFHelper.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract/ThreadPool.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/SipStackSDK.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/SipStackImpl.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/SipEventManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/SipUtil.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/WaiterManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/ResponseWaiter.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common/ClientInfoManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/client/SipClientImpl.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/server/SipServerImpl.cpp
)

set(GB_CLIENT_SDK_PORT_SRC
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/Lock.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/Interlocked.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux/PFHelper.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract/TVTXMLParse.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract/tinyxml2.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract/SysHelper.cpp
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract/MarkupSTL.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/GB28181Client/GB28181ClientSDK.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/GB28181Client/GBClientImpl.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/SDP/SdpUtil.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/SDP/SdpMessage.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/StreamRequestManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/Rtsp/GBMANSRTSP.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/GBTime.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/slothxml.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/catalog_query.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/device_info.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/ptz_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/record_query.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/zoom_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/record_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/Iframe_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/alarm_sub.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/alarm_reset.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/catalog_sub.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/mobile_position_sub.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/mobile_position_notify.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/config_query.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/record_index_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/device_status_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/home_position_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/catalog_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/alarm_notify.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/base_proco.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/preset_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/configdownload_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/broadcast_notify.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/media_status_notify.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/keepalive_response.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/guard_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML/teleboot_control.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/SubscribeManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/waiter/WaiterManager.cpp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/waiter/ResponseWaiter.cpp
	${GB_NET_SOCKET_SHIM_SRC}
)

add_library(share_sdk_port STATIC ${GB_SHARE_SHIM_SRC})
target_include_directories(share_sdk_port PRIVATE
	${PLATFORM_PORT_ROOT}/CommonFile/CommonLib
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer1_Common
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux
)
target_compile_definitions(share_sdk_port PRIVATE CONFIG_LINK_WITHOUT_LOG4CXX __ENVIRONMENT_LINUX__)
target_compile_options(share_sdk_port PRIVATE -fpermissive -Wno-unused-parameter -Wno-invalid-offsetof)

add_library(sip_sdk_port SHARED ${SIP_SDK_PORT_SRC})
target_include_directories(sip_sdk_port PRIVATE
	${PLATFORM_PORT_ROOT}/CommonFile/CommonLib
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer1_Common
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/client
	${PLATFORM_PORT_ROOT}/CommonLibSrc/SipSDK/common
	${GB_SIP_ROOT}/include
	${GB_SIP_ROOT}/include/osipparser2/headers
)
target_compile_definitions(sip_sdk_port PRIVATE CONFIG_LINK_WITHOUT_LOG4CXX __ENVIRONMENT_LINUX__)
target_compile_options(sip_sdk_port PRIVATE -fpermissive -Wno-unused-parameter -Wno-invalid-offsetof -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_libraries(sip_sdk_port
	share_sdk_port
	${GB_SIP_ROOT}/lib/libeXosip2.a
	${GB_SIP_ROOT}/lib/libosip2.a
	${GB_SIP_ROOT}/lib/libosipparser2.a
	pthread
	resolv
	uuid
	dl
)
set_target_properties(sip_sdk_port PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Bin
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "$ORIGIN"
)

add_library(gb28181_client_sdk_port SHARED ${GB_CLIENT_SDK_PORT_SRC})
target_include_directories(gb28181_client_sdk_port PRIVATE
	${PLATFORM_PORT_ROOT}/CommonFile/CommonLib
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer1_Common
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer2_Platform/Linux
	${PLATFORM_PORT_ROOT}/CommonFile/Common/Layer3_Abstract
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/GB28181Client
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/SDP
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/Rtsp
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/XML
	${PLATFORM_PORT_ROOT}/CommonLibSrc/GB28181SDK/include/waiter
	${GB_SIP_ROOT}/include
	${GB_SIP_ROOT}/include/osipparser2/headers
)
target_compile_definitions(gb28181_client_sdk_port PRIVATE CONFIG_LINK_WITHOUT_LOG4CXX __ENVIRONMENT_LINUX__)
target_compile_options(gb28181_client_sdk_port PRIVATE -fpermissive -Wno-unused-parameter -Wno-invalid-offsetof -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_libraries(gb28181_client_sdk_port
	share_sdk_port
	sip_sdk_port
	pthread
	resolv
	uuid
	dl
)
set_target_properties(gb28181_client_sdk_port PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Bin
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "$ORIGIN"
)
#可执行文件
add_executable(${PROJECT_NAME} ./Main.cpp ./Common.cpp ./Inifile.cpp
	${Media_SRC} 
	${Manager_SRC} 
	${Tuya_SRC} 
	${Net_SRC} 
	${QrCode_SRC} 
	${Produce_SRC} 
	${TestModuleV2_SRC}
	${Storage_SRC} 
	${Ptz_SRC} 
	${Pcba_SRC} 
	${Rtsp_SRC} 
	${RtspServer_net_SRC} 
	${RtspServer_xop_SRC}
#	${HttpServer_SRC}
	${Onvif} 
	${Update}
	${Protocol_SRC}
    ${Encryption_SRC}
    ${ProductCof_SRC}
	${Wdt_SRC}
)

#指定连接库文件
set(COMLIBSDIR libManager.a
	libInfra.a
	libExchanger.a
	libjson.a
	libDG_crypto.a
	libDebugInfo.a
	libOnvif.a
)

set(LIBMIDWARE_DIR libmpp.a)

set(LIBTUYA_DIR libtuya_iot.a)

if(ATBM6062 OR ATBM6012B OR ATBM6132)
	set(LIBBLE_DIR libnimble_linux.a)
elseif (AIC8800DL)
	set(LIBBLE_DIR liblbh.a)
endif()


set(LIBFFMPEG_DIR libavdevice.a
	libavfilter.a
	libavformat.a
	libavcodec.a
	libavutil.a
	libswresample.a
	libswscale.a
)

set(LIBEVENT_DIR libevent.a libevent_core.a libevent_extra.a libevent_pthreads.a)
set(LIBICONV_DIR libiconv.a)
#set(LIBFDKAAC_DIR libfdk-aac.a)

set(LIB_TESTMODULEV2   libTestModuleV2.a libTcpModule.a libhv_static.a libLinuxApi.a libLog.a libLogAbstract.a  libTestProtocol.a libStatusCode.a libcjson.a libModBusCRC16.a libBase64.a libb64.a libTestAskBase.a)

set(LIBTI_DIR
z rockit_full rockchip_mpp rkaiq rtsp rkaudio_detect rkaudio rkaudio_common
rkaudio_vqe wpa_client m rkmuxer freetype rockiva iconv rknnmrt rga stdc++ rksysutils drm librkbar.a smartIr
)

#target_link_libraries(${PROJECT_NAME} -Os -Wl,--gc-sections,--no-whole-archive,--no-export-dynamic
target_link_libraries(
	${PROJECT_NAME}
	${COMLIBSDIR}
	${LIBMIDWARE_DIR}
	${LIBBLE_DIR}
	${LIBTUYA_DIR}
	${LIBFFMPEG_DIR}
#	${LIBICONV_DIR}
	${LIBRTSP_DIR}
	${LIBTI_DIR}
#	${LIBFDKAAC_DIR}
#	${LIBEVENT_DIR}
	${LIB_TESTMODULEV2}
	gb28181_client_sdk_port
	sip_sdk_port
	dl
	pthread
)

# 添加子目录，并使用子目录中的 CMakeLists.txt 文件
# add_subdirectory(src/module1)
# add_subdirectory(src/module2)

#PROJECT_SOURCE_DIR：工程的根目录
#PROJECT_BINARY_DIR：运行cmake命令的目录，通常为${CMAKE_CURRENT_SOURCE_DIR}/build
#PROJECT_NAME：返回通过 project 命令定义的项目名称
#CMAKE_CURRENT_SOURCE_DIR：当前处理的 CMakeLists.txt 所在的路径
#CMAKE_CURRENT_BINARY_DIR：target 编译目录
#CMAKE_CURRENT_LIST_DIR：CMakeLists.txt 的完整路径
#EXECUTABLE_OUTPUT_PATH：重新定义目标二进制可执行文件的存放位置
#LIBRARY_OUTPUT_PATH：重新定义目标链接库文件的存放位置
set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)








