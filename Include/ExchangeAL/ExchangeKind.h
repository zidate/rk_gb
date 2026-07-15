#ifndef __EXCHANGE_AL_EXCHANAGE_KIND_H__
#define __EXCHANGE_AL_EXCHANAGE_KIND_H__

#include "Types/Defs.h"
#include <vector>
#include <string>

/// 配置操作相关错误号
enum ConfigErrorNo
{
	CONFIG_OPT_RESTART = ERROR_BEGIN_CONFIG + 1,		///< 需要重启应用程序
	CONFIG_OPT_REBOOT = ERROR_BEGIN_CONFIG + 2,			///< 需要重启系统
	CONFIG_OPT_FILE_ERROR = ERROR_BEGIN_CONFIG + 3,		///< 写文件出错
	CONFIG_OPT_CAPS_ERROR = ERROR_BEGIN_CONFIG + 4,		///< 特性不支持
	CONFIG_OPT_VALIT_ERROR = ERROR_BEGIN_CONFIG + 5,	///< 验证失败 
	CONFIG_OPT_NOT_EXSIST = ERROR_BEGIN_CONFIG + 8,		///< 配置不存在
	CONFIG_OPT_PARSE_FAILED = ERROR_BEGIN_CONFIG + 6,	///< 配置解析出错
};

/// 默认配置种类
enum DefaultConfigKinds
{
	DEFAULT_CFG_GENERAL,			// 普通配置
	DEFAULT_CFG_ENCODE,				// 编码配置
	DEFAULT_CFG_RECORD,				// 录像配置
	DEFAULT_CFG_NET_SERVICE,		// 网络服务
	DEFAULT_CFG_NET_COMMON,			// 通用网络
	DEFAULT_CFG_ALARM,				// 报警
	DEFAULT_CFG_PTZCOMM,			// 云台，串口
	DEFAULT_CFG_USERMANAGER,		// 用户管理
	DEFAULT_CFG_PREVIEW,			// 预览配置
	DEFAULT_CFG_CAMERA_PARAM,		// 网络摄像头配置
	DEFAULT_CFG_END
};

/// 根据默认配置种类DefaultConfigKinds取得相应的名称
const char *getDefaultKindName(int iDefaultKind);

/// 根据配置名得到枚举常量
int getDefaultKindIndex(const char *iDefaultName);

/// 配置种类
enum ConfigKinds
{
	CFG_RECORD,			///< 录像配置
	CFG_MOTIONDETECT,	///< 动检配置
	CFG_MOTIONTRACK,	///< 人体追踪配置
	CFG_PIR,				//pir模式
	CFG_NETNTP,			///< 时间同步服务
	CFG_COMM,			///< 串口配置
	CFG_WIFI,          ///< WIFI
	CFG_WIFI_AP, 		///< AP模式配置
	CFG_CAMERA_PARAM,         //网络摄像头参数配置
	CFG_SDF,		        //SD卡 状态
	CFG_UPDATE,		        //升级 状态
	CFG_AIONOFF,		    //ai开关
	CFG_PRIVATE,            //隐私模式
	CFG_LIGHT,				 //指示灯配置
	CFG_PTZ_RESUME_POS,     //ptz
	CFG_FLIGHT_WARN,		//灯控配置
	CFG_USAGE_MODE, 		//推送配置
	CFG_VOICE,              //声音检测配置
	CFG_SIREN,              //蜂鸣器设置
	CFG_ONVIF, 			    //Onvif配置
	CFG_LUX,                //联动开灯亮度
	CFG_IPC_VOL,            //设备音量
	CFG_REBOOT, 			//设备重启配置
	CFG_VIDEO, 			//视频
	CFG_AUDIO, 			//音频
	CFG_OSD_TIME, 		//OSD 时间
	CFG_OSD_TEXT, 		//OSD 自定义文字
	CFG_CMIOT, 			//千里眼
	CFG_CMIOT_INTRUDE, 	//千里眼 入侵配置
	CFG_CLOUD_PLATFORM, //当前云平台：GB28181/CMIOT
	CFG_CMIOT_OSD,      //cmiot 独立 OSD 配置
	CFG_NR,
};

/// 根据配置种类ConfigKinds取得相应名称
const char *getConfigName(int iConfigKind);

/// 根据配置名称得到配置种类常量
int getConfigIndex(const char *sConfigKind);

enum AbilityKinds
{
	ABILITY_SYSTEM_FUNCTION,	///< 系统功能
	ABILITY_ENCODE,				///< 编码功能
	ABILITY_BLIND,				///< 遮挡检测功能
	ABILITY_MOTION,				///< 动态检测功能
	ABILITY_DDNS_TYPES,			///< DDNS服务类型
	ABILITY_COMM_PROTOCOLS,		///< 串口协议
	ABILITY_PTZ_PROTOCOLS,		///< 云台协议
	ABILITY_TALK_ATTRIBUTE,		///< 对讲属性
	ABILITY_MULTI_LANG,			///< 允许设置的语言
	ABILITY_LANG_LIST,			///< 实际支持的语言集
	ABILITY_MULTI_VSTD,			///< 允许设置的视频制式
	ABILITY_VSTD_LIST,			///< 实际支持的视频制式
	ABILITY_UART_PROTOCOLS,		///<458协议
	ABILITY_CAMERA,             ///< 摄像头相关能力集
	ABILITY_NETORDER,           //网络优先级设置能力
	ABILITY_INTELLIGENT,	//智能分析能力集
	ABILITY_NET_CONNECT,	//网络链接能力
	ABILITY_CARSTATUSNUM,	//车载车辆状态数
	ABILITY_VGARESOLUTION,// 支持的VGA分辨率能力集
	ABILITY_KIND_NR,
};

/// 根据能力集获取相应的字符串
const char *getAbilityName(int iAbilityKind);

int getAbilityIndex(const char *sAbilityKind);

/// 系统信息种类
enum DeviceInfoKinds
{
	DEVICE_INFO_SYSTEM,		///< 系统信息
	DEVICE_INFO_STORAGE,	///< 存储设备信息
	DEVICE_INFO_WORKSTATE,	///< 工作状态信息
	DEVICE_INFO_WIFI_AP,    ///< WIFI AP信息
	DEVICE_OEM_INFO,
	DEVICE_INFO_NR,
};

const char *getDeviceInfoName(int iDeviceInfoKind);

int getDeviceInfoIndex(const char *sDeviceInfoKind);

/// 基本系统操作
enum SystemOperations
{
	OPERATION_MACHINE,			///< 关机，重启操作
	OPERATION_DEFAULT_CONFIG,	///< 默认配置操作
	OPERATION_PTZ,				///< 云台控制
	OPERATION_MONITOR,			///< 监视控制
	OPERATION_PLAYBACK,			///< 回放控制
	OPERATION_TALK,				///< 语音对讲控制
	OPERATION_DISK_MANAGER,		///< 磁盘管理
	OPERATION_LOG_MANAGER,		///< 日志管理
	OPERATION_SYSTEM_UPGRADE,	///< 系统升级
	OPERATION_FILE_QUERY,		///< 文件查询
	OPERATION_LOG_QUERY,		///< 日志查询
	OPERATION_TIME_SETTING,		///< 设置系统时间
	OPERATION_NET_KEYBOARD,		///< 网络键盘
	OPERATION_NET_ALARM,		///< 网络告警
	OPERATION_SNAP, 		    ///< 网络手动抓图
	OPERATION_TRANS, 			///< 透明串口
	OPERATION_UPDATA,			///< 上传数据
	OPERATION_TIME_SETTING_NORTC,///<对于没有rtc的设备配置时间
	OPERATION_CPCDATA,		///<人数统计查询
	OPERATION_NR,
};

const char *getOperationName(int iOperationKind);

int getOperationIndex(const char *sOperationKind);

typedef enum app_event_code
{
	appEventInit = 0,
	appEventAlarmLocal = 1,
	appEventAlarmNet,
	appEventAlarmManual,
	appEventVideoMotion,
	appEventVideoLoss,
	appEventVideoBlind,
	appEventVideoTitle,
	appEventVideoSplit,
	appEventVideoTour,
	appEventStorageNotExist,
	appEventStorageFailure,
	appEventStorageLowSpace,
	appEventNetAbort,
	appEventComm,
	appEventStorageReadErr,
	appEventStorageWriteErr,
	appEventIPConflict,
	appEventAlarmEmergency,
	appEventDecConnect,
	appEventUpgrade,
	appEventBackup,
	appEventShutdown,
	appEventReboot,
	appEventNewFile,
	appEventVideoAnalyze,
	appEventAll
}appEventCode;

const char *getEventName(int iOperationKind);

int getEventIndex(const char *sOperationKind);

// 日志类型
enum LogType_E
{
	logTypeAll,
	logTypeSystem,	/// 系统日志
	logTypeConfig,	/// 
	logTypeStorage,
	logTypeAlarm,
	logTypeRecord,
	logTypeAccount,
	logTypeAccess,
	logTypeNr,
};

/// 得到日志类型名称
const char *getLogTypeName(int iLogType);

enum LogItemType
{
	logItemReboot,
	logItemShutDown,
	logItemClearLog,
	logItemModifyTime,
	logItemZeroBitrate,
	logItemUpgrade,
	logItemException,
	logItemUpdate,
	logItemSetTime,
	logItemSaveConfig,
	logItemSetDriverType,
	logItemClearDriver,
	logItemStorageDeviceError,
	logItemDiskChanged,
	logItemEventStart,
	logItemEventStop,
	logItemRecord,
	logItemLogIn,
	logItemLogOut,
	logItemAddUser,
	logItemDeleteUser,
	logItemModifyUser,
	logItemModifyPassword,
	logItemAddGroup,
	logItemDeleteGroup,
	logItemModifyGroup,
	logItemAccountRestore,
	logItemFileAccessError,
	logItemFileSearch,
	logItemFileAccess,
	logItemRecoverTime,
	logItemNr,
};

const char *getLogData(int iLogItemKind);
int getLogType(const char *sLog);


//系统调试相关
enum SystemDebug
{
	DEBUG_CAMERA,
	DEBUG_SHELL,
	DEBUG_CAMERA_SAVE_CMD,
	DEBUG_NR
};

const char *getDebugName(int iDebugKind);
int getDebugKind(const char *pDebugName);

#endif

