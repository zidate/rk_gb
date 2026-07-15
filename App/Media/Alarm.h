#ifndef __COMM_ALARM_H__
#define __COMM_ALARM_H__

#include "Common.h"

/*事件类型*/
#define ALARM_TYPE_NONE 	(0) 	/*无*/
#define ALARM_TYPE_MOTION 	(1<<0)  /*移动侦测*/
#define ALARM_TYPE_HUMAN 	(1<<1)  /*人形侦测*/
#define ALARM_TYPE_CAR 	    (1<<2)  /*车形侦测*/

class CAlarm : public CThread
{
	PATTERN_SINGLETON_DECLARE(CAlarm);

public:

	Bool Start();
	Bool Stop();

private:
	void ThreadProc();	
	int  m_AlarmType;
	bool m_bAlarmFlag;

public:	
	int MotionDetectLightNotify(int status);
	/// @brief 获取报警推送间隔
	/// @return 报警推送间隔单位秒
	int GetAlarmInterval();

	/// @brief 获取联动触发报警亮灯时长
	/// @return 单位秒60 300 600
	int GetAlarmLightOnTime();
	
	/// @brief 获取Pir联动触发报警亮灯使能开关
	/// @return 0关闭 1开启
	int GetAlarmLightSwitch();

	/// @brief 设置移动侦测允许报警的时间
	/// @param atime 单位秒
	/// @return 返回设置的参数
	int SetAllowMotionDetTime(int atime);

	/// @brief 获取报警状态（移动侦测以及Pir组合）
	/// @return 0-不报警 1-报警
	bool GetAlarmStatus();
};

#define g_Alarm (*CAlarm::instance())

////////////////////PIR////////////////////

class CAlarmPir : public CThread
{
	PATTERN_SINGLETON_DECLARE(CAlarmPir);

public:

	Bool Start();
	Bool Stop();

	/// @brief 获取中间pir检测结果
	/// @return 0触发 1不触发
	Bool GetMiddleDetResult() { return m_DetResult_M; }

	/// @brief 获取右边pir检测结果
	/// @return 0触发 1不触发
	Bool GetRightDetResult()  { return m_DetResult_R; }

	/// @brief 获取左边pir检测结果
	/// @return 0触发 1不触发
	Bool GetLeftDetResult()   { return m_DetResult_L; }

	/// @brief 获取中间pir检测开关
	/// @return 0关闭 1打开
	int GetMiddleDetSwitch() { return m_CCfgPir.bpir1; }

	/// @brief 获取左边pir检测开关
	/// @return 0关闭 1打开
	int GetLeftDetSwitch()   { return m_CCfgPir.bpir3; }

	/// @brief 获取右边pir检测开关
	/// @return 0关闭 1打开
	int GetRightDetSwitch()  { return m_CCfgPir.bpir2; }

	/// @brief 获取pir触发后亮灯时长
	/// @return 亮灯时长0-1分钟 1-5分钟 2-10分钟
	int GetLightOnTime()     { return m_CCfgPir.iPirLightOnTime; }

private:
	void ThreadProc();	
	void onConfigPir(const CConfigTable &table, int &ret);

private:
	Bool m_DetResult_M; //中间
	Bool m_DetResult_R; //右
	Bool m_DetResult_L; //左

	PirConfig m_CCfgPir;

	int m_AllowPirDetTIme;
};

////////////////////MD////////////////////

class CAlarmMotion : public CThread
{
	PATTERN_SINGLETON_DECLARE(CAlarmMotion);

public:

	Bool Start();
	Bool Stop();

	/// @brief 获取移动侦测结果
	/// @return 0触发 1不触发
	static Bool GetMotionResult()     { return m_MotionResult; }

	/// @brief 获取人形检测结果
	/// @return 0触发 1不触发
	static Bool GetPersonResult()     { return m_PersonResult; }
	static Bool GetPersonResult(DETECT_RESULT *result);

	/// @brief 获取车形检测结果
	/// @return 0触发 1不触发
	static Bool GetVehicleResult()    { return m_VehicleResult; }

	/// @brief 获取非机动车形检测结果
	/// @return 0触发 1不触发
	static Bool GetNonvehicleResult() { return m_NonvehicleResult; }

	static void MotionCb(int action);
	static void PersonCb(int status,DETECT_RESULT result);
	static void VehicleCb(int status, DETECT_RESULT result);
	static void NonvehicleCb(int status, DETECT_RESULT result);
	static void TrackerCb(MOTION_TRACKER_RESULT result);

	/// @brief 获取报警推送间隔
	/// @return 报警间隔单位秒
	int GetAlarmInterval()      	{ return m_CCfgMotion.vMotionDetectAll[0].iInterval; }

	/// @brief 获取移动侦测开关
	/// @return 0关闭 1打开
	int GetMotionSwitch()  			{ return m_CCfgMotion.vMotionDetectAll[0].bEnable; }

	/// @brief 获取人形检测开关
	/// @return 0关闭 1打开
	int GetPersonSwitch()  			{ return m_CCfgMotion.vMotionDetectAll[0].bPersonFilterEnable; }

	/// @brief 获取车形检测开关
	/// @return 0关闭 1打开
	int GetVehicleSwitch()  		{ return m_CCfgMotion.vMotionDetectAll[0].bVehicleDetectEnable; }

	/// @brief 获取非机动车形检测开关
	/// @return 0关闭 1打开
	int GetNonvehicleSwitch()  		{ return m_CCfgMotion.vMotionDetectAll[0].bNonVehicleDetectEnable; }

	int GetMotionTrackerSwitch() 	{ return m_CCfgMotionTrack.bEnable; }

	/// @brief 设置允许报警的时间
	/// @param atime 时间单位秒
	/// @return 设置的参数
	static int SetAllowMotionDetTime(int atime);

private:
	void ThreadProc();
	void onConfigCamera(const CConfigTable &config, int &ret);
	void onConfigMotion(const CConfigTable &config, int &ret);
	void onConfigMotionTrack(const CConfigTable &table, int &ret);
	void onConfigCmiotIntrude(const CConfigTable &config, int &ret);
private:
	static Bool m_MotionResult;
	static Bool m_PersonResult;
	static Bool m_VehicleResult;
	static Bool m_NonvehicleResult;

	static DETECT_RESULT m_PersonRectResult;

	static int m_allowMotionDetTIme;

	CameraParamAll m_configAll;
	MotionDetectConfigAll m_CCfgMotion;
	MotionTrackConfig m_CCfgMotionTrack;

	CmiotIntrudeConf_S m_cfgCmiotIntrude;
};
#endif
