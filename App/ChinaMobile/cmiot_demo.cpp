#include "demo_public.h"
#include "Common.h"
#include "dev_log.h"
#include "dev_log_test.h"


#define AI_CAP_CONFIG_PATH      "./ai_cap_config.json"

#define CMIOT_CONFIG_PATH 		"/userdata/conf/cmiot/"
//#define CMIOT_CONFIG_PATH 		"/tmp"
#define CMIOT_LOG_PATH 			"/userdata/conf/Log/sdk"
//#define CMIOT_LOG_PATH 			CMIOT_CONFIG_PATH


/* 设备参数 */
cmiotDevParams_t g_devParams = {0};

/* 模拟wifi信息 */
cmiotWifiList_t g_wifiList = {0};

/* 设备信息 */
char g_mac[CMIOT_MAX_MAC_LEN] = {0};
char g_imei[CMIOT_MAX_DEV_ID_LEN] = {0};
char g_key[CMIOT_MAX_ACCESS_KEY_LEN] = {0};
char g_secret[CMIOT_MAX_ACCESS_SECRET_LEN] = {0};
char g_modelid[CMIOT_MAX_MOD_ID_LEN] = {0};
char g_appversion[CMIOT_MAX_VERSION_LEN] = {0};
char g_fwversion[CMIOT_MAX_VERSION_LEN] = {0};

//是否使用无线绑定模式
cmiot_bool_t g_wirelessMode = CMIOT_TRUE;

//是否使用AEC绑定模式
cmiot_bool_t g_aecBindMode  = CMIOT_FALSE;

//SD卡存储视频流是否单独推
cmiot_bool_t g_separateSdStream = CMIOT_FALSE;

//SD卡存储视频流单独推时，SD卡存储视频流参数
cmiotSdStreamParam_t g_sdStreamParams = {0};


/* 二维码数据 */
cmiot_char_t g_qrcodeData[2][128] = {"", ""};

/* 设备类型 */
cmiotDevType_e g_devType = CMIOT_DEV_TYPE_IPC;

/* 日志参数 */
cmiot_uint32_t g_logFileSize = 0;
cmiot_uint32_t g_logFileNum = SDK_LOG_DEFAULT_ROTATE;

/* idleBeforeSleep */
cmiot_uint32_t g_idleCnt = 0;

/* 是否支持1400 */
cmiot_uint32_t g_supportGAT1400 = 0;

/* 是否外部录制 */
cmiot_bool_t g_externalSdRecord = CMIOT_FALSE;
/* 外部录制开关 */
cmiot_int32_t g_externalSdRecordStatus = CMIOT_TRUE;
/* 外部录制模式 */
cmiot_int32_t g_externalSdRecordType = CMIOT_TRUE; 

/* 设备日志保存路径 */
cmiot_char_t g_devLogPath[256] = "";

/* AI能力集 */
cmiot_char_t *g_aiCap = NULL;
cmiot_bool_t g_aiCapJsonFilePath[256] = "";

/* 定时器中模拟云台坐标变化 */
timer_t g_ptzMoveTimerID = 0;

/* 是否使用TUI模式 */
cmiot_bool_t g_tuiMode = CMIOT_FALSE;

/* 是否开启辅码流录制模式 */
cmiot_bool_t g_subcodingMode = CMIOT_FALSE;

/* openapi地址 */
cmiot_char_t g_openapiAddr[256] = {0};

/* 主动日志上报开关 */
cmiot_bool_t g_activeLogReport = CMIOT_FALSE;



pthread_t g_devLogGenTid = 0;

int g_cmiot_inited = 0;
int g_cmiot_online = 0;
int g_cmiot_video_quality = 1;


extern cmiot_bool_t g_devOnlineStatus;
extern cmiot_uint32_t g_upgradeType;
extern cmiotUpgradeInfo_t g_upgradeInfoFw;
extern cmiotUpgradeInfo_t g_upgradeInfoApp;
//extern demoMenu_t g_mainMenu;

extern unsigned char bStartPrivateMode; 		//是否开启隐私模式

extern cmiot_int32_t demo_parse_ai_cap_from_json(cmiot_char_t *aiCap); 

extern int start_ntp();
extern int is_ntp_calibrated();


/* 获取当前系统时间，单位ms */
cmiot_uint64_t get_utc_time_ms(void)
{
    cmiot_uint64_t time = 0;
    struct timeval tv = {0};

    gettimeofday(&tv, NULL);
    time = (cmiot_uint64_t)tv.tv_sec*1000 + (cmiot_uint64_t)tv.tv_usec/1000;

    return time;
}
/* 获取当前系统时间，单位s */
cmiot_uint32_t get_utc_time_s(void)
{
    cmiot_uint32_t time = 0;
    struct timeval tv = {0};

    gettimeofday(&tv, NULL);
    time = (cmiot_uint32_t)tv.tv_sec;

    return time;
}


cmiot_int32_t demo_init_callback_list(cmiotCallbackList_t *cbList)
{
    if (cbList)
    {
        cbList->dev_config_cb = demo_dev_config_callback;
        cbList->get_info_cb = demo_get_Info_callback;
        cbList->dev_ctrl_cb = demo_dev_control_callback;
        cbList->ptz_ctrl_cb = demo_ptz_control_callback;
        cbList->audio_cb = demo_audio_callback;
        cbList->upgrade_cb = demo_upgrade_callback;
        cbList->running_status_cb = demo_running_status_callback;
        cbList->dev_ai_config_cb = demo_dev_ai_config_callback;
        cbList->time_cb = NULL;
        return 0;
    }

    return -1;
}

cmiot_int32_t demo_init_dev_params(cmiotDevParams_t *devParams)
{
    if (devParams)
    {
        strcpy(devParams->mac, g_mac);
        strcpy(devParams->devId, g_imei);
        strcpy(devParams->key, g_key);
        strcpy(devParams->secret, g_secret);
        strcpy(devParams->modelId, g_modelid);
        strcpy(devParams->vendor, "XWRJ");
        strcpy(devParams->productModel, "C4611");
        strcpy(devParams->camAppVer, strlen(g_appversion) ? g_appversion : "2.0.0");
        strcpy(devParams->fwVer, strlen(g_fwversion) ? g_fwversion : "2.0.0");
        strcpy(devParams->configPath, CMIOT_CONFIG_PATH);
        if(!g_externalSdRecord)
            strcpy(devParams->sdCardPath, "/mnt/sdcard");
        else
            memset(devParams->sdCardPath, 0, sizeof(devParams->sdCardPath));

        strcpy(devParams->logPath, CMIOT_LOG_PATH);
        devParams->streamSpace = 4*1024*1024;
        devParams->stream.videoType = 2;    // demo中修改此项时注意同时修改g_curVideoFormat和demo_set_media_thread中配置的流参数
        devParams->stream.videoWidth = 2560;
        devParams->stream.videoHeight = 1440;
        devParams->stream.audioType = 2;
        devParams->stream.audioKhz = 8;
        devParams->stream.audioChannel= 1;

        devParams->subStream.videoType = 2;    // demo中修改此项时注意同时修改g_curVideoFormat和demo_set_media_thread中配置的流参数
        devParams->subStream.videoWidth = 1280;
        devParams->subStream.videoHeight = 720;
        devParams->subStream.audioType = 2;
        devParams->subStream.audioKhz = 8;
        devParams->subStream.audioChannel= 1;

        devParams->devType = g_devType;
        if(devParams->devType == CMIOT_DEV_TYPE_MULTI_CAM)
        {
            devParams->stream.multiCamReso.num = 2;
            devParams->stream.multiCamReso.reso = (cmiotVideoReso_t *)calloc(1, sizeof(cmiotVideoReso_t) * 2);
            if(devParams->stream.multiCamReso.reso == NULL)
            {
                return -1;
            }
            devParams->stream.multiCamReso.reso[0].streamId = 0;
            devParams->stream.multiCamReso.reso[0].videoWidth = 2560;
            devParams->stream.multiCamReso.reso[0].videoHeight = 1440;
            devParams->stream.multiCamReso.reso[1].streamId = 1;
            devParams->stream.multiCamReso.reso[1].videoWidth = 2560;
            devParams->stream.multiCamReso.reso[1].videoHeight = 1440;
            devParams->stream.multiCamPrimaryStreamId = 0;

            devParams->stream.multiCamStream.num = 2;
            devParams->stream.multiCamStream.stream = (cmiotVideoStreamParam_t *)calloc(1, sizeof(cmiotVideoStreamParam_t) * 2);
            if(devParams->stream.multiCamStream.stream == NULL)
            {
                return -1;
            }
            devParams->stream.multiCamStream.stream[0].streamId = 0;
            devParams->stream.multiCamStream.stream[0].streamSpace = 2*1024*1024;
            devParams->stream.multiCamStream.stream[1].streamId = 1;
            devParams->stream.multiCamStream.stream[1].streamSpace = 2*1024*1024;
        }
        devParams->bindMode = g_wirelessMode ? CMIOT_BIND_MODE_WIRELESS : CMIOT_BIND_MODE_WIRED;
        devParams->logMaxSize = g_logFileSize;
        devParams->logRotate = g_logFileNum;
        devParams->supportGAT1400 = g_supportGAT1400;
        devParams->idleBeforeSleep = g_idleCnt;
        devParams->externalSdRecord = g_externalSdRecord;

        strcpy(devParams->devfixedInfo.wiredMac, "AA:BB:CC:DD:EE:01");
        strcpy(devParams->devfixedInfo.wifiMac, "AA:BB:CC:DD:EE:02");
        strcpy(devParams->devfixedInfo.cellularMac, "AA:BB:CC:DD:EE:03");
        devParams->devfixedInfo.ramSize = 128; /* 兆比特 */
        devParams->devfixedInfo.flashSize = 128; /* 兆比特 */

        return 0;
    }

    return -1;
}

cmiot_int32_t demo_init_fixed_capability(cmiotFixedCapability_t *fixedCap)
{
    if (fixedCap)
    {
        fixedCap->wifi       = CMIOT_TRUE;
        fixedCap->ptz        = CMIOT_TRUE;
        fixedCap->ptzEnhance = CMIOT_TRUE;
        fixedCap->ptzSpeed   = CMIOT_TRUE;
        fixedCap->microphone = CMIOT_TRUE;
        fixedCap->fisheye    = CMIOT_FALSE;
        fixedCap->speaker    = CMIOT_TRUE;
        fixedCap->cellular   = CMIOT_FALSE;
        return 0;
    }

    return -1;
}

cmiot_int32_t demo_init_dynamic_capability(cmiotDynamicCapability_t *dynamicCap)
{
    if (dynamicCap)
    {
        dynamicCap->motionRegion                = CMIOT_TRUE;
        dynamicCap->duplexAudioTalk             = CMIOT_TRUE;
        dynamicCap->sdcard                      = CMIOT_TRUE;
        dynamicCap->sdcardEventPlayback         = CMIOT_TRUE;
        dynamicCap->sdcardEventPlaybackV2       = CMIOT_FALSE;
        dynamicCap->motionTrack                 = CMIOT_TRUE;
        dynamicCap->peopleDetection             = CMIOT_TRUE;
        dynamicCap->wideDynamic                 = CMIOT_TRUE;
        dynamicCap->battery                     = CMIOT_FALSE;
        dynamicCap->volumeMute                  = CMIOT_TRUE;
        dynamicCap->motionTrackBackTime         = CMIOT_TRUE;
        dynamicCap->sdcardPlayback              = CMIOT_TRUE;
        dynamicCap->soundLightAlarm             = CMIOT_TRUE;
        dynamicCap->soundLightAlarmVolume       = CMIOT_TRUE;
        dynamicCap->soundLightAlarmModel        = 1;
        dynamicCap->fullColorNightVision        = CMIOT_TRUE;
        dynamicCap->fullColorNightVisionModel   = CMIOT_TRUE;
        dynamicCap->regionInvasion              = CMIOT_TRUE;
        dynamicCap->voiceBroadcast              = CMIOT_TRUE;
        dynamicCap->lightIntensity              = CMIOT_TRUE;
        dynamicCap->peopleTrack                 = CMIOT_TRUE;
        dynamicCap->statusLight                 = CMIOT_TRUE;
        dynamicCap->soundLightAlarmSoundContext = CMIOT_TRUE;
        dynamicCap->soundLightAlarmSoundTimes   = CMIOT_TRUE;
        dynamicCap->soundLightAlarmLightTime    = CMIOT_TRUE;
        dynamicCap->remoteReboot                = CMIOT_TRUE;
        dynamicCap->h264                        = CMIOT_TRUE;
        dynamicCap->h265                        = CMIOT_TRUE;
        dynamicCap->g711a                       = CMIOT_TRUE;
        dynamicCap->aac                         = CMIOT_FALSE;
        dynamicCap->aiAbilityFaceCapture        = 100;
        dynamicCap->aiAbilityFaceContrast       = 0;
        dynamicCap->aiAbilityVehicleCapture     = 100;
        dynamicCap->aiAbilityAiSensitivity      = CMIOT_TRUE;
        dynamicCap->aiAbilityRegions            = CMIOT_TRUE;
        dynamicCap->osd                         = 7;
        dynamicCap->aiAbilityMaskDetect         = 0;
        dynamicCap->aiAbilityEbikeCapture       = 100;
        dynamicCap->aiAbilityPassengerStatistics = 0;
        dynamicCap->aiAbilityPassengerStatisticsRule = 0;
        dynamicCap->aiFaceCaptureSen            = CMIOT_TRUE;
        dynamicCap->aiFaceContrastSen           = CMIOT_FALSE;
        dynamicCap->aiVehicleCaptureSen         = CMIOT_TRUE;
        dynamicCap->aiMaskDetectSen             = CMIOT_FALSE;
        dynamicCap->aiEbikeCaptureSen           = CMIOT_TRUE;
        dynamicCap->aiPassengerStatisticsSen    = CMIOT_FALSE;
        dynamicCap->aiFaceCaptureRegion         = CMIOT_TRUE;
        dynamicCap->aiFaceContrastRegion        = CMIOT_FALSE;
        dynamicCap->aiVehicleCaptureRegion      = CMIOT_TRUE;
        dynamicCap->aiMaskDetectRegion          = CMIOT_FALSE;
        dynamicCap->aiEbikeCaptureRegion        = CMIOT_TRUE;
        dynamicCap->batMode                     = CMIOT_FALSE;
        dynamicCap->simInfo                     = CMIOT_FALSE;
        dynamicCap->cellularSignal              = CMIOT_FALSE;
        dynamicCap->speakerVolume               = CMIOT_TRUE;
        dynamicCap->babyCry                     = CMIOT_FALSE;
        dynamicCap->aec                         = CMIOT_TRUE;
        dynamicCap->presetPoint                 = CMIOT_TRUE;
        dynamicCap->cruise                      = CMIOT_TRUE;
        dynamicCap->clickAlarm                  = CMIOT_TRUE;
        dynamicCap->batAlarm                    = CMIOT_FALSE;
        dynamicCap->cellularAlarm               = CMIOT_FALSE;
        if(g_devType == CMIOT_DEV_TYPE_MULTI_CAM)
            dynamicCap->multiCam = 0x2 << 8 | 0x1;  /* 高8bit表示支持的摄像头个数，范围[2-8]，即至少2个，至多8个。
                                                       低8bit表示每路流对应摄像头是否支持云台，0-不支持，1-支持，
                                                       对应关系bit0-stream_id0, bit1-stream_id1…bit7-stream_id7。
                                                       此处表示streamId0对应画面支持云台，streamId1不支持 */
        dynamicCap->aperture                    = CMIOT_FALSE;
        dynamicCap->focusing                    = CMIOT_FALSE;
        dynamicCap->zoom                        = CMIOT_FALSE;
        dynamicCap->oneTimeSoundAlarm           = CMIOT_TRUE;
        dynamicCap->oneTimeLightAlarm           = CMIOT_TRUE;
        dynamicCap->focalPoint                  = CMIOT_FALSE;
        dynamicCap->aiAlgoDispatch              = CMIOT_FALSE;
        dynamicCap->nightVision                 = CMIOT_TRUE;
        dynamicCap->voiceBroadcastSchedule      = CMIOT_TRUE;

        dynamicCap->osdStatus                   = CMIOT_TRUE;
        dynamicCap->osdGb                       = 0x0406;
        dynamicCap->osdFontSize                 = 0b01000101;
        dynamicCap->osdFontColor                = 0b1100;
        dynamicCap->regionInvasionBox           = CMIOT_TRUE;
        dynamicCap->regionInvasionMode          = 3;
        dynamicCap->soundLightAlarmCfgNew       = 2;
        dynamicCap->soundLightAlarmLightContinuous = CMIOT_TRUE;
        dynamicCap->soundLightAlarmSoundContinuous = CMIOT_TRUE;
        dynamicCap->restoreSettings             = CMIOT_TRUE;
        dynamicCap->cameraImageFlip             = CMIOT_TRUE;
        dynamicCap->microphoneVolume            = CMIOT_TRUE;
        dynamicCap->whiteLight                  = CMIOT_TRUE;
        dynamicCap->eventPushStatus             = CMIOT_TRUE;
        dynamicCap->activeDeviceLogReport       = CMIOT_TRUE;

        dynamicCap->commonSoundAlarm            = 0 << 3 | 5;
        dynamicCap->commonLightAlarm            = 0 << 3 | 5;
        dynamicCap->subStreamRecording = g_subcodingMode ? 0x2F : 0;
        
        return 0;
    }

    return -1;
}

cmiot_int32_t demo_init_ai_capability(const cmiot_char_t *filePath, cmiot_char_t **aiCap)
{
    cmiot_char_t defaultSetCap[] = "{\"face\":{\"capture\":20,\"recognition\":10,\"attr\":10,\"live\":10},\"vehicle\":{\"capture\":5,\"park\":10,\"roadBlock\":15,\"nonVehicleCapture\":20,\"nonVehicleOverload\":10,\"nonVehicleNoHelmet\":10},\"behavior\":{\"regionInvasion\":50,\"lineInvasion\":50,\"fight\":10,\"stay\":10,\"phone\":10,\"hover\":10,\"sleep\":10,\"call\":10,\"smoke\":10,\"fall\":10,\"workClothes\":10,\"run\":10},\"crowdState\":{\"personCount\":100},\"customerFlow\":{\"inOutCount\":100,\"crossLineCount\":100},\"kitchen\":{\"hat\":20,\"mask\":50,\"coatColor\":10},\"elevator\":{\"ebikeNoEntry\":50,\"ebikeFireEscape\":100},\"fallingObjects\":{\"fallingObjects\":100},\"fireSmoke\":{\"fire\":100,\"smoke\":100}}";
	FILE* file = NULL;
	cmiot_uint32_t fileSize;

#if 0
    if(!filePath || !strlen(filePath))
        goto DEFAULT;

    file = fopen(filePath, "r");
    if(!file) {
        DEMO_PRINT("open ai cap json file failed, set to default!\n");
        goto DEFAULT;
    }
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    *aiCap = (cmiot_char_t *)calloc(fileSize + 1, sizeof(cmiot_char_t));
    if(!(*aiCap))
    {
        DEMO_PRINT("malloc ai cap failed, set to default!\n");
        goto DEFAULT;
    }

    fread(*aiCap, 1, fileSize, file);
    (*aiCap)[fileSize] = '\0';

    if(demo_parse_ai_cap_from_json(*aiCap))
    {
        DEMO_PRINT("parse ai cap json[%s] failed, set ai cap empty!\n", filePath);
        memset(*aiCap, 0, fileSize + 1);
    }
    else
    {
        DEMO_PRINT("aiCap: %s\n",*aiCap);
    }

    return 0;
#endif

DEFAULT:
    *aiCap = (cmiot_char_t *)calloc(1, sizeof(defaultSetCap));
    if(!(*aiCap))
    {
        DEMO_PRINT("malloc ai cap failed, set to default!\n");
        return -1;
    }
    memcpy(*aiCap, defaultSetCap, sizeof(defaultSetCap));
    DEMO_PRINT("aiCap: %s\n",*aiCap);
    return 0;
}

/* SDK启动初始化 */
cmiot_int32_t demo_sdk_init(cmiot_bool_t wakeupInit)
{
    cmiotCallbackList_t         cbList = {0};  
    cmiotFixedCapability_t      fixedCap = {0}; 
    cmiotDynamicCapability_t    dynamicCap = {0};

//    if(strlen(g_openapiAddr))
//    {
//        cmiot_char_t server[129] = {0};
//        cmiot_int32_t port = 0;
//        if(sscanf(g_openapiAddr, "%128[^ ] %d", server, &port) != 2)
//        {
//            DEMO_PRINT("server port error, exit!\n");
//            return -1; 
//        }
//    
//        if(cmiot_set_server(server, port) != CMIOT_RETURN_CODE_SUCCESS)
//        {
//            return -1;
//        }
//    }
//	if (cmiot_set_server("111.10.41.192", 8143) == CMIOT_RETURN_CODE_SUCCESS)
//		printf("cmiot_set_server(\"111.10.41.192\", 8143) succ.\n");
//	else
//		printf("cmiot_set_server(\"111.10.41.192\", 8143) fail.\n");

    if (0 != demo_init_callback_list(&cbList))
    {
        DEMO_PRINT("demo_init_callback_list failed!\n");
        return -1;
    }

    if (0 != demo_init_dev_params(&g_devParams))
    {
        DEMO_PRINT("demo_init_dev_params failed!\n");
        return -1;
    }
    g_devParams.wakeupInit = wakeupInit;

    if (0 != demo_init_fixed_capability(&fixedCap))
    {
        DEMO_PRINT("demo_init_fixed_capability failed!\n");
        return -1;
    }

    if (0 != demo_init_dynamic_capability(&dynamicCap))
    {
        DEMO_PRINT("demo_init_dynamic_capability failed!\n");
        return -1;
    }

    if(0 != demo_init_ai_capability(g_aiCapJsonFilePath, &g_aiCap))
    {
        DEMO_PRINT("demo_init_ai_capability failed!\n");
        return -1;
    }

    cmiotAlgoManagerParams_t amParams = {0};
    strcpy(amParams.aiAlgoPath, "./aiAlgo/packages");
    strcpy(amParams.configPath, "./aiAlgo/config");
    strcpy(amParams.logPath, "./aiAlgo/log");
    strcpy(amParams.picPath, "./aiAlgo/pic");
    
    if (CMIOT_RETURN_CODE_SUCCESS != cmiot_sdk_init(&cbList, &g_devParams, &fixedCap, &dynamicCap, strlen(g_aiCap) ? g_aiCap : NULL, /*&amParams*/NULL))
    {
        DEMO_PRINT("cmiot_sdk_init failed!\n");
        return -1;
    }

    g_wifiList.wifiNum = 3;
    g_wifiList.wifi = (cmiotWifiInfo_t *)calloc(1, (sizeof(cmiotWifiInfo_t)) * g_wifiList.wifiNum);
    g_wifiList.wifi[0].connected = 1;
    g_wifiList.wifi[0].signal = 1;
    strcpy(g_wifiList.wifi[0].ssid, "CMIOT_WIFI_1");
    strcpy(g_wifiList.wifi[0].bssid, "0A:52:13:47:29:82");
    strcpy(g_wifiList.wifi[0].encryption, "WPA2");
    g_wifiList.wifi[1].connected = 0;
    g_wifiList.wifi[1].signal = 2;
    strcpy(g_wifiList.wifi[1].ssid, "CMIOT_WIFI_2");
    strcpy(g_wifiList.wifi[1].bssid, "9D:1F:23:47:26:52");
    strcpy(g_wifiList.wifi[1].encryption, "WEP");
    g_wifiList.wifi[2].connected = 0;
    g_wifiList.wifi[2].signal = 4;
    strcpy(g_wifiList.wifi[2].ssid, "CMIOT_WIFI_3");
    strcpy(g_wifiList.wifi[2].bssid, "1D:FC:23:07:66:F3");
    strcpy(g_wifiList.wifi[2].encryption, "WPA2");

    struct sigevent evp; 
    memset(&evp, 0, sizeof(struct sigevent));
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = ptz_timer_handle;
    // evp.sigev_signo = SIGUSR1;
    evp.sigev_value.sival_ptr = NULL;
    evp.sigev_notify_attributes = NULL;
    if (timer_create(CLOCK_MONOTONIC, &evp, &g_ptzMoveTimerID) == -1) 
    {   
        DEMO_PRINT("timer create failed");
        return -1;
    }

    DEMO_PRINT("cmiot_sdk_init success!\n");

    return 0;
}

/* SDK注销 */
cmiot_int32_t demo_sdk_deinit(void)
{
    /* 先停止设备日志测试线程 */
    dev_log_test_stop();

    /* 关闭设备日志模块 */
    dev_log_deinit();

    if (CMIOT_RETURN_CODE_SUCCESS != cmiot_sdk_deinit())
    {
        DEMO_PRINT("cmiot_sdk_deinit failed!\n");
        return -1;
    }

    if(g_wifiList.wifi)
    {
        free(g_wifiList.wifi);
        g_wifiList.wifi = NULL;
    }

    g_devOnlineStatus = CMIOT_FALSE;

    DEMO_PRINT("cmiot_sdk_deinit success!\n");

    return 0;
}

void *demo_report_event(void* args)
{
    if(!args)
    {
        DEMO_PRINT("demo_report_event input params invalid!\n");
        return NULL;
    }

    /* 设置线程取消使能 */
#ifdef ANDROID_ENV
    struct sigaction sa;
    sa.sa_handler = demo_sigterm_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        DEMO_PRINT("sigaction failed!\n");
    }
#else
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#endif

    cmiot_uint32_t streamId = *(cmiot_uint32_t*)args;

    while(1)
    {
        cmiot_uint64_t t = get_utc_time_ms();
        cmiot_event_start(CMIOT_EVENT_MOTION_DETECT, streamId, t, NULL, NULL, 0);
        DEMO_PRINT("demo report event CMIOT_EVENT_MOTION_DETECT start[%llu]!\n", t);
        sleep(11);
        t = get_utc_time_ms();
        cmiot_event_stop(CMIOT_EVENT_MOTION_DETECT, streamId, time(NULL)*1000);
        DEMO_PRINT("demo report event CMIOT_EVENT_MOTION_DETECT stop[%llu]!\n", t);
        sleep(15);
    }
}


cmiot_char_t *trim_end(cmiot_char_t *string)
{
    cmiot_char_t *pTemp = &string[strlen(string)-1];
    while (*pTemp == '\n')
    {
        *pTemp = '\0';
        pTemp--;
    }

    return string;
}

/* 模拟扫描绑定，从文件读取二维码信息，string1、string2分别为两张二维码的信息，string2可选
 * 文件格式：
 * [string1]
 * xxxx
 * xxxx
 * [string2]
 * xxxx
 * xxxx
 */
void read_qrcode_from_file(char *path)
{
    char lineContent[128] = {0};
    char *p = NULL;

    FILE *file = fopen(path, "r");
    if(!file)
    {
        DEMO_PRINT("qrcode file read failed!\n");
        exit(EXIT_FAILURE);
    }
    
    while(fgets(lineContent, sizeof(lineContent), file) != NULL)
    {
        if(strstr(lineContent, "[string1]") != NULL)
        {
            p = g_qrcodeData[0];
            continue;
        }
        if(strstr(lineContent, "[string2]") != NULL)
        {
            p = g_qrcodeData[1];
            continue;
        }

        if(!p)
            continue;

        cmiot_int32_t len = strlen(lineContent);
        if(lineContent[len-2] == '\r' && lineContent[len-1] == '\n')
        {
            lineContent[len-2] = '\n';
            lineContent[len-1] = '\0';
        }
        strncat(p, lineContent, sizeof(g_qrcodeData[0])-strlen(p)-1);
        memset(lineContent, 0, sizeof(lineContent));
    }

    trim_end(g_qrcodeData[0]);
    trim_end(g_qrcodeData[1]);
    DEMO_PRINT("str1:%s , str2:%s", g_qrcodeData[0], g_qrcodeData[1]);
    fclose(file);
}


#if 0
/* ===== 以下为demo日志生成代码, 保留作为参考 ===== */
void *demo_gen_log_to_file(void* args)
{
    FILE *fp = NULL;
    cmiot_char_t filePath[CMIOT_MAX_PATH_LEN] = {0};
    cmiot_char_t logLine[512] = {0};
    cmiot_char_t dateTime[20] = {0};
    cmiot_int32_t fileSize = 0;
    cmiotLogReportInfo_t logInfo = {0};
    struct tm tm = {0};
    struct timeval tv = {0};

    DEMO_PRINT("demo_gen_log_to_file thread started\n");

    while(1)
    {
        memset(dateTime, 0, sizeof(dateTime));
        if(!fp)
        {
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &tm);
            strftime(dateTime, sizeof(dateTime), DEV_LOG_FILE_TIME_PATTERN, &tm);
            snprintf(filePath, sizeof(filePath), "%s/DEV_%s.txt", g_devLogPath, dateTime);
            fp = fopen(filePath, "a");
            if (fp == NULL) 
            {
                DEMO_PRINT("Error opening file %s\n", filePath);
                pthread_exit(NULL);
            }
        }
        else
        {
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &tm);
            strftime(dateTime, sizeof(dateTime), "%Y-%m-%d %H:%M:%S", &tm);
            snprintf(logLine, sizeof(logLine), "[Demo][%s] this is a simulated device log text!!!!!!!!!!!!!!!!!!!!!!!!!\n", dateTime);
            fprintf(fp, "%s", logLine);
            /* 此处为便于测试，仅模拟写日志，实际对接中需要设备按需输出日志到文件 */

            // 获取当前文件大小
            fseek(fp, 0, SEEK_END);
            fileSize = ftell(fp);

            // 写满一个文件时，如主动上报开启，调用上报接口
            if( fileSize >= DEV_LOG_FILE_SIZE ) 
            {
                if(g_activeLogReport)
                {
                    DEMO_PRINT("upload log[%s]\n", filePath);
                    logInfo.logNum = 1;
                    logInfo.logSize = fileSize;
					cmiotLogPath_t stPathInfo;
					memcpy(stPathInfo.logPath, filePath, sizeof(filePath));
                    logInfo.pathInfo = &stPathInfo;
                    cmiot_device_log_report(&logInfo);
                }

                fclose(fp);
                fp = NULL; 
            }
            usleep(100 * 1000);
        }
    }
    pthread_exit(0);
}

/* ===== 以上为demo日志生成代码, 保留作为参考 ===== */
#endif

/* ===== 新的设备日志主动上报回调 ===== */
static void dev_log_upload_handler(const char *filePath, cmiot_uint64_t fileSize)
{
    cmiotLogReportInfo_t logInfo;
    cmiotLogPath_t stPathInfo;

    memset(&logInfo, 0, sizeof(logInfo));
    memset(&stPathInfo, 0, sizeof(stPathInfo));

    logInfo.logNum = 1;
    logInfo.logSize = fileSize;
    strncpy(stPathInfo.logPath, filePath, sizeof(stPathInfo.logPath) - 1);
    logInfo.pathInfo = &stPathInfo;

    printf("[dev_log] uploading log file: %s, size=%llu\n", filePath, fileSize);
    cmiot_device_log_report(&logInfo);
}
static int find_spspps(int codec_type, unsigned char *frame_data, int frame_len)
{
	if (frame_len > 256) //不用查太长
		frame_len = 256;
	if (DMC_MEDIA_TYPE_H264 == codec_type)
	{
		for(int i = 0; i < (frame_len-4); i++)
		{
			if( (0x00 == frame_data[i]) && (0x00 == frame_data[i+1]) && (0x00 == frame_data[i+2]) && (0x01 == frame_data[i+3]) && (0x5 == (frame_data[i+4]&0x1F)) )
			{
				return i;
			}
		}
	}
	else if (DMC_MEDIA_TYPE_H265 == codec_type)
	{
		for(int i = 0; i < (frame_len-1); i++)
		{
			if( (0x00 == frame_data[i]) && (0x00 == frame_data[i+1]) && (0x00 == frame_data[i+2]) && (0x01 == frame_data[i+3]) && (19 == ((frame_data[i+4]&0x7E) >> 1)) )
			{
				return i;
			}
		}
	}
	return -1;
}

static int push_video_to_cmiot_main(int media_type, int media_subtype, unsigned char *frame_data, 
												int frame_len, unsigned int ts)
{
	static uint32_t frame_seq = 0;
	int ret;
	int spspps_len = 0;
	cmiotVideoData_t video;
	
	if (DMC_MEDIA_SUBTYPE_IFRAME == media_subtype)
	{
		spspps_len = find_spspps(media_type, frame_data, frame_len);
		if (spspps_len <= 0)
			return -1;
		
		video.frameType = CMIOT_FRAME_TYPE_I;
		video.isSPSPPS = CMIOT_TRUE;
		video.seqNum = frame_seq++;
		video.ts = ts;
		video.utcms = get_utc_time_ms();// - (8*60*60*1000);
		video.dataSize = spspps_len;
		video.data = (char *)frame_data;
		ret = cmiot_push_video_data(&video, 0);
//		printf("cmiot_push_video_data( frameType: %d, isSPSPPS: %d, seqNum: %u, ts: %u, utcms: %llu, dataSize: %u ) -> return: %d\n", 
//			video.frameType, video.isSPSPPS, video.seqNum, video.ts, video.utcms, video.dataSize, ret);
	}
	
	video.frameType = (DMC_MEDIA_SUBTYPE_IFRAME == media_subtype) ? CMIOT_FRAME_TYPE_I : CMIOT_FRAME_TYPE_P;
	video.isSPSPPS = CMIOT_FALSE;
	video.seqNum = frame_seq++;
	video.ts = ts;
	video.utcms = get_utc_time_ms();// - (8*60*60*1000);
	video.dataSize = frame_len - spspps_len;
	video.data = (char *)frame_data + spspps_len;
	ret = cmiot_push_video_data(&video, 0);
//	printf("cmiot_push_video_data( frameType: %d, isSPSPPS: %d, seqNum: %u, ts: %u, utcms: %llu, dataSize: %u ) -> return: %d\n", 
//		video.frameType, video.isSPSPPS, video.seqNum, video.ts, video.utcms, video.dataSize, ret);
	return 0;
}
												
static int push_video_to_cmiot_sub(int media_type, int media_subtype, unsigned char *frame_data, 
												int frame_len, unsigned int ts)
{
	static uint32_t frame_seq = 0;
	int ret;
	int spspps_len = 0;
	cmiotVideoData_t video;
	
	if (DMC_MEDIA_SUBTYPE_IFRAME == media_subtype)
	{
		spspps_len = find_spspps(media_type, frame_data, frame_len);
		if (spspps_len <= 0)
			return -1;
		
		video.frameType = CMIOT_FRAME_TYPE_I;
		video.isSPSPPS = CMIOT_TRUE;
		video.seqNum = frame_seq++;
		video.ts = ts;
		video.utcms = get_utc_time_ms();// - (8*60*60*1000);
		video.dataSize = spspps_len;
		video.data = (char *)frame_data;
		ret = cmiot_push_sub_video_data(&video, 0);
//		printf("cmiot_push_sub_video_data( frameType: %d, isSPSPPS: %d, seqNum: %u, ts: %u, utcms: %llu, dataSize: %u ) -> return: %d\n", 
//			video.frameType, video.isSPSPPS, video.seqNum, video.ts, video.utcms, video.dataSize, ret);
	}
	
	video.frameType = (DMC_MEDIA_SUBTYPE_IFRAME == media_subtype) ? CMIOT_FRAME_TYPE_I : CMIOT_FRAME_TYPE_P;
	video.isSPSPPS = CMIOT_FALSE;
	video.seqNum = frame_seq++;
	video.ts = ts;
	video.utcms = get_utc_time_ms();// - (8*60*60*1000);
	video.dataSize = frame_len - spspps_len;
	video.data = (char *)frame_data + spspps_len;
	ret = cmiot_push_sub_video_data(&video, 0);
//	printf("cmiot_push_sub_video_data( frameType: %d, isSPSPPS: %d, seqNum: %u, ts: %u, utcms: %llu, dataSize: %u ) -> return: %d\n", 
//		video.frameType, video.isSPSPPS, video.seqNum, video.ts, video.utcms, video.dataSize, ret);
	return 0;
}

static int onCapture_fn(int media_chn,
        int media_type,
        int media_subtype,
        unsigned long long frame_pts,
        unsigned char *frame_data,
        int frame_len,
        int frame_end_flag)
{
	if (bStartPrivateMode)
		return 0;

	static int s_video_quality = g_cmiot_video_quality;
	static int s_key_frame = 0;
	int ret;

	if (s_video_quality != g_cmiot_video_quality)
	{
		s_key_frame = 0;
		s_video_quality = g_cmiot_video_quality;
	}
	
	if (DMC_MEDIA_VIDEO_MAIN_STREAM == media_chn)
	{
		if (1 != s_video_quality)
			return 0;
		if (0 == s_key_frame)
		{
			if (DMC_MEDIA_SUBTYPE_IFRAME != media_subtype)
				return 0;
			s_key_frame = 1;
		}
		push_video_to_cmiot_main(media_type, media_subtype, frame_data, frame_len, (unsigned int)frame_end_flag);
	}
	else if  (DMC_MEDIA_VIDEO_SUB_STREAM == media_chn)
	{
		if (0 == s_video_quality)
		{
			if (0 == s_key_frame)
			{
				if (DMC_MEDIA_SUBTYPE_IFRAME == media_subtype)
					s_key_frame = 1;
			}
			if (1 == s_key_frame)
				push_video_to_cmiot_main(media_type, media_subtype, frame_data, frame_len, (unsigned int)frame_end_flag);
		}
		
		push_video_to_cmiot_sub(media_type, media_subtype, frame_data, frame_len, (unsigned int)frame_end_flag);		
	}
	else if (DMC_MEDIA_AUDIO_SECOND_STREAM == media_chn)
	{
		static uint32_t frame_seq = 0;
		
		cmiotAudioData_t audio;
		audio.seqNum = frame_seq;
		audio.ts = frame_end_flag;
		audio.utcms = get_utc_time_ms();
		audio.dataSize = frame_len;
		audio.data = (char *)frame_data;
		ret = cmiot_push_audio_data(&audio, 0);
		cmiot_push_sub_audio_data(&audio, 0);
//		printf("cmiot_push_audio_data( seqNum: %u, ts: %u, utcms: %llu, dataSize: %u ) -> return: %d\n", 
//			audio.seqNum, audio.ts, audio.utcms, audio.dataSize, ret);
		
		frame_seq++;
	}

	return 0;
}

static void *thread_sd_status_report(void *args)
{
	static int sdcard_error_count = 0;
	static int iLastDiskStatus = DISK_STATUS_UNKNOWN;
	int iDiskStatus = DISK_STATUS_UNKNOWN;
	//监测SD卡状态, 如果卡异常一分钟, 则重启设备
	while (1)
	{
		//获取SD卡状态
		iDiskStatus = g_StorageManager->GetDiskState();
		
//		if( DISK_STATUS_UNKNOWN == iLastDiskStatus )
//		{
//			iLastDiskStatus = iDiskStatus;
//		}
		if( iLastDiskStatus != iDiskStatus )
		{
			/* 通知设备日志模块SD卡状态变化 */
			/* 离开上一个状态 */
			if (iLastDiskStatus == DISK_STATUS_NORMAL) {
				dev_log_notify_sd_removed();
			    dev_log_write(DEV_LOG_INFO, DEV_EVENT_SD_CARD, "SD card removed");
			}
			if (iLastDiskStatus == DISK_STATUS_FORMATING && iDiskStatus == DISK_STATUS_NORMAL) {
				dev_log_notify_sd_format_done();
			}

			/* 进入新状态 */
			if (iDiskStatus == DISK_STATUS_NORMAL) {
				dev_log_notify_sd_inserted();
			    dev_log_write(DEV_LOG_INFO, DEV_EVENT_SD_CARD, "SD card inserted");
			} else if (iDiskStatus == DISK_STATUS_FORMATING) {
				dev_log_notify_sd_format_start();
			}

			iLastDiskStatus = iDiskStatus;

			cmiotSdCardInfo_t data = {CMIOT_SD_CARD_STATUS_READY, ""};
			if( iDiskStatus == DISK_STATUS_NORMAL )
			{
	            /* 格式化成功 */
	            data.status = CMIOT_SD_CARD_STATUS_READY;
			}
			else if ( iDiskStatus == DISK_STATUS_UNKNOWN || iDiskStatus == DISK_STATUS_NO_DISK )
			{
	            data.status = CMIOT_SD_CARD_STATUS_NOT_READY;
			}
			else if ( iDiskStatus == DISK_STATUS_NOT_MOUNT || iDiskStatus == DISK_STATUS_ERROR )
			{
	            data.status = CMIOT_SD_CARD_STATUS_ABNORMAL;
			}
			else if ( iDiskStatus == DISK_STATUS_FORMATING )
			{
	            data.status = CMIOT_SD_CARD_STATUS_FORMATTING;
			}
			cmiot_update_device_config(CMIOT_UPDATE_CFG_SD_STATUS, (char *)&data, sizeof(int));
			demo_warp_cmiot_update_device_config_send();
		}
		
		if( DISK_STATUS_ERROR == iDiskStatus )
		{
			sdcard_error_count++;
		}
		else
		{
			sdcard_error_count = 0;
		}
		
		if( sdcard_error_count >= 60 )
		{
			sdcard_error_count = 0;
			AppErr("sd card error more than 1 min. reboot\n");
//			AbnormalRestart();
		}
		usleep(200*1000);
	}
    pthread_exit(NULL);
}

static void init_cimot_config()
{
	Json::Value table;
	
	//配置文件读取掉电前隐私模式状态
	int status = 0;
	g_configManager.getConfig(getConfigName(CFG_PRIVATE), table);
	status = table["Status"].asInt();
	if(status)
		bStartPrivateMode=1;
	else
		bStartPrivateMode=0;

	table.clear();
	//视频清晰度
	//不需要，SDK 会自己保存，并在初始化后，通过 CMIOT_CMD_CONTROL_VIDEO_QUALITY 通知
	#if 0
	CmiotConf_S stCmiotConf;
	g_configManager.getConfig(getConfigName(CFG_CMIOT), table);
    TExchangeAL<CmiotConf_S>::getConfig(table, stCmiotConf);
	if (stCmiotConf.video_quality)
		g_cmiot_video_quality = 1;
	else
		g_cmiot_video_quality = 0;
	#endif
}

int cmiot_start()
{
    cmiot_int32_t ret;
	
    // 初始化SDK
    DEMO_PRINT("sdk demo start!\n");

	//snprintf(g_mac, sizeof(g_mac), "2ce09989748f");
	//snprintf(g_imei, sizeof(g_imei), "119032221000020");
#if 1//WiFi枪机
#if 1
	snprintf(g_mac, sizeof(g_mac), "2ce099897495");
	snprintf(g_imei, sizeof(g_imei), "119032221000026");
#else//罗工
    snprintf(g_mac, sizeof(g_mac), "2CE099897497");
	snprintf(g_imei, sizeof(g_imei), "119032221000028");
#endif
#else
    snprintf(g_mac, sizeof(g_mac), "2ce099897508");
	snprintf(g_imei, sizeof(g_imei), "119011961000006");
#endif
	snprintf(g_key, sizeof(g_key), "np4pqwcdmu4");
	snprintf(g_secret, sizeof(g_secret), "cwh5ujguzp6tv6jfvwwwh5");
	snprintf(g_modelid, sizeof(g_modelid), "cmiot_c4611_001");

printf("CMIOT_CONFIG_PATH:%s\n", CMIOT_CONFIG_PATH);
	if(access(CMIOT_CONFIG_PATH, F_OK) == -1)
		mkdir(CMIOT_CONFIG_PATH, 0777);
	if(access(CMIOT_LOG_PATH, F_OK) == -1)
		mkdir(CMIOT_LOG_PATH, 0777);

		/* 初始化设备日志模块(确保父目录存在) */
		mkdir("/userdata/conf/Log", 0777);
		if (dev_log_init() != 0) {
			DEMO_PRINT("ERROR: dev_log_init failed!\n");
		}
		dev_log_register_upload_callback(dev_log_upload_handler);

		/* 启动设备日志轮询测试线程(每条间隔2s) */
		dev_log_test_start();


	//获取WiFi配置
	Json::Value WifiTable;
	NetWifiConfig config;
	g_configManager.getConfig(getConfigName(CFG_WIFI), WifiTable);
	TExchangeAL<NetWifiConfig>::getConfig(WifiTable, config);
	if (config.bBind)
	{
		g_NetConfigHook.SetWifiSwitch(true);
		g_NetConfigHook.Init();
		while(NET_WORK_MODE_NONE == g_NetConfigHook.GetNetWorkLindMode())
		{
			sleep(1);
		}
	}
	else
	{
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "rm -f %s/*", CMIOT_CONFIG_PATH);
		printf("cmd: %s\n", cmd);
		START_PROCESS("sh", "sh", "-c", cmd, NULL);
	}

    ret = demo_sdk_init(CMIOT_FALSE);
    if (CMIOT_RETURN_CODE_SUCCESS != ret)
    {
        DEMO_PRINT("init SDK failed. ret = %d!\n",ret);
        sleep(1);  // 防止sdk启动失败后有部分日志打印不全
        return ret;
    }
	g_cmiot_inited = 1;

	while (0 == g_cmiot_online)
	{
		printf("wait cmiot online\n");
		sleep(1);
	}
	start_ntp();
	while (0 == is_ntp_calibrated())
	{
		printf("wait ntp time calibrated\n");
		sleep(1);
	}

	g_Siren.Start();
	g_Siren.SetMDEnable(true);
	g_Camera.start();
	g_Alarm.SetAllowMotionDetTime(time(NULL) + 10);
	g_Alarm.Start();

	CreateDetachedThread((char*)"sd_status_report", thread_sd_status_report, NULL, true);
	
    /* 默认SDK启动后即可推流 */
//    demo_set_media_thread();
	//启动音视频模块
	g_AVManager.RealTimeStreamStart(DMC_MEDIA_TYPE_H264 | DMC_MEDIA_TYPE_H265 | DMC_MEDIA_TYPE_AUDIO, onCapture_fn);


    /* 销毁SDK */
//    demo_sdk_deinit();
    return 0;
}

int cmiot_reset()
{
	if (g_cmiot_inited)
		return cmiot_notify_reset();
	return -1;
}

void cmoit_intrude_event_start_proc(int type, char *pic, int pic_size, uint64_t utcms)
{
	int ret;
	cmiotEventType_e eCmiotEventType;

	if (1 == type)
		eCmiotEventType = CMIOT_EVENT_PEOPLE_DETECT;
	else if (2 == type)
		eCmiotEventType = CMIOT_EVENT_REGION_HACK;
	else if (3 == type)
		eCmiotEventType = CMIOT_EVENT_MOTION_DETECT;
	else
		return ;
	
	ret = cmiot_event_start(eCmiotEventType, 0, utcms, NULL, (cmiot_uint8_t *)pic, pic_size);
	AppErr("cmiot_event_start(type = %d) ret: %d\n", eCmiotEventType, ret);
}
void cmoit_intrude_event_stop_proc(int type, uint64_t utcms)
{
	int ret;
	cmiotEventType_e eCmiotEventType;

	if (1 == type)
		eCmiotEventType = CMIOT_EVENT_PEOPLE_DETECT;
	else if (2 == type)
		eCmiotEventType = CMIOT_EVENT_REGION_HACK;
	else if (3 == type)
		eCmiotEventType = CMIOT_EVENT_MOTION_DETECT;
	else
		return ;
	
	ret = cmiot_event_stop(eCmiotEventType, 0, utcms);
	AppErr("cmiot_event_stop(type = %d) ret: %d\n", eCmiotEventType, ret);
}


