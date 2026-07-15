#include "demo_public.h"
#include "dev_log.h"
#include "CmiotOsdControl.h"
#include "Common.h"
#include <sys/vfs.h>    /* or <sys/statfs.h> */
#include <resolv.h>


/* 云台坐标范围 */
#define PTZ_POS_MIN (-1000000)
#define PTZ_POS_MAX (1000000)

/* 云台坐标 */
cmiotPtzPositionInfo_t g_ptzPos = {0, 0};

/* 云台通道0预置点编码列表 */
cmiot_bool_t g_presetPointCode_0[256] = {0};

/* 云台通道1预置点编码列表 */
cmiot_bool_t g_presetPointCode_1[256] = {0};

/* 云台通道0巡航路线编码列表 */
cmiot_bool_t g_cruiseCode_0[256] = {0};

/* 云台通道1巡航路线编码列表 */
cmiot_bool_t g_cruiseCode_1[256] = {0};

/* 云台通道0预置点编码对应的坐标 */
cmiotPtzPositionInfo_t g_presetPointPos_0[256] = {0};

/* 云台通道0巡航路线对应的预置点编码列表 */
cmiot_char_t *g_cruisePresetPointCode_0[256] = {0};

/* 保活服务信息 */
cmiotKeepaliveInfo_t g_keepaliveInfo = {0};
demoKeepaliveThreadInfo_t g_keepaliveThreadInfo = {.info = &g_keepaliveInfo, 0};

pthread_t g_keepaliveTid = 0;
pthread_t g_handleSleepTid = 0;
pthread_t g_handleWakeupTid = 0;

extern cmiotDevParams_t g_devParams;
extern cmiotWifiList_t g_wifiList;
extern cmiot_bool_t g_aecBindMode;
extern cmiot_bool_t g_separateSdStream;
extern cmiotSdStreamParam_t g_sdStreamParams;
extern cmiot_char_t g_qrcodeData[2][128];
extern cmiot_char_t g_devLogPath[256];
extern timer_t g_ptzMoveTimerID;
cmiotMediaStream_t g_mediaStream0;
cmiotMediaStream_t g_sdMediaStream0;
cmiotMediaStream_t g_mediaStream1;
cmiot_int32_t g_curVideoFormat = 2;
extern cmiot_int32_t g_externalSdRecordStatus;
extern cmiot_int32_t g_externalSdRecordType;
extern cmiotDevType_e g_devType;
extern cmiot_bool_t g_activeLogReport;

extern int g_cmiot_online;
extern int g_cmiot_video_quality;

extern unsigned char bStartPrivateMode; 		//是否开启隐私模式

extern void demo_external_sd_record_stop(void);
extern cmiot_int32_t demo_sdk_init(cmiot_bool_t wakeupInit);
extern cmiot_int32_t demo_sdk_deinit(void);

extern "C" {
	int tmp_rkipc_snap(void **pic_data, unsigned int *pic_size);
}

static int s_QrcodeExit = 0;
static char s_qr_scan_result[128] = {0};
//提取到二维码信息时的回调
static bool QrCodeExtractedCB(const std::string &strResult)
{
	AppErr("strResult: %s\n", strResult.c_str());

	bool bFound = false;
	for (int i = 0; i < 2; i++)
	{
		if (strcmp(g_qrcodeData[i], strResult.c_str()) == 0)
		{
			bFound = true;
			break;
		}
	}

	if (bFound)
		return false;
	
	if (strlen(g_qrcodeData[0]) == 0)
	{
		memset(g_qrcodeData[0], 0, sizeof(g_qrcodeData[0]));
		strncpy(g_qrcodeData[0], strResult.c_str(), sizeof(g_qrcodeData[0])-1);
		return false;
	}
	if (strlen(g_qrcodeData[1]) == 0)
	{
		memset(g_qrcodeData[1], 0, sizeof(g_qrcodeData[1]));
		strncpy(g_qrcodeData[1], strResult.c_str(), sizeof(g_qrcodeData[1])-1);
		return false;
	}
	
	memset(g_qrcodeData[0], 0, sizeof(g_qrcodeData[0]));
	memset(g_qrcodeData[1], 0, sizeof(g_qrcodeData[1]));
	snprintf(g_qrcodeData[0], sizeof(g_qrcodeData[0]), strResult.c_str());
	
	return false;
}
static void* DoQrcode(void* args)
{
	printf("DoQrcode start\n");

	CAudioPrompt::AudioFileParm audioFile;
	audioFile.strFileName = AUDIO_FILE_PLEASE_SET_WIFI;
	audioFile.type = 0;
	g_AudioPrompt.aoPlay(audioFile);
	
	int count = 15;
	while (!s_QrcodeExit) {

		if (count -- < 0) {
			count = 15;
			
			g_AudioPrompt.aoPlay(audioFile);
		}
		sleep(1);
	}
	return NULL;
}


/* demo更新在线配置上报处理函数，若上报时设备不在线，上线后再进行上报 */
cmiot_bool_t g_devOnlineStatus = CMIOT_FALSE;
cmiot_int32_t demo_warp_cmiot_update_device_config_send(void)
{

    if(g_devOnlineStatus)
    {
        cmiot_update_device_config_send();
    }
    return CMIOT_RETURN_CODE_SUCCESS;
}


#if 0
/* ===== 以下为demo日志过滤函数, 保留作为参考 ===== */
static int _custom_filter_dev_log_file(const struct dirent *pDir)
{
    struct tm tm = {0};
    char *p = NULL;
    char time[32] = {""};
    if(sscanf(pDir->d_name, DEV_LOG_FILE_NAME_PATTERN, time) == 1)
    {
        p = strptime(time, DEV_LOG_FILE_TIME_PATTERN, &tm); 
        if(p != NULL)  
        {
            return 1;
        } 
    }
    
    return 0;
}
/* ===== 以上为demo日志过滤函数, 保留作为参考 ===== */
#endif

/* ===== 新的设备日志文件名过滤函数 ===== */
static int _custom_filter_dev_log_file(const struct dirent *pDir)
{
    return dev_log_filter_by_name(pDir->d_name);
}

void *demo_processe_whiteligt(void* args)
{
    pthread_detach(pthread_self());
    cmiotWhiteLightDelaySwitch_t *info = (cmiotWhiteLightDelaySwitch_t *)args;
    usleep(info->delayTime * 1000000);
    cmiot_update_device_config(CMIOT_UPDATE_CFG_WHITE_LIGHT, (cmiot_char_t *)&(info->status), sizeof(int));
    demo_warp_cmiot_update_device_config_send();
    free(info);
    pthread_exit(0);
}

void *handle_sleep(void *args)
{
    pthread_detach(pthread_self());

//    demo_set_media_thread_stop();
    demo_sdk_deinit();
    g_handleSleepTid = 0;
    pthread_exit(NULL);
}

void *handle_wakeup(void *args)
{
    pthread_detach(pthread_self());

//    demo_set_media_thread_stop();
    demo_sdk_deinit();
    sleep(1);
    cmiot_int32_t ret = demo_sdk_init(CMIOT_TRUE);
    if (CMIOT_RETURN_CODE_SUCCESS != ret)
    {
        DEMO_PRINT("init SDK failed. ret = %d!\n",ret);
        sleep(1);  // 防止sdk启动失败后有部分日志打印不全
    }
    else
    {
//        demo_set_media_thread();
    }
    g_handleWakeupTid = 0;
    pthread_exit(NULL);
}

void *demo_keepalive_thread(void* args)
{
    /* 入参检查 */
    if (!args)
    {
        DEMO_PRINT("demo_keepalive_thread invalid params!\n");
        pthread_exit(NULL);
    }

    DEMO_PRINT("demo_keepalive_thread start: pid [%d] tid[%ld]\n", (int)getpid(), syscall(SYS_gettid));

    demoKeepaliveThreadInfo_t *threadInfo = (demoKeepaliveThreadInfo_t*)args;
    cmiot_int32_t sockfd = -1;
    cmiot_bool_t firConFlag = CMIOT_TRUE;
    cmiot_uint32_t firPingTime = 0;

	fd_set rset;
	cmiot_int32_t maxfd;
	cmiot_uint8_t *readBuff = NULL;
	struct timeval tv;
	cmiot_int32_t ret;

    while(!threadInfo->stopFlag)
    {
        if(sockfd < 0)
        {
            cmiot_int32_t i = 0;
            for(i = 0; i < threadInfo->info->iplistNum; i++)
            {
                DEMO_PRINT("start connect keepalive ip[%s], port[%d]\n", threadInfo->info->iplist[i].ip, threadInfo->info->iplist[i].port);
                sockfd = demo_tcp_connect(threadInfo->info->iplist[i].ip, threadInfo->info->iplist[i].port);
                if(sockfd)
                {
                    DEMO_PRINT("connect keepalive ip[%s], port[%d] success\n", threadInfo->info->iplist[i].ip, threadInfo->info->iplist[i].port);
                    break;
                }
            }
            if(sockfd < 0)
            {
                goto SOCKET_EXIT_PROCESS;
            }
        }

        if(firConFlag || (get_utc_time_s() - firPingTime) >= (threadInfo->info->pingSpan))
        {
            if(firConFlag)
                firConFlag = CMIOT_FALSE;
            
            DEMO_PRINT("time[%u] send ping data\n", get_utc_time_s());
            if(demo_tcp_writen(sockfd, (cmiot_char_t*)threadInfo->info->pingData, threadInfo->info->pingDataLen) != threadInfo->info->pingDataLen)
            {
                DEMO_PRINT("send ping data failed!\n");
                goto SOCKET_EXIT_PROCESS;
            }
            firPingTime = get_utc_time_s();
        }

        maxfd = sockfd + 1;
        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        tv.tv_sec = 0;
        tv.tv_usec = DEMO_KEEPALIVE_SELECT_TIMEOUT_MS * 1000;

        ret = select(maxfd, &rset, NULL, NULL, &tv);
        if(ret < 0)
        {
            DEMO_PRINT("select error, ret[%d] errno[%d]\n", ret, errno);
        }

        if(FD_ISSET(sockfd, &rset))
        {
            readBuff = (cmiot_uint8_t*)calloc(1, threadInfo->info->wakeDataLen);
            if(!readBuff)
            {
                DEMO_PRINT("malloc failed!\n");
                usleep(10 * 1000);
                continue;
            }

            if(demo_tcp_readn(sockfd, (cmiot_char_t*)readBuff, threadInfo->info->wakeDataLen) != threadInfo->info->wakeDataLen)
            {
                DEMO_PRINT("read wake date failed!\n");
                if(readBuff)
                {
                    free(readBuff);
                }
                continue;
            }

            if(memcmp((cmiot_char_t*)threadInfo->info->wakeData, readBuff, threadInfo->info->wakeDataLen) != 0)
            {
                int i = 0;
                DEMO_PRINT("read wake date failed, wakeData [");
                for(i = 0; i<threadInfo->info->wakeDataLen; i++)
                {
                    printf("%02x ", threadInfo->info->wakeData[i]);
                }
                printf("], but receive[");
                for(i = 0; i<threadInfo->info->wakeDataLen; i++)
                {
                    printf("%02x ", readBuff[i]);
                }
                printf("]\n!");
                continue;
            }

            int i = 0;
            DEMO_PRINT("receive wake data[");
            for(i = 0; i<threadInfo->info->wakeDataLen; i++)
            {
                printf("%02x ", readBuff[i]);
            }
            printf("]\n!");
            free(readBuff);
            if(!g_handleWakeupTid)
            {
                cmiot_int32_t ret = 0;
                if((ret = pthread_create(&g_handleWakeupTid, NULL, handle_wakeup, NULL)))
                {
                    DEMO_PRINT("create handle_wakeup failed, ret = %d!\n", ret);
                }
            }
        }
        continue;

SOCKET_EXIT_PROCESS:
        if(sockfd)
        {
            close(sockfd);
            sockfd = -1;
        }
        firConFlag = CMIOT_TRUE;
        usleep(10 * 1000);
        continue;
    }

    if(sockfd)
    {
        close(sockfd);
        sockfd = -1;
    }

    DEMO_PRINT("demo_keepalive_thread exit!\n");

    pthread_exit(NULL);
}


void demo_keepalive_info_clear(void)
{
    if(g_keepaliveInfo.pingData)
    {
        free(g_keepaliveInfo.pingData);
    }
    if(g_keepaliveInfo.wakeData)
    {
        free(g_keepaliveInfo.wakeData);
    }
    if(g_keepaliveInfo.iplist)
    {
        free(g_keepaliveInfo.iplist);
    }
    memset(&g_keepaliveInfo, 0, sizeof(cmiotKeepaliveInfo_t));
}

int demo_keepalive_info_save(cmiotKeepaliveInfo_t *keepAliveInfo)
{
    if(keepAliveInfo == NULL)
    {
        DEMO_PRINT("keep alive info is NULL!\n");
        return -1;
    }
    g_keepaliveInfo.pingSpan = keepAliveInfo->pingSpan;
    g_keepaliveInfo.pingDataLen = keepAliveInfo->pingDataLen;
    g_keepaliveInfo.pingData = (cmiot_uint8_t*)calloc(1, keepAliveInfo->pingDataLen);
    if(!g_keepaliveInfo.pingData)
    {
        DEMO_PRINT("ping data malloc failed!\n");
        goto ALIVE_INFO_MAL_FAIL;
    }
    memcpy(g_keepaliveInfo.pingData, keepAliveInfo->pingData, keepAliveInfo->pingDataLen);
    g_keepaliveInfo.wakeDataLen = keepAliveInfo->wakeDataLen;
    g_keepaliveInfo.wakeData = (cmiot_uint8_t*)calloc(1, keepAliveInfo->wakeDataLen);
    if(!g_keepaliveInfo.wakeData)
    {
        DEMO_PRINT("wakeData data malloc failed!\n");
        goto ALIVE_INFO_MAL_FAIL;
    }
    memcpy(g_keepaliveInfo.wakeData, keepAliveInfo->wakeData, keepAliveInfo->wakeDataLen);
    g_keepaliveInfo.iplistNum = keepAliveInfo->iplistNum;
    g_keepaliveInfo.iplist = (cmiotIpList_t*)calloc(1, keepAliveInfo->iplistNum * sizeof(cmiotIpList_t));
    if(!g_keepaliveInfo.iplist)
    {
        DEMO_PRINT("ipList malloc failed!\n");
        goto ALIVE_INFO_MAL_FAIL;
    }
    memcpy(g_keepaliveInfo.iplist, keepAliveInfo->iplist, keepAliveInfo->iplistNum * sizeof(cmiotIpList_t));

    return 0;

ALIVE_INFO_MAL_FAIL:
    demo_keepalive_info_clear();
    return -1;
}

cmiot_int32_t demo_set_keepalive_thread(cmiotKeepaliveInfo_t *info, cmiot_bool_t saveFlag)
{
    if(g_keepaliveTid)
    {
        g_keepaliveThreadInfo.stopFlag = CMIOT_TRUE;
        pthread_join(g_keepaliveTid, NULL);
        g_keepaliveTid = 0;
        g_keepaliveThreadInfo.stopFlag = CMIOT_FALSE;
    }
    if(saveFlag)
    {
        demo_keepalive_info_clear();
        demo_keepalive_info_save(info);
    }
    cmiot_int32_t ret = 0;
    if((ret = pthread_create(&g_keepaliveTid, NULL, demo_keepalive_thread, &g_keepaliveThreadInfo)) != 0)
    {
        DEMO_PRINT("create keepalive thread failed, ret = %d!\n", ret);
        return -1;
    }
    return 0;
}

cmiot_int32_t demo_dev_config_callback(cmiotDevConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    int i=0;
    
    switch (cmd)
    {
        case CMIOT_CMD_SET_MIC_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MIC_STATUS **********, value is [%d]\n", *(int *)input);
            int onoff = *(int *)input;
            
			CConfigTable table;
			AudioConf_S stAudioConfig;
			g_configManager.getConfig(getConfigName(CFG_AUDIO), table);
			TExchangeAL<AudioConf_S>::getConfig(table, stAudioConfig);
			
			if( onoff != stAudioConfig.mic_enable )
			{
				stAudioConfig.mic_enable = onoff;
				TExchangeAL<AudioConf_S>::setConfig(stAudioConfig, table);
				g_configManager.setConfig(getConfigName(CFG_AUDIO), table,0, IConfigManager::applyOK);
			}
			cmiot_update_device_config(CMIOT_UPDATE_CFG_VOLUME_MUTE, (cmiot_char_t *)input, sizeof(int));
			demo_warp_cmiot_update_device_config_send();

            break;
        }

        case CMIOT_CMD_SET_ANTIFLICKER_VALUE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_ANTIFLICKER_VALUE **********, value is [%d]\n", *(int *)input);
            int hz = *(int *)input;
			int new_iAntiFlicker;
			if (50 == hz)
				new_iAntiFlicker = 1;
			else if (60 == hz)
				new_iAntiFlicker = 2;
			else
				new_iAntiFlicker = 0;

			CConfigTable table;
			CameraParamAll cpa;
			memset(&cpa, 0, sizeof(cpa));
			g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
			TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
			
			if( new_iAntiFlicker != cpa.vCameraParamAll[0].iAntiFlicker )
			{
				cpa.vCameraParamAll[0].iAntiFlicker = new_iAntiFlicker;
				TExchangeAL<CameraParamAll>::setConfigV2(cpa, table, 1);
				g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table,0, IConfigManager::applyOK);
			}

			break;
        }
        
        case CMIOT_CMD_SET_DEVICE_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_DEVICE_STATUS **********, value is [%d]\n", *(int *)input);
            int tmp = *(int *)input == 4 ? 0 : 1;

			Json::Value table;
			//int status = 0;
			g_configManager.getConfig(getConfigName(CFG_PRIVATE), table);
			if(tmp)
            {
				/* 开始推流 */
				table["Status"] = 0;
				bStartPrivateMode = 0;
            }
            else
            {
				/* 停止推流 */
				table["Status"] = 1;
				bStartPrivateMode = 1;
            }
			g_configManager.setConfig(getConfigName(CFG_PRIVATE), table, 0, IConfigManager::applyOK);
			
            cmiot_update_device_config(CMIOT_UPDATE_CFG_STATUS, (cmiot_char_t *)&tmp, sizeof(tmp));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_VIDEO_ROTATE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_VIDEO_ROTATE **********, value is [%d]\n", *(int *)input);
            int rotate = *(int *)input;
			int new_mirror;
			int new_flip;
			if (rotate)
			{
				new_mirror = 1;
				new_flip = 1;
			}
			else
			{
				new_mirror = 0;
				new_flip = 0;
			}
			
			CConfigTable table;
			CameraParamAll cpa;
			memset(&cpa, 0, sizeof(cpa));
			g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
			TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
			
			if( new_mirror != cpa.vCameraParamAll[0].mirror || new_flip != cpa.vCameraParamAll[0].flip )
			{
				cpa.vCameraParamAll[0].mirror = new_mirror;
				cpa.vCameraParamAll[0].flip = new_flip;
				TExchangeAL<CameraParamAll>::setConfigV2(cpa, table, 1);
				g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table,0, IConfigManager::applyOK);
			}

			break;
        }

        case CMIOT_CMD_SET_NIGHTVISIGON_MODE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_NIGHTVISIGON_MODE **********, value is [%d]\n", *(int *)input);
			break;
        }

        case CMIOT_CMD_SET_SOUND_DETECTION_SENSITIVITY:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_DETECTION_SENSITIVITY **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_MOTION_DETECTION_SENSITIVITY:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MOTION_DETECTION_SENSITIVITY **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_LED_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_LED_STATUS **********, value is [%d]\n", *(int *)input);
            //--------------------------------
            int light_on_off = *(int *)input;
            bool ret;
            Json::Value table;
            ret = g_configManager.getConfig(getConfigName(CFG_LIGHT), table);
            AppWarning("get (CFG_LIGHT) config %s\n", ret ? "succ" : "failed");
            AppWarning("(CFG_LIGHT) status %d\n",table["Light"].asInt());
            
            table["Light"] = light_on_off ? 1 : 0;
            ret = ((g_configManager.setConfig(getConfigName((CFG_LIGHT)), table, 0, IConfigManager::applyOK)) & IConfigManager::applyFileError)==0?true:false;
            AppWarning("set (CFG_LIGHT) config %s\n", ret ? "succ" : "failed");

            //--------------------------------
            break;
        }

        case CMIOT_CMD_SET_MOTION_DETECTION_REGION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MOTION_DETECTION_REGION **********\n");
            cmiotMotionRegionList_t *list = (cmiotMotionRegionList_t *)input;
            DEMO_PRINT("id  left  right  top  bottom\n");
            for(i=0; i<list->regionNum; i++)
            {
                DEMO_PRINT("%-2d  %-4d  %-5d  %-3d  %-6d\n", list->region[i].id, list->region[i].left, list->region[i].right, \
                                                  list->region[i].top, list->region[i].bottom);
            }
            break;
        }

        case CMIOT_CMD_SET_HDR_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_HDR_STATUS **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_STATUS **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_VOL:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_VOL **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_VOLUME, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_MODE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_MODE **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_MODE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_STATUS **********, value is [%d]\n", *(int *)input);
			int new_switch = *(int *)input ? 1 : 0;
			
			CConfigTable table;
			CameraParamAll cpa;
			memset(&cpa, 0, sizeof(cpa));
			g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
			TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
			if (new_switch != cpa.vCameraParamAll[0].nightVisionSwitch)
			{
				cpa.vCameraParamAll[0].nightVisionSwitch = new_switch;
				TExchangeAL<CameraParamAll>::setConfigV2(cpa, table, 1);
				g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table,0, IConfigManager::applyOK);
			}

			cmiot_update_device_config(CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_MODE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_MODE **********, value is [%d]\n", *(int *)input);
			int new_mode = DOUBLE_IRMODE_SMART;
			if (1 == *(int *)input)
				new_mode = DOUBLE_IRMODE_IR;
			if (2 == *(int *)input)
				new_mode = DOUBLE_IRMODE_FULLCOLOR;
			
			CConfigTable table;
			CameraParamAll cpa;
			memset(&cpa, 0, sizeof(cpa));
			g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
			TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
			if (new_mode != cpa.vCameraParamAll[0].nightVisionMode)
			{
				cpa.vCameraParamAll[0].nightVisionMode = new_mode;
				TExchangeAL<CameraParamAll>::setConfigV2(cpa, table, 1);
				g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table,0, IConfigManager::applyOK);
			}

			cmiot_update_device_config(CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION_MODE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_INVADE_DETECTION_REGION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_INVADE_DETECTION_REGION **********\n");
            cmiotInvadeRegionList_t *list = (cmiotInvadeRegionList_t *)input;

			CConfigTable table;
			CmiotIntrudeConf_S stIntrudeConf;
			g_configManager.getConfig(getConfigName(CFG_CMIOT_INTRUDE), table);
			printJsonValue(table);
			TExchangeAL<CmiotIntrudeConf_S>::getConfig(table, stIntrudeConf);
			printf("stIntrudeConf: [%d, %d], [%d, %d], [%d, %d], [%d, %d], per_en: %d, per_int: %d, int_en: %d, int_int: %d, real: %d, s_l_a: %d\n", 
				stIntrudeConf.rect_point[0].x, stIntrudeConf.rect_point[0].y, 
				stIntrudeConf.rect_point[1].x, stIntrudeConf.rect_point[1].y, 
				stIntrudeConf.rect_point[2].x, stIntrudeConf.rect_point[2].y, 
				stIntrudeConf.rect_point[3].x, stIntrudeConf.rect_point[3].y, 
				stIntrudeConf.person_report_en, stIntrudeConf.person_report_interval, 
				stIntrudeConf.intrude_report_en, stIntrudeConf.intrude_report_interval, 
				stIntrudeConf.real_time_frame, stIntrudeConf.intrude_sound_light_alarm);

            for(i=0; i<list->regionNum; i++)
            {
                DEMO_PRINT("region %d: x: %-4d %-4d %-4d %-4d\n", i, list->region[i].point[0].x, list->region[i].point[1].x, \
                                                  list->region[i].point[2].x, list->region[i].point[3].x);
                DEMO_PRINT("          y: %-4d %-4d %-4d %-4d\n", list->region[i].point[0].y, list->region[i].point[1].y, \
                                                  list->region[i].point[2].y, list->region[i].point[3].y);

				if (0 == i)
				{
					stIntrudeConf.rect_point[0].x = list->region[i].point[0].x;
					stIntrudeConf.rect_point[0].y = list->region[i].point[0].y;
					stIntrudeConf.rect_point[1].x = list->region[i].point[1].x;
					stIntrudeConf.rect_point[1].y = list->region[i].point[1].y;
					stIntrudeConf.rect_point[2].x = list->region[i].point[2].x;
					stIntrudeConf.rect_point[2].y = list->region[i].point[2].y;
					stIntrudeConf.rect_point[3].x = list->region[i].point[3].x;
					stIntrudeConf.rect_point[3].y = list->region[i].point[3].y;
					
					TExchangeAL<CmiotIntrudeConf_S>::setConfig(stIntrudeConf, table);
					g_configManager.setConfig(getConfigName(CFG_CMIOT_INTRUDE), table, 0, IConfigManager::applyOK);
				}
            }			
            break;
        }

        case CMIOT_CMD_SET_LIGHT_BRIGHTNESS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_LIGHT_BRIGHTNESS **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_HUMAN_TRACKING_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_HUMAN_TRACKING_STATUS **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_I_FRAME:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_I_FRAME **********, value is [%d]\n", *(int *)input);
			if (1 == *(int *)input)
				CaptureForceIFrame(0,0);
			else
				CaptureForceIFrame(1,0);
            break;
        }

        case CMIOT_CMD_SET_MOTION_TRACKING_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MOTION_TRACKING_STATUS **********, value is [%d]\n", *(int *)input);
            // cmiot_update_device_config(CMIOT_UPDATE_CFG_MOTION_TARCK, (cmiot_char_t *)input, sizeof(int));
            // demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_MOTION_TRACKING_RESET_TIMEOUT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MOTION_TRACKING_RESET_TIMEOUT **********, value is [%d]\n", *(int *)input);
            // cmiot_update_device_config(CMIOT_UPDATE_CFG_MOTION_TRACK_BACK_TIME, (cmiot_char_t *)input, sizeof(int));
            // demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_DEVICE_CLOSE_SCHEDULE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_DEVICE_CLOSE_SCHEDULE **********\n");
            cmiotScheduleList_t *list = (cmiotScheduleList_t *)input;
            DEMO_PRINT("index  enable  repeat  start  end\n");
            for(i=0; i<list->scheduleNum; i++)
            {
                DEMO_PRINT("%-5d  %-6d  %-6d  %-5s  %s\n", list->schedule[i].index, list->schedule[i].enable, \
                                                  list->schedule[i].repeat, list->schedule[i].startTime, list->schedule[i].endTime);
            }
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_LIGHT_TIME:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_LIGHT_TIME **********\n");
            DEMO_PRINT("SoundLightAlarmEffect LightTime=%d\n", *(int*)input);
            break;
        }
        
        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_TIMES:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_TIMES **********\n");
            DEMO_PRINT("SoundLightAlarmEffect SoundTimes=%d\n", *(int*)input);
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_URL:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_URL **********\n");
            DEMO_PRINT("SoundLightAlarmEffect SoundURL=%s\n", (char*)input);
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_ID:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_ID **********\n");
            DEMO_PRINT("SoundLightAlarmEffect SoundID=%s\n", (char*)input);
            break;
        }

        case CMIOT_CMD_SET_AI_FACE_CAPTURE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CAPTURE **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_FACE_CAPTURE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_FACE_CONTRAST:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CONTRAST **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_FACE_CONTRAST, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_VEHICLE_CAPTURE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_VEHICLE_CAPTURE **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_VEHICLE_CAPTURE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_SENSITIVITY:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_SENSITIVITY **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_SENSITIVITY, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_REGIONS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_REGIONS **********\n");
            cmiotRectangleRegionInfo_t *info = (cmiotRectangleRegionInfo_t *)input;
            DEMO_PRINT("x     y\n");
            for(i=0; i<4; i++)
            {
                DEMO_PRINT("%-4d  %-4d\n", info->point[i].x, info->point[i].y);
            }
            break;
        }

        case CMIOT_CMD_SET_AI_FACE_CAPTURE_REGION:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CAPTURE_REGION **********\n");
            goto next;
        case CMIOT_CMD_SET_AI_FACE_CONTRAST_REGION:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CONTRAST_REGION **********\n");
            goto next;
        case CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_REGION:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_REGION **********\n");
            goto next;
        case CMIOT_CMD_SET_AI_MASK_DETECTION_REGION:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_MASK_DETECTION_REGION **********\n");
            goto next;
        case CMIOT_CMD_SET_AI_EBIKE_CAPTURE_REGION:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_EBIKE_CAPTURE_REGION **********\n");
            goto next;
next:
        {
            cmiotRectangleRegionInfo_t *info = (cmiotRectangleRegionInfo_t *)input;
            DEMO_PRINT("x     y\n");
            for(i=0; i<4; i++)
            {
                DEMO_PRINT("%-4d  %-4d\n", info->point[i].x, info->point[i].y);
            }
            break;
        }

        case CMIOT_CMD_SET_CODED_FORMAT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_CODED_FORMAT **********, value is [%d]\n", *(int *)input);
            if(g_devParams.stream.videoType != *(int *)input)
            {
                if(*(int *)input == 1)
                {
                    /* 先停止推流 */
//                    demo_set_media_thread_stop();
                    /* 更新配置 */
                    g_devParams.stream.videoType = 1;
                    cmiot_set_stream_param(&(g_devParams.stream));
                    if(g_separateSdStream)
                    {
                        g_sdStreamParams.videoType = 1;
                        cmiot_set_sd_stream_param(&g_sdStreamParams);
                    }

                    /* 264推流 */
                    strncpy(g_mediaStream0.mediaFileName, TEST_MEDIA_FILE_264_STREAM_0, sizeof(g_mediaStream0.mediaFileName));
                    strncpy(g_mediaStream1.mediaFileName, TEST_MEDIA_FILE_264_STREAM_1, sizeof(g_mediaStream1.mediaFileName));
                    if(g_separateSdStream)
                        strncpy(g_sdMediaStream0.mediaFileName, TEST_MEDIA_FILE_264_STREAM_1, sizeof(g_sdMediaStream0.mediaFileName));
//                    demo_set_media_thread();
                }

                if(*(int *)input == 2)
                {
                    /* 先停止推流 */
//                    demo_set_media_thread_stop();
                    /* 更新配置 */
                    g_devParams.stream.videoType = 2;
                    cmiot_set_stream_param(&(g_devParams.stream));
                    if(g_separateSdStream)
                    {
                        g_sdStreamParams.videoType = 2;
                        cmiot_set_sd_stream_param(&g_sdStreamParams);
                    }

                    /* 265推流 */
                    strncpy(g_mediaStream0.mediaFileName, TEST_MEDIA_FILE_265_STREAM_0, sizeof(g_mediaStream0.mediaFileName));
                    strncpy(g_mediaStream1.mediaFileName, TEST_MEDIA_FILE_265_STREAM_1, sizeof(g_mediaStream1.mediaFileName));
                    if(g_separateSdStream)
                        strncpy(g_sdMediaStream0.mediaFileName, TEST_MEDIA_FILE_265_STREAM_1, sizeof(g_sdMediaStream0.mediaFileName));
//                    demo_set_media_thread();
                }

				printf("cmiot_update_device_config(CMIOT_UPDATE_CFG_CODED_FORMAT, %d)\n", (cmiot_char_t *)input);
                cmiot_update_device_config(CMIOT_UPDATE_CFG_CODED_FORMAT, (cmiot_char_t *)input, sizeof(int));
                demo_warp_cmiot_update_device_config_send();

                g_curVideoFormat = *(int *)input;

                /* 此处仅示例切换成功的流程，
                   若切换失败，需要调用cmiot_set_stream_param恢复为原配置，
                   再调用cmiot_update_device_config、cmiot_update_device_config_send上报当前生效的编码格式 */
            }
            break;
        }

        case CMIOT_CMD_SET_OSD:
        {
            if (input == NULL)
            {
                DEMO_PRINT("CMIOT_CMD_SET_OSD invalid input\n");
                return -1;
            }
            cmiotOSDInfo_t* info = static_cast<cmiotOSDInfo_t*>(input);
            const int ret = cmiot_osd_set_config(info);
            DEMO_PRINT("CMIOT_CMD_SET_OSD switch=%d ret=%d\n",
                       info->osdSwitch, ret);
            return ret;
        }

        case CMIOT_CMD_SET_AI_MASK_DETECTION_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_MASK_DETECTION_STATUS **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_MASK_DETECT, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_EBIKE_CAPTURE_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_EBIKE_CAPTURE_STATUS **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_EBIKE_CAPTURE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_PASSENGER_STAT_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_PASSENGER_STAT_STATUS **********, value is [%d]\n", *(int *)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AI_PASSENGER_STAT, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_AI_FACE_CAPTURE_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CAPTURE_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }
        case CMIOT_CMD_SET_AI_FACE_CONTRAST_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_CONTRAST_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }
        case CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }
        case CMIOT_CMD_SET_AI_MASK_DETECTION_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_MASK_DETECTION_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }
        case CMIOT_CMD_SET_AI_EBIKE_CAPTURE_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_EBIKE_CAPTURE_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }
        case CMIOT_CMD_SET_AI_PASSENGER_STAT_SEN:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_PASSENGER_STAT_SEN **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_AI_PASSENGER_STAT_RULE:
        {
            cmiotPassengerStatRule_t *info = (cmiotPassengerStatRule_t *)input;
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_PASSENGER_STAT_RULE **********\n");
            DEMO_PRINT("AI Passenger Stat Rule : enableLine=%u enableRegion=%u lineCountCycle=%u regionCountCycle=%u\n", info->enableLine, info->enableRegion, info->lineCountCycle, info->regionCountCycle);
            if(info->enableLine) {
                DEMO_PRINT("lineRule : point1[%u,%u] point2[%u,%u] mode=%u\n", \
                    info->lineRule.x1, info->lineRule.y1, info->lineRule.x2, info->lineRule.y2, info->lineRule.mode);
            }
            if(info->enableRegion) {
                DEMO_PRINT("regionRule : point1[%u,%u] point2[%u,%u] point3[%u,%u] point4[%u,%u]\n", \
                    info->regionRule.point[0].x, info->regionRule.point[0].y, info->regionRule.point[1].x, info->regionRule.point[1].y, info->regionRule.point[2].x, info->regionRule.point[2].y, info->regionRule.point[3].x, info->regionRule.point[3].y);
            }
            break;
        }

        case CMIOT_CMD_SET_BATT_LOW_POWER_MODE:
        {
            cmiotBattLowPowerMode_t *info = (cmiotBattLowPowerMode_t *)input;
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_BATT_LOW_POWER_MODE **********\n");
            DEMO_PRINT("Battery Low Power Mode : mode=%u schedule=%s\n", info->mode, info->schedule);
            break;
        }

        case CMIOT_CMD_SET_SPEAKER_VOLUME:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SPEAKER_VOLUME **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_MOTION_DETECTION_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MOTION_DETECTION_STATUS **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_SOUND_DETECTION_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_DETECTION_STATUS **********, value is [%d]\n", *(int *)input);
            break;
        }

        case CMIOT_CMD_SET_BABYCRY_DETECTION_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_BABYCRY_DETECTION_STATUS **********, value is [%d]\n", *(int*)input);
            break;
        }

        case CMIOT_CMD_SET_BAT_ALARM_RULE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_BAT_ALARM_RULE **********\n");
            cmiotBatAlarmRuleInfo_t *info = (cmiotBatAlarmRuleInfo_t*)input;
            DEMO_PRINT("status: %d\n", info->status);
            if(info->status)
            {
                DEMO_PRINT("ruleInfo: fullBatTrigger:%u, fullBatLowerTrigger:%u, lowBatTrigger:%u, batExhaustTrigger:%u\n", \
                info->rule.fullBatTrigger, info->rule.fullBatLowerTrigger, info->rule.lowBatTrigger, info->rule.batExhaustTrigger);
                DEMO_PRINT("span:%u\n", info->span);
            }
            else
            {
                if(info->ruleExist)
                {
                    DEMO_PRINT("ruleInfo: fullBatTrigger:%u, fullBatLowerTrigger:%u, lowBatTrigger:%u, batExhaustTrigger:%u\n", \
                    info->rule.fullBatTrigger, info->rule.fullBatLowerTrigger, info->rule.lowBatTrigger, info->rule.batExhaustTrigger);
                }
                if(info->spanExist)
                {
                    DEMO_PRINT("span:%u\n", info->span);
                }
            }
            break;
        }

        case CMIOT_CMD_SET_CELLULAR_ALARM_RULE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_CELLULAR_ALARM_RULE **********\n");
            cmiotCellAlarmRuleInfo_t *info = (cmiotCellAlarmRuleInfo_t*)input;
            DEMO_PRINT("status: %d\n", info->status);
            if(info->status)
            {
                DEMO_PRINT("threshold: %u, span: %u\n", info->threshold, info->span);
            }
            else
            {
                if(info->thresholdExist)
                {
                    DEMO_PRINT("threshold: %u\n", info->threshold);
                }
                if(info->spanExist)
                {
                    DEMO_PRINT("span: %u\n", info->span);
                }
            }
            break;
        }

        case CMIOT_CMD_SET_LOW_POWER_KEEPALIVE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_LOW_POWER_KEEPALIVE **********\n");
            cmiotKeepaliveInfo_t *info = (cmiotKeepaliveInfo_t *)input;
            DEMO_PRINT("pingSpan: %u\n", info->pingSpan);
            DEMO_PRINT("pingData: ");
            int i = 0;
            for(i=0; i<info->pingDataLen; i++)
            {
                printf("%02x ", info->pingData[i]);
            }
            printf("\n");

            DEMO_PRINT("wakeData: ");
            for(i=0; i<info->wakeDataLen; i++)
            {
                printf("%02x ", info->wakeData[i]);
            }
            printf("\n");

            for(i=0; i<info->iplistNum; i++)
            {
                DEMO_PRINT("ip: %s, port: %u\n", info->iplist[i].ip, info->iplist[i].port);
            }

            if(g_devType == CMIOT_DEV_TYPE_LOWPOWER_CAM || g_devType == CMIOT_DEV_TYPE_DOORBELL)
                demo_set_keepalive_thread((cmiotKeepaliveInfo_t *)input, CMIOT_TRUE);

            break;
        }

        case CMIOT_CMD_SET_CLICK_ALARM_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_CLICK_ALARM_STATUS **********, value is [%d]\n", *(int*)input);
            break;
        }

        case CMIOT_CMD_SET_VOICE_BROADCAST_SCHEDULE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_VOICE_BROADCAST_SCHEDULE **********\n");
            cmiotVoiceBroadcastScheduleInfo_t *info = (cmiotVoiceBroadcastScheduleInfo_t*)input;
            DEMO_PRINT("mode:%u, loop:%u\n", info->mode, info->loop);
            if(info->mode == 1)
            {
                DEMO_PRINT("week:%u\n", info->week);
            }
            if(info->mode == 2)
            {
                DEMO_PRINT("days:[%s]\n", info->days);
            }
            if(info->loop == 1)
            {
                DEMO_PRINT("span:%u\n", info->span);
            }
            int i = 0;
            for(i = 0; i < 5; i++)
            {
                DEMO_PRINT("timePeriod[%d]:[%s]\n", i, info->timePeriod[i]);
            }
            // 如果id与本地已有id相同，返回0
            // 如果id与本地已有id不同，则需主动通过url进行下载，下载成功返回0，失败返回-1
            DEMO_PRINT("id:[%s]\n", info->id);
            DEMO_PRINT("url:[%s]\n", info->url);
            break;
        }

        case CMIOT_CMD_SET_REGION_INVASION_BOX_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_REGION_INVASION_BOX_STATUS **********, value is [%d]\n", *(int*)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_REGION_INVASION_BOX_STATUS, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_REGION_INVASION_MODE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_REGION_INVASION_MODE **********, value is [%d]\n", *(int*)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_REGION_INVASION_MODE, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_SOUND_LIGHT_ALARM_CFG_NEW:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SOUND_LIGHT_ALARM_CFG_NEW **********\n");
            cmiotSoundLightAlarmCfgNewInfo_t *cfgNewInfo = (cmiotSoundLightAlarmCfgNewInfo_t*)input;

            cmiot_int32_t i = 0;
            for(i = 0; i < cfgNewInfo->num; i++)
            {
                cmiotSoundLightCfgNewSingleInfo_t singCfgInfo = cfgNewInfo->cfgInfo[i];
                DEMO_PRINT("index[%d]: repeat:%d, startTime:%s, endTime:%s, mode=%d\n", i, singCfgInfo.schedule.repeat, \
                            singCfgInfo.schedule.startTime, singCfgInfo.schedule.endTime, singCfgInfo.mode);

                if(singCfgInfo.mode == CMIOT_SOUND_LIGHT_ALARM_MODE_MONITER || singCfgInfo.mode == CMIOT_SOUND_LIGHT_ALARM_MODE_SOUND)
                {
                    DEMO_PRINT("index[%d]: volume:%d, soundTimes:%d", i, singCfgInfo.volume, singCfgInfo.soundTimes);
                    if(singCfgInfo.mode == CMIOT_SOUND_LIGHT_ALARM_MODE_MONITER)
                    {
                        printf(", lightTime:%d", singCfgInfo.lightTime);
                    }
                    printf("\n");
                    DEMO_PRINT("index[%d]: soundUrl:%s\n", i, singCfgInfo.soundUrl);
                    DEMO_PRINT("index[%d]: soundId:%s\n", i, singCfgInfo.soundId);
                    
                }
                if(singCfgInfo.mode == CMIOT_SOUND_LIGHT_ALARM_MODE_FLASH || singCfgInfo.mode == CMIOT_SOUND_LIGHT_ALARM_MODE_LAMP)
                {
                    DEMO_PRINT("index[%d]: lightTime:%d\n", i, singCfgInfo.lightTime);
                }
            }
            break;
        }
        
        case CMIOT_CMD_SET_RESTORE_SETTINGS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_RESTORE_SETTINGS **********\n");
            /* 此回调中，需将全部支持的配置项恢复默认设置，然后调用cmiot_update_device_config接口上报发生变化的设置项，最后调用cmiot_update_device_config_send发送 */
            break;
        }

        case CMIOT_CMD_SET_CAMERA_IMAGE_FLIP:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_CAMERA_IMAGE_FLIP **********, value is [%d]\n", *(int*)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_CAMERA_IMAGE_FLIP, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_MICROPHONE_VOLUME:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_MICROPHONE_VOLUME **********, value is [%d]\n", *(int*)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_MICROPHONE_VOLUME, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_WHITE_LIGHT:
        {
            int ret = 0;
            pthread_t g_set_white_light_tid = 0;
            cmiotWhiteLightDelaySwitch_t *info = (cmiotWhiteLightDelaySwitch_t *)calloc(sizeof(cmiotWhiteLightDelaySwitch_t *), 1);
            memcpy(info, input, sizeof(cmiotWhiteLightDelaySwitch_t));
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_WHITE_LIGHT **********, status[%d], delay[%d]\n", info->status, info->delayTime);
            ret = pthread_create(&g_set_white_light_tid, NULL, demo_processe_whiteligt, (void*)info);
            if(ret != 0)
            {
                DEMO_PRINT("pthread_create error\n");
            }
            break;
        }

        case CMIOT_CMD_SET_EVENT_PUSH_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_EVENT_PUSH_STATUS **********, value is [%d]\n", *(int*)input);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_EVENT_PUSH_STATUS, (cmiot_char_t *)input, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_COMMON_SOUND_ALARM_CFG:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_COMMON_SOUND_ALARM_CFG **********\n");
            cmiotCommonSoundAlarmCfgs_t *cfgs = (cmiotCommonSoundAlarmCfgs_t *)input;

            cmiot_int32_t i = 0;
            for(i = 0; i < cfgs->num; i++)
            {
                DEMO_PRINT("cfg[%d]: type[%d] status[%d] times[%d] volume[%d] id[%s] url[%s] schedule[ repeat[%hhu],start[%s],end[%s] ]\n", i, 
                    cfgs->cfg[i].type, cfgs->cfg[i].status, cfgs->cfg[i].times, cfgs->cfg[i].volume, cfgs->cfg[i].id, cfgs->cfg[i].url, 
                    cfgs->cfg[i].schedule.repeat, cfgs->cfg[i].schedule.startTime, cfgs->cfg[i].schedule.endTime);
            }

            cmiot_update_device_config(CMIOT_UPDATE_CFG_COMMON_SOUND_ALARM, (cmiot_char_t *)input, sizeof(cmiotCommonSoundAlarmCfgs_t) + cfgs->num * sizeof(cmiotCommonSoundAlarmCfg_t));
            demo_warp_cmiot_update_device_config_send();
            break;
        }

        case CMIOT_CMD_SET_COMMON_LIGHT_ALARM_CFG:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_COMMON_LIGHT_ALARM_CFG **********\n");
            cmiotCommonLightAlarmCfgs_t *cfgs = (cmiotCommonLightAlarmCfgs_t *)input;

            cmiot_int32_t i = 0;
            for(i = 0; i < cfgs->num; i++)
            {
                DEMO_PRINT("cfg[%d]: type[%d] status[%d] mode[%d] time[%d] schedule[ repeat[%hhu],start[%s],end[%s] ]\n", i, 
                    cfgs->cfg[i].type, cfgs->cfg[i].status, cfgs->cfg[i].mode, cfgs->cfg[i].type,
                    cfgs->cfg[i].schedule.repeat, cfgs->cfg[i].schedule.startTime, cfgs->cfg[i].schedule.endTime);
            }

            cmiot_update_device_config(CMIOT_UPDATE_CFG_COMMON_LIGHT_ALARM, (cmiot_char_t *)input, sizeof(cmiotCommonLightAlarmCfgs_t) + cfgs->num * sizeof(cmiotCommonLightAlarmCfg_t));
            demo_warp_cmiot_update_device_config_send();
            break;
        }
        case CMIOT_CMD_SET_SUBCODING_RESOLUTION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SUB_STREAM_RECORDING **********, value is [%d]\n", *(int *)input);
            cmiot_int32_t resolution = *(int *)input;
            
            // 辅码流分辨率配置: 0:240P, 1:360P, 2:480P, 3:720P, 4:960P, 5:1080P
            // 设备需要根据此配置调整辅码流编码器的分辨率
            // 这里仅作示例，实际应用中需要调整编码器参数并重启辅码流推送线程
            
            // TODO: 根据resolution值调整辅码流编码器分辨率参数 在devparm.substream上
            
            // 更新配置并上报到平台
            cmiot_update_device_config(CMIOT_UPDATE_CFG_SUBCODING_RESOLUTION, (cmiot_char_t *)&resolution, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            
            DEMO_PRINT("Sub-stream resolution configured to level %d\n", resolution);
            break;
        }

        case CMIOT_CMD_SET_SD_REC_STREAM_TYPE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_SD_REC_STREAM_TYPE **********, value is [%d]\n", *(int *)input);
            cmiot_int32_t sdRecStreamType = *(int *)input;
            
//            if(sdRecStreamType == cmiot_sd_card_get_rec_stream_type())
            {
                DEMO_PRINT("the sd rec stream type remain %d\n", sdRecStreamType);
            }
//            else
            {
//                demo_set_media_thread_stop();
                cmiot_set_main_sub_recording(sdRecStreamType);
//                demo_set_media_thread();
                DEMO_PRINT("the sd rec stream type change to %d\n", sdRecStreamType);
            }
            break;

        }

        default:
            break;
    }
    return CMIOT_RETURN_CODE_SUCCESS;
}

cmiot_int32_t demo_get_Info_callback(cmiotGetInfoCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    switch (cmd)
    {
        case CMIOT_CMD_GET_WIFI_LIST:
        {
            cmiotWifiList_t* pWifiList = (cmiotWifiList_t *)output;
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_WIFI_LIST **********\n");

			router_signal_s stWifiInfo = {0};
			WifiSignal(&stWifiInfo);

			int ap_num = 0;
			router_list_3_s st_ap_list[30];
			ap_num = WifiList_3(st_ap_list, 30);
            printf("\033[1;36m    ap_num = %d  \033[0m\n", ap_num);
			if (ap_num > 0)
			{  
				pWifiList->wifiNum = ap_num;
				pWifiList->wifi = (cmiotWifiInfo_t *)calloc(ap_num, sizeof(cmiotWifiInfo_t));
				for (int i = 0; i < ap_num; i++)
				{
                    strncpy(pWifiList->wifi[i].bssid, st_ap_list[i].bssid, sizeof(pWifiList->wifi[i].bssid) -1);
					strncpy(pWifiList->wifi[i].ssid, st_ap_list[i].essid, sizeof(pWifiList->wifi[i].ssid) -1);
                    printf("\033[1;36m  wifi - %s    \033[0m\n",pWifiList->wifi[i].ssid);
					if (0 == st_ap_list[i].encryptType)
						snprintf(pWifiList->wifi[i].encryption, sizeof(pWifiList->wifi[i].encryption), "OPEN");
					else if (1 == st_ap_list[i].encryptType)
						snprintf(pWifiList->wifi[i].encryption, sizeof(pWifiList->wifi[i].encryption), "WEP");
					else if (2 == st_ap_list[i].encryptType)
						snprintf(pWifiList->wifi[i].encryption, sizeof(pWifiList->wifi[i].encryption), "WPA");
					else if (3 == st_ap_list[i].encryptType)
						snprintf(pWifiList->wifi[i].encryption, sizeof(pWifiList->wifi[i].encryption), "WPA2");
					else if (4 == st_ap_list[i].encryptType)
						snprintf(pWifiList->wifi[i].encryption, sizeof(pWifiList->wifi[i].encryption), "WPA3");
					pWifiList->wifi[i].signal = st_ap_list[i].quality / 20 + 1;
					if (pWifiList->wifi[i].signal < 1) pWifiList->wifi[i].signal = 1;
					if (pWifiList->wifi[i].signal > 5) pWifiList->wifi[i].signal = 5;

					if (strcmp(stWifiInfo.essid, st_ap_list[i].essid) == 0)
                    {
                        pWifiList->wifi[i].connected = 1;
                        printf("\033[1;32m  wifi - %s is connected   \033[0m\n",pWifiList->wifi[i].ssid);
                    }
						

				}
			}
			
            break;
        }
        
        case CMIOT_CMD_GET_SD_CARD_SPACE:
        {
			unsigned long long ullDiskSize = 0;
			unsigned long long ullDiskUsedSize = 0;
			unsigned long long ullDiskFreeSize = 0;
			g_StorageManager->GetDiskcapacity(&ullDiskSize, &ullDiskUsedSize, &ullDiskFreeSize);
			
			AppInfo("ullDiskTotalSize=%llu\n", ullDiskSize);
			AppInfo("ullDiskUsedSize=%llu\n", ullDiskUsedSize);
			AppInfo("ullDiskFreeSize=%llu\n", ullDiskFreeSize);
						
            cmiotSdcardSpaceInfo_t* pSpace = (cmiotSdcardSpaceInfo_t *)output;
            pSpace->freesize = ullDiskFreeSize;
            pSpace->totalsize = ullDiskSize;
            DEMO_PRINT("Queried SD Space with Used %llu Free %llu Total %llu \n", ullDiskUsedSize, pSpace->freesize,pSpace->totalsize);
            break;
        }

        case CMIOT_CMD_GET_SD_CARD_PARTITION:
        {
            //获取SD卡分区情况 测试用例
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_SD_CARD_PARTITION **********\n");

			unsigned long long ullDiskSize = 0;
			unsigned long long ullDiskUsedSize = 0;
			unsigned long long ullDiskFreeSize = 0;
			g_StorageManager->GetDiskcapacity(&ullDiskSize, &ullDiskUsedSize, &ullDiskFreeSize);

            cmiotSdcardPartList_t* pSDPartList = (cmiotSdcardPartList_t *)output;
            pSDPartList->partNum = 1;
            pSDPartList->part = (cmiotSdcardPartInfo_t *)calloc(1, (sizeof(cmiotSdcardPartInfo_t)) * pSDPartList->partNum);
            pSDPartList->part[0].total  = ullDiskSize;
            pSDPartList->part[0].remain = ullDiskFreeSize;
            strcpy(pSDPartList->part[0].name, "/dev/mmcblk1p1");
            strcpy(pSDPartList->part[0].fs, "vfat");
            break;
        }

        case CMIOT_CMD_GET_DEV_SD_NORMAL_TIMELINE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_DEV_SD_NORMAL_TIMELINE **********\n");
			#if 0
            cmiotTimelineRange_t* tlRange = (cmiotTimelineRange_t *)input;
            cmiotTimelineInfo_t   tlResult = {0};
            cmiotTimelineInfo_t*  tlOutput = (cmiotTimelineInfo_t *)output;
            DEMO_PRINT("request timeline for [%12llu - %12llu], pageSize=%u !\n", tlRange->startTime, tlRange->endTime, tlRange->pageSize);

            // output已在SDK内申请内存, output->sections内存需设备申请，SDK释放
            if(CMIOT_RETURN_CODE_SUCCESS != demo_external_sd_record_get_normal_timeline("./extSdCard", tlRange->startTime, tlRange->endTime, tlRange->pageSize, &tlResult))
            {
                DEMO_PRINT("demo_external_sd_record_get_normal_timeline failed !\n");
                return CMIOT_RETURN_CODE_FAILED;
            }
            cmiot_uint32_t i;
            DEMO_PRINT("get external sd record timeline num[%u]:\n", tlResult.sectionNum);
            for(i = 0; i < tlResult.sectionNum; i++)
            {
                DEMO_PRINT("\tsection[%u]: startTime[%12llu] endTime[%12llu]\n", i, tlResult.sections[i].startTime, tlResult.sections[i].endTime);
            }

            tlOutput->sectionNum = tlResult.sectionNum;
            tlOutput->sections = tlResult.sections;
            tlOutput->hasMore = tlResult.hasMore;
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_GET_DEV_SD_ALARM_TIMELINE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_DEV_SD_ALARM_TIMELINE **********\n");
			#if 0
            cmiotAlarmTimelineRange_t* tlRange = (cmiotAlarmTimelineRange_t *)input;
            cmiotAlarmTimelineInfo_t   tlResult = {0};
            cmiotAlarmTimelineInfo_t*  tlOutput = (cmiotAlarmTimelineInfo_t *)output;
            cmiot_char_t alarmTypeStr[512] = {0};
            cmiot_int32_t i = 0;
            for(i = 0; i < tlRange->alarmTypeNum; i++)
            {
                if(i != tlRange->alarmTypeNum - 1)
                {
                    snprintf(alarmTypeStr + strlen(alarmTypeStr), sizeof(alarmTypeStr) - strlen(alarmTypeStr), "%d,", tlRange->alarmTypeList[i]);
                }
                else
                {
                    snprintf(alarmTypeStr + strlen(alarmTypeStr), sizeof(alarmTypeStr) - strlen(alarmTypeStr), "%d", tlRange->alarmTypeList[i]);
                }
            }
            DEMO_PRINT("request alarm timeline for [%12llu - %12llu], pageSize=%u, streamId=%u, alarmTypes[%s]!\n", \
                        tlRange->rangeInfo.startTime, tlRange->rangeInfo.endTime, tlRange->rangeInfo.pageSize, streamId, alarmTypeStr);

            // output已在SDK内申请内存, output->sections内存需设备申请，SDK释放
            if(CMIOT_RETURN_CODE_SUCCESS != demo_external_get_alarm_timeline("./extSdCard", tlRange, streamId, &tlResult))
            {
                DEMO_PRINT("demo_external_get_alarm_timeline failed !\n");
                return CMIOT_RETURN_CODE_FAILED;
            }
            
            DEMO_PRINT("get external alarm timeline num[%u]:\n", tlResult.sectionNum);
            for(i = 0; i < tlResult.sectionNum; i++)
            {
                DEMO_PRINT("\tsection[%u]: type[%d] startTime[%12llu] endTime[%12llu]\n", i, tlResult.sections[i].alarmType, tlResult.sections[i].startTime, tlResult.sections[i].endTime);
            }

            tlOutput->sectionNum = tlResult.sectionNum;
            tlOutput->sections = tlResult.sections;
            tlOutput->hasMore = tlResult.hasMore;
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_GET_WIFI_SIGNAL:
        {
            //获取Wifi强度 测试用例
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_WIFI_SIGNAL **********\n");
            cmiotConWifiInfo_t* pConWifi = (cmiotConWifiInfo_t *)output;

			int ret;
			router_signal_s stWifiInfo;
			ret = WifiSignal(&stWifiInfo);
			if (0 == ret)
			{
	            snprintf(pConWifi->ssid, sizeof(pConWifi->ssid), stWifiInfo.essid);
	            pConWifi->signal = stWifiInfo.uchSignal;
			}
            break;
        }

        case CMIOT_CMD_GET_SYSTEM_STATUS:
        {
            //获取设备运行状态 测试用例
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_SYSTEM_STATUS **********\n");
            cmiotSystemStatInfo_t *info = (cmiotSystemStatInfo_t *)output;
            strcpy(info->model, "RV1106g2");
            strcpy(info->producer, "Rockchip");
            strcpy(info->systemEnv, "Linux Rockchip 5.10.160");
            strcpy(info->power, "1TOPS");
			struct sysinfo si;
			sysinfo(&si);
			printf("---Totalram:		%d\n", si.totalram);
			printf("---Available:		%d\n", si.freeram);
            info->totalMemory = si.totalram/1024/1024;
            info->remainMemory = si.freeram/1024/1024;
			struct statfs sd_fs;
			if (statfs("/userdata", &sd_fs) != 0)
				AppErr("statfs failed!\n");
            info->totalStorage = ((uint64_t)sd_fs.f_blocks * (uint64_t)sd_fs.f_bsize)/1024/1024;
            info->remainStorage = ((uint64_t)sd_fs.f_bavail * (uint64_t)sd_fs.f_bsize)/1024/1024;
            strcpy(info->networkType, "DHCP");
            info->packetLossRate = 19.19f;
            break;
        }

        case CMIOT_CMD_GET_CELLULAR_SIGNAL:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_CELLULAR_SIGNAL **********\n");
            cmiot_uint8_t *strength = (cmiot_uint8_t *)output;
            *strength = 90;
            break;
        }

        case CMIOT_CMD_GET_BATTERY_STATUS:
        {
            //获取电池电量 测试用例
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_BATTERY_STATUS **********\n");
            cmiotBatteryInfo_t *info = (cmiotBatteryInfo_t *)output;
            info->battery = 4;
            info->percent = 80;
            break;
        }

#if 0
/* ===== 以下为demo CMIOT_CMD_GET_DEV_LOG处理, 保留作为参考 ===== */
#ifdef FEATURE_LOGUPLOAD
        case CMIOT_CMD_GET_DEV_LOG:
        {
            cmiotLogReportTimeRange_t* info = (cmiotLogReportTimeRange_t *)input;
            cmiotLogReportInfo_t *pLogInfo = (cmiotLogReportInfo_t *)output;
            cmiotReturnCode_e ret = CMIOT_RETURN_CODE_SUCCESS;

            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_LOG_REPORT **********,startTime:%u, endTime:%u\n", info->startTime, info->endTime);

            // 此处仅为示例，厂商需返回时间范围内的设备日志，文件命名格式为DEV_%Y-%m-%d-%H-%M-%S.txt（日志文件开始记录的时间），单个文件不超过1Mbyte，
            // 如startTime为0，则返回结束时间前的所有日志文件路径
            // 如不支持，返回CMIOT_RETURN_CODE_FAILED

            /* 获取所有日志文件 */
            if(0 == strlen(g_devLogPath))
            {
                DEMO_PRINT("dev log path not set\n");
                return CMIOT_RETURN_CODE_FAILED;
            }

            struct dirent **files = NULL;
            cmiot_int32_t fileNum = 0;
            cmiot_int32_t i = 0;

            fileNum = scandir(g_devLogPath, &files, _custom_filter_dev_log_file, alphasort);
            if(fileNum < 0)
            {
                DEMO_PRINT("scandir failed, errno:%d, %s\n", errno, strerror(errno));
                return CMIOT_RETURN_CODE_FAILED;
            }
            else if(fileNum == 0)
            {
                DEMO_PRINT("there is no dev log file\n");
                return CMIOT_RETURN_CODE_FAILED;
            }

            for(i = 0; i < fileNum; i++)
            {
                cmiot_uint64_t fileEndTime = 0;
                cmiot_uint64_t fileStartTime = 0;

                /* 获取文件起止时间 */
                struct stat buf = {0};
                char tmpPath[512]  = {0};
                snprintf(tmpPath, sizeof(tmpPath)-1, "%s/%s", g_devLogPath, files[i]->d_name);
                if(0 != stat(tmpPath, &buf))
                {
                    DEMO_PRINT("get dev log file stat failed errno:%d, %s\n", errno, strerror(errno));
                    continue;
                }
                fileEndTime = buf.st_mtime;

                cmiot_char_t time[32] = {0};
                struct tm tm = {0};
                if(sscanf(files[i]->d_name, DEV_LOG_FILE_NAME_PATTERN, time) != 1)
                {
                    DEMO_PRINT("log file[%s] name is invalid\n", files[i]->d_name);
                    continue;
                }
                if(strptime(time, DEV_LOG_FILE_TIME_PATTERN, &tm) == NULL)
                {
                    DEMO_PRINT("log file[%s] name is invalid\n", files[i]->d_name);
                    continue;
                }
                fileStartTime = mktime(&tm);

                //判断符合条件的日志
                if( !(info->startTime == 0 && fileEndTime <= info->endTime) && (fileStartTime > info->endTime || fileEndTime < info->startTime) )
                    continue;
                else
                    DEMO_PRINT("find dev log file[%d]:[%s]\n", i, files[i]->d_name);

                pLogInfo->logNum++;
                pLogInfo->logSize += buf.st_size;

                //追加内存
                cmiotLogPath_t *tmpPtr = (cmiotLogPath_t *)realloc(pLogInfo->pathInfo, pLogInfo->logNum * sizeof(cmiotLogPath_t));
                if(tmpPtr == NULL)
                {
                    DEMO_PRINT("malloc failed!\n");
                    if(pLogInfo->pathInfo)
                    {
                        free(pLogInfo->pathInfo);
                        pLogInfo->pathInfo = NULL;
                    }

                    ret = CMIOT_RETURN_CODE_FAILED;
                    break;
                }
                strncpy(tmpPtr[pLogInfo->logNum - 1].logPath, tmpPath, sizeof(tmpPtr[pLogInfo->logNum - 1].logPath));
                pLogInfo->pathInfo = tmpPtr;
            }

            for(i = 0; i < fileNum; i++)
            {
                free(files[i]);
            }
            free(files);

            return ret;
        }
#endif
/* ===== 以上为demo CMIOT_CMD_GET_DEV_LOG处理, 保留作为参考 ===== */
#endif

        /* ===== 新的 CMIOT_CMD_GET_DEV_LOG 实现 ===== */
        case CMIOT_CMD_GET_DEV_LOG:
        {
            cmiotLogReportTimeRange_t* info = (cmiotLogReportTimeRange_t *)input;
            cmiotLogReportInfo_t *pLogInfo = (cmiotLogReportInfo_t *)output;

            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_DEV_LOG **********, startTime:%u, endTime:%u\n", info->startTime, info->endTime);

            if (CMIOT_RETURN_CODE_SUCCESS != dev_log_get_report_info(info->startTime, info->endTime, pLogInfo))
            {
                DEMO_PRINT("dev_log_get_report_info failed!\n");
                return CMIOT_RETURN_CODE_FAILED;
            }

            DEMO_PRINT("found %u dev log files, total size: %llu bytes\n", pLogInfo->logNum, pLogInfo->logSize);
            return CMIOT_RETURN_CODE_SUCCESS;
        }

        case CMIOT_CMD_GET_DEV_SD_CODED_FORMAT_BY_TIME:
        {
            //获取回放文件编码格式 测试用例
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_DEV_SD_CODED_FORMAT_BY_TIME **********, starttime:%llu\n", *(cmiot_uint64_t *)input);
			#if 0
			cmiot_int32_t *codeFormat = (cmiot_int32_t *)output;
            *codeFormat = demo_external_sd_record_get_file_coded_format_by_time("./extSdCard", *(cmiot_uint64_t *)input);
            if(*codeFormat == -1)
            {
                DEMO_PRINT("demo_external_sd_record_get_file_coded_format_by_time failed\n");
                return -1;
            }
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_GET_DEV_ACCESS_NET_INFO:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_GET_DEV_ACCESS_NET_INFO **********\n");
            printf("\033[1;36m   CMIOT_CMD_GET_DEV_ACCESS_NET_INFO  \033[0m\n");
            cmiotAccessNetInfo_t* netInfo = (cmiotAccessNetInfo_t*)output;

			NET_WORK_LIND_MODE e_link_type = g_NetConfigHook.GetNetWorkLindMode();

			if (NET_WORK_MODE_ETH0 == e_link_type)
			{
				netInfo->accessMethod = CMIOT_ACCESS_WIRED;
                netInfo->netInfo.accessWiredInfo.reportWiredInfo = CMIOT_TRUE;
                netInfo->netInfo.accessWiredInfo.networkSpeed = 100;
			}
			else if (NET_WORK_MODE_STA == e_link_type)
            {
				netInfo->accessMethod = CMIOT_ACCESS_WIFI;
                netInfo->netInfo.accessWifiInfo.reportWifiInfo = CMIOT_TRUE;
				router_signal_s stWifiInfo = {0};
				WifiSignal(&stWifiInfo);
                netInfo->netInfo.accessWifiInfo.linkSpeed = stWifiInfo.uchLindSpeed;
                netInfo->netInfo.accessWifiInfo.wifiSignal = stWifiInfo.dBm;
				if (1 == stWifiInfo.uchFreq)
	                netInfo->netInfo.accessWifiInfo.frequency = CMIOT_BAND_2_4GHZ;
				else if (2 == stWifiInfo.uchFreq)
					netInfo->netInfo.accessWifiInfo.frequency = CMIOT_BAND_5GHZ;
            }

			char dns[16] = {0};
			NetGetDns(dns);
			printf("dns: %s\n", dns);
            snprintf(netInfo->accessNetParams.dns, sizeof(netInfo->accessNetParams.dns), dns);
            strncpy(netInfo->accessNetParams.ipv6Dns, "2001:4860:4860::8888", sizeof(netInfo->accessNetParams.ipv6Dns) - 1);
			char ip[16] = {0};
			g_NetConfigHook.GetNetWorkIp(ip, sizeof(ip));
			printf("ip: %s\n", ip);
            snprintf(netInfo->accessNetParams.ipv4Addr, sizeof(netInfo->accessNetParams.ipv4Addr), ip);
            strncpy(netInfo->accessNetParams.ipv6Addr, "fe80::f02d:2dff:fe6b:af42", sizeof(netInfo->accessNetParams.ipv6Addr) - 1);
			char gw_addr[16] = {0};
			char gw_mac[18] = {0};
			int ret = NetGetGateway(gw_addr, gw_mac);
			printf("gw_addr: %s, gw_mac: %s\n", gw_addr, gw_mac);
			snprintf(netInfo->accessNetParams.gateway, sizeof(netInfo->accessNetParams.gateway), gw_addr);
            snprintf(netInfo->accessNetParams.gatewayMac, sizeof(netInfo->accessNetParams.gatewayMac), gw_mac);
            break;
        }

        default:
            break;
    }

    return 0;
}

cmiot_int32_t demo_dev_control_callback(cmiotDevControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    switch (cmd)
    {
        case CMIOT_CMD_CONTROL_MODIFY_WIFI:
        {
#if 0
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_MODIFY_WIFI **********\n");
            cmiotModifyWifiReqInfo_t *wifi = (cmiotModifyWifiReqInfo_t *)input;
            DEMO_PRINT("ssid[%s], bssid[%s], enc[%s], key[%s]", wifi->ssid, wifi->bssid, wifi->encryption, wifi->key);
            sleep(10);
            g_wifiList.wifi[0].connected = g_wifiList.wifi[1].connected = g_wifiList.wifi[2].connected = 0;
            if(strstr(wifi->ssid, "1"))
            {
                g_wifiList.wifi[0].connected = 1;
            }
            else if(strstr(wifi->ssid, "2"))
            {
                g_wifiList.wifi[1].connected = 1;
            }
            else
            {
                g_wifiList.wifi[2].connected = 1;
            }
            cmiot_char_t wifiSSID[128] = {0};
            strncpy(wifiSSID, wifi->ssid, sizeof(wifiSSID)-1);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_WIFI_SSID, wifiSSID, strlen(wifiSSID));
            demo_warp_cmiot_update_device_config_send();
#else
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_MODIFY_WIFI **********\n");
            printf("\033[1;36m CMIOT_CMD_CONTROL_MODIFY_WIFI    \033[0m\n");
            cmiotModifyWifiReqInfo_t *wifi = (cmiotModifyWifiReqInfo_t *)input;
            DEMO_PRINT("ssid[%s], bssid[%s], enc[%s], key[%s]", wifi->ssid, wifi->bssid, wifi->encryption, wifi->key);
            //检查WiFi ssid是否为空
            if (!(wifi->ssid) || wifi->ssid[0] == '\0')//扫描所有ap
            {
                printf("\033[1;35m CMIOT_CMD_CONTROL_MODIFY_WIFI    \033[0m\n");
                printf("ssid is empty.\n");
                return CMIOT_RETURN_CODE_FAILED;
            }
           
            g_NetConfigHook.SetWifi(wifi->ssid, wifi->key);
            g_NetConfigHook.ReConn();
            g_NetConfigHook.SetWifiSwitch(true);
            printf("\033[1;36m CMIOT_CMD_CONTROL_MODIFY_WIFI---------end    \033[0m\n");
            // g_AudioPrompt.aoPlay(AUDIO_FILE_QRCODE_GET_COMPLETE);
            
#endif
            break;
        }

        case CMIOT_CMD_CONTROL_SD_CARD_FORMAT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SD_CARD_FORMAT **********\n");
            cmiotSdCardInfo_t data = {CMIOT_SD_CARD_STATUS_UNFORMATTED, ""};
            cmiot_update_device_config(CMIOT_UPDATE_CFG_SD_STATUS, (char *)&data, sizeof(int));
            demo_warp_cmiot_update_device_config_send();
            /* 通知日志模块格式化即将开始，提前切回Flash路径 */
			dev_log_notify_sd_format_start();
            dev_log_write(DEV_LOG_INFO, DEV_EVENT_SD_CARD, "SD card format start");
            /* 执行格式化操作 */
			g_StorageManager->DiskFmt(false);
			int count = 50;
			while(count-- > 0)
			{
				if( g_StorageManager->GetDiskState() == DISK_STATUS_FORMATING )
				{
					printf("start format. count=%d\n", count);
					break;
				}
				usleep(100000); 	// 100ms
			}
			if(count > 0)
			{
				memset(&data, 0, sizeof(cmiotSdCardInfo_t));
				data.status = CMIOT_SD_CARD_STATUS_FORMATTING;
				cmiot_update_device_config(CMIOT_UPDATE_CFG_SD_STATUS, (char *)&data, sizeof(int));
				demo_warp_cmiot_update_device_config_send();
				
				while(g_StorageManager->GetDiskState() == DISK_STATUS_FORMATING)
				{					
					usleep(100000); 	// 100ms
				}
				printf("finish format.\n");
			}

			memset(&data, 0, sizeof(cmiotSdCardInfo_t));
			data.status = CMIOT_SD_CARD_STATUS_NOT_READY;
			
			int sd_status = g_StorageManager->GetDiskState();
			if( sd_status == DISK_STATUS_NORMAL )
			{
	            /* 格式化成功 */
	            data.status = CMIOT_SD_CARD_STATUS_READY;
	            /* 通知日志模块格式化已完成，后续SD_INSERTED事件将触发转存 */
	            dev_log_notify_sd_format_done();
                dev_log_write(DEV_LOG_INFO, DEV_EVENT_SD_CARD, "SD card format done");
			}
			else if ( sd_status == DISK_STATUS_UNKNOWN || sd_status == DISK_STATUS_NO_DISK )
			{
	            data.status = CMIOT_SD_CARD_STATUS_NOT_READY;
			}
			else if ( sd_status == DISK_STATUS_NOT_MOUNT || sd_status == DISK_STATUS_ERROR )
			{
	            data.status = CMIOT_SD_CARD_STATUS_ABNORMAL;
			}
			cmiot_update_device_config(CMIOT_UPDATE_CFG_SD_STATUS, (char *)&data, sizeof(int));
			demo_warp_cmiot_update_device_config_send();
	
            break;
        }

        case CMIOT_CMD_CONTROL_VIDEO_QUALITY:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_VIDEO_QUALITY **********, value is [%d]\n", *(int *)input);

			cmiotVideoReso_t stVideoReso;
			stVideoReso.streamId = 0;

			if (*(int *)input)
			{
				g_cmiot_video_quality = 1;
				stVideoReso.videoWidth = 2560;
				stVideoReso.videoHeight = 1440;
			}
			else
			{
				g_cmiot_video_quality = 0;
				stVideoReso.videoWidth = 1280;
				stVideoReso.videoHeight = 720;
			}
			
			int ret = cmiot_notify_resolution_change(&stVideoReso);
			printf("cmiot_notify_resolution_change(%d * %d) ret: %d\n", stVideoReso.videoWidth, stVideoReso.videoHeight, ret);
            break;
        }

        case CMIOT_CMD_CONTROL_DEVICE_REBOOT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_DEVICE_REBOOT **********\n");
            dev_log_write(DEV_LOG_INFO, DEV_EVENT_SYSTEM, "device reboot");
            AbnormalRestart();
            break;
        }
        
        case CMIOT_CMD_CONTROL_THUMBNAIL_PUSH:      /* 控制上传缩略图 */
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_THUMBNAIL_PUSH **********\n");
            cmiotThumbnailReqInfo_t *thumbnail = (cmiotThumbnailReqInfo_t *)input;
            DEMO_PRINT("width: %u, height: %u \n", thumbnail->width, thumbnail->height);
            cmiotThumbUploadInfo_t *info = (cmiotThumbUploadInfo_t*)output;

			info->num = 1;
            info->thumbInfo = (cmiotPicData_t*)calloc(1, info->num * sizeof(cmiotPicData_t));
            if(!info->thumbInfo)
            {
                DEMO_PRINT("malloc failed!");
                return CMIOT_RETURN_CODE_FAILED;
            }

			void *pic_data = NULL;
			unsigned int pic_size = 0;
			int ret = tmp_rkipc_snap(&pic_data, &pic_size);
			if (ret)
			{
                DEMO_PRINT("tmp_rkipc_snap failed!");
                if(info->thumbInfo)
                {
                    free(info->thumbInfo);
                    info->thumbInfo = NULL;
                }
                return CMIOT_RETURN_CODE_FAILED;
			}

            info->thumbInfo[0].streamId = 0;
            info->thumbInfo[0].data = (cmiot_char_t *)pic_data;
			info->thumbInfo[0].dataSize = pic_size;
			info->thumbInfo[0].ts = GetSystemUptime_ms();
			info->thumbInfo[0].utcms = get_utc_time_ms();
			            
            break;
        }

        case CMIOT_CMD_CONTROL_TIME_CALIBRATION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_TIME_CALIBRATION **********, value is [%llu]\n", *(cmiot_uint64_t *)input);
            break;
        }

        case CMIOT_CMD_CONTROL_SYNC_TIME:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SYNC_TIME **********, value is [%u]\n", *(cmiot_uint32_t *)input);
            struct timeval tv;
            tv.tv_sec = *(cmiot_uint32_t *)input;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            dev_log_write(DEV_LOG_INFO, DEV_EVENT_TIME, "system time synced from platform, time=%u", *(cmiot_uint32_t *)input);
            break;
        }

        case CMIOT_CMD_CONTROL_RTN_VIDEO_PARAMS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_RTN_VIDEO_PARAMS **********\n");
            cmiotRtnVideoParams_t *params = (cmiotRtnVideoParams_t *)input;
            DEMO_PRINT("resolution:%d, bps: %d, fps: %d, gop: %d, initQP: %d, ImaxQP: %d, IminQP: %d, PmaxQP: %d, PminQP:%d\n", \
            params->resolution ,params->bps, params->fps, params->gop, params->initQP, params->ImaxQP, params->IminQP, params->PmaxQP, params->PminQP);

            /* 分辨率产生变化后，调用cmiot_notify_resolution_change通知平台 */
            break;
        }
        
        case CMIOT_CMD_CONTROL_RTN_VIDEO_FPS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_RTN_VIDEO_FPS **********, value is [%u]\n", *(cmiot_int32_t *)input);
            break;
        }

        case CMIOT_CMD_CONTROL_RTN_VIDEO_RC_CHANGE_PARAMS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_RTN_VIDEO_PARAMS **********\n");
            cmiotRtnRcChangeParams_t *params = (cmiotRtnRcChangeParams_t *)input;
            DEMO_PRINT("bitrate:%d, IminQP: %d, ImaxQP: %d, PminQP: %d, PmaxQP: %d, fps: %d, maxpercent: %d\n", \
            params->bitrate, params->IminQP, params->ImaxQP, params->PminQP, params->PmaxQP, params->fps, params->maxpercent);
            break;
        }

        case CMIOT_CMD_CONTROL_SOUND_ALARM_ONCE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SOUND_ALARM_ONCE **********\n");
            cmiotSoundLightAlarmOnce_t *lAinfo = (cmiotSoundLightAlarmOnce_t *)input;
            DEMO_PRINT("ctrl sound alarm once: times[%u] url[%s]\n", lAinfo->soundTimes, lAinfo->soundUrl);
            break;
        }
        
        case CMIOT_CMD_CONTROL_LIGHT_ALARM_ONCE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_LIGHT_ALARM_ONCE **********\n");
            cmiotSoundLightAlarmOnce_t *sAinfo = (cmiotSoundLightAlarmOnce_t *)input;
            DEMO_PRINT("ctrl light alarm once: mode[%u] time[%u]\n", sAinfo->lightMode, sAinfo->lightTime);
            break;
        }

        case CMIOT_CMD_CONTROL_DEVICE_COMMON_CMD:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_DEVICE_COMMON_CMD **********\n");
            cmiot_char_t *cmdInfo = (cmiot_char_t *)input;
            DEMO_PRINT("cmd:%s\n", cmdInfo);
            break;
        }

        case CMIOT_CMD_CONTROL_SHMEM_WRITE_START:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SHMEM_WRITE_START **********\n");
            cmiotShmemWriteInfo_t *info = (cmiotShmemWriteInfo_t *)input;
            DEMO_PRINT("handle:%p, height:%u, width:%u, fps:%u\n", info->handle, info->height, info->width, info->fps);
            break;
        }

        case CMIOT_CMD_CONTROL_SHMEM_WRITE_STOP:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SHMEM_WRITE_STOP **********, value is [%p]\n", (void *)input);
            break;
        }

        case CMIOT_CMD_CONTROL_SOUND_ALARM_AI:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SOUND_ALARM_AI **********\n");
            cmiotSoundAlarmInfo_t* soundParam = (cmiotSoundAlarmInfo_t*)input;
            DEMO_PRINT("times: %u, audioPath: %s\n", soundParam->times, soundParam->audioPath);
            break;
        }

        case CMIOT_CMD_CONTROL_LIGHT_ALARM_AI:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_LIGHT_ALARM_AI **********\n");
            cmiotLightAlarmInfo_t* lightParam = (cmiotLightAlarmInfo_t*)input;
            DEMO_PRINT("mode: %u, time: %u\n", lightParam->mode, lightParam->time);
            break;
        }

        case CMIOT_CMD_CONTROL_SD_REPLAY_STRAT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SD_REPLAY_STRAT **********, value is [%llu]\n", *(cmiot_uint64_t*)input);
			#if 0
			cmiot_uint64_t startTime = *(cmiot_uint64_t*)input;
            demo_set_sd_external_replay_thread(startTime);
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_CONTROL_SD_REPLAY_END:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SD_REPLAY_END **********\n");
			#if 0
            demo_set_sd_external_replay_stop();
            cmiot_notify_sd_replay_stop();
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_CONTROL_SD_RECORD_STATUS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_SD_RECORD_STATUS **********, value is [%d]\n", *(cmiot_int32_t *)input);
			#if 0
			g_externalSdRecordStatus = *(cmiot_int32_t *)input;
            if(g_externalSdRecordStatus == 0)
            {
                demo_external_sd_record_stop();
            }
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

        case CMIOT_CMD_CONTROL_SD_RECORD_TYPE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_EXTERNAL_SD_RECORD_TYPE **********, value is [%d]\n", *(cmiot_int32_t *)input);
			#if 0
			g_externalSdRecordType = *(cmiot_int32_t *)input;
    
            if(g_externalSdRecordType == SD_CARD_RECORD_TYPE_ALL_DAY) {
                DEMO_PRINT("External SD record type set to ALL DAY\n");
                demo_external_event_record_buf_deinit();
                
            } else if(g_externalSdRecordType == SD_CARD_RECORD_TYPE_EVENT) {
                DEMO_PRINT("External SD record type set to EVENT\n");
                 // 初始化事件录制外部缓冲区
                if(demo_external_event_record_buf_init() != CMIOT_RETURN_CODE_SUCCESS)
                {
                    DEMO_PRINT("init external event record buffer failed!\n");
                }
                
            } else {
                DEMO_PRINT("External SD record type set to DEFAULT\n");
            }
			#else
			return CMIOT_RETURN_CODE_FAILED;
			#endif
            break;
        }

#if 0
/* ===== 以下为demo CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT处理, 保留作为参考 ===== */
        case CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT **********, value is [%d]\n", *(cmiot_int32_t*) input);
            g_activeLogReport = *(cmiot_int32_t*) input;
            break;
        }
/* ===== 以上为demo CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT处理, 保留作为参考 ===== */
#endif

/* ===== 新的 CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT 实现 ===== */
        case CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT **********, value is [%d]\n", *(cmiot_int32_t*) input);
            g_activeLogReport = *(cmiot_int32_t*) input;
            dev_log_notify_active_upload(g_activeLogReport);
            break;
        }

        default:
            break;
    }

    return CMIOT_RETURN_CODE_SUCCESS;
}

static cmiot_int32_t s_panDirection = 0;
static cmiot_int32_t s_tiltDirection = 0;
static cmiot_int32_t s_panSpeed = 0;
static cmiot_int32_t s_tiltSpeed = 0;

void ptz_timer_handle(union sigval arg)  
{
    cmiot_int32_t newPan = g_ptzPos.pan + s_panDirection * s_panSpeed;
    cmiot_int32_t newTilt = g_ptzPos.tilt + s_tiltDirection * s_tiltSpeed;

    // 限制 pan 的范围
    if (newPan < PTZ_POS_MIN) {
        g_ptzPos.pan = PTZ_POS_MIN;
    } else if (newPan > PTZ_POS_MAX) {
        g_ptzPos.pan = PTZ_POS_MAX;
    } else {
        g_ptzPos.pan = newPan;
    }

    // 限制 tilt 的范围
    if (newTilt < PTZ_POS_MIN) {
        g_ptzPos.tilt = PTZ_POS_MIN;
    } else if (newTilt > PTZ_POS_MAX) {
        g_ptzPos.tilt = PTZ_POS_MAX;
    } else {
        g_ptzPos.tilt = newTilt;
    }

    DEMO_PRINT("pan: %d, tilt: %d\n",  g_ptzPos.pan, g_ptzPos.tilt);
}



cmiot_int32_t demo_ptz_control_callback(cmiotPtzControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    switch (cmd)
    {
        case CMIOT_CMD_PTZ_GET_POSITION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_GET_POSITION **********\n");
            cmiotPtzPositionInfo_t *info = (cmiotPtzPositionInfo_t *)output;
            info->pan = g_ptzPos.pan;
            info->tilt = g_ptzPos.tilt;
            DEMO_PRINT("streamId: %u, pan: %d, tilt: %d\n", streamId, info->pan, info->tilt);
            break;
        }

        case CMIOT_CMD_PTZ_MOVE_TO_POSITION:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_MOVE_TO_POSITION **********\n");
            cmiotPtzPositionInfo_t *info = (cmiotPtzPositionInfo_t *)input;
            DEMO_PRINT("streamId: %u, pan: %d, tilt: %d\n", streamId, info->pan, info->tilt);
            g_ptzPos.pan = info->pan;
            g_ptzPos.tilt = info->tilt;
            break;
        }

        case CMIOT_CMD_PTZ_MOVE://方向盘控制时走这个回调
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_MOVE **********\n");
            cmiotPtzMoveInfo_t *info = (cmiotPtzMoveInfo_t *)input;
            DEMO_PRINT("streamId: %u, value: %d, speed: %d, speedVertical: %d, ts: %llu\n", streamId, info->value, info->speed, info->speedVertical, info->ts);

            if(info->value == 1 || info->value == 5 || info->value == 7)
                s_panDirection = -1;
            else if(info->value == 2 || info->value == 6 || info->value == 8)
                s_panDirection = 1;

            if(info->value == 3 || info->value == 5 || info->value == 6)
                s_tiltDirection = 1;
            else if(info->value == 4 || info->value == 7 || info->value == 8)
                s_tiltDirection = -1;
            
            if(info->speed)
                s_panSpeed = 2000 * info->speed;
            else
                s_panSpeed = 2000;

            if(info->speedVertical)
                s_tiltSpeed = 2000 * info->speedVertical;
            else
                s_tiltSpeed = 2000;

            if(info->value != 0)
            {
                struct itimerspec it = {0}; 
                it.it_interval.tv_sec = 0;
                it.it_interval.tv_nsec = 100 * 1000000;
                it.it_value.tv_sec = 0;
                it.it_value.tv_nsec = 1000000;
                timer_settime(g_ptzMoveTimerID, 0, &it, NULL);
            }
            else
            {
                struct itimerspec it = {0}; 
                timer_settime(g_ptzMoveTimerID, 0, &it, NULL);
                s_panDirection = s_tiltDirection = s_panSpeed = s_tiltSpeed = 0;
            }

            break;
        }

        case CMIOT_CMD_PTZ_MOVE_BY_STEP://校准时走这个回调
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_MOVE_BY_STEP **********\n");
            cmiotPtzMoveInfo_t *info = (cmiotPtzMoveInfo_t *)input;
            DEMO_PRINT("streamId: %u, value: %d, speed: %d, speedVertical: %d, ts: %llu\n", streamId, info->value, info->speed, info->speedVertical, info->ts);
            cmiot_int32_t panDirection = 0;
            cmiot_int32_t tileDirection = 0;
            cmiot_int32_t panStepSize = 0;
            cmiot_int32_t tiltStepSize = 0;

            /* 方向 */
            if(info->value == 1 || info->value == 5 || info->value == 7)
                panDirection = -1;
            else if(info->value == 2 || info->value == 6 || info->value == 8)
                panDirection = 1;

            if(info->value == 3 || info->value == 5 || info->value == 6)
                tileDirection = 1;
            else if(info->value == 4 || info->value == 7 || info->value == 8)
                tileDirection = -1;
            
            /* 步长 */
            if(info->speed)
                panStepSize = 10000 * info->speed;
            else
                panStepSize = 10000;

            if(info->speedVertical)
                tiltStepSize = 10000 * info->speedVertical;
            else
                tiltStepSize = 10000;

            cmiot_int32_t newPan = g_ptzPos.pan + panDirection * panStepSize;
            cmiot_int32_t newTilt = g_ptzPos.tilt + tileDirection * tiltStepSize;

            // 限制 pan 的范围
            if (newPan < PTZ_POS_MIN) {
                g_ptzPos.pan = PTZ_POS_MIN;
            } else if (newPan > PTZ_POS_MAX) {
                g_ptzPos.pan = PTZ_POS_MAX;
            } else {
                g_ptzPos.pan = newPan;
            }

            // 限制 tilt 的范围
            if (newTilt < PTZ_POS_MIN) {
                g_ptzPos.tilt = PTZ_POS_MIN;
            } else if (newTilt > PTZ_POS_MAX) {
                g_ptzPos.tilt = PTZ_POS_MAX;
            } else {
                g_ptzPos.tilt = newTilt;
            }

            if(info->value == 0)
            {
                g_ptzPos.tilt = g_ptzPos.pan = 0;
            }

            DEMO_PRINT("pan: %d, tilt: %d\n",  g_ptzPos.pan, g_ptzPos.tilt);

            break;
        }

        case CMIOT_CMD_PTZ_SET_PRESET_POINT://类似涂鸦的收藏点
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_SET_PRESET_POINT **********\n");
            cmiotPtzPresetPointInfo_t *presetPointInfo = (cmiotPtzPresetPointInfo_t*)input;
            cmiot_uint32_t action = presetPointInfo->action;
            cmiot_uint32_t code = presetPointInfo->presetPointCode;
            
            DEMO_PRINT("streamId: %u, action: %u, presetPointCode:%u\n", streamId, action, code);
            if(code >= 1 && code <= 255)
            {
                if(action == 1)
                {
                    DEMO_PRINT("insert preset point pan: %d, tilt :%d\n", g_ptzPos.pan, g_ptzPos.tilt);
                    g_presetPointCode_0[code] = 1;
                    g_presetPointPos_0[code].pan = g_ptzPos.pan;
                    g_presetPointPos_0[code].tilt = g_ptzPos.tilt;
                }
                else if(action == 2)
                {
                    DEMO_PRINT("modify preset point to pan: %d, tilt: %d\n", g_ptzPos.pan, g_ptzPos.tilt);
                    g_presetPointPos_0[code].pan = g_ptzPos.pan;
                    g_presetPointPos_0[code].tilt = g_ptzPos.tilt;
                }
                else if(action == 3)
                {
                    DEMO_PRINT("delete preset point");
                    g_presetPointCode_0[code] = 0;
                    g_presetPointPos_0[code].pan = 0;
                    g_presetPointPos_0[code].tilt = 0;
                }
                else if(action == 4)
                {
                    g_ptzPos.pan = g_presetPointPos_0[code].pan;
                    g_ptzPos.tilt = g_presetPointPos_0[code].tilt;
                    DEMO_PRINT("jump to the specified preset point, pan: %d, tilt: %d\n", g_presetPointPos_0[code].pan, g_presetPointPos_0[code].tilt);
                }
            }

            if(g_devParams.devType == CMIOT_DEV_TYPE_MULTI_CAM && streamId == 1 && code >= 1 && code <= 255)
            {
                if(action == 1)
                {
                    DEMO_PRINT("insert preset point pan: %d, tilt :%d\n", g_ptzPos.pan, g_ptzPos.tilt);
                    g_presetPointCode_1[code] = 1;
                }
                if(action == 2)
                {
                    DEMO_PRINT("modify preset point to pan: %d, tilt: %d\n", g_ptzPos.pan, g_ptzPos.tilt);
                }
                if(action == 3)
                {
                    DEMO_PRINT("delete preset point");
                    g_presetPointCode_1[code] = 0;
                }
                if(action == 4)
                {
                    DEMO_PRINT("jump to the specified preset point");
                }
            }
            break;
        }

        case CMIOT_CMD_PTZ_SET_CRUISE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_SET_CRUISE **********\n");
            cmiotPtzCruiseInfo_t *cruiseInfo = (cmiotPtzCruiseInfo_t*)input;
            cmiot_uint32_t action = cruiseInfo->action;
            cmiot_uint32_t cruiseCode = cruiseInfo->cruiseCode;
            DEMO_PRINT("streamId: %u, action: %u, cruiseCode: %u, speed: %u, stayTime: %u\n", streamId, \
            action, cruiseCode, cruiseInfo->speed, cruiseInfo->stayTime);
            cmiot_int32_t i = 0;
            cmiot_char_t presetPointList[1024] = {0};
            if(cruiseCode >= 1 && cruiseCode <= 255)
            {
                if(action == 1 || action == 2)
                {
                    if(action == 1)
                    {
                        DEMO_PRINT("insert cruiseCode:");
                    } 
                    else
                    {
                        DEMO_PRINT("update cruiseCode:");
                    }
                    printf(" %d\n", cruiseCode);
                    DEMO_PRINT("presetPointCodes:");
                    for(i = 0; i < cruiseInfo->codesInfo.presetPointCodeNum; i++)
                    {
                        cmiot_char_t tmpStr[5] = {0};
                        if(i == 0)
                            snprintf(tmpStr, sizeof(tmpStr), "%d", cruiseInfo->codesInfo.codes[i]);
                        else
                            snprintf(tmpStr, sizeof(tmpStr), " %d", cruiseInfo->codesInfo.codes[i]);
                        tmpStr[4] = '\0';
                        strncat(presetPointList, tmpStr, strlen(tmpStr));
                    }

                    if(strlen(presetPointList))
                        printf("%s", presetPointList);
                    g_cruiseCode_0[cruiseCode] = 1;
                    if(g_cruisePresetPointCode_0[cruiseCode])
                    {
                        free(g_cruisePresetPointCode_0[cruiseCode]);
                        g_cruisePresetPointCode_0[cruiseCode] = NULL;
                    }
                    g_cruisePresetPointCode_0[cruiseCode] = (cmiot_char_t*)calloc(1, strlen(presetPointList) + 1);
                    memcpy(g_cruisePresetPointCode_0[cruiseCode], presetPointList, strlen(presetPointList));
                    g_cruisePresetPointCode_0[cruiseCode][strlen(presetPointList)] = '\0';
                    printf("\n");
                }
                
                if(action == 3)
                {
                    DEMO_PRINT("delete cruiseCode:");
                    printf(" %d\n", cruiseCode);
                    DEMO_PRINT("presetPointCodes:");
                    for(i = 0; i < cruiseInfo->codesInfo.presetPointCodeNum; i++)
                    {
                        printf(" %d", cruiseInfo->codesInfo.codes[i]);
                    }
                    g_cruiseCode_0[cruiseCode] = 0;
                    if(g_cruisePresetPointCode_0[cruiseCode])
                    {
                        free(g_cruisePresetPointCode_0[cruiseCode]);
                        g_cruisePresetPointCode_0[cruiseCode] = NULL;
                    }
                    printf("\n");
                }
            }

            if(g_devParams.devType == CMIOT_DEV_TYPE_MULTI_CAM && streamId == 1 && cruiseCode >= 1 && cruiseCode <= 255)
            {
                if(action == 1)
                {
                    DEMO_PRINT("insert cruiseCode:");
                    printf(" %d\n", cruiseCode);
                    DEMO_PRINT("presetPointCodes:");
                    for(i = 0; i < cruiseInfo->codesInfo.presetPointCodeNum; i++)
                    {
                        printf(" %d", cruiseInfo->codesInfo.codes[i]);
                    }
                    g_cruiseCode_1[cruiseCode] = 1;
                }
                if(action == 2)
                {
                    DEMO_PRINT("update cruiseCode:");
                    printf(" %d\n", cruiseCode);
                    DEMO_PRINT("presetPointCodes:");
                    for(i = 0; i < cruiseInfo->codesInfo.presetPointCodeNum; i++)
                    {
                        printf(" %d", cruiseInfo->codesInfo.codes[i]);
                    }
                }
                if(action == 3)
                {
                    DEMO_PRINT("delete cruiseCode:");
                    printf(" %d", cruiseCode);
                    DEMO_PRINT("presetPointCodes:");
                    for(i = 0; i < cruiseInfo->codesInfo.presetPointCodeNum; i++)
                    {
                        printf(" %d\n", cruiseInfo->codesInfo.codes[i]);
                    }
                    g_cruiseCode_1[cruiseCode] = 0;
                }
                printf("\n");
            }
            break;
        }

        case CMIOT_CMD_PTZ_CRUISE_CONTROL:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_CRUISE_CONTROL **********\n");
            cmiotPtzCruiseControlInfo_t *cruiseControlInfo = (cmiotPtzCruiseControlInfo_t*)input;
            DEMO_PRINT("streamId: %u, action: %u, cruiseCode: %u\n", streamId, cruiseControlInfo->action, cruiseControlInfo->cruiseCode);
            if(cruiseControlInfo->flag & 8)
            {
                DEMO_PRINT("repeat: %u, startTime: %u, endTime: %u\n", cruiseControlInfo->schedule.repeat, \
                cruiseControlInfo->schedule.startTime, cruiseControlInfo->schedule.endTime);
            }
            break;
        }

        case CMIOT_CMD_PTZ_GET_PRESET_POINT_LIST:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_GET_PRESET_POINT_LIST **********\n");
            cmiotPresetPointCodeInfo_t *prePointList = (cmiotPresetPointCodeInfo_t*)output;
            cmiot_int32_t i = 0;
            
            for(i = 0; i < sizeof(g_presetPointCode_0)/sizeof(cmiot_bool_t); i++)
            {
                if(g_presetPointCode_0[i])
                    prePointList->presetPointCodeNum++;
            }
            DEMO_PRINT("%-15s %-10s %-10s\n", "presetPoint", "pan", "tilt");
            prePointList->codes = (cmiot_uint32_t*)calloc(1, prePointList->presetPointCodeNum * sizeof(cmiot_uint32_t));
            cmiot_int32_t j = 0;
            for(i = 0; i < sizeof(g_presetPointCode_0)/sizeof(cmiot_bool_t); i++)
            {
                if(g_presetPointCode_0[i])
                {
                    prePointList->codes[j++] = i;
                    DEMO_PRINT("%-15d %-10d %-10d\n", i, g_presetPointPos_0[i].pan, g_presetPointPos_0[i].tilt);
                }
            }

            if(g_devParams.devType == CMIOT_DEV_TYPE_MULTI_CAM && streamId == 1)
            {
                for(i = 0; i < sizeof(g_presetPointCode_1)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_presetPointCode_1[i])
                        prePointList->presetPointCodeNum++;
                }
                prePointList->codes = (cmiot_uint32_t*)calloc(1, prePointList->presetPointCodeNum * sizeof(cmiot_uint32_t));
                cmiot_int32_t j = 0;
                for(i = 0; i < sizeof(g_presetPointCode_1)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_presetPointCode_1[i])
                        prePointList->codes[j++] = i;
                }
            }
            break;
        }

        case CMIOT_CMD_PTZ_GET_CRUISE_LIST:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_GET_CRUISE_LIST **********\n");
            cmiotCruiseCodeInfo_t *cruiseList = (cmiotCruiseCodeInfo_t*)output;
            cmiot_int32_t i = 0;
            DEMO_PRINT("%-15s %s\n", "cruiseCode", "presetPointList");
            if(streamId == 0)
            {
                for(i = 0; i < sizeof(g_cruiseCode_0)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_cruiseCode_0[i])
                    {
                        cruiseList->cruiseCodeNum++;
                        DEMO_PRINT("%-15d ", i);
                        if(g_cruisePresetPointCode_0[i])
                        {
                            cmiot_int32_t left = 0;
                            cmiot_int32_t right = 0;
                            cmiot_bool_t flag = 0;
                            while(1)
                            {
                                if(right >= strlen(g_cruisePresetPointCode_0[i]))
                                    break;

                                left = right;
                                while(g_cruisePresetPointCode_0[i][left] < '0' || g_cruisePresetPointCode_0[i][left] > '9')
                                    left++;
                                
                                right = left;
                                while(g_cruisePresetPointCode_0[i][right] >= '0' && g_cruisePresetPointCode_0[i][right] <= '9')
                                    right++;
                                
                                if(!flag)
                                    flag = 1;
                                else
                                    printf("->");
                                cmiot_char_t tmpStr[4] = {0};
                                memcpy(tmpStr, g_cruisePresetPointCode_0[i] + left, right - left);
                                cmiot_int32_t code = strtol(tmpStr, NULL, 10);
                                printf("%d(%d,%d)", code, g_presetPointPos_0[code].pan, g_presetPointPos_0[code].tilt);
                            }
                            printf("\n");
                        }
                    }
                        
                }
                cruiseList->codes = (cmiot_uint32_t*)calloc(1, cruiseList->cruiseCodeNum * sizeof(cmiot_uint32_t));
                cmiot_int32_t j = 0;
                for(i = 0; i < sizeof(g_cruiseCode_0)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_cruiseCode_0[i])
                        cruiseList->codes[j++] = i;
                }
            }

            if(g_devParams.devType == CMIOT_DEV_TYPE_MULTI_CAM && streamId == 1)
            {
                for(i = 0; i < sizeof(g_cruiseCode_1)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_cruiseCode_1[i])
                        cruiseList->cruiseCodeNum++;
                }
                cruiseList->codes = (cmiot_uint32_t*)calloc(1, cruiseList->cruiseCodeNum * sizeof(cmiot_uint32_t));
                cmiot_int32_t j = 0;
                for(i = 0; i < sizeof(g_cruiseCode_1)/sizeof(cmiot_bool_t); i++)
                {
                    if(g_cruiseCode_1[i])
                        cruiseList->codes[j++] = i;
                }
            }
            break;
        }

        case CMIOT_CMD_PTZ_SET_LENS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_CMD_PTZ_SET_LENS **********\n");
            cmiotLensControlInfo_t *info = (cmiotLensControlInfo_t *)input;
            DEMO_PRINT("streamId: %u, action: %u, speed: %u, move :%u\n", streamId, info->action, info->speed, info->move);
            break;
        }

        default:
            break;
    }
    return 0;
}

static cmiot_bool_t talk_enable = FALSE;

/* ===== 云广播 ===== */
#define CLOUD_BROADCAST_DIR          "/mnt/sdcard/audio"
#define CLOUD_BROADCAST_DOWNLOAD_TO  30

cmiot_int32_t demo_audio_callback(cmiotAudioCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    //音频回调，语音对讲和云广播会调用
    if(cmd == CMIOT_CMD_AUDIO_TALK)
    {
        DEMO_PRINT("get cmd ********** CMIOT_CMD_AUDIO_TALK **********\n");
        //语音对讲，将获取的语音数据写入到当前目录audio.alaw中
        cmiotAudioData_t *data = (cmiotAudioData_t *)input;
        DEMO_PRINT("[AudioTalk]: Get Audio Data With Size %u Seq %u ts %u utcms %llu !\n",data->dataSize,data->seqNum,data->ts,data->utcms);
        #if 0
        FILE* writeFDAll= fopen("/mnt/sdcard/audioAll.alaw", "ab+");
        fwrite(data->data, data->dataSize, 1, writeFDAll);
        fclose(writeFDAll);
        #else
        if( data->dataSize > 0 && talk_enable == 0)
        {
            talk_enable = TRUE;
            IAudioManager::instance()->StartAudioOut(IAudioManager::AUDIO_TALK_TYPE);
        }
        // else
        // {
            
        // }
        if( data->dataSize > 0)
        {
            IAudioManager::instance()->PlayVoice((unsigned char *)data->data, data->dataSize, IAudioManager::AUDIO_TALK_TYPE);
        }
        
        #endif
        /* dataSize为零表示本次对讲结束 */
        if(data->dataSize == 0 && (1 == talk_enable))
        {
            talk_enable = FALSE;
            IAudioManager::instance()->StopAudioOut(IAudioManager::AUDIO_TALK_TYPE);
            DEMO_PRINT("audio talk stop\n");
        }
    }
    else if(cmd == CMIOT_CMD_AUDIO_PLAY_START)
    {
#if 0
        DEMO_PRINT("get cmd ********** CMIOT_CMD_AUDIO_PLAY_START **********\n");
        //云广播开始，打印音频的相关信息
        cmiotAudioPlayInfo_t *data = (cmiotAudioPlayInfo_t *)input;
        DEMO_PRINT("[AudioPlay]: Get Audio Play Start, id: %s, url: %s, times: %u, span: %u !\n",data->id, data->url, data->times, data->span);
#endif
        DEMO_PRINT("get cmd ********** CMIOT_CMD_AUDIO_PLAY_START **********\n");
        cmiotAudioPlayInfo_t *data = (cmiotAudioPlayInfo_t *)input;
        DEMO_PRINT("[AudioPlay]: Get Audio Play Start, id: %s, url: %s, times: %u, span: %u !\n",
                   data->id, data->url, data->times, data->span);

        /* 1. 构建文件路径: /mnt/sdcard/audio/{id}.g711a */
        char filePath[512] = {0};
        snprintf(filePath, sizeof(filePath), "%s/%s.g711a", CLOUD_BROADCAST_DIR, data->id);

        /* 2. 文件不存在则下载 */
        if (access(filePath, F_OK) != 0)
        {
            DEMO_PRINT("[AudioPlay] file not exist, downloading: %s\n", filePath);

            char mkdirCmd[256] = {0};
            snprintf(mkdirCmd, sizeof(mkdirCmd), "mkdir -p %s", CLOUD_BROADCAST_DIR);
            system(mkdirCmd);

            /* 先下载到临时文件，完成后再重命名，防止下载中断留下损坏文件 */
            char tmpPath[512] = {0};
            snprintf(tmpPath, sizeof(tmpPath), "%s/%s.g711a.tmp", CLOUD_BROADCAST_DIR, data->id);

            char downloadCmd[1024] = {0};
            snprintf(downloadCmd, sizeof(downloadCmd),
                     "wget -q -O \"%s\" \"%s\" -T 10",
                     tmpPath, data->url);

            int ret = system(downloadCmd);
            if (ret != 0 || access(tmpPath, F_OK) != 0)
            {
                DEMO_PRINT("[AudioPlay] download FAILED, ret=%d\n", ret);
                unlink(tmpPath);
                return CMIOT_RETURN_CODE_FAILED;
            }

            /* 下载完成，重命名为正式文件 */
            rename(tmpPath, filePath);
            DEMO_PRINT("[AudioPlay] download success: %s\n", filePath);
        }
        else
        {
            DEMO_PRINT("[AudioPlay] file cached: %s\n", filePath);
        }

        /* 3. 计算播放时长(G711A: 8K采样率 16bit = 64kbps = 8KB/s) */
        int playTimeSec = 1;
        {
            struct stat fileStat;
            if (stat(filePath, &fileStat) == 0)
            {
                playTimeSec = fileStat.st_size / 8000 + 1;
                DEMO_PRINT("[AudioPlay] fileSize=%ld, playTime=%d s\n", fileStat.st_size, playTimeSec);
            }
        }

        /* 4. 播放: times==0 表示播放一次 */
        int playCount = (data->times == 0) ? 1 : (int)data->times;
        int spanSec   = (int)data->span;

        for (int round = 0; round < playCount; round++)
        {
            DEMO_PRINT("[AudioPlay] round %d/%d\n", round + 1, playCount);

            CAudioPrompt::AudioFileParm audioFile;
            audioFile.strFileName = filePath;
            audioFile.type = 0;
            g_AudioPrompt.aoPlay(audioFile);

            /* 等待本轮播放完成 */
            sleep(playTimeSec);

            /* 多轮间隔 */
            if (round < playCount - 1 && spanSec > 0)
            {
                DEMO_PRINT("[AudioPlay] span wait %d s\n", spanSec);
                sleep(spanSec);
            }
        }
    }
    else if(cmd == CMIOT_CMD_AUDIO_PLAY_STOP)
    {
#if 0
        DEMO_PRINT("get cmd ********** CMIOT_CMD_AUDIO_PLAY_STOP **********\n");
        //云广播结束
        cmiotAudioPlayInfo_t *data = (cmiotAudioPlayInfo_t *)input;
        DEMO_PRINT("[AudioPlay]: Get Audio Play Stop, id: %s, url: %s, times: %u, span: %u !\n",data->id, data->url, data->times, data->span);
#endif
        DEMO_PRINT("get cmd ********** CMIOT_CMD_AUDIO_PLAY_STOP **********\n");
        g_AudioPrompt.stopPlay();
    }
    return 0;
}


cmiotUpgradeInfo_t g_upgradeInfoFw = {0};
cmiotUpgradeInfo_t g_upgradeInfoApp = {0};
cmiot_uint32_t g_upgradeType = 0;
cmiot_int32_t demo_upgrade_callback(cmiotUpgradeCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    if (!input)
    {
        DEMO_PRINT("demo_upgrade_callback invalid parameters!\n");
        return -1;
    }

    switch (cmd)
    {
        case CMIOT_CMD_UPGRADE_FW:
            memcpy(&g_upgradeInfoFw, input, sizeof(cmiotUpgradeInfo_t));
            g_upgradeType = 1;
            DEMO_PRINT("get cmd ********** CMIOT_CMD_UPGRADE_FW **********\n");
            DEMO_PRINT("url = %s, version = %s, checkSum = %s\n", g_upgradeInfoFw.url, g_upgradeInfoFw.version, g_upgradeInfoFw.checkSum);
            break;

        case CMIOT_CMD_UPGRADE_APP:
            g_upgradeType = 2;
            memcpy(&g_upgradeInfoApp, input, sizeof(cmiotUpgradeInfo_t));
            DEMO_PRINT("get cmd ********** CMIOT_CMD_UPGRADE_APP **********\n");
            DEMO_PRINT("url = %s, version = %s, checkSum = %s\n", g_upgradeInfoApp.url, g_upgradeInfoApp.version, g_upgradeInfoApp.checkSum);
            break;

        default:
            DEMO_PRINT("demo_upgrade_callback invalid upgrade command = [%d]\n", cmd);
            return -1;
    }

    return 0;
}

static int hostname_to_ip(const char* hostname)
{
    int ret = 0;

    if (!hostname) {
        printf("invalid params\n");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *res, *res_p;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    hints.ai_protocol = 0;

    ret = getaddrinfo(hostname, NULL, &hints, &res);
    if (ret != 0) {
        printf("getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (res_p = res; res_p != NULL; res_p = res_p->ai_next) {
        char host[1024] = {0};
        ret = getnameinfo(res_p->ai_addr, res_p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);
        if(ret != 0)
            printf("getnameinfo: %s\n", gai_strerror(ret));
        else
            printf("%s -> ip: %s\n", hostname, host);
    }

    freeaddrinfo(res);
    return ret;
}

cmiot_int32_t demo_running_status_callback(cmiotRunningStatus_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{

    switch (cmd)
    {
        case CMIOT_RUNNING_STATUS_BIND_START:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_START **********\n");
			cmiot_uint32_t *type = (cmiot_uint32_t *)input;
			DEMO_PRINT("bind type: %d\n", *type);
			if (1 == *type) //有线配网
			{
			}
			else if(2 == *type) //无线配网
			{
				memset(g_qrcodeData[0], 0, sizeof(g_qrcodeData[0]));
				memset(g_qrcodeData[1], 0, sizeof(g_qrcodeData[1]));
				//启动二维码扫描
				if( false == g_QrCodeConHandle.GetCreatedStatus() )
				{
//					WifiApModeCreate("CMCC-IPC-1234", "");
//					char ip_addr[16] = {0};
//					while (1)
//					{
//						memset(ip_addr, 0, sizeof(ip_addr));
//						int ret = NetGetLocalIp("wlan0", ip_addr);
//						if ((ret == 0) && (ip_addr[0] != '\0'))
//							break;
//						sleep(1);
//					}
					g_Camera.setMode_ScanQrcode(1);
					g_QrCodeConHandle.Create(QrCodeExtractedCB);
					g_QrCodeConHandle.Start();
				}
			}
			
			g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_SLOW_FLICKER);
			//间隔30 s 循环播报请配置WiFi语音提示
			s_QrcodeExit = 0;		//停止播报
//			CreateDetachedThread((char*)"DoQrcode",DoQrcode, (void *)NULL, true);
			
            break;
        }

        case CMIOT_RUNNING_STATUS_BIND_GET_QRCODE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_GET_QRCODE **********\n");

			memset(output, 0, 128);
			
			if(strlen(g_qrcodeData[0]) == 0 || strlen(g_qrcodeData[1]) == 0)
	            break;

			for (int i = 0; i < 2; i++)
			{
				printf("g_qrcodeData[%d]: %s\b", i, g_qrcodeData[i]);
			}

            static cmiot_bool_t flg = CMIOT_FALSE;
            if(flg)
            {
                memcpy(output, g_qrcodeData[0], sizeof(g_qrcodeData[0]));
                flg = CMIOT_FALSE;
            }
            else
            {
				memcpy(output, g_qrcodeData[1], sizeof(g_qrcodeData[1]));
                flg = CMIOT_TRUE;
            }
			printf("output: %s\b", (char *)output);
            
            break;
        }

        case CMIOT_RUNNING_STATUS_BIND_CONNECT_WIFI:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_CONNECT_WIFI **********\n");
            cmiotModifyWifiReqInfo_t *wifiInfo = (cmiotModifyWifiReqInfo_t *)input;
            DEMO_PRINT("get wifi ssid: %s, key: %s, encryption: %s\n", wifiInfo->ssid, wifiInfo->key, wifiInfo->encryption);

			s_QrcodeExit = 1;
				
			CAudioPrompt::AudioFileParm audioFile;
			audioFile.strFileName = AUDIO_FILE_QRCODE_GET_COMPLETE; 
			audioFile.type = 0;
			g_AudioPrompt.aoPlay(audioFile);

			if( true == g_QrCodeConHandle.GetCreatedStatus() )
			{
				g_QrCodeConHandle.Stop();
				g_QrCodeConHandle.Destory();		//退出二维码配网模式
				WifiApModeDestroy();
			}

			// 启动网络管理模块
			g_NetConfigHook.SetWifi(wifiInfo->ssid, wifiInfo->key);
			g_NetConfigHook.SetWifiSwitch(true);
			g_NetConfigHook.Init();

			int wait_count = 0;
			while(NET_WORK_MODE_NONE == g_NetConfigHook.GetNetWorkLindMode() && wait_count < 120)
			{
				wait_count++;
				sleep(1);
			}
			if (NET_WORK_MODE_NONE == g_NetConfigHook.GetNetWorkLindMode())
			{
				printf("wifi connect failed.\n");
				return -1;
			}

			printf("wifi connected.\n");
//			int ret = res_init();
//			printf("res_init() return %d\n", ret);
			hostname_to_ip("www.baidu.com");
			hostname_to_ip("openapi.videiot.cn");

			break;
        }

        case CMIOT_RUNNING_STATUS_BIND_CONNECT_AEC_WIFI:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_CONNECT_AEC_WIFI **********\n");
            cmiotModifyWifiReqInfo_t *aecWifiInfo = (cmiotModifyWifiReqInfo_t *)input;
            DEMO_PRINT("get aec wifi ssid: %s, key: %s, encryption: %s\n", aecWifiInfo->ssid, aecWifiInfo->key, aecWifiInfo->encryption);
//			snprintf((char *)output, 32, ip_addr);
//            if(g_aecBindMode)
//                break;
//            else
                return -1;
			break;
        }

        case CMIOT_RUNNING_STATUS_BIND_SUCCESS:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_SUCCESS **********\n");

			//保存WiFi信息
			g_NetConfigHook.SaveWifi();
			
			//获取WiFi配置
			Json::Value WifiTable;
			NetWifiConfig config;
			g_configManager.getConfig(getConfigName(CFG_WIFI), WifiTable);
			TExchangeAL<NetWifiConfig>::getConfig(WifiTable, config);

			config.bBind = true;
			TExchangeAL<NetWifiConfig>::setConfig(config, WifiTable);
			printJsonValue(WifiTable);
			g_configManager.setConfig(getConfigName(CFG_WIFI), WifiTable, 0, IConfigManager::applyOK);

            /* 绑定成功后，上报SD卡状态以录制本地存储 */
//#ifdef FEATURE_SDCARD
//            cmiotSdCardInfo_t sdInfo = {CMIOT_SD_CARD_STATUS_READY, "./sdCard"};
//            cmiot_update_device_config(CMIOT_UPDATE_CFG_SD_STATUS, (cmiot_char_t *)&sdInfo, sizeof(sdInfo));
//            demo_warp_cmiot_update_device_config_send();
//#endif
            /* 推流 */
//            demo_set_media_thread();
            break;
        }

        case CMIOT_RUNNING_STATUS_SERVER_ONLINE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_SERVER_ONLINE **********\n");
			g_cmiot_online = 1;
            /* 网络恢复，记录日志并触发异常上报(若之前处于离线状态) */
            dev_log_write(DEV_LOG_INFO, DEV_EVENT_NETWORK, "network reconnected");
            if (g_devOnlineStatus == CMIOT_FALSE) {
                /* 从离线恢复: 上报离线期间产生的日志 */
                time_t now = time(NULL);
                dev_log_trigger_exception_upload(DEV_EXCEPTION_NETWORK, 0, (cmiot_uint32_t)now);
            }
            g_devOnlineStatus = CMIOT_TRUE;
            /* 设备上线后，合并上报配置 */
			Json::Value table;
			NetWifiConfig wifiConfig;
			g_configManager.getConfig(getConfigName(CFG_WIFI), table);
			TExchangeAL<NetWifiConfig>::getConfig(table, wifiConfig);
			char wifi_ssid[128] = {0};
			snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", wifiConfig.strSSID.c_str());
            cmiot_update_device_config(CMIOT_UPDATE_CFG_WIFI_SSID, wifi_ssid, strlen(wifi_ssid));
            cmiot_int32_t tmp = 1;
			
			table.clear();
			CameraParamAll cpa;
			memset(&cpa, 0, sizeof(cpa));
			g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
			TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
			if (cpa.vCameraParamAll[0].nightVisionSwitch)
				tmp = 1;
			else
				tmp = 0;
            cmiot_update_device_config(CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION, (cmiot_char_t *)&tmp, sizeof(tmp));
			if (DOUBLE_IRMODE_IR == cpa.vCameraParamAll[0].nightVisionMode)
				tmp = 1;
			else if (DOUBLE_IRMODE_FULLCOLOR == cpa.vCameraParamAll[0].nightVisionMode)
				tmp = 2;
			else
				tmp = 3;
            cmiot_update_device_config(CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION_MODE, (cmiot_char_t *)&tmp, sizeof(tmp));
			
			printf("cmiot_update_device_config(CMIOT_UPDATE_CFG_CODED_FORMAT, %d)\n", g_curVideoFormat);
            cmiot_update_device_config(CMIOT_UPDATE_CFG_CODED_FORMAT, (cmiot_char_t *)&g_curVideoFormat, sizeof(int));
            tmp = 1;
            cmiot_update_device_config(CMIOT_UPDATE_CFG_AUDIO_CODEC_FORMAT, (cmiot_char_t *)&tmp, sizeof(tmp));
            tmp = 100;
            cmiot_update_device_config(CMIOT_UPDATE_CFG_LIGHT_INTENSITY, (cmiot_char_t *)&tmp, sizeof(tmp));
            demo_warp_cmiot_update_device_config_send();

            if(g_devType == CMIOT_DEV_TYPE_LOWPOWER_CAM || g_devType == CMIOT_DEV_TYPE_DOORBELL)
            {
                cmiot_get_lowpower_keepalive(); 
            }
            break;
        }

        case CMIOT_RUNNING_STATUS_SERVER_OFFLINE:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_SERVER_OFFLINE **********\n");
            dev_log_write(DEV_LOG_INFO, DEV_EVENT_NETWORK, "network disconnected");
            g_devOnlineStatus = CMIOT_FALSE;
            break;
        }
        
        case CMIOT_RUNNING_STATUS_UNBIND:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_UNBIND **********\n");
            /* 停止推流 */
//            demo_set_media_thread_stop();
			SystemReset();
            break;
        }

        case CMIOT_RUNNING_STATUS_BIND_FAILED:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_FAILED **********\n");
            break;
        }

        case CMIOT_RUNNING_STATUS_BIND_STOP:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_BIND_STOP **********\n");
            break;
        }

        case CMIOT_RUNNING_STATUS_NOTIFY_ZERO_CONFIG:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_NOTIFY_ZERO_CONFIG **********\n");
            cmiotZeroCfgInfo_t *info = (cmiotZeroCfgInfo_t *)input;
            DEMO_PRINT("registerProtocolType: %s\nSIPServiceId: %s\nSIPServiceDomain: %s\nSIPIPAddress: %s\nSIPIPPort: %u\n\
                SIPProtocol: %s\ndeviceRegisterPassword: %s\ndeviceCode: %s\nbps: %u\n", info->registerProtocolType, info->SIPServiceId,info->SIPServiceDomain, \
                info->SIPIPAddress, info->SIPIPPort, info->SIPProtocol, info->deviceRegisterPassword,info->deviceCode, info->bps);
            /* 根据零配置信息，连接相应平台，如后续不使用千里眼私有协议接入，则调用cmiot_sdk_deinit释放资源（请勿在回调中调用） */
            break;
        }

        case CMIOT_RUNNING_STATUS_NOTIFY_1400_CONFIG:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_NOTIFY_1400_CONFIG **********\n");
            cmiot1400CfgInfo_t *info1400 = (cmiot1400CfgInfo_t *)input;
            DEMO_PRINT("\tGAT1400IPAddress: %s\n\tGAT1400IPPort: %u\n\tGAT1400Id: %s\n\tGAT1400RegisterPassword: %s\n\tGAT1400RegisterAccount: %s\n\tGAT1400RegisterChannelCode: %s\n\tGAT1400KeepaliveCycle: %u\n\tGAT1400MaxKeepaliveTimeoutNum: %u\n", \
                info1400->GAT1400IPAddress, info1400->GAT1400IPPort, info1400->GAT1400Id, info1400->GAT1400RegisterPassword, \
                info1400->GAT1400RegisterAccount, info1400->GAT1400RegisterChannelCode, info1400->GAT1400KeepaliveCycle, info1400->GAT1400MaxKeepaliveTimeoutNum);
            if(strlen(info1400->GAT1400IPV6Address) && info1400->GAT1400IPV6Port)
            {
                DEMO_PRINT("\tGAT1400IPV6Address: %s\n\tGAT1400IPV6Port: %u\n", info1400->GAT1400IPV6Address, info1400->GAT1400IPV6Port);
            }
            /* 根据GAT1400配置信息，连接GAT1400平台，如后续不使用千里眼私有协议接入，则调用cmiot_sdk_deinit释放资源（请勿在回调中调用） */
            /* GAT1400IPV6Address和GAT1400IPV6Port的值有效时，设备需要优先使用其提供的IPV6地址/域名和端口连接GAT1400平台，无法连接时再使用IPV4地址 */
            /* 若GAT1400平台被配置为单栈IPV6，GAT1400IPAddress 也将返回IPV6地址 */
            /* 若GAT1400平台被配置为域名，GAT1400IPAddress 和 GAT1400IPV6Address 都返回域名，设备需自行解析地址，并在IPV6网络可用时优先使用V6连接 */
            break;
        }

        case CMIOT_RUNNING_STATUS_LOWPOWER_SLEEP:
        {
            DEMO_PRINT("get cmd ********** CMIOT_RUNNING_STATUS_LOWPOWER_SLEEP **********\n");
            if(!g_handleSleepTid)
            {
                cmiot_int32_t ret = 0;
                if((ret = pthread_create(&g_handleSleepTid, NULL, handle_sleep, NULL)))
                {
                    DEMO_PRINT("create handle_sleep failed, ret = %d!\n", ret);
                }
            }
            break;
        }

        default:
            DEMO_PRINT("demo_running_status_callback invalid command = [%d]\n", cmd);
            return -1;
    }

    return 0;
}

cmiot_int32_t demo_dev_ai_config_callback(cmiotDevAIConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output)
{
    switch (cmd)
    {
        case CMIOT_CMD_SET_AI_FACE_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FACE_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_VEHICLE_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_VEHICLE_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_BASIC_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_BASIC_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_CROWDED_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_CROWDED_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_PASSFLOW_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_PASSFLOW_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_KITCHEN_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_KITCHEN_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_ELEVATOR_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_ELEVATOR_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_FALLINGOBJ_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_FALLINGOBJ_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_SMOKEFIRE_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_SMOKEFIRE_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_CITYOFFICER_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_CITYOFFICER_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_WORKSITE_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_WORKSITE_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_GASSTATION_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_GASSTATION_SCENE_CFG **********\n");
            goto aiParse;
        case CMIOT_CMD_SET_AI_SAFEDRIVING_SCENE_CFG:
            DEMO_PRINT("get cmd ********** CMIOT_CMD_SET_AI_SAFEDRIVING_SCENE_CFG **********\n");
            goto aiParse;
aiParse:
        {
            cmiotAIConfigParam_t *info = (cmiotAIConfigParam_t *)input;
            DEMO_PRINT("[type=%u]\n", info->aiAlgoType);

            DEMO_PRINT("[common]\n");
            if(info->statusExist)
                DEMO_PRINT("\tstatus=%d\n", info->status);
            if(info->sensitivityExist)
                DEMO_PRINT("\tsensitivity=%u\n", info->sensitivity);
            if(info->regionExist)
                DEMO_PRINT("\tregion=[(%u, %u) (%u, %u) (%u, %u) (%u, %u)]\n", info->region.point[0].x, info->region.point[0].y, info->region.point[1].x, info->region.point[1].y, info->region.point[2].x, info->region.point[2].y, info->region.point[3].x, info->region.point[3].y);
            if(info->audioExist)
            {
                DEMO_PRINT("\taudio");
                if(info->audio.flag & 1)
                    printf(" status=%d", info->audio.status);
                if(info->audio.flag & 2)
                    printf(" times=%u", info->audio.times);
                if(info->audio.flag & 4)
                    printf(" id=%s", info->audio.id);
                if(info->audio.flag & 8)
                    printf(" url=%s", info->audio.url);
                if(info->audio.flag & 16)
                    printf(" schedule=[ repeat[%u],start[%s],end[%s] ]", info->audio.schedule.repeat, info->audio.schedule.startTime, info->audio.schedule.endTime);
                printf("\n");
            }
                
            if(info->lightExist)
            {
                DEMO_PRINT("\tlight");
                if(info->light.flag & 1)
                    printf(" status=%d", info->light.status);
                if(info->light.flag & 2)
                    printf(" times=%u", info->light.time);
                if(info->light.flag & 4)
                    printf(" mode=%u", info->light.mode);
                if(info->light.flag & 8)
                    printf(" schedule=[ repeat[%u],start[%s],end[%s] ]", info->light.schedule.repeat, info->light.schedule.startTime, info->light.schedule.endTime);
                printf("\n");
            }

            if(info->countCycleExist)
                DEMO_PRINT("\tcountCycle=%u\n", info->countCycle);

            if(info->specificExist)
            {
                DEMO_PRINT("[specific]\n");
                DEMO_PRINT("\tlen=%u\n", info->specificLen);
                DEMO_PRINT("\tjson str=%s\n", info->specificStr);
            }
            
            break;
        }

        default:
            break;
    }

    return CMIOT_RETURN_CODE_SUCCESS;
}
