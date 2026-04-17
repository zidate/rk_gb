#include "Media/Camera.h"
#include "PAL/Camera.h"
#include "PAL/Capture.h"
#include "Manager/ConfigManager.h"

#include "../Manager/ManagerDefaultConfig.h"
#include "ExchangeAL/ExchangeKind.h"
#include "ExchangeAL/ManagerExchange.h"
#include "PAL/Misc.h"
#include "Common.h"
#include "Log/DebugDef.h"
#include "CameraFilter.h"

/**
问题：
	客户现成测试，发现如下问题，客户的led灯打开，设备灯打开，图像有闪烁

测试发现如下： 
	室内：设备灯打开到最亮，客户寄过来的测试的灯打开，房间灯打开的情况下，开启弱抗，亮度值60，开强抗 亮度值75
	室外：有少许太阳情况下，对准窗外，开启弱抗，亮度值60左右；开启强抗后过曝，对准天空亮度值高达240，对准树木操场 150左右

总结如下：
	室内开启强抗可以解决掉闪烁问题，开启弱抗在设备灯打开且客户寄过来的测试的灯也开的情况下弱抗也会有图像闪烁现象，在室内开启强抗比弱抗整体图像亮度提升一些，图像质量尚可
	但是到了室外强抗模式下图像严重过曝。
前提：
	1，客户测试的时候，一般是 在室内上电测试，到室外测试的时候是要断电再上电，产品安装场景基本是在室外。
	2，通过获取到的环境亮度值，在室外开启强抗后能区分是过曝状态
*/
static int flicker_run = 1;
float g_start_meanLuma = 0.0f;
static float flicker_get_meanluma()
{
	float meanLuma = 0.0f;
	int ret = -1;
	ret = CaptureGetMeanLuma(&meanLuma);
	if (ret)
		return  0.0f;
	
	return meanLuma;
}
static void *flicker_time_func(void *arg)
{
	//初始化为弱抗进入状态0。
	//状态0：强开强抗，间隔判断，如检测到图像过曝立刻强开弱抗进入过曝后强制弱抗状态2；如检测到图像没有过曝，进入状态1。
	//状态1：保持强抗，间隔判断，如检测到图像过曝立刻强开弱抗进入过曝后强制弱抗状态2。
	//过曝后强制弱抗情况状态2：如0-》2 30内检测到亮度突变进入0； 如1-》2 5秒内检测到亮度突变进入0。

	int flicker_status = 0; //0-弱抗 1-强抗 2-过曝后强制弱抗后情况状态
	int check_num = 6; //连续检测次数
	int initial_exposure_environment = 0; //0-初始化强抗曝光正常 0-初始化强抗曝光过曝
	int flicker_strength_count = 0;
	int flicker_weakness_count = 0;
	int flicker_weakness_count_1 = 0;

	printf("init set weakness flicker\n");
	CaptureSetAntiFlicker(1);//50hz 弱
	sleep(1);

    while(flicker_run)
    {
		if (flicker_status == 0)
		{
			flicker_strength_count = 0;
			CaptureSetAntiFlicker(2);//50hz 强

			if (flicker_get_meanluma() >= 150.0f)
			{
				flicker_weakness_count_1 = 0;
				flicker_weakness_count ++;

				//连续检测到图像过曝
				if (flicker_weakness_count >= check_num)
				{
					printf("set weakness flicker break\n");
					initial_exposure_environment = 1;
					CaptureSetAntiFlicker(1);//50hz 弱
					flicker_status = 2;
				}
			}
			else
			{
				flicker_weakness_count = 0;
				flicker_weakness_count_1 ++;

				//连续检测到图像没有过曝
				if (flicker_weakness_count_1 >= check_num)
				{
					printf("set strength flicker\n");
					flicker_status = 1;
				}
			}
		}
		else if (flicker_status == 1)
		{
			flicker_weakness_count_1 = 0;
			flicker_weakness_count = 0;

			if ( flicker_get_meanluma() >= 150.0f)
			{
				flicker_strength_count ++;

				//连续检测到图像过曝
				if (flicker_strength_count >= check_num)
				{
					printf("set weakness flicker\n");
					initial_exposure_environment = 0;
					CaptureSetAntiFlicker(1);//50hz 弱
					flicker_status = 2;
				}
			}
			else
			{
				flicker_strength_count = 0;
			}
		}
		else if (flicker_status == 2) //过曝后强制弱抗后情况处理
		{
#if 1
			flicker_run = 0;
			break; //退出检测
#else
			if ( false == g_TuyaHandle.IsSyncUTCTime())
			{
				sleep(1);
				continue;
			}
			float current_meanluma = 0.0f;
			time_t current_time = 0;

			static time_t start_time = 0;
			static float start_meanluma = 0;
			if (start_time == 0)
			{
				sleep(6);//环境稳定后
				start_meanluma = flicker_get_meanluma();// 记录开始亮度值
				g_start_meanLuma = start_meanluma;
				start_time = time(NULL); // 记录开始时间
			}
			current_meanluma = flicker_get_meanluma();  // 获取当前亮度值
			current_time = time(NULL);  // 获取当前时间
			double elapsed_time = difftime(current_time, start_time);  // 计算已经过去的时间
			printf("current_time=%d,start_time=%d,elapsed_time=%f\n",current_time,start_time,elapsed_time);
			int time_period = 30;  // 时间段（秒）
			if (0 == initial_exposure_environment)
			{
				time_period = 5;
			}

			if (elapsed_time >= time_period)
			{
				if (fabs(current_meanluma - start_meanluma) > 10.0f)
				{
					check_num = 3;
					initial_exposure_environment = 0;
					flicker_strength_count = 0;
					flicker_weakness_count = 0;
					flicker_weakness_count_1 = 0;
					flicker_status = 0;
					AppErr("meanluma change detected\n");
				}
				else
				{
					AppErr("meanluma change not detected\n");
				}
				start_time = 0;
				start_meanluma = 0;
			}
#endif
		}

		sleep(1);
    }
	printf("quit flicker\n");
    pthread_exit(0);
}

//---------------------------------------------------------------

extern int DeviceMode_g; 		//0:正常模式， 1:测试模式
//extern int allowMotionDetTIme_g; 		//允许报警的时间

//extern VOID IPC_APP_report_anbao_light_status(BOOL_T status);


CCamera* CCamera::instance()
{
	static CCamera* _instance = NULL;

	if(!_instance)
	{
		_instance = new CCamera;
	}
	return _instance;
}

CCamera::CCamera() : CThread("Camera", TP_PLAY), m_ircutCfgTimer("IRCutCfg")
{
	m_bSingleLightMode = false;//双光
	m_bScanQrcodeMode = false;
	m_last_mode = -1;
	m_avg_cds_value = -1;
	doIrcutCtrl(false);
	doIrLedCtrl(false);
	doWhiteLedCtrlSwitch(false);
}

CCamera::~CCamera()
{

}

void CCamera::SetLightMode(bool bSingleLightMode)
{
	m_bSingleLightMode = bSingleLightMode;
}

bool CCamera::start()
{
	m_last_mode = -1;

	//获取配置信息 add on 2025.01.15 添加注释
	CConfigTable CameraParamTable;
	memset(&m_configAll, 0, sizeof(m_configAll));
	g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), CameraParamTable);
	TExchangeAL<CameraParamAll>::getConfig(CameraParamTable, m_configAll);
	g_configManager.attach(getConfigName(CFG_CAMERA_PARAM), IConfigManager::Proc(&CCamera::onConfigCamera, this));

	CConfigTable FlightTable;
	g_configManager.getConfig(getConfigName(CFG_FLIGHT_WARN), FlightTable);
    TExchangeAL<FlightWarnConfig>::getConfig(FlightTable, m_CfgFlightWarn);
	printf("m_CfgFlightWarn.bManualEnable = %d\n", m_CfgFlightWarn.bManualEnable);
	printf("m_CfgFlightWarn.bEnable = %d\n", m_CfgFlightWarn.bEnable);
    printf("m_CfgFlightWarn.iDuration = %d\n", m_CfgFlightWarn.iDuration);
    printf("m_CfgFlightWarn.iLuminance = %d\n", m_CfgFlightWarn.iLuminance);
	printf("m_CfgFlightWarn.iLuminance_yellow = %d\n", m_CfgFlightWarn.iLuminance_yellow);
	g_configManager.attach(getConfigName(CFG_FLIGHT_WARN), IConfigManager::Proc(&CCamera::onConfigFlightWarn, this));

	//----------------------------------
//	g_configManager.GetConfig(m_stCameraParam);
//    g_configManager.GetConfig(m_CfgLightWarn);
//	
//	AppInfo("m_CfgLightWarn.bManualEnable = %d\n", m_CfgLightWarn.bManualEnable);
//	AppInfo("m_CfgLightWarn.bEnable = %d\n", m_CfgLightWarn.bEnable);
//    AppInfo("m_CfgLightWarn.iDuration = %d\n", m_CfgLightWarn.iDuration);
//    AppInfo("m_CfgLightWarn.iLuminance = %d\n", m_CfgLightWarn.iLuminance);
//	AppInfo("m_CfgLightWarn.iLuminance_yellow = %d\n", m_CfgLightWarn.iLuminance_yellow);
//
//	g_configManager.Attach(CFG_CAMERA_PARAM, CConfigManager::Proc(&CCamera::onConfigCamera, this));
//	g_configManager.Attach(CFG_LIGHT_WARN, CConfigManager::Proc(&CCamera::onConfigLightWarn, this));
	//----------------------------------


	//画面翻转初始化
	#if 0
	if(1 == ProductCof_g.image_flip)
	{
		if (RA_NONE == m_configAll.vCameraParamAll[0].rotateAttr)
		{
			CaptureSetRotate(RA_180);
		}
		else
		{
			CaptureSetRotate(RA_NONE);
		}
	}
	else
	{
		CaptureSetRotate(m_configAll.vCameraParamAll[0].rotateAttr);
	}
	#else
	doMirrorFlipCtrl(m_configAll.vCameraParamAll[0].mirror, m_configAll.vCameraParamAll[0].flip);
	#endif

	//宽动态初始化
//	CaptureSetWdr(m_stCameraParam.wdrSwitch);

	//防闪烁
	CaptureSetAntiFlicker(m_configAll.vCameraParamAll[0].iAntiFlicker);//双目枪用这个 change on 2025.01.15
	// CreateDetachedThread((char*)"flicker_time_func",flicker_time_func, (void *)NULL, true);//励国有对应的需求才需要开启

	//时间水印初始化
//	CaptureSetOSDSwitch(m_stCameraParam.osdSwitch);
	CaptureSetOSDSwitch(m_configAll.vCameraParamAll[0].osdSwitch);

	//白光灯亮度
//	doWhiteLedCtrlBrightness(m_CfgLightWarn.iLuminance);

	m_eDayNightStatus = CAMERA_MODE_NONE;	//默认未初始化状态
	m_eNewDayNightStatus = CAMERA_MODE_NONE;
	m_eSenJudgeDayNight = CAMERA_MODE_NONE;
	m_iSmartIrLastValue = -1;
	m_iSmartIrSwitchTime = 0;
	m_iAllowGetDayNightStatusTime = 0;
	m_iForceNightStatusEndTime = 0;
	m_bAutoDayNight = true; 	//默认自动切换
	m_bManualOpenWhiteLed = false; 	//默认没开白光灯
	m_iManualOpenWhiteLedWorktime = 0;
	m_u64AutoOpenLightTime_ms = 0;
	m_bAlarmTrigger = false;
	m_bAlarmTurnOnWihte = false;
	m_iAllowbAlarmTriggerTime = 0;
	m_bCheckDayNightStartTiming = false;
	m_iCheckAntiFlickerMode = 0;
	m_iCurrAntiFlickerMode = 0;
	
	if( 1 == DeviceMode_g  ) 		//测试模式
	{
		if(1 == ProductCof_g.manual_ircut) 	//手动切换模式
		{
			//默认白天模式
			DoDayNightMode(SINGLE_IRMODE_CLOSE);
		}
		else
		{
			//测试模式下，非手动切换，则默认自动切换
			m_iGetLvxCount = 0;
			m_sensorJudgeDayNightTimer.Start(CTimer::Proc(&CCamera::onSensorJudgeDayNight, this), 0, 500);
			m_ircutCfgTimer.Start(CTimer::Proc(&CCamera::onConfigAdcIRCut, this), 0, 2*1000);
			if( !m_bSingleLightMode ) //双光
			{
//				m_stCameraParam.nightVisionMode = DOUBLE_IRMODE_IR; //双光，测试模式下需要按什么模式运行改这里
				m_configAll.vCameraParamAll[0].nightVisionMode = SINGLE_IRMODE_AUTO; //励国 双光，测试模式下需要按什么模式运行改这里
				CreateThread();
			}
		}
	}
	else 						//正常启动
	{
		if( m_bSingleLightMode ) //单光，需要设置初始模式
		{
			DoDayNightMode(m_configAll.vCameraParamAll[0].nightVisionMode);
		}
		m_iGetLvxCount = 0;
		m_cdsJudgeDayNightTimer.Start(CTimer::Proc(&CCamera::onCdsJudgeDayNight, this), 0, 100);
		m_sensorJudgeDayNightTimer.Start(CTimer::Proc(&CCamera::onSensorJudgeDayNight, this), 0, 500);//软光敏判断白天黑夜的状态 add on  2025.01.15 添加注释
		m_ircutCfgTimer.Start(CTimer::Proc(&CCamera::onConfigAdcIRCut, this), 0, 2*1000);
		if( !m_bSingleLightMode ) //双光
		{
			//恢复手动控灯的状态
			if( m_CfgFlightWarn.bManualEnable )
				SetWhiteLed(true);
			
			CreateThread();
		}
	}

	return true;
}

bool CCamera::stop()
{
	DestroyThread(true);
	m_sensorJudgeDayNightTimer.Stop();
    m_ircutCfgTimer.Stop();
//	g_configManager.Detach(CFG_CAMERA_PARAM, CConfigManager::Proc(&CCamera::onConfigCamera, this));
//	g_configManager.Detach(CFG_LIGHT_WARN, CConfigManager::Proc(&CCamera::onConfigLightWarn, this));
	g_configManager.detach(getConfigName(CFG_CAMERA_PARAM), IConfigManager::Proc(&CCamera::onConfigCamera, this));
	g_configManager.detach(getConfigName(CFG_FLIGHT_WARN), IConfigManager::Proc(&CCamera::onConfigFlightWarn, this));
	return true;
}

// 功能:		对摄像头的各种参数进行设置
// return:		-1: 失败, 0: 成功
// pNewConfig:	指向新配置的结构体指针，默认值为NULL
int CCamera::configure(int chn, CameraParam* pNewConfig)
{
	if( (chn != 0) || (!pNewConfig) ) //暂时只支持设置第一通道
	{
		return -1;
	}
	int ret = 0;

//	if(m_configAll.vCameraParamAll[0].iAntiFlicker != pNewConfig->iAntiFlicker)
//	{		
//		CaptureSetAntiFlicker(pNewConfig->iAntiFlicker);
//		
//		m_configAll.vCameraParamAll[0].iAntiFlicker = pNewConfig->iAntiFlicker;
//	}

	#if 0
	if(m_configAll.vCameraParamAll[0].rotateAttr != pNewConfig->rotateAttr)
	{
//		allowMotionDetTIme_g = GetSystemUptime_s() + 10;
		g_Alarm.SetAllowMotionDetTime(time(NULL)+10);
        if(1 == ProductCof_g.image_flip)
        {
            if (RA_NONE == pNewConfig->rotateAttr)
            {
                CaptureSetRotate(RA_180);
            }
            else
            {
                CaptureSetRotate(RA_NONE);
            }
        }
        else
        {
            CaptureSetRotate(pNewConfig->rotateAttr);
        }
	}
	#else
	if(m_configAll.vCameraParamAll[0].mirror != pNewConfig->mirror || 
		m_configAll.vCameraParamAll[0].flip != pNewConfig->flip)
	{
		g_Alarm.SetAllowMotionDetTime(time(NULL)+10);
		doMirrorFlipCtrl(pNewConfig->mirror, pNewConfig->flip);
	}
	#endif
	
	if(m_configAll.vCameraParamAll[0].osdSwitch != pNewConfig->osdSwitch)
	{
		CaptureSetOSDSwitch(pNewConfig->osdSwitch);
	}


	if(m_configAll.vCameraParamAll[0].nightVisionMode != pNewConfig->nightVisionMode)
	{		
		m_bCheckDayNightStartTiming = false;
		m_iAllowbAlarmTriggerTime = GetSystemUptime_s() + 5; 	//切换模式，5秒后再做报警联动
		if( m_bSingleLightMode ) //单光
		{
			DoDayNightMode(pNewConfig->nightVisionMode);
		}
		else //双光
		{
			CGuard guard(m_mutex);				//为了避免m_eDayNightStatus的值被修改，导致出现“手动切换不生效，第二次切换连续切换两次的情况”
			m_iAllowGetDayNightStatusTime = 0; //模式切换，取消不获取日夜状态的操作，强制开启状态判断
			m_iForceNightStatusEndTime = 0;
			m_eDayNightStatus = CAMERA_MODE_NONE;
		}
		m_configAll.vCameraParamAll[0].nightVisionMode = pNewConfig->nightVisionMode;
	}

	return 0;
}

//作  用:摄像头配置修改回调
//参  数:[in]  configAll 多个摄像头的最新参数
//       [out] ret 校验结果
//返回值:无
void CCamera::onConfigCamera(const CConfigTable &configAll, int &ret)
{
	CameraParamAll newParamAll;
	TExchangeAL<CameraParamAll>::getConfig(configAll, newParamAll);

	for(int i = 0; i < 1; i++)
	{
		if(configAll[i].type() != Json::nullValue)
		{
			int nTempRet = configure(i, &newParamAll.vCameraParamAll[i]);
			if (nTempRet < 0)
				ret |= CONFIG_APPLY_VALIT_ERROR;
			else
			{
				if(nTempRet == 1)
					ret |= CONFIG_APPLY_REBOOT;
				memcpy(&m_configAll.vCameraParamAll[i], &newParamAll.vCameraParamAll[i], sizeof(newParamAll.vCameraParamAll[0]));

			}
		}
	}
}

//白光灯参数变化
void CCamera::onConfigFlightWarn(const CConfigTable &table, int &ret)
{
	FlightWarnConfig CfgFlightWarn;
	TExchangeAL<FlightWarnConfig>::getConfig(table, CfgFlightWarn);
    
	if(m_CfgFlightWarn.iLuminance != CfgFlightWarn.iLuminance)//只有亮度变化的时候才进行亮度设置，否则会导致白光灯一直亮，手动关不掉 
	{
		doWhiteLedCtrlBrightness(CfgFlightWarn.iLuminance);
	}
	m_CfgFlightWarn = CfgFlightWarn;
	
}

void CCamera::ThreadProc()
{
	CAMERA_MODE_E eNewDayNightStatus;//检测到的日夜模式

	bool motion_alarm_is_triggerd = false;
	int current_time;
	int last_md_time;
//	int test_time = 0;
	
	bool bLastWhiteLedStatus = m_bCurrWhiteLedStatus; 	//旧的白光灯状态，状态改变时主动上报

	//延时12小时关灯
	bool bStartTime = false; 	//是否开始计时
	int time_finish;
	int cur_time;
	int remain_time;
	
	int night_vision_mode;
	int iAntiFlickerMode;

	prctl(PR_SET_NAME, "Camera_ThreadProc");
	while(m_bLoop)
	{
		m_mutex.Enter();
		iAntiFlickerMode = m_iCheckAntiFlickerMode;
		if( false == m_bManualOpenWhiteLed )//未手动开白光灯
		{
			night_vision_mode = m_configAll.vCameraParamAll[0].nightVisionMode;//获取当前的夜视模式 add on 2025.01.15 添加注释
			bStartTime = false;
//			eNewDayNightStatus = m_eNewDayNightStatus;
			if (GetSystemUptime_s() > m_iAllowGetDayNightStatusTime)//允许获取日夜状态的时间 add on 2025.05.19 
				eNewDayNightStatus = m_eNewDayNightStatus;
			else
				eNewDayNightStatus = m_eDayNightStatus; //m_eNewDayNightStatus 2秒更新一次，防止未更新时切到旧的状态，现象有报警关灯后切黑白了马上又切彩色

			//以下内容相当于双光的智能夜视 2025.01.15 添加注释
			current_time = GetSystemUptime_s();
			if( m_bAlarmTrigger )//报警状态触发
			{
				last_md_time = current_time;	//将当前的时间 记录移动侦测触发的时间 add on 2025.05.21<添加注释>
				if( !motion_alarm_is_triggerd ) 		//移动开始
				{
					motion_alarm_is_triggerd = true;
//					printf("mode[%s] cur[%s] judge[%s]-----md start.\n", test_get_ir_mode(night_vision_mode), 
//						test_get_camera_mode(m_eDayNightStatus), test_get_camera_mode(eNewDayNightStatus));
				}
			}
			else//报警状态触发结束
			{
//				if( motion_alarm_is_triggerd && ((current_time - last_md_time) > m_CfgFlightWarn.iDuration) )
				// if( motion_alarm_is_triggerd && ((current_time - last_md_time) > GetLinkageLightTime()) )//犇哥确认使用旧的逻辑，移动侦测开关关闭之后，就把联动亮灯关闭
				if( motion_alarm_is_triggerd && ((current_time - last_md_time) > GetLinkageLightTime()) ||  (0 == CAlarmMotion::instance()->GetMotionSwitch()) )
				{
					motion_alarm_is_triggerd = false;
				}
			}
			if (current_time < m_iAllowbAlarmTriggerTime)	//切换模式，5秒后再做报警联动
				motion_alarm_is_triggerd = false;

//			if (test_time != current_time && motion_alarm_is_triggerd)
//				printf("mode[%s] cur[%s] judge[%s]-----md trigger.\n", test_get_ir_mode(night_vision_mode), 
//						test_get_camera_mode(m_eDayNightStatus), test_get_camera_mode(eNewDayNightStatus));
//			test_time = current_time;
			// printf("night_vision_mode:%d,eNewDayNightStatus:%d,m_eDayNightStatus---%d,m_eNewDayNightStatus---%d",night_vision_mode,eNewDayNightStatus,m_eDayNightStatus,m_eNewDayNightStatus);
			//夜视模式--------强制白天
			if (SINGLE_IRMODE_CLOSE == night_vision_mode)//关闭夜视——白天 add on 2025.01.15 添加注释
			{
				//如果为CAMERA_MODE_NONE表明刚切换为强制白天的模式 add on 2025.01.15 添加注释
				if( CAMERA_MODE_NONE == m_eDayNightStatus )
				{
					motion_alarm_is_triggerd = false;
					m_bAlarmTurnOnWihte = false;
					//白天模式	
					AppErr("camera mode[%d]. init switch day.\n", night_vision_mode);
//					setMode(CAMERA_MODE_DAY);
					setMode(CAMERA_MODE_NIGHT, false);
					m_eDayNightStatus = CAMERA_MODE_DAY;
				}
			}
			else if (SINGLE_IRMODE_OPEN == night_vision_mode)//开启夜视
			{	
				//如果为CAMERA_MODE_NONE表明刚切换为强制夜视黑白的模式
				if( CAMERA_MODE_NONE == m_eDayNightStatus )
				{
					motion_alarm_is_triggerd = false;
					m_bAlarmTurnOnWihte = false;
					//黑夜模式
					AppErr("camera mode[%d]. init switch night.\n", night_vision_mode);
					setMode(CAMERA_MODE_NIGHT, true);
					m_eDayNightStatus = CAMERA_MODE_NIGHT;//同步当前所处的状态 add on 2025.05.19
				}
			}
			else//自动
			{	
				//先确定当前的日夜状态 add on 2025.05.19 添加注释
				if( CAMERA_MODE_NONE == m_eDayNightStatus )//当前状态未定的情况下，以检测的结果为准//add on 2025.01.15 添加注释
				{
					motion_alarm_is_triggerd = false;
					m_bAlarmTurnOnWihte = false;
					//白天模式	
					if( CAMERA_MODE_DAY == eNewDayNightStatus )
					{
						AppErr("camera mode[%d]. init switch day.\n", night_vision_mode);
						setMode(CAMERA_MODE_DAY);
						m_eDayNightStatus = eNewDayNightStatus;
					}
					//黑夜模式
					else if( CAMERA_MODE_NIGHT == eNewDayNightStatus )
					{
						AppErr("camera mode[%d]. init switch night.\n", night_vision_mode);
						setMode(CAMERA_MODE_NIGHT, true);
						m_eDayNightStatus = eNewDayNightStatus;
					}						
				}


				if( false == motion_alarm_is_triggerd ) 		//没有触发移动侦测
				{
					if( true == m_bAlarmTurnOnWihte )//当前处于联动控灯状态，但是移动侦测结束，恢复到夜视状态add on 2025.01.15 添加注释
					{
						m_bAlarmTurnOnWihte = false;
						m_iAllowGetDayNightStatusTime = GetSystemUptime_s() + 10; //定时忽略灯对白天黑夜状态判断的影响
						AppErr("camera mode[%d]. alarm end. close lights.\n", night_vision_mode);
						setMode(CAMERA_MODE_NIGHT, true);//夜视——红外灯 add on 2025.01.15 添加注释
						m_eDayNightStatus = CAMERA_MODE_NIGHT;
						
						g_Siren.SetSirenStatus(0); //关警笛
					}
					else//根据实际的状态判断是否需要切换状态 add on 2025.01.15 添加注释
					{
						if( CAMERA_MODE_DAY == eNewDayNightStatus )
						{
							//白天模式	
							if( eNewDayNightStatus != m_eDayNightStatus )
							{
								AppErr("camera mode[%d]. alarm end. switch day.\n", night_vision_mode);
								setMode(CAMERA_MODE_DAY);
								m_eDayNightStatus = eNewDayNightStatus;
							}
						}
						else if( CAMERA_MODE_NIGHT == eNewDayNightStatus )
						{
							//黑夜模式
							if( eNewDayNightStatus != m_eDayNightStatus )
							{
								AppErr("camera mode[%d]. alarm end. switch night.\n", night_vision_mode);
								setMode(CAMERA_MODE_NIGHT, true);
								m_eDayNightStatus = eNewDayNightStatus;
							}						
						}
					}
				}
				else											//触发了移动侦测
				{
					if (CAMERA_MODE_DAY == m_eDayNightStatus)
					{
						//白天模式	-> 黑夜
						if (CAMERA_MODE_NIGHT == eNewDayNightStatus)
						{
							AppErr("camera mode[%d]. alarm trigger. switch night.\n", night_vision_mode);
							setMode(CAMERA_MODE_NIGHT, true);//切换模式，10s内不做联动开灯的动作 add on 2025.01.15 添加注释
							m_eDayNightStatus = eNewDayNightStatus;
						}
					}
					else if( CAMERA_MODE_NIGHT == m_eDayNightStatus )
					{
						if( false == m_bAlarmTurnOnWihte )
						{
							m_bAlarmTurnOnWihte = true;//联动开灯 add on 2025.01.15 添加注释
							m_iAllowGetDayNightStatusTime = GetSystemUptime_s() + 10; //定时忽略灯对白天黑夜状态判断的影响
							m_u64AutoOpenLightTime_ms = GetSystemUptime_ms();
							AppErr("camera mode[%d]. alarm trigger. turn on light.\n", night_vision_mode);
							setMode(CAMERA_MODE_NIGHT, false, false);//避免联动时长小于10s时，持续触发报警的状态下无法持续亮灯 add on 2025.05.19<添加注释>
							
							g_Siren.SetSirenStatus(1); //开警笛
						}
					}
				}
			}

			if( bLastWhiteLedStatus != m_bCurrWhiteLedStatus )
			{
//				if( g_TuyaHandle.IsTuyaSdkStarted() )	//双光自动控白光灯需要主动上报状态
//				if (! g_Alarm.MotionDetectLightNotify(m_bCurrWhiteLedStatus))	//双光自动控白光灯需要主动上报状态
				{
					bLastWhiteLedStatus = m_bCurrWhiteLedStatus;
//					IPC_APP_report_anbao_light_status(bLastWhiteLedStatus);
				}
			}
			
		}
		else//手动开灯
		{
			//手动开灯时
			//延时12小时自动关灯
			if((1 == ProductCof_g.auto_light_off) || (m_iManualOpenWhiteLedWorktime > 0))
			{
				if( false == bStartTime )//计算关灯的结束时间点add on 2025.01.15 添加注释
				{
					cur_time = GetSystemUptime_s();
					if (1 == ProductCof_g.auto_light_off)	//超时自动关灯
					{
						remain_time = (ProductCof_g.auto_light_off_time*60*60);
						if (m_iManualOpenWhiteLedWorktime > 0)
							remain_time = remain_time < m_iManualOpenWhiteLedWorktime ? remain_time : m_iManualOpenWhiteLedWorktime;//取较小的时长 add on 2025.01.15 添加注释					
					}
					else									//超时不关灯
					{
						remain_time = m_iManualOpenWhiteLedWorktime;//以dp点下发的时长为准 add on 2025.01.15 添加注释						
					}
					printf("manual open light, duration: %d\n", m_iManualOpenWhiteLedWorktime);
					time_finish = cur_time + remain_time;
					bStartTime = true;
				}
				else					//关灯倒计时add on 2025.01.15 添加注释
				{
					cur_time = GetSystemUptime_s();
					if( cur_time > time_finish )
					{
						m_iAllowGetDayNightStatusTime = GetSystemUptime_s() + 10; //定时忽略灯对白天黑夜状态判断的影响
						m_mutex.Leave();
						printf("manual open light. timeout close.\n");
						SetWhiteLed(false);
						m_mutex.Enter();
//						if( g_TuyaHandle.IsTuyaSdkStarted() )	//双光自动控白光灯需要主动上报状态
//							IPC_APP_report_anbao_light_status(false);
						g_Alarm.MotionDetectLightNotify(false);
						// 关灯后更新配置
						m_CfgFlightWarn.bManualEnable = false;
//						g_configManager.SetConfig(m_CfgLightWarn);
						CConfigTable table;
						TExchangeAL<FlightWarnConfig>::setConfig(m_CfgFlightWarn, table);
						g_configManager.setConfig(getConfigName(CFG_FLIGHT_WARN), table, 0, IConfigManager::applyOK);
					}
				}
			}
		}
		
//		if (iAntiFlickerMode != m_iCurrAntiFlickerMode)
//		{
//			if (2 == iAntiFlickerMode)
//				CaptureSetBrightness(-20);
//			else
//				CaptureSetBrightness(0);
//			CaptureSetAntiFlicker(iAntiFlickerMode);
//			m_iCurrAntiFlickerMode = iAntiFlickerMode;
//		}
		m_mutex.Leave();
		
		usleep(50000);	//50ms
	}
}

//通过光敏判断白天黑夜状态
CAMERA_MODE_E CCamera::CheckDayNightByCds(int cdsValue, CAMERA_MODE_E oldStatus)
{
//	printf("cdsValue: %d, oldStatus: %d\n", cdsValue, oldStatus);
//	printf("lamp_board: %d, lamp_board_value: %d, lamp_board_value_method: %d\n", ProductCof_g.lamp_board, ProductCof_g.lamp_board_value, ProductCof_g.lamp_board_value_method);

	if (-1 == cdsValue) //未完成首次采集统计
		return oldStatus;

	if (ProductCof_g.lamp_board == 0)
	{
		if( CAMERA_MODE_NONE == oldStatus )
		{
			if (cdsValue < ProductCof_g.lamp_board_value)			//黑夜模式
			{
				return CAMERA_MODE_NIGHT;
			}
			else						//白天模式	
			{
				return CAMERA_MODE_DAY;
			}
		}
		else if( CAMERA_MODE_DAY == oldStatus )
		{
			if (cdsValue < ProductCof_g.lamp_board_value-ProductCof_g.lamp_board_value_method)			//黑夜模式
			{
				return CAMERA_MODE_NIGHT;
			}
		}
		else if( CAMERA_MODE_NIGHT == oldStatus )
		{
			if(cdsValue > ProductCof_g.lamp_board_value+ProductCof_g.lamp_board_value_method)	//白天模式	
			{
				return CAMERA_MODE_DAY;
			}
		}
	}
	else
	{
		if( CAMERA_MODE_NONE == oldStatus )
		{
			if (cdsValue > ProductCof_g.lamp_board_value)	//黑夜模式
			{
				return CAMERA_MODE_NIGHT;
			}
			else				//白天模式	
			{
				return CAMERA_MODE_DAY;
			}
		}
		else if( CAMERA_MODE_DAY == oldStatus )
		{
			if (cdsValue > ProductCof_g.lamp_board_value+ProductCof_g.lamp_board_value_method)	//黑夜模式
			{
				return CAMERA_MODE_NIGHT;
			}
		}
		else if( CAMERA_MODE_NIGHT == oldStatus )
		{
			if (cdsValue < ProductCof_g.lamp_board_value-ProductCof_g.lamp_board_value_method)	//白天模式	
			{
				return CAMERA_MODE_DAY;
			}
		}
	}	
	return oldStatus;
}

//通过sensor判断白天黑夜状态
CAMERA_MODE_E CCamera::CheckDayNightBySensor()
{	
	return m_eSenJudgeDayNight;
}

//综合判断白天黑夜状态，并做频繁切换检测
CAMERA_MODE_E CCamera::CheckDayNight()
{
	int night_vision_mode = m_configAll.vCameraParamAll[0].nightVisionMode;
	CAMERA_MODE_E cur_day_night_status = CAMERA_MODE_NONE;
	static CAMERA_MODE_E last_day_night_status = CAMERA_MODE_NONE; 	//记录上一次的白天黑夜状态，全彩模式下用
//	static bool bStartTiming = false; 	//开始计时
	static int begin_time = 0; 		//开始时间
	static int status_change_count = 0; 	//状态切换计数
	/* 
	 *CHECK_FREQUENT_SWITCH_DURATION和FREQUENT_SWITCH_THRESHOLD可以根据实际需求调整。
	 *目前的逻辑每次切换后强制保持状态10秒，检测白天黑夜状态定时器周期2秒，加上其他操作消耗时间，所以1分钟最多切换5次。
	 *调整这两个常量时设定值按比例不能超过每分钟5次，否则检测不到频繁切换。
	 *当前给定默认值为3分钟6次切换判断为频繁切换。
	*/
	const int CHECK_FREQUENT_SWITCH_DURATION = 180; //检测频繁切换的时长，单位秒
	const int FREQUENT_SWITCH_THRESHOLD = 8; 	//定义为频繁切换的次数。
	const int FORCE_NIGHT_TIME = 30*60; 	//检测到频繁切换后强制成夜晚状态的时长，单位秒。这里暂定半小时
	const int FULLCOLOR_FORCE_NIGHT_TIME = 2*60*60; //双光全彩模式下强制成夜晚状态的时长，单位秒。

	int cur_absolute_time = GetSystemUptime_s();
	//开关白光灯
	if (cur_absolute_time < m_iAllowGetDayNightStatusTime)
	{
		last_day_night_status = m_eDayNightStatus;
		return last_day_night_status;
	}

	//不获取白天黑夜状态
	if (cur_absolute_time < m_iForceNightStatusEndTime)
	{
		static int dbg_last_time = 0;
		if ((cur_absolute_time - dbg_last_time) > 10)
		{
			dbg_last_time = cur_absolute_time;
			AppErr("CheckDayNight ---> cur_absolute_time: %d, m_iAllowGetDayNightStatusTime: %d, m_iForceNightStatusEndTime: %d, last_day_night_status: %d\n", 
					cur_absolute_time, m_iAllowGetDayNightStatusTime, m_iForceNightStatusEndTime, last_day_night_status);
		}
		return last_day_night_status;
	}

	#if 01
//	if (false == m_bSingleLightMode && 
//		DOUBLE_IRMODE_FULLCOLOR == night_vision_mode && 
//		CAMERA_MODE_NIGHT == m_eDayNightStatus)
//	{
//		/* 双光 且 全彩模式下从夜晚切到白天通过sensor判断 */
//		cur_day_night_status = CheckDayNightBySensor();
//	}
//	else
	{
		//可以选择使用硬光敏还是软光敏
		if(ProductCof_g.smartir_en == 0)
		{
//			static int dbg_last_time = 0;
//			int iCdsValue = SystemReadAdc();
//			if ((cur_absolute_time - dbg_last_time) > 10)
//			{
//				dbg_last_time = cur_absolute_time;
//				AppInfo("CheckDayNight ---> iCdsValue: %d\n", iCdsValue);
//			}
			cur_day_night_status = CheckDayNightByCds(m_avg_cds_value, m_eDayNightStatus);
//			printf("cur_day_night_status: %d\n", cur_day_night_status);
		}
		else
		{
			cur_day_night_status = CheckDayNightBySensor();
		}
	}
	#else
	//励国
//	//可以选择使用硬光敏还是软光敏
//	if(ProductCof_g.smartir_en == 0)
//	{
//		static int dbg_last_time = 0;
//		int iCdsValue = SystemReadAdc();
//		if ((cur_absolute_time - dbg_last_time) > 10)
//		{
//			dbg_last_time = cur_absolute_time;
//			AppInfo("CheckDayNight ---> iCdsValue: %d\n", iCdsValue);
//		}
//		cur_day_night_status = CheckDayNightByCds(iCdsValue, m_eDayNightStatus);
//	}
//	else//双目枪以及双目安保灯只有软光敏
	{
		cur_day_night_status = CheckDayNightBySensor();//通过软光敏判断当前的白天黑夜状态 add on 2025.01.15添加注释

	}
	//告知后面的代码，当前处于强制黑夜的状态 add on 2025.01.15添加注释
	if (SINGLE_IRMODE_OPEN == night_vision_mode) //强制开夜视
		cur_day_night_status = CAMERA_MODE_NIGHT;
	#endif

	//手动开白光灯时 或 联动开白光灯 或 使用硬光敏时 或 测试模式下，不做频繁切换判断
	if (m_bManualOpenWhiteLed || m_bAlarmTurnOnWihte || ProductCof_g.smartir_en == 0 || 1 == DeviceMode_g)
	{
		m_bCheckDayNightStartTiming = false;
		last_day_night_status = cur_day_night_status;
		return cur_day_night_status;
	}
	
	/* 如果出现频繁切换的情况，强制为夜视模式FORCE_NIGHT_TIME秒 */
	if (false == m_bCheckDayNightStartTiming)
	{
		m_bCheckDayNightStartTiming = true;
		status_change_count = 0;
		begin_time = GetSystemUptime_s();
		m_iForceNightStatusEndTime = 0;
	}
	if (true == m_bCheckDayNightStartTiming)
	{
		if (last_day_night_status != cur_day_night_status)
		{
			last_day_night_status = cur_day_night_status;
			status_change_count++;
			AppErr("CheckDayNight ---> status_change_count: %d\n", status_change_count);
		}
		if (status_change_count > FREQUENT_SWITCH_THRESHOLD) /* 判断为频繁切换 */
		{
			if (false == m_bSingleLightMode && 
				DOUBLE_IRMODE_FULLCOLOR == night_vision_mode)
				m_iForceNightStatusEndTime = GetSystemUptime_s() + FULLCOLOR_FORCE_NIGHT_TIME; //FULLCOLOR_FORCE_NIGHT_TIME秒不做白天黑夜状态判断
			else
				m_iForceNightStatusEndTime = GetSystemUptime_s() + FORCE_NIGHT_TIME; //FORCE_NIGHT_TIME秒不做白天黑夜状态判断
			//强制黑夜模式
			last_day_night_status = CAMERA_MODE_NIGHT;
	
			m_bCheckDayNightStartTiming = false;
			begin_time = GetSystemUptime_s();
		}
		if (GetSystemUptime_s() > (begin_time + CHECK_FREQUENT_SWITCH_DURATION))
		{
			m_bCheckDayNightStartTiming = false;
		}
	}
	
	//printf("CheckDayNight ---> last_day_night_status: %d\n", last_day_night_status);
	return last_day_night_status;
}

void CCamera::onCdsJudgeDayNight(uint arg)
{
	static int s_count = 0;
	static int value_arr[20];
	
	int iCdsValue = SystemReadAdc();
	if (-1 == m_avg_cds_value)
		m_avg_cds_value = iCdsValue;

	int i;
	for (i = 0; i < s_count; i++)
	{
		if (iCdsValue >= value_arr[i])
			break;
	}
	memmove(&value_arr[i+1], &value_arr[i], (s_count-i)*sizeof(int));
	value_arr[i] = iCdsValue;
	s_count++;

	if (s_count < 20)
		return;

//	printf("cds value: ");
//	for (i = 0; i < s_count; i++)
//		printf("%d ", value_arr[i]);
//	printf("\n");

	// 测试发现开了红外灯后，采样值都是跳高，把最高9个和最低1个去掉再求平均
	int average = 0;
	for (i = 9; i < s_count-1; i++)
		average += value_arr[i];
	average /= 10;
//	printf("average value: %d\n", average);

	s_count = 0;
	m_avg_cds_value = average;
}

void CCamera::onSensorJudgeDayNight(uint arg)
{
//	int value = CaptureGetSmartirResult();
	unsigned int value = 0;
	VideoGetChnLuma(0,&value);

	int curr_time = GetSystemUptime_s();
	
	if (m_iSmartIrLastValue != value)
		m_iSmartIrSwitchTime = curr_time;

	if (curr_time > (m_iSmartIrSwitchTime+3))
	{
		if (value == 0) 			//白天模式
		{
			m_eSenJudgeDayNight = CAMERA_MODE_DAY;
		}
		else if (value == 1)		//黑夜模式
		{
			m_eSenJudgeDayNight = CAMERA_MODE_NIGHT;
		}
	}

	m_iSmartIrLastValue = value;
}

//extern int g_curr_ev;
void CCamera::onConfigAdcIRCut(uint arg)
{
//	int ev = CaptureGetEv();
//	printf("curr ev: %d\n", ev);
//	if (ev >= 0)
//		g_curr_ev = ev;
//	if (0 == m_iCheckAntiFlickerMode)
//	{
//		if (6050 < ev && ev < 6650)
//			m_iCheckAntiFlickerMode = 2;
//		else
//			m_iCheckAntiFlickerMode = 1;
//	}
//	else
//	{
//		if (1 == m_iCheckAntiFlickerMode && 6100 < ev && ev < 6600)
//			m_iCheckAntiFlickerMode = 2;
//		else if (2 == m_iCheckAntiFlickerMode && (ev < 6000 || ev > 6700))
//			m_iCheckAntiFlickerMode = 1;
//	}

	if( m_bSingleLightMode ) //单光
	{
		m_eNewDayNightStatus = CheckDayNight();

		if( true == m_bAutoDayNight ) //自动
		{
			CGuard guard(m_mutex);
			
			if( m_eNewDayNightStatus != m_eDayNightStatus )
			{
				if( CAMERA_MODE_DAY == m_eNewDayNightStatus )
				{
					//白天模式	
					setMode(CAMERA_MODE_DAY);
					m_eDayNightStatus = m_eNewDayNightStatus;
				}
				else if( CAMERA_MODE_NIGHT == m_eNewDayNightStatus )
				{
					//黑夜模式
					setMode(CAMERA_MODE_NIGHT, true);
					m_eDayNightStatus = m_eNewDayNightStatus;
				}
			}
		}
	}
	else //双光
	{
		m_eNewDayNightStatus = CheckDayNight();
	}

//	printf("check day night status ---> %s\n", CAMERA_MODE_DAY == m_eNewDayNightStatus ? "day" : (CAMERA_MODE_NIGHT == m_eNewDayNightStatus?"night":"none"));
}

//bIrLed: 夜视模式下有效 true-夜视开启红外灯,黑白夜视模式  false-夜视开启白光灯,全彩夜视模式
//bFilterMdAlarm: 是否过滤移动侦测报警，报警亮灯时不过滤移动侦测报警，防止误触发报警结束
void CCamera::setMode(CAMERA_MODE_E mode, bool bIrLed/*=true*/, bool bFilterMdAlarm/* = true*/)
{
	int new_mode;
	if( CAMERA_MODE_DAY == mode ) 	//白天模式	
	{
		new_mode = 0;
	}
	else 							//黑夜模式
	{
		if( bIrLed )
		{
			new_mode = 1;
		}
		else
		{
			new_mode = 2;
		}
	}
	if (new_mode == m_last_mode)
		return ;
	m_last_mode = new_mode;

	if (bFilterMdAlarm)
//		allowMotionDetTIme_g = GetSystemUptime_s() + 10;
		g_Alarm.SetAllowMotionDetTime(time(NULL)+10);//调用time(NULL)的方式，是因为Motion_CB用的夜也是这个方式
//	g_PtzHandle.SuspendTrack(10); //人形追踪可以注释

	if( CAMERA_MODE_DAY == mode ) 	//白天模式	
	{
		m_bCurrDayNightStatus = false;
		//关红外灯
		doIrLedCtrl(false);
		//关白光灯
		doWhiteLedCtrlSwitch(false);
		//ircut截止
		doIrcutCtrl(false);
		usleep(200000);
		//isp彩色
		CaptureSetISPMode(0);
		SystemSetColor(0);
		m_iCurrAntiFlickerMode = 0;
	}
	else 							//黑夜模式
	{
		if( bIrLed )
		{
			m_bCurrDayNightStatus = true;
			//isp黑白
			CaptureSetISPMode(1); //2024.12.07注释，参考1.5.0
			SystemSetColor(1);
			m_iCurrAntiFlickerMode = 0;
			usleep(200000);
			//ircut全透
			doIrcutCtrl(true);
			usleep(200000);
			//开红外灯
			doIrLedCtrl(true);
			//关白光灯
			doWhiteLedCtrlSwitch(false);
		}
		else
		{
			m_bCurrDayNightStatus = false;
			//关红外灯
			doIrLedCtrl(false);
			//开白光灯
			doWhiteLedCtrlSwitch(true);
			usleep(200000);
			//ircut截止
			doIrcutCtrl(false);
			usleep(200000);
			//isp彩色
			CaptureSetISPMode(0);
			SystemSetColor(0);
			m_iCurrAntiFlickerMode = 0;
		}
	}
}

void CCamera::setMode_ScanQrcode(bool enable)
{
//	if (enable == m_bScanQrcodeMode)
//		return ;
//	m_bScanQrcodeMode = enable;
//	
//	if (true == enable)
//	{
//		stop();
//		CaptureSetISPMode(1);//黑白
//		//ircut截止
//		doIrcutCtrl(false);
//		//关红外灯
//		doIrLedCtrl(false);
//		//关白光灯
//		doWhiteLedCtrlSwitch(false);
//		
//		CaptureSetBrightness(-120);
//		CaptureSetContrast(150);
//	}
//	else
//	{
//		stop();
//		start();
//	}
	SystemSetInfraredLamp(0);
    SystemSetIrcut(0);
	CaptureSetISPMode(0);
	SystemSetColor(0);
	doWhiteLedCtrlSwitch(false);
}

//获取翻转参数
int CCamera::getRotateAttr()
{
	return m_configAll.vCameraParamAll[0].rotateAttr;
}

//切换夜视模式，单光时使用
int CCamera::DoDayNightMode(int mode)
{
	CGuard guard(m_mutex);

    if (SINGLE_IRMODE_CLOSE == mode)		//彩色
    {
        m_bAutoDayNight = false;
		setMode(CAMERA_MODE_DAY);
		m_eDayNightStatus = CAMERA_MODE_NONE;	//未初始化状态
    }
    else if (SINGLE_IRMODE_OPEN == mode) 	//黑白
    {
        m_bAutoDayNight = false;
		setMode(CAMERA_MODE_NIGHT);
		m_eDayNightStatus = CAMERA_MODE_NONE;	//未初始化状态
    }
    else //if (SINGLE_IRMODE_AUTO == mode) 	//自动
    {
        m_bAutoDayNight = true;
    }

	return 0;
}

CAMERA_MODE_E CCamera::getDayNightStatus()
{
	return m_eDayNightStatus;
}

//APP手动控制白光灯
void CCamera::SetWhiteLed(bool enable, int worktime/* = -1*/)
{
	static CAMERA_MODE_E s_OriginalStatus = CAMERA_MODE_NONE; //记录开灯时的状态，关灯时恢复回去
	if (CAMERA_MODE_NONE == s_OriginalStatus)
		s_OriginalStatus = m_eDayNightStatus;
	
	CGuard guard(m_mutex);

	m_iAllowGetDayNightStatusTime = GetSystemUptime_s() + 10; //定时忽略灯对白天黑夜状态判断的影响
	
	if( enable )
	{
		m_iManualOpenWhiteLedWorktime = worktime;
		m_bManualOpenWhiteLed = true;
		//手动开灯和夜视全彩是一样的
//		//先手动控灯的亮度恢复亮度
//		doWhiteLedCtrlBrightness(m_CfgLightWarn.iLuminance);
		setMode(CAMERA_MODE_NIGHT, false);
		s_OriginalStatus = m_eDayNightStatus;
	}
	else
	{
//		if (false == m_bSingleLightMode && 
//			DOUBLE_IRMODE_FULLCOLOR == m_stCameraParam.nightVisionMode)
//		{
//			//全彩模式下，把等关掉，并且标记当前状态为黑夜，防止又自动开灯
//			setMode(CAMERA_MODE_DAY, false);
//			m_eDayNightStatus = CAMERA_MODE_NIGHT;
//		}
//		else
		{
			//手动关灯就先把灯光了然后让程序根据当前配置和环境去切换
			setMode(s_OriginalStatus, true);
			m_eDayNightStatus = s_OriginalStatus;
		}
		m_bManualOpenWhiteLed = false;
	}
}

bool CCamera::GetDayNightStatus(void)
{
	return m_bCurrDayNightStatus;
}

int CCamera::GetLinkageLightTime(void)
{
	return m_CfgFlightWarn.iDuration;
}

//报警抓图推送时调用，判断是否需要延时抓图
/* 富翰平台上ISP切换黑白和彩色时图像会有短时间颜色偏绿的情况，所以增加此接口在需要时延时抓图 */
bool CCamera::SnapCheckNeedWait(uint64 *pu64AutoOpenLightTime_ms)
{
	bool bNeedWait = false;
	
	if( !m_bSingleLightMode ) //双光
	{
		if (DOUBLE_IRMODE_SMART == m_configAll.vCameraParamAll[0].nightVisionMode && 
			CAMERA_MODE_NIGHT == m_eDayNightStatus)
		{
			bNeedWait = true;
			
			if(true == m_bCurrWhiteLedStatus)
			{
				if( pu64AutoOpenLightTime_ms )
					*pu64AutoOpenLightTime_ms = m_u64AutoOpenLightTime_ms;
			}
			else
			{
				if( pu64AutoOpenLightTime_ms )
					*pu64AutoOpenLightTime_ms = GetSystemUptime_ms();
			}
		}
	}

	return bNeedWait;
}

//外部设置报警状态
void CCamera::SetAlarmTriggerStatus(bool bTrigger)
{
	m_bAlarmTrigger = bTrigger;
//	printf("bTrigger: %d\n", bTrigger);
}

//获取白光灯状态
bool CCamera::GetWhiteLedStatus()
{
	return m_bCurrWhiteLedStatus;
}

/*****************************底层控制接口*****************************/
//控制红外灯
int CCamera::doIrLedCtrl(bool enable)
{
	int value;
	
	if( enable )
		value = 1;
	else
		value = 0;
	//励国双目安保灯的红外灯不需要做成可配的，所以这里将可配逻辑删除
	
	return SystemSetInfraredLamp(value);
}

//控制白光灯
int CCamera::doWhiteLedCtrlSwitch(bool enable)
{
	m_bCurrWhiteLedStatus = enable;

	#if 01
//	return SystemSetIncandescentLampPwm(LAMP_SWITCH, enable);
	if( enable )
	{
		printf("\n====================================light on===============================================\n");
		
		#if defined(RC0240) || defined(RC0240V20) || defined(RC0240V30) || defined(RC0240V40)
		if(m_CfgFlightWarn.iLuminance <= 15 )
		{
			SystemSetIncandescentLampLumi(15,m_CfgFlightWarn.iLuminance_yellow);
			SystemSetIncandescentLampLumi_yellow(m_CfgFlightWarn.iLuminance,1000-m_CfgFlightWarn.iLuminance_yellow);
		}
		else
		{
			SystemSetIncandescentLampLumi(m_CfgFlightWarn.iLuminance,m_CfgFlightWarn.iLuminance_yellow);
			SystemSetIncandescentLampLumi_yellow(m_CfgFlightWarn.iLuminance,1000-m_CfgFlightWarn.iLuminance_yellow);
		}
		#endif
		
		#if defined(RC0240_LGV10)
		if(m_CfgFlightWarn.iLuminance <= 20 )
		{
			SystemSetIncandescentLampLumi(100-20,m_CfgFlightWarn.iLuminance_yellow);
			SystemSetIncandescentLampLumi_yellow(m_CfgFlightWarn.iLuminance,1000-m_CfgFlightWarn.iLuminance_yellow);
		}
		else
		{
			SystemSetIncandescentLampLumi(100-m_CfgFlightWarn.iLuminance,m_CfgFlightWarn.iLuminance_yellow);
			SystemSetIncandescentLampLumi_yellow(m_CfgFlightWarn.iLuminance,1000-m_CfgFlightWarn.iLuminance_yellow);
		}
		#endif
	}
	else
	{
		printf("\n====================================light off===============================================\n");
		#if defined(RC0240) || defined(RC0240V20) || defined(RC0240V30) || defined(RC0240V40)
		SystemSetIncandescentLampLumi(0,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(0,1000-m_CfgFlightWarn.iLuminance_yellow);
		#endif

		#if defined(RC0240_LGV10)
		SystemSetIncandescentLampLumi(100,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(100,1000-m_CfgFlightWarn.iLuminance_yellow);
		#endif
	}

	#else
	int value = enable;
	if (ProductCof_g.light_ctrl)
		value = !value;
	return SystemSetIncandescentLamp(value);
	#endif
}

int CCamera::doWhiteLedCtrlBrightness(int brightness)
{
#if 01
//	allowMotionDetTIme_g = GetSystemUptime_s() + 10;
//	//励国 特殊版本 最低亮度20%占空比
//	brightness = 20 + (80 * brightness / 100);
//	return SystemSetIncandescentLampPwm(LAMP_BRIGHTNESS, brightness);
#endif
	g_Alarm.SetAllowMotionDetTime(time(NULL)+10);
	#if defined(RC0240) || defined(RC0240V20) || defined(RC0240V30) || defined(RC0240V40)
	if(brightness <= 15 )
	{
		SystemSetIncandescentLampLumi(15,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(brightness,1000-m_CfgFlightWarn.iLuminance_yellow);
	}
	else
	{
		SystemSetIncandescentLampLumi(brightness,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(brightness,1000-m_CfgFlightWarn.iLuminance_yellow);
	}
	#endif

	#if defined(RC0240_LGV10)
	if(brightness <= 20 )
	{
		SystemSetIncandescentLampLumi(100-20,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(brightness,1000-m_CfgFlightWarn.iLuminance_yellow);
	}
	else
	{
		SystemSetIncandescentLampLumi(100-brightness,m_CfgFlightWarn.iLuminance_yellow);
		SystemSetIncandescentLampLumi_yellow(brightness,1000-m_CfgFlightWarn.iLuminance_yellow);
	}
	#endif
	return 0;
}

//控制IRCUT true-全透  false-截止
int CCamera::doIrcutCtrl(bool enable)
{
	m_bCurrIrcutStatus = enable;

	static int last_value = -1;
	int value;
	
	if( enable )
		value = 1;
	else
		value = 0;
	
	if( ProductCof_g.ircut_flip )
		value = !value;

	if (value == last_value)
		return 0;
	last_value = value;
	
	return SystemSetIrcut(value);	
}

//控制镜像和翻转
int CCamera::doMirrorFlipCtrl(bool mirror, bool flip)
{
	if( ProductCof_g.image_flip )
	{
		mirror = !mirror;
		flip = !flip;
	}
	
	return CaptureSetMirrorAndFlip(mirror, flip);
}

/*****************************底层控制接口*****************************/

