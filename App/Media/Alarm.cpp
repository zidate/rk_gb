#include <unistd.h>
#include <time.h>
#include "Common.h"

extern unsigned char bStartPrivateMode; 		//是否开启隐私模式

PATTERN_SINGLETON_IMPLEMENT(CAlarm);

CAlarm::CAlarm() : CThread("CAlarm", 50)
{
	AppInfo("CAlarm::CAlarm\n");
	m_AlarmType = 0;
}

CAlarm::~CAlarm()
{
	AppInfo("CAlarm::~CAlarm\n");
}

Bool CAlarm::Start()
{
	AppInfo("CAlarm::Start()\n");

	CAlarmMotion::instance()->Start();

#ifdef RC0240_LGV10
	CAlarmPir::instance()->Start();
#endif

	CreateThread();
	
	return TRUE;
}

Bool CAlarm::Stop()
{
	AppInfo("CAlarm::Stop()\n");

	DestroyThread();

	CAlarmMotion::instance()->Stop();

#ifdef RC0240_LGV10
	CAlarmPir::instance()->Stop();
#endif

	return TRUE;
}

void CAlarm::ThreadProc()
{
	bool bMotionAlarmFlag = false;
	bool bPirAlarmFlag = false;
	bool bVehicleAlarmFlag = false;
	bool bNonVehicleAlarmFlag = false;
	bool alarm_is_triggerd = false;
	bool bFloodLightTrigger = false;
	bool bOtherTrigger = false;

	int current_time = 0;
    int last_time = 0;
	int bPirInited = 0;

	while(m_bLoop)
	{
		int MiddleDetSwitch = CAlarmPir::instance()->GetMiddleDetSwitch();
		int RightDetSwitch  = CAlarmPir::instance()->GetRightDetSwitch();
		int LeftDetSwitch   = CAlarmPir::instance()->GetLeftDetSwitch();

		int MiddleDetResult = CAlarmPir::instance()->GetMiddleDetResult();
		int RightDetResult  = CAlarmPir::instance()->GetRightDetResult();
		int LeftDetResult   = CAlarmPir::instance()->GetLeftDetResult();

		int MotionSwitch    	= CAlarmMotion::instance()->GetMotionSwitch();
		int PersonSwitch    	= CAlarmMotion::instance()->GetPersonSwitch();
		int VehicleSwitch    	= CAlarmMotion::instance()->GetVehicleSwitch();
		int NonVehicleSwitch    = CAlarmMotion::instance()->GetNonvehicleSwitch();

		int MotionResult   		= CAlarmMotion::instance()->GetMotionResult();
		int PersonResult    	= CAlarmMotion::instance()->GetPersonResult();
		int VehicleResult    	= CAlarmMotion::instance()->GetVehicleResult();
		int NonVehicleResult    = CAlarmMotion::instance()->GetNonvehicleResult();

		m_AlarmType = ALARM_TYPE_NONE;
		bMotionAlarmFlag = false;
		bPirAlarmFlag = false;
		bVehicleAlarmFlag = false;
		bNonVehicleAlarmFlag = false;

		// if( (MiddleDetSwitch || RightDetSwitch || LeftDetSwitch) && MotionSwitch )
		//判断时候有联动警笛的基础条件
		if( (MiddleDetSwitch || RightDetSwitch || LeftDetSwitch) || MotionSwitch )//此处若为与逻辑，则关闭PIR会导致移动侦测触发的情况下警笛不播
		{
			// g_AnBaoLightManager.SetMDEnable(true);
			g_Siren.SetMDEnable(true);
		}
		else
		{
			// g_AnBaoLightManager.SetMDEnable(false);
			g_Siren.SetMDEnable(false);
		}
		

		if (MotionSwitch)//移动侦测打开
		{
//			g_AnBaoLightManager.SetMDEnable(true);
//			g_Siren.SetMDEnable(true);

			if  (PersonSwitch)//人形检测打开
			{
				bMotionAlarmFlag = MotionResult & PersonResult;
				// if (bMotionAlarmFlag)
				// {
				// 	m_AlarmType |= ALARM_TYPE_HUMAN;//人形检测类型
				// }
			}
			else
			{
				bMotionAlarmFlag = MotionResult;
				// if (bMotionAlarmFlag)
				// {
				// 	m_AlarmType |= ALARM_TYPE_MOTION;//移动侦测类型
				// }
				
			}
		}

		//打印 add on 2025.01.06 添加注释
		if ( LeftDetSwitch && LeftDetResult )
		{
			AppErr("g_pir_det_result_L pir 3 triger\n");
		}
		if ( MiddleDetSwitch && MiddleDetResult )
		{
			AppErr("g_pir_det_result_M pir 1 triger\n");
		}
		if ( RightDetSwitch && RightDetResult )
		{
			AppErr("g_pir_det_result_R pir 2 triger\n");
		}
		//打印 add on 2025.01.06 添加注释

	    //PIR的触发情况 add on 2025.01.06 添加注释
		if ((MiddleDetSwitch && MiddleDetResult) || 
			(RightDetSwitch  && RightDetResult) || 
			(LeftDetSwitch   && LeftDetResult) )
		{
			bPirAlarmFlag = true;
			AppErr("pir triger\n");
		}
		else
		{
			bPirAlarmFlag = false;
		}

        //联动控灯逻辑 add on 2025.01.06 添加注释
		bFloodLightTrigger = bMotionAlarmFlag || bPirAlarmFlag;
		

		//录像模块、Tuya消息上报、警笛联动逻辑 add on 2025.01.06 添加注释
		if( (MiddleDetSwitch || RightDetSwitch || LeftDetSwitch) && MotionSwitch )
			bOtherTrigger = bMotionAlarmFlag && bPirAlarmFlag;
		else
			bOtherTrigger = bMotionAlarmFlag || bPirAlarmFlag;

		if(bStartPrivateMode == 1)
		{
			bFloodLightTrigger = false;
			bOtherTrigger = false;
		}

		if (bFloodLightTrigger)
			g_Camera.SetAlarmTriggerStatus(1);
		else
			g_Camera.SetAlarmTriggerStatus(0);

		//联动录像，放在这里是为了没联网也能录像
		current_time = time(0);
		if ( bOtherTrigger )
		{
			// AppErr("------------->on\n");
			last_time = current_time;
			if (MotionSwitch)//移动侦测打开
			{
				if(PersonSwitch)//人形检测打开
				{
					m_AlarmType |= ALARM_TYPE_HUMAN;//人形检测类型
				}
				else
				{
					m_AlarmType |= ALARM_TYPE_MOTION;//移动侦测类型
				}
			}
			else//未开移动侦测，则默认为移动侦测事件
			{
				m_AlarmType |= ALARM_TYPE_MOTION;//移动侦测类型
			}
			//推送消息
			g_TuyaHandle.EventNotification(1,m_AlarmType);
			//联动控灯
//			g_AnBaoLightManager.SetMDStatus(1);
			//联动警笛
			g_Siren.SetMDStatus(1);
			
            if(!alarm_is_triggerd)
            {
                alarm_is_triggerd = true;
				//通知开始录像
				g_RecordManager.DoRecord(0);
            }
		}
		else
		{
			//No motion detect for more than 110 seconds, stop the event
			g_TuyaHandle.EventNotification(0,m_AlarmType);
//			g_AnBaoLightManager.SetMDStatus(0);
			g_Siren.SetMDStatus(0);

            if(current_time - last_time > 110 && alarm_is_triggerd)
            {
				//通知停止录像
				g_RecordManager.ClearRecord(0);
                alarm_is_triggerd = false;
            }
		}
		m_bAlarmFlag = false;
		usleep(50000);
	}
}

bool CAlarm::GetAlarmStatus()
{
	return m_bAlarmFlag;
}

int CAlarm::MotionDetectLightNotify(int status)
{
	return g_TuyaHandle.ReportAnbaoLightSwich(status);
}
int CAlarm::GetAlarmInterval()
{
	return CAlarmMotion::instance()->GetAlarmInterval();
}


int CAlarm::GetAlarmLightOnTime()
{
	int atime = CAlarmPir::instance()->GetLightOnTime();
	if (0 == atime)
	{
		return 60;
	}
	else if (1 == atime)
	{
		return 5*50;
	}
	else if (2 == atime)
	{
		return 10*60;
	}
	
	return 60;
}


int CAlarm::GetAlarmLightSwitch()
{
	int MiddleDetSwitch = CAlarmPir::instance()->GetMiddleDetSwitch();
	int RightDetSwitch  = CAlarmPir::instance()->GetRightDetSwitch();
	int LeftDetSwitch   = CAlarmPir::instance()->GetLeftDetSwitch();

	if( MiddleDetSwitch || RightDetSwitch || LeftDetSwitch )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int CAlarm::SetAllowMotionDetTime(int atime)
{
	return CAlarmMotion::instance()->SetAllowMotionDetTime(atime);
}

///////////////////PIR//////////////////////

PATTERN_SINGLETON_IMPLEMENT(CAlarmPir);

CAlarmPir::CAlarmPir() : CThread("CAlarmPir", 50)
{
	AppInfo("CAlarmPir::CAlarmPir\n");
	m_DetResult_M = 0;
	m_DetResult_R = 0;
	m_DetResult_L = 0;
	m_AllowPirDetTIme = 0;

	CConfigTable tablePir;
	g_configManager.getConfig(getConfigName(CFG_PIR), tablePir);
	TExchangeAL<PirConfig>::getConfig(tablePir, m_CCfgPir);

	AppInfo("m_CCfgPir.bpir M =%d\n",m_CCfgPir.bpir1);
	AppInfo("m_CCfgPir.bpir R =%d\n",m_CCfgPir.bpir2);
	AppInfo("m_CCfgPir.bpir L =%d\n",m_CCfgPir.bpir3);
	AppInfo("m_CCfgPir.iSensitivity = %d\n", m_CCfgPir.iSensitivity);
	AppInfo("m_CCfgPir.iPirLightOnTime=%d\n",m_CCfgPir.iPirLightOnTime);
}

CAlarmPir::~CAlarmPir()
{
	AppInfo("CAlarmPir::~CAlarmPir\n");
}

Bool CAlarmPir::Start()
{
	AppInfo("CAlarmPir::Start()\n");
	g_configManager.attach(getConfigName(CFG_PIR), IConfigManager::Proc(&CAlarmPir::onConfigPir, this));
	CreateThread();

	return TRUE;
}

Bool CAlarmPir::Stop()
{
	AppInfo("CAlarmPir::Stop()\n");
	DestroyThread();

	g_configManager.detach(getConfigName(CFG_PIR), IConfigManager::Proc(&CAlarmPir::onConfigPir, this));
	SystemPirDeInit();

	return TRUE;
}

void CAlarmPir::ThreadProc()
{
	int bPirInited = 0;

	while(m_bLoop)
	{
		if (!bPirInited)
		{
			int ret = -1;
			
			//AS006A PIR 上电后OUT先输出高电平，热机15秒后，输出低电平，进入检测状态
			ret = SystemPirInit();
			if (ret != -1)
			{
				bPirInited = 1;
				SystemSetPirSen(m_CCfgPir.iSensitivity,0);
				m_AllowPirDetTIme = time(NULL)+5; //延后5秒再检测，因为设置pir灵敏度时，会检测到pir触发
			}
			usleep(50000);
		}
		else
		{
			int value = 0;
			if( time(NULL) < m_AllowPirDetTIme)
			{
				usleep(50000);
				continue;
			}
			//1中，2右，3左
			value = SystemPirDet();
			// AppErr("SystemPirDet=%d\n",value);
			if (value == -1)
			{
				m_DetResult_M = 0;
				m_DetResult_R = 0;
				m_DetResult_L = 0;
			}
			else
			{
				if (value & 0x01)//pir 1
				{
					m_DetResult_M = 1;
				}
				if ( value & 0x02)//pir 2
				{
					m_DetResult_R = 1;
				}
				if ( value& 0x04) //pir 3
				{
					m_DetResult_L = 1;
				}
			}

			usleep(10000);
		}
	}
}

void CAlarmPir::onConfigPir(const CConfigTable &table, int &ret)
{
	PirConfig Cfgpir;
	TExchangeAL<PirConfig>::getConfig(table, Cfgpir);
	
	if(m_CCfgPir.iSensitivity != Cfgpir.iSensitivity)
	{			
		m_AllowPirDetTIme = time(NULL)+5; //延后5秒再检测，因为设置pir灵敏度时，会检测到pir触发
		SystemSetPirSen(Cfgpir.iSensitivity,0);
	}

	m_CCfgPir.bpir1 = Cfgpir.bpir1;
	m_CCfgPir.bpir2 = Cfgpir.bpir2;
	m_CCfgPir.bpir3 = Cfgpir.bpir3;
	m_CCfgPir.iSensitivity = Cfgpir.iSensitivity;
	m_CCfgPir.iPirLightOnTime = Cfgpir.iPirLightOnTime;

	AppInfo("m_CCfgPir.bpir M :%d\n",m_CCfgPir.bpir1);
	AppInfo("m_CCfgPir.bpir R:%d\n",m_CCfgPir.bpir2);
	AppInfo("m_CCfgPir.bpir L:%d\n",m_CCfgPir.bpir3);
	AppInfo("m_CCfgPir.iSensitivity:%d\n",m_CCfgPir.iSensitivity);
	AppInfo("m_CCfgPir.iPirLightOnTime:%d\n",m_CCfgPir.iPirLightOnTime);
}

/////////////////////MD//////////////////////////

Bool CAlarmMotion::m_MotionResult = 0;
Bool CAlarmMotion::m_PersonResult = 0;
Bool CAlarmMotion::m_VehicleResult = 0;
Bool CAlarmMotion::m_NonvehicleResult = 0;
DETECT_RESULT CAlarmMotion::m_PersonRectResult;
int CAlarmMotion::m_allowMotionDetTIme = 0;

PATTERN_SINGLETON_IMPLEMENT(CAlarmMotion);

CAlarmMotion::CAlarmMotion() : CThread("CAlarmMotion", 50)
{
	AppInfo("CAlarmMotion::CAlarmMotion\n");
}

CAlarmMotion::~CAlarmMotion()
{
	AppInfo("CAlarmMotion::~CAlarmMotion\n");
}

Bool CAlarmMotion::Start()
{
	AppInfo("CAlarmMotion::Start()\n");

	CConfigTable tableMotion;
	g_configManager.getConfig(getConfigName(CFG_MOTIONDETECT), tableMotion);
	TExchangeAL<MotionDetectConfigAll>::getConfigV2(tableMotion, m_CCfgMotion, 1);

	AppWarning("m_CCfgMotion.vMotionDetectAll[0].bEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bEnable);
    AppWarning("m_CCfgMotion.vMotionDetectAll[0].iLevel = %d\n", m_CCfgMotion.vMotionDetectAll[0].iLevel);
    AppWarning("m_CCfgMotion.vMotionDetectAll[0].iInterval = %d\n", m_CCfgMotion.vMotionDetectAll[0].iInterval);
    AppWarning("m_CCfgMotion.vMotionDetectAll[0].bRegionEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bRegionEnable);
    AppWarning("m_CCfgMotion.vMotionDetectAll[0].mRegion[0] = 0x%x\n", m_CCfgMotion.vMotionDetectAll[0].mRegion[0]);
	AppWarning("m_CCfgMotion.vMotionDetectAll[0].bPersonFilterEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bPersonFilterEnable);
	AppWarning("m_CCfgMotion.vMotionDetectAll[0].bPersonDetectEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bPersonDetectEnable);
	AppWarning("m_CCfgMotion.vMotionDetectAll[0].bVehicleDetectEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bVehicleDetectEnable);
	AppWarning("m_CCfgMotion.vMotionDetectAll[0].bNonVehicleDetectEnable = %d\n", m_CCfgMotion.vMotionDetectAll[0].bNonVehicleDetectEnable);

	g_configManager.attach(getConfigName(CFG_MOTIONDETECT), IConfigManager::Proc(&CAlarmMotion::onConfigMotion, this));
	

	CConfigTable CameraParamTable;
	CameraParamAll m_configAll;
	memset(&m_configAll, 0, sizeof(m_configAll));
	g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), CameraParamTable);
	TExchangeAL<CameraParamAll>::getConfig(CameraParamTable, m_configAll);
	g_configManager.attach(getConfigName(CFG_CAMERA_PARAM), IConfigManager::Proc(&CAlarmMotion::onConfigCamera, this));

	CaptureDetectInit(NULL);
	if( m_CCfgMotion.vMotionDetectAll[0].bEnable )//移动侦测打开
	{
		if( m_CCfgMotion.vMotionDetectAll[0].bPersonFilterEnable || 
			m_CCfgMotion.vMotionDetectAll[0].bVehicleDetectEnable || 
			m_CCfgMotion.vMotionDetectAll[0].bNonVehicleDetectEnable )
		{
			DETECT_INIT detInit;
			detInit.Rotate = (RotateAttr_t)m_configAll.vCameraParamAll[0].rotateAttr;
			detInit.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
			detInit.RegionEnable = m_CCfgMotion.vMotionDetectAll[0].bRegionEnable;
			detInit.Region.Num = 0;
			for (int i = 0; i < 4; i++)//移动侦测区域数量
			{
				detInit.Region.RegionAttr[i] = m_CCfgMotion.vMotionDetectAll[0].mRegion[i];
				if (detInit.Region.RegionAttr[i] != 0)
				{
					detInit.Region.Num++;
				}
			}
			CaptureDetectSet(&detInit);
			CaptureDetectStart();
		}

		//人型过滤
		if( m_CCfgMotion.vMotionDetectAll[0].bPersonFilterEnable )
		{
			DETECT_ATTR attr;
			attr.Callback = CAlarmMotion::PersonCb;
			attr.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
			attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_PERSON;
			CaptureDetectObjectStart(&attr);
		}

		//车形检测
		if( m_CCfgMotion.vMotionDetectAll[0].bVehicleDetectEnable )
		{
			DETECT_ATTR attr;
			attr.Callback = CAlarmMotion::VehicleCb;
			attr.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
			attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_VEHICLE;
			CaptureDetectObjectStart(&attr);
		}

		//非机动车形检测
		if( m_CCfgMotion.vMotionDetectAll[0].bNonVehicleDetectEnable )
		{
			DETECT_ATTR attr;
			attr.Callback = CAlarmMotion::NonvehicleCb;
			attr.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
			attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE;
			CaptureDetectObjectStart(&attr);
		}
	}
	else
	{
		if( m_CCfgMotionTrack.bEnable )
		{
			DETECT_INIT detInit;
			detInit.Rotate = (RotateAttr_t)m_configAll.vCameraParamAll[0].rotateAttr;
			detInit.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
			detInit.RegionEnable = m_CCfgMotion.vMotionDetectAll[0].bRegionEnable;
			detInit.Region.Num = 0;
			for (int i = 0; i < 4; i++)
			{
				detInit.Region.RegionAttr[i] = m_CCfgMotion.vMotionDetectAll[0].mRegion[i];
				if (detInit.Region.RegionAttr[i] != 0)
				{
					detInit.Region.Num++;
				}
			}
			CaptureDetectSet(&detInit);
			CaptureDetectStart();
		}
	}
	//移动侦测
	if( m_CCfgMotion.vMotionDetectAll[0].bEnable )
	{
		MotionDetectCreate(	CAlarmMotion::MotionCb, 
							m_CCfgMotion.vMotionDetectAll[0].iLevel, 
							m_CCfgMotion.vMotionDetectAll[0].bRegionEnable, 
							m_CCfgMotion.vMotionDetectAll[0].mRegion );
	}

	CConfigTable tableMotionTrack;
	g_configManager.getConfig(getConfigName(CFG_MOTIONTRACK), tableMotionTrack);
	TExchangeAL<MotionTrackConfig>::getConfig(tableMotionTrack, m_CCfgMotionTrack);
	AppWarning("m_CCfgMotionTrack.bEnable = %d\n", m_CCfgMotionTrack.bEnable);
	g_configManager.attach(getConfigName(CFG_MOTIONTRACK), IConfigManager::Proc(&CAlarmMotion::onConfigMotionTrack, this));

	if(m_CCfgMotionTrack.bEnable)
	{
		CaptureMotionTrackerStart(CAlarmMotion::TrackerCb);
	}

	CreateThread();
	
	return TRUE;
}

Bool CAlarmMotion::Stop()
{
	AppInfo("CAlarmMotion::Stop()\n");
	DestroyThread();

	MotionDetectDestory();
	CaptureDetectDeInit();
	g_configManager.detach(getConfigName(CFG_CAMERA_PARAM), IConfigManager::Proc(&CAlarmMotion::onConfigCamera, this));
	g_configManager.detach(getConfigName(CFG_MOTIONDETECT), IConfigManager::Proc(&CAlarmMotion::onConfigMotion, this));
	g_configManager.detach(getConfigName(CFG_MOTIONTRACK), IConfigManager::Proc(&CAlarmMotion::onConfigMotionTrack, this));
	return TRUE;
}

void CAlarmMotion::MotionCb(int action)
{
	//通过时间过滤掉移动侦测
	if( time(NULL) < m_allowMotionDetTIme)
		m_MotionResult = 0;
	else
		m_MotionResult = action;
}
Bool CAlarmMotion::GetPersonResult(DETECT_RESULT *result)
{
	memcpy(result,&m_PersonRectResult,sizeof(DETECT_RESULT));
	return m_PersonRectResult.Status;
}
void CAlarmMotion::PersonCb(int status, DETECT_RESULT result)
{
	// AppErr("m_PersonResult status=%d\n",status);
	m_PersonResult = status;
	m_PersonRectResult = result;

}

void CAlarmMotion::VehicleCb(int status, DETECT_RESULT result)
{
	// AppErr("m_VehicleResult status=%d\n",status);
	m_VehicleResult = status;
}

void CAlarmMotion::NonvehicleCb(int status, DETECT_RESULT result)
{
	// AppErr("m_NonvehicleResult status=%d\n",status);
	m_NonvehicleResult = status;
}

void CAlarmMotion::TrackerCb(MOTION_TRACKER_RESULT result)
{
	/*1.哪个大追哪个
	 *2.以重叠区域最大的框为下一个轨迹
	 */

	int i;
	int index;
	static unsigned char bTracking = 0;
	static unsigned char ucCaptureLostCount = 0;
	static DETECT_RECT stTrackingBox;
	float fBoxArea;
	float fMaxBoxArea;
	
	const int CENTER_X = 0; //视频区域的中心店
	const int CENTER_Y = -2000;

	unsigned int topLeftx = 0;
	unsigned int topLefty = 0;
	unsigned int width = 0;
	unsigned int hight = 0;

	int center_x_ratio = 0;
	int center_y_ratio = 0;

	int ptz_tracker_max_x = ProductCof_g.lr_len * 8;
	int ptz_tracker_max_y = ProductCof_g.ud_len * 8;

	unsigned int ptz_center_x_ratio = 0;
	unsigned int ptz_center_y_ratio = 0;

	unsigned int width_ratio = 0;
	unsigned int height_ratio = 0;

	if (result.Status != 0)
	{
		return;
	}

	if (false == CAlarmMotion::instance()->GetMotionTrackerSwitch())
	{
		bTracking = 0;
		ucCaptureLostCount = 0;
		return;
	}
	
	if (0 == bTracking)
	{
		//没有在追踪，找最大的去追
		if (0 == result.ObjNum)
			return;
		index = 0;
		topLeftx = result.ObjInfo[index].Rect.topLeft.x;
		topLefty = result.ObjInfo[index].Rect.topLeft.y;
		width = result.ObjInfo[index].Rect.bottomRight.x - result.ObjInfo[index].Rect.topLeft.x;
		hight = result.ObjInfo[index].Rect.bottomRight.y - result.ObjInfo[index].Rect.topLeft.y;
		// printf("[%d]-[x=%u,y=%u,w=%u,h=%u]\n",index,topLeftx,topLefty,width,hight);
		fMaxBoxArea = width * hight;

		for (i = 1; i < result.ObjNum; i++)
		{
			topLeftx = result.ObjInfo[i].Rect.topLeft.x;
			topLefty = result.ObjInfo[i].Rect.topLeft.y;
			width = result.ObjInfo[i].Rect.bottomRight.x - result.ObjInfo[i].Rect.topLeft.x;
			hight = result.ObjInfo[i].Rect.bottomRight.y - result.ObjInfo[i].Rect.topLeft.y;

			// printf("[%d]-[x=%u,y=%u,w=%u,h=%u]\n",i,topLeftx,topLefty,width,hight);
			fBoxArea = width  * hight;
			
			if (fBoxArea > fMaxBoxArea)
			{
				index = i;
				fMaxBoxArea = fBoxArea;
			}
		}

		bTracking = 1;
		ucCaptureLostCount = 0;
		stTrackingBox = result.ObjInfo[index].Rect;
		topLeftx = result.ObjInfo[index].Rect.topLeft.x;
		topLefty = result.ObjInfo[index].Rect.topLeft.y;
		width = result.ObjInfo[index].Rect.bottomRight.x - result.ObjInfo[index].Rect.topLeft.x;
		hight = result.ObjInfo[index].Rect.bottomRight.y - result.ObjInfo[index].Rect.topLeft.y;

		center_x_ratio = ((topLeftx + width / 2) + CENTER_X) ;
		center_y_ratio = ((topLefty + hight / 2) + CENTER_Y) ;
		width_ratio = width;
		height_ratio = hight;
		//todo 调用ptz云台转到 center_x_ratio center_y_ratio

		// ptz_center_x_ratio = center_x_ratio * (ProductCof_g.lr_len * 8) / 10000;
		// ptz_center_y_ratio = center_y_ratio * (ProductCof_g.ud_len * 8) / 10000;

		ptz_center_x_ratio = center_x_ratio * ptz_tracker_max_x / 10000;
		ptz_center_y_ratio = center_y_ratio * ptz_tracker_max_y / 10000;

		printf("ptz1----> center_ratio [%u,%u] ptz [%u,%u]\n",center_x_ratio,center_y_ratio,ptz_center_x_ratio,ptz_center_y_ratio);
		g_PtzHandle.motion_track_ptz_op(ptz_center_x_ratio,ptz_center_y_ratio);
		return;
	}
	else
	{
		//寻找正在追踪的人形框，以重合度最大为准
		if (0 == result.ObjNum)
		{
			ucCaptureLostCount++;
		}
		else
		{
			fMaxBoxArea = 0;
			float overlap_x1, overlap_y1, overlap_x2, overlap_y2;
			for (i = 0, index = -1; i < result.ObjNum; i++)
			{
				float track_box_x1 = stTrackingBox.topLeft.x;
				float track_box_y1 = stTrackingBox.topLeft.y;
				float track_box_x2 = stTrackingBox.bottomRight.x;
				float track_box_y2 = stTrackingBox.bottomRight.y;
				float box_x1 =  result.ObjInfo[i].Rect.topLeft.x;
				float box_y1 =  result.ObjInfo[i].Rect.topLeft.y;
				float box_x2 =  result.ObjInfo[i].Rect.bottomRight.x;
				float box_y2 =  result.ObjInfo[i].Rect.bottomRight.y;
//				printf("track box --- [%.2f, %.2f] [%.2f, %.2f]\n", track_box_x1, track_box_y1, track_box_x2, track_box_y2);
//				printf("new   box --- [%.2f, %.2f] [%.2f, %.2f]\n", box_x1, box_y1, box_x2, box_y2);
				overlap_x1 = track_box_x1 > box_x1 ? track_box_x1 : box_x1;
				overlap_y1 = track_box_y1 > box_y1 ? track_box_y1 : box_y1;
				overlap_x2 = track_box_x2 < box_x2 ? track_box_x2 : box_x2;
				overlap_y2 = track_box_y2 < box_y2 ? track_box_y2 : box_y2;
//				printf("overlap_x1 box --- [%.2f, %.2f] [%.2f, %.2f]\n", overlap_x1, overlap_y1, overlap_x2, overlap_y2);

				if (overlap_x1 > overlap_x2 || overlap_y1 > overlap_y2) //非法矩形
					continue;

				fBoxArea = (overlap_x2 - overlap_x1) * (overlap_y2 - overlap_y1);
				if (fBoxArea > fMaxBoxArea)
				{
					index = i;
					fMaxBoxArea = fBoxArea;
				}
			}
			if (-1 == index) //没有重叠区域
				ucCaptureLostCount++;
			else
				ucCaptureLostCount = 0;

			if (-1 != index) //选重合最大的框作为下一个轨迹
			{
//				printf("---1111111\n");
				stTrackingBox = result.ObjInfo[index].Rect;
				topLeftx = result.ObjInfo[index].Rect.topLeft.x;
				topLefty = result.ObjInfo[index].Rect.topLeft.y;
				width = result.ObjInfo[index].Rect.bottomRight.x - result.ObjInfo[index].Rect.topLeft.x;
				hight = result.ObjInfo[index].Rect.bottomRight.y - result.ObjInfo[index].Rect.topLeft.y;

				center_x_ratio = ((topLeftx + width / 2) + CENTER_X) ;
				center_y_ratio = ((topLefty + hight / 2) + CENTER_Y) ;
				width_ratio = width;
				height_ratio = hight;
				//todo 调用ptz云台转到 center_x_ratio center_y_ratio

				// ptz_center_x_ratio = center_x_ratio * (ProductCof_g.lr_len * 8) / 10000;
				// ptz_center_y_ratio = center_y_ratio * (ProductCof_g.ud_len * 8) / 10000;

				ptz_center_x_ratio = center_x_ratio * ptz_tracker_max_x / 10000;
				ptz_center_y_ratio = center_y_ratio * ptz_tracker_max_y / 10000;

				printf("ptz2----> center_ratio [%u,%u] ptz [%u,%u]\n",center_x_ratio,center_y_ratio,ptz_center_x_ratio,ptz_center_y_ratio);
				g_PtzHandle.motion_track_ptz_op(ptz_center_x_ratio,ptz_center_y_ratio);
				return;
			}
			else
			{
//				printf("---2222222\n");
				index = 0;
				topLeftx = result.ObjInfo[index].Rect.topLeft.x;
				topLefty = result.ObjInfo[index].Rect.topLeft.y;
				width = result.ObjInfo[index].Rect.bottomRight.x - result.ObjInfo[index].Rect.topLeft.x;
				hight = result.ObjInfo[index].Rect.bottomRight.y - result.ObjInfo[index].Rect.topLeft.y;
				fMaxBoxArea = width * hight;
				for (i = 1; i < result.ObjNum; i++)
				{
					topLeftx = result.ObjInfo[i].Rect.topLeft.x;
					topLefty = result.ObjInfo[i].Rect.topLeft.y;
					width = result.ObjInfo[i].Rect.bottomRight.x - result.ObjInfo[i].Rect.topLeft.x;
					hight = result.ObjInfo[i].Rect.bottomRight.y - result.ObjInfo[i].Rect.topLeft.y;
					fBoxArea = width  * hight;

					if (fBoxArea > fMaxBoxArea)
					{
						index = i;
						fMaxBoxArea = fBoxArea;
					}
				}
				
				ucCaptureLostCount = 0;
				stTrackingBox = result.ObjInfo[index].Rect;
				topLeftx = result.ObjInfo[index].Rect.topLeft.x;
				topLefty = result.ObjInfo[index].Rect.topLeft.y;
				width = result.ObjInfo[index].Rect.bottomRight.x - result.ObjInfo[index].Rect.topLeft.x;
				hight = result.ObjInfo[index].Rect.bottomRight.y - result.ObjInfo[index].Rect.topLeft.y;

				center_x_ratio = ((topLeftx + width / 2) + CENTER_X) ;
				center_y_ratio = ((topLefty + hight / 2) + CENTER_Y) ;
				width_ratio = width;
				height_ratio = hight;
			}
		}
		if (ucCaptureLostCount> 0) 	//连续30次找不到轨迹，判定为追踪丢失
		{
			bTracking = 0;
			ucCaptureLostCount = 0;
			memset(&stTrackingBox, 0, sizeof(stTrackingBox));
		}
	}
}

void CAlarmMotion::ThreadProc()
{
	while(m_bLoop)
	{
		usleep(500000);
	}
}
void CAlarmMotion::onConfigCamera(const CConfigTable &configAll, int &ret)
{
	CameraParamAll newParamAll;
	TExchangeAL<CameraParamAll>::getConfig(configAll, newParamAll);
	if (m_configAll.vCameraParamAll[0].rotateAttr != newParamAll.vCameraParamAll[0].rotateAttr)
	{
		m_configAll.vCameraParamAll[0].rotateAttr = newParamAll.vCameraParamAll[0].rotateAttr;

		// CaptureDetectDeInit();
		// DETECT_INIT detInit;
		// detInit.Rotate = (RotateAttr_t)m_configAll.vCameraParamAll[0].rotateAttr;
		// detInit.Level = m_CCfgMotion.vMotionDetectAll[0].iLevel;
		// detInit.RegionEnable = m_CCfgMotion.vMotionDetectAll[0].bRegionEnable;
		// detInit.Region.Num = 4;
		// for (int i = 0; i < 4; i++)
		// {
		// 	detInit.Region.RegionAttr[i] = m_CCfgMotion.vMotionDetectAll[0].mRegion[i];
		// }
		// CaptureDetectInit(&detInit);
	}
}

void CAlarmMotion::onConfigMotion(const CConfigTable &table, int &ret)
{
	bool bNeedRestart = false;
	TStrWrapper<MotionDetectConfigAll> pCfgMotion;

	TExchangeAL<MotionDetectConfigAll>::getConfigV2(table, *pCfgMotion, 1);
	//for (int i = 0; i < g_nDeviceChannel; i++)
	{
		MotionDetectConfig& cfgOld = m_CCfgMotion.vMotionDetectAll[0];
		MotionDetectConfig& cfgNew = pCfgMotion->vMotionDetectAll[0];

		if( (cfgOld.bEnable != cfgNew.bEnable) ||
			(cfgOld.iLevel != cfgNew.iLevel) || 
			(cfgOld.bRegionEnable != cfgNew.bRegionEnable) || 
			(memcmp(cfgOld.mRegion, cfgNew.mRegion, sizeof(uint)*MD_REGION_ROW) != 0) ||
			(cfgOld.bPersonFilterEnable != cfgNew.bPersonFilterEnable) ||
			(cfgOld.bVehicleDetectEnable != cfgNew.bVehicleDetectEnable) ||
			(cfgOld.bNonVehicleDetectEnable != cfgNew.bNonVehicleDetectEnable) )
		{
			bNeedRestart = true;
		}

		if( bNeedRestart )
		{
			if (cfgNew.bEnable)
			{
				MotionDetectCreate(	CAlarmMotion::MotionCb,
									cfgNew.iLevel, 
									cfgNew.bRegionEnable, 
									cfgNew.mRegion );

				if ((cfgNew.bPersonFilterEnable) ||
					(cfgNew.bVehicleDetectEnable) ||
					(cfgNew.bNonVehicleDetectEnable))
				{
					DETECT_INIT detInit;
					detInit.Rotate = (RotateAttr_t)m_configAll.vCameraParamAll[0].rotateAttr;
					detInit.Level = cfgNew.iLevel;
					detInit.RegionEnable = cfgNew.bRegionEnable;
					detInit.Region.Num = 0;
					for (int i = 0; i < 4; i++)
					{
						detInit.Region.RegionAttr[i] = cfgNew.mRegion[i];
						if (detInit.Region.RegionAttr[i] != 0)
						{
							detInit.Region.Num++;
						}
					}
					CaptureDetectSet(&detInit);
					CaptureDetectStart();
				}
				else
				{
					// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_PERSON);
					// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_VEHICLE);
					// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE);
					CaptureDetectStop();
				}

				if (cfgNew.bPersonFilterEnable)
				{
					DETECT_ATTR attr;
					attr.Callback = CAlarmMotion::PersonCb;
					attr.Level = cfgNew.iLevel;
					attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_PERSON;
					CaptureDetectObjectStart(&attr);
				}
				else
				{
					CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_PERSON);
				}

				if (cfgNew.bVehicleDetectEnable)
				{
					DETECT_ATTR attr;
					attr.Callback = CAlarmMotion::VehicleCb;
					attr.Level = cfgNew.iLevel;
					attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_VEHICLE;
					CaptureDetectObjectStart(&attr);
				}
				else
				{
					CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_VEHICLE);
				}

				if (cfgNew.bNonVehicleDetectEnable)
				{
					DETECT_ATTR attr;
					attr.Callback = CAlarmMotion::NonvehicleCb;
					attr.Level = cfgNew.iLevel;
					attr.ObjectType = DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE;
					CaptureDetectObjectStart(&attr);
				}
				else
				{
					CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE);
				}
			}
			else
			{
				MotionDetectDestory();

				// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_PERSON);
				// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_VEHICLE);
				// CaptureDetectObjectStop(DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE);
				CaptureDetectStop();
			}
		}
	}
	m_CCfgMotion=*pCfgMotion;
}
void CAlarmMotion::onConfigMotionTrack(const CConfigTable &table, int &ret)
{
	MotionTrackConfig CfgMotionTrack;
	TExchangeAL<MotionTrackConfig>::getConfig(table, CfgMotionTrack);
	if ( m_CCfgMotionTrack.bEnable != CfgMotionTrack.bEnable)
	{
		if( false == CfgMotionTrack.bEnable)
		{
			CaptureMotionTrackerStop();
		}
		else
		{
			CaptureMotionTrackerStart(CAlarmMotion::TrackerCb);
		}
	}
	m_CCfgMotionTrack = CfgMotionTrack;
}

int CAlarmMotion::SetAllowMotionDetTime(int atime)
{
	m_allowMotionDetTIme = atime;
	return m_allowMotionDetTIme;
}
