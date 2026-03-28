#ifndef _GB28181DEFS_DEF_H_
#define _GB28181DEFS_DEF_H_

#ifdef WIN32
#ifdef GB28181_EXPORTS
#define EXPORT_GB28181_SDK  __declspec(dllexport)
#else
#define EXPORT_GB28181_SDK __declspec(dllimport)
#endif

#else
#define EXPORT_GB28181_SDK  __attribute__ ((visibility("default")))
#endif

#include <stdint.h>
#include <string.h>
#include "WarningDisable.h"
#define GB_ID_LEN                20+1
#define COMM_LEN               64
#define TIME_LEN                  64
#define IP_LEN                       20
#define REQUEST_TYPE_LEN   64
#define URI_LEN                     128
#define CODE_LEN                 20
#define STR_LEN                    128
#define ROUTE_LEN                258
#define TYPE_LEN                   10
#define EXTEND_LEN              1024
#define DESCRI_LEN               1024
#define EVENT_LEN               16
#define SSRC_LEN                11

typedef void * SessionHandle;				//会话句柄
typedef void * StreamHandle;				//流句柄
typedef void * ResponseHandle;
typedef void * SubscribeHandle;


enum GB28181ErrorCode
{
     kGbFail,
     kGb28181Success,
     kGbNullPointer,
     kGbStreamHandleNotExist,
     kGbRequestTimeout,
     kGbResponseCodeError,
     kGbXmlEncodeFail,
     kGbSdpParseFail,
     kGbRegistFail,
     kGbMessageFail,
     kGbInviteFail,
     kGbAckFail,
     kGbInfoFail,
	 kGbByeFail,
	 kGbSubScribeFail,
	 kGbSubHandleNotExist,
     kGbRegistRedirectFail,
     kGbRegistFormalFail,
     kGbRegistRedirectInvalid
};

//协议版本
enum GB28181Version
{
    kGB2011Version,
    kGB2016Version
};

//传输方式
enum TransPorco
{
    kSipProcoOverTCP,
    kSipProcoOverUDP
};

//连接参数
struct ConnectParam
{
    char   GBCode[GB_ID_LEN];
    char   ip[IP_LEN];
    uint16_t     port;
    char   server_domain[STR_LEN];
    char   server_id[GB_ID_LEN];
};

//注册信息
struct GBRegistParam
{
    bool         use_zero_config;
    uint32_t     expires;
    char    username[STR_LEN];
    char    password[STR_LEN];
    char    auth_username[STR_LEN];
    char    local_name[STR_LEN];
    char    device_name[STR_LEN];
    char    string_code[STR_LEN];
    char    mac_address[STR_LEN];
    char    line_id[STR_LEN];
    char    manufacturer[STR_LEN];
    char    model[STR_LEN];
    char    custom_protocol_version[STR_LEN];
};

//针对查询条件和回复的信息结构，带*号为必填内容，其它为可选填内容

struct DeviceCapabilityInfo
{
    char    PositionCapability[STR_LEN];
    char    AlarmOutCapability[STR_LEN];
    char    MICCapability[STR_LEN];
    char    SpeakerCapability[STR_LEN];
    char    ImagingCapability[STR_LEN];
    char    StreamPeramCapability[STR_LEN];
    char    SupplyLightCapability[STR_LEN];
};

struct ProtocolFunctionInfo
{
    char    BroadcastCapability[STR_LEN];
    char    FrameMirrorCapability[STR_LEN];
    char    MultiStreamCapability[STR_LEN];
    char    OSDCapability[STR_LEN];
    char    DeviceUpgradeCapability[STR_LEN];
    char    AlarmCapability[STR_LEN];
    char    BroadcastTcpActiveCapability[STR_LEN];
};

//设备信息
struct DeviceInfo
{
    char    GBCode[GB_ID_LEN];
    char    device_name[STR_LEN];
    char    device_type[TYPE_LEN];
    char    manufacturer[STR_LEN];	                                     //设备生产商
    char    model[STR_LEN];				                    //设备型号
    char    firmware[STR_LEN];			                            //设备固件版本
    char    string_code[STR_LEN];
    char    mac_address[STR_LEN];
    char    line_id[STR_LEN];
    char    custom_protocol_version[STR_LEN];
    DeviceCapabilityInfo device_capability_list;
    ProtocolFunctionInfo protocol_function_list;
    unsigned int video_channel_num;
    bool    result;								   //是否查询成功 *
};

struct AlarmResetInfo
{
     char AlarmMethod[STR_LEN];
     char AlarmType[STR_LEN];
};

struct TimeInterval
{
    char StartTime[TIME_LEN];			//增加设备的起始时间(可选) 空表示不限
    char EndTime[TIME_LEN];			//增加设备的终止时间(可选) 空表示到当前时间
};

//表示仅获取在这个时间段内添加的设备目录信息
struct CatalogQueryParam
{
    char              DeviceID[GB_ID_LEN];
    TimeInterval   time;
};

//设备目录
//当为目录通知消息时，除设备编码和通知事件两项外，其他都可选填
struct CatalogInfo
{
	char Event[EVENT_LEN];					//<!-- 设备状态改变事件 NONE:无， ON:上线，OFF:下线，VLOST:视频丢失，DETECT: 故障， ADD:增加， DEL:删除， UPDATE:更新 -->
    char DeviceID[GB_ID_LEN];				//<!-- 设备/区域/系统编码（必选） -->
    char Name[STR_LEN];			       //<!-- 设备/区域/系统名称（必选） -->
    char Manufacturer[COMM_LEN];			//<!-- 当为设备时，设备厂商（必选） -->
    char Model[COMM_LEN];					//<!-- 当为设备时，设备型号（必选） -->
    char Owner[COMM_LEN];					//<!-- 当为设备时，设备归属（必选） -->
    char CivilCode[GB_ID_LEN];				//<!-- 行政区域（必选） -->
    char Block[COMM_LEN];					//<!-- 警区（可选） -->
    char Address[COMM_LEN];				//<!-- 当为设备时，安装地址（必选） -->

    bool Parental;				                      //<!-- 当为设备时，是否有子设备（必选）1有，0没有 -->
    char ParentID[GB_ID_LEN];				//<!-- 父设备/区域/系统ID（可选，有父设备需要填写） -->
    char StreamNumberList[32];              //<!-- ???????/???????? -->

    char CertNum[COMM_LEN];				    //<!-- 证书序列号（有证书的设备必选） -->
    char EndTime[TIME_LEN];				        //<!-- 证书终止有效期（有证书的设备必选） -->

    char IPAddress[IP_LEN];				//<!-- 设备/区域/系统IP地址（可选）-->
    unsigned int port;					//<!-- 设备/区域/系统端口（可选）-->
    char Password[COMM_LEN];				         //<!-- 设备口令（可选）-->
    bool Status;						//<!-- 设备状态（必选） --> //设备状态 0:OFF  1:ON *


    unsigned int RegisterWay;			   //<!-- 注册方式（必选）缺省为1； 1： 符合sip3261标准的认证注册模式；2： 基于口令的双向认证注册模式；3： 基于数字证书的双向认证注册模式-->
    bool Secrecy;						                //保密属性 0不涉密 1涉密 *

    unsigned int SafetyWay;					  //<!-- 信令安全模式（可选）缺省为0； 0：不采用；2：S/MIME签名方式；3：S/MIME加密签名同时采用方式；4：数字摘要方式-->
    unsigned int Certifiable;					//有证书时必选 证书有效标志 0无效 1有效
    unsigned int ErrCode;				        //有证书且该证书无效的设备必选 证书无效原因码

	//扩展字段
    unsigned int PtzType;						//摄像机类型  当目录项为摄像机时可选
    unsigned int PositionType;					//摄像机位置类型，当目录项为摄像机时可选
    unsigned int RoomType;						//室内属性 1:室外 2:室内 缺省为1，当目录项为摄像机时可选
    unsigned int UseType;						//用途属性 1:治安 2:交通 3:重点，当目录项为摄像机时可选
    unsigned int SUpplyLightType;				         //补光属性 1:无补光 2:红外补光 3:白光补光，当目录项为摄像机时可选
    unsigned int DirectionType;				        //方位属性 1:东 2:西 3:南 4:北 5:东南 6:东北 7:西南 8:西北
    unsigned int SvcSpaceSUpportMode;			//空域编码能力，取值0：不支持； 1：1级增强（1个增强层）； 2：2级增强（2个增强层）； 3：3级增强（3个增强层）
    unsigned int SvcTimeSUpportMode;			        //时域编码能力，取值0：不支持； 1：1级增强； 2：2级增强； 3：3级增强
	
};

struct DeviceCatalogList
{
    char   GBCode[GB_ID_LEN];	//目标设备/区域/系统的编码 取值与目录查询请求相同 *
    unsigned int  sum;			//传输的目录总数 *
    CatalogInfo  *catalog;	       //动态分配的目录数组的头指针
};


//预置位参数
struct PresetParam
{
    char  PresetID[STR_LEN];//预置位编码
    char  PresetName[STR_LEN];//预置位名称
};

//预置位信息
struct PresetInfo
{
    char                DeviceID[GB_ID_LEN];//查询目标设备/区域/系统的编码
    unsigned int    PersetListNum;	//预置位的数量
    PresetParam *PresetList;//预置位信息的数组头指针
};

//故障设备参数
struct FaultDevice
{
   char DeviceID[GB_ID_LEN];
   FaultDevice()
   {
	   memset(DeviceID, 0, sizeof(DeviceID));
   }
};

struct KeepaliveInfo
{
    bool status;
    unsigned int  Num;	                 //故障设备数量
    FaultDevice*  fault_device;	//故障设备数组头指针
};


//设备状态
enum Status
{
      kOffDuty,
      kOnDuty,
      kAlarm

};

//设备报警状态
struct AlarmStatus
{
    char    GBCode[GB_ID_LEN];	                   //报警设备编码
    Status status;				//报警设备状态
};

//媒体状态通知信息
struct StruMediaNotifyInfo
{
    StreamHandle  handle;		          //对应的流句柄
    char  GBCode[GB_ID_LEN];		  //媒体发送者ID
    unsigned int NotifyType;			  //通知事件类型，取值“1”表示历史媒体文件发送结束
};

//广播通知信息
struct BroadcastInfo
{
    char  SourceID[GB_ID_LEN];	//语音输入设备编码
    char  TargetID[GB_ID_LEN];	//语音输出设备编码
};

//设备状态
struct DeviceStatus
{
    char  GBCode[GB_ID_LEN];				       //目标设备/区域/系统的编码 *
    char  DevDateTime[TIME_LEN];		                       //设备时间和日期
    char  ErrReason[STR_LEN];		                       //不正常工作原因
    bool  Result;						               //查询结果标志 *
    bool  Encode;						               //是否编码
    bool  Record;						               //是否录像
    bool  OnLine;						               //是否在线 *
    bool  StatusOK;						       //是否正常工作 *

    unsigned int    AlarmNum;				       //表示一共有几个报警状态数量
    AlarmStatus * Alarm_Status;					//表示报警状态数组的头指针
};

//录像视频 下列参数同时为查询条件和回复参数
struct RecordParam
{
    char  DeviceID[GB_ID_LEN];				             //<!-- 目录设备/安全防范视频监控联网系统/区域编码（必选） -->
    char  StartTime[TIME_LEN];				              //<!-- 录像起始时间（可选）空表示不限 -->
    char  EndTime[TIME_LEN];				              //<!-- 增加录像终止时间（可选）空表示到当前时间-->
    char  FilePath[ROUTE_LEN];				             //<!-- 文件路径名 （可选）-->
    char  Address[ROUTE_LEN];				            //<!-- 录像地址（可选 支持不完全查询） -->
    int    Secrecy;						               //<!-- 保密属性（可选）缺省为0；0：不涉密，1：涉密-->
    char  Type[TYPE_LEN];					          //<!-- 录像产生类型（可选）time或alarm或manual或all -->
    char  RecorderID[GB_ID_LEN];			            //<!-- 录像触发者ID（可选）-->
	char  Name[COMM_LEN];                               //设备名字
     unsigned int   IndistinctQuery;			        //缺省为0  0:不进行模糊查询  1:进行模糊查询
};


//音视频索引信息
struct RecordIndex
{
    char                 GBCode[GB_ID_LEN];	          //目标设备/系统的编码 *
    unsigned int     Num;			                 //查询结果总数 *
    RecordParam  *record_list;	                         //动态分配的目录项数组首地址
};

enum GBMediaType
{
    kGBVideo = 1,	         //视频
    kGBAudio		        //音频
};

//流传输方式
enum NetTransType
{
    kRtpOverUdp,
	kRtpOverTcp,
    kRtpOverTcpActive,    /*主动发起TCP连接，作为TCP client*/
	kRtpOverTcpPassive    /*作为TCP server*/
};

enum StreamRequestType
{
      kLiveStream,
      kPlayback,
      kDownload,
      kAudioStream,
      kStopStream
};

//告警方式
enum AlarmMethod
{
	kGB_ALARM_METHOD_BEGIN = 0x00,
	kGB_ALARM_METHOD_PHONE,			//电话告警
	kGB_ALARM_METHOD_DEVICE,		//设备告警，默认方式
	kGB_ALARM_METHOD_SHORT_MESSAGE,	//短信告警
	kGB_ALARM_METHOD_GPS,			//GPS告警
	kGB_ALARM_METHOD_VIDEO,			//视频告警
	kGB_ALARM_METHOD_DEFECT,		//故障告警
	kGB_ALARM_METHOD_OTHER,			//其它告警
};

//告警方式为设备告警时
enum AlarmTypeDevice
{
	kGB_ALARM_TYPE_DEVICE_BEGIN = 0x00,
	kGB_ALARM_TYPE_DEVICE_VIDEO_LOSS,		//视频丢失
	kGB_ALARM_TYPE_DEVICE_DISMANTLE,		//设备防拆
	kGB_ALARM_TYPE_DEVICE_DISK_FULL,		//存储设备磁盘满
	kGB_ALARM_TYPE_DEVICE_HIGH_TEMPERATURE,	//高温
	kGB_ALARM_TYPE_DEVICE_LOW_TEMPERATURE,	//低温
};


//告警方式为视频告警时
enum AlarmTypeVideo
{
	kGB_ALARM_TYPE_VIDEO_BEGIN = 0x00,
	kGB_ALARM_TYPE_VIDEO_ARTIFICIAL_VIDEO,	//人工视频报警
	kGB_ALARM_TYPE_VIDEO_MOTION,			//运动目标检测（移动侦测）
	kGB_ALARM_TYPE_VIDEO_REMAIN,			//物品遗留
	kGB_ALARM_TYPE_VIDEO_LOST,				//物品移除
	kGB_ALARM_TYPE_VIDEO_TRIP,				//绊线入侵
	kGB_ALARM_TYPE_VIDEO_PEA_PERIMETER,		//入侵检测
	kGB_ALARM_TYPE_VIDEO_RETROGRADE,		//逆行检测报警
	kGB_ALARM_TYPE_VIDEO_HOVER,				//徘徊检测报警
	kGB_ALARM_TYPE_VIDEO_FLOW,				//流量统计
	kGB_ALARM_TYPE_VIDEO_DENSITY,			//密度检测
	kGB_ALARM_TYPE_VIDEO_ABNORMAL,			//视频异常
	kGB_ALARM_TYPE_VIDEO_RAPID_MOVEMENT,	//快速移动
};

//RTP编码参数
struct RtpMap
{
    char MimeType[CODE_LEN];                         //编码名称 如PS/H264/MPEG4
    unsigned int MediaFormat;				   //负载类型 指例子中的 96
    unsigned int SampleRate;				    //时钟频率 指例子中的 90000
};


//RTP音视频封装参数的描述
struct RtpDescriptopn
{
    GBMediaType                 type;				                            //描述类型 video/audio
    unsigned int               DescriNum;						        //音视频描述数
    RtpMap*                   mapDescri;	                               //具体的音视频描述
};

//请求的媒体信息
/*回放和下载请求需要加上开始和结束时间戳*/
struct MediaInfo
{
    NetTransType RtpType;			               //传输方式
    char DeviceID[GB_ID_LEN];	                        //点流目标设备ID
    int  StreamNum;                                            //
    char IP[IP_LEN];		                        //本地用于RTP流传输的IP地址
    unsigned int Port;					    //本地用于RTP流传输的端口号
    char Url[URI_LEN];			                    //媒体源标志，对应音视频URL
	char Ssrc[SSRC_LEN];                           // SSRC 值
    char MediaF[STR_LEN];                         // SDP f=

    StreamRequestType RequestType;		      //请求类型填入Play/Playback/Download/Talk等
    RtpDescriptopn  RtpDescri;			   //音视频RTP参数的描述

//playback
    uint64_t StartTime;						//开始UNIX时间戳
    uint64_t EndTime;						//结束UNIX时间戳

//Download
    unsigned int DownloadSpeed;		//下载速度
    uint64_t      FileSize;					//表示文件大小参数，用于下载时的进度进度
	
};

//报警事件通知设置信息
struct AlarmNotifyInfo
{
    char    AlarmID[GB_ID_LEN];				//<!-- 报警中心编码（必选）-->
    char    DeviceID[GB_ID_LEN];			        ////<!-- 报警设备编码（必选）-->

    unsigned int    AlarmPriority;			        //<!-- 报警级别（必选），1为一级警情，2为二级警情，3为三级警情，4为四级警情-->
    unsigned int    AlarmMethod;			//<!-- 报警方式（必选），取值1为电话报警，2为设备报警，3为短信报警，4为GPS报警，5为视频报警，6为设备故障报警，7其它报警-->
	unsigned int    AlarmType;			//<!-- 具体报警类型 ，参考AlarmTypeDevice和 AlarmTypeVideo-->
	unsigned int    AlarmState;         //报警状态，1-开始，0-结束

    char    AlarmTime[TIME_LEN];			        //<!--报警时间（必选）-->
    char    AlarmDescription[DESCRI_LEN];		//<!--报警内容描述（可选）-->
    char    ExtendInfo[EXTEND_LEN];
    double Longitude;				                //<!-- 经纬度信息可选 -->
    double Latitude;			                        //<!-- 经纬度信息可选 -->
};

//报警订阅信息
struct AlarmSubcribeInfo
{
    char  DeivceID[GB_ID_LEN];			//订阅目标设备/区域/系统的编码*
    char  StartTime[TIME_LEN];			//报警发生开始时间
    char  EndTime[TIME_LEN];			//报警发生结束时间

    unsigned int   StartPriority;	//报警起始级别（可选），0为全部，1为一级警情，2为二级警情，3为三级警情，4为四级警情
    unsigned int  EndPriority;		//报警终止级别（可选），0为全部，1为一级警情，2为二级警情，3为三级警情，4为四级警情

    unsigned int   Method;			      //报警方式条件（可选），取值0为全部，1为电话报警，2为设备报警，3为短信报警，4为GPS报警，
												//5为视频报警，6为设备故障报警，7其他报警；可以为直接组合如12为电话报警或设备报警

    unsigned int  AlarmType;		//报警类型

    unsigned int SubID;	            //订阅ID 大于0，用于标识不同订阅*
    unsigned int Expires;	        //订阅有效期 单位：秒*
};

//目录订阅消息
struct CatalogSubcribeInfo
{
	char  DeivceID[GB_ID_LEN];			                           //订阅目标设备/区域/系统的编码*
    CatalogQueryParam   param;
    unsigned int Expires;					                      //订阅持续时间 单位：秒*
    unsigned int ID;						                      //订阅ID 大于0，用于标识不同订阅*
};

//移动设备位置消息订阅
struct MobilePositionSubInfo
{
    char     DeivceID[GB_ID_LEN];	        //移动设备编码 *
    unsigned int Interval;			//移动设备位置信息上报时间间隔 单位：秒  默认值5（选填）
    unsigned int Expires;			//订阅持续时间 单位：秒 *
    unsigned int SubID;			    //订阅ID 大于0，用于标识不同订阅 *
};

//移动设备位置通知
struct MobilePositionInfo
{
    char  GBCode[GB_ID_LEN];		             //移动设备编码 *
    char  DateTime[TIME_LEN];	              //产生通知时间 *
    double Longitude;					 //经度 *
    double Latitude;					     //纬度 *
    double Speed;						 //速度，单位: km/h
    double Direction;					     //方向，取值为当前摄像头方向与正北方的顺时针夹角，取值范围0~360，单位°
    int       Altitude;						 //海拔高度，单位：m
};

enum PTZType
{
	//PTZ命令
    kPtzUp = 0,		        //向上
    kPtzDown,			//向下
    kPtzLeft,			//向左
    kPtzRight,			//向右
    kPtzUpLeft,			//向左上
    kPtzUpRight,			//向右上
    kPtzDownLeft,	        //向左下
    kPtzDownRight,		//向右下
    kPtzZoomIn,			//放大
    kPtzZoomOut,			//缩小

	//FI命令
    kPtzFocusNear,		//聚焦近
    kPtzFocusFar,			//聚焦远
    kPtzIRISOpen,		//光圈放大
    kPtzIRISClose,	        //光圈缩小

	//预置位命令
    kPtzPosAdd,			//添加预置位
    kPtzPosCall,			//调用预置位
    kPtzPosDel,			//删除预置位

	//巡航控制命令
    kPtzCruAdd,			//添加巡航点
    kPtzCruDel,			//删除一个巡航点
    kPtzCruSpeed,			//设置巡航速度
    kPtzCruTime,			//设置巡航停留时间
    kPtzCruStart,			//开始巡航

	//扫描命令
    kPtzScanStart,		//开始自动扫描
    kPtzScanLeft,			//设置自动扫描左边界
    kPtzScanRight,		//设置自动扫描右边界
    kPtzScanSet,			//设置自动扫描速度

	//辅助开关控制指令
    kPtzAidOpen,			//开关开
    kPtzAidClose,			//开关关
	
    kPtzStop,				//停止操作

    kPtzUnkonw = 999
};

//配置类型
enum ConfigType
{
    kUnknow,                  //未知配置
    kBasicParam,            //基本参数配置
    kVideoParamOpt,       //视频参数配置范围
    kSVACEncodeConfig,  //SVAC编码配置
    kSVACDecodeConfig,  //SVAC解码配置
    kOsdConfig,         //前端OSD配置
    kVideoParamAttribute //GB/T 28181-2022 视频参数属性
};

#define MAX_CONFIG_TYPE_NUM 8
#define VIDEO_PARAM_TEXT_LEN 32

//配置查询条件描述
struct ConfigDownloadQuery
{
    char                  DeviceID[GB_ID_LEN];
    unsigned int       Num;						                //想要查询的配置类型的数量
    ConfigType         Type[MAX_CONFIG_TYPE_NUM];	                               //具体的配置类型
};

//用于回答查询请求的基础配置参数结构体
struct CfgBasicParam
{
    char  DeviceName[GB_ID_LEN];		//设备名称
    unsigned int Expiration;				//注册周期
    unsigned int HeartBeatInterval;		//心跳时间间隔
    unsigned int HeartBeatcount;			//心跳超时次数
    unsigned int PosCapability;			//定位功能支持情况 0:不支持 1:支持GPS定位 2:支持北斗定位
    double Longitude;						//经度
    double Latitude;						//纬度
};

//基础配置参数的类型
enum BasicParamType
{
        kUnknowBasic,
        kNameBasic,
        kExpireBasic,
        kHeartBeatInternalBasic,
        kHeartBeatCountBasic
};

//基础配置参数的联合体
union  UnionBasicParam
{
    char  DeviceName[GB_ID_LEN];		//设备名称
    unsigned int Expiration;				//注册周期
    unsigned int HeartBeatInterval;		//心跳时间间隔
    unsigned int HeartBeatcount;			//心跳超时次数
};

//要设置的基础配置参数的具体描述
struct BasicParamToSet
{
    BasicParamType   ParmaType;
	UnionBasicParam unionParam;
};

#define MAX_BASIC_SETTING_NUM 4

//要设置的基础配置参数
struct BasicSetting
{
	char  DeviceName[GB_ID_LEN];		//设备名称  
	unsigned int Expiration;				//注册周期
	unsigned int HeartBeatInterval;		//心跳时间间隔
	unsigned int HeartBeatcount;			//心跳超时次数
};

//视频参数配置范围 每个范围的各可选参数以"/"分隔
struct CfgVideoParamOpt
{
    char  DownloadSpeedOpt;		//视频下载速度可选范围,各可选参数以"/"分隔
    char  Resolution[52];				//摄像机支持分辨率范围,各可选参数以"/"分隔
    char  ImageFlip[32];               //???????????
};

struct CfgVideoParamAttribute
{
    unsigned int StreamNumber;                    //码流序号
    char VideoFormat[VIDEO_PARAM_TEXT_LEN];       //视频编码格式
    char Resolution[VIDEO_PARAM_TEXT_LEN];        //分辨率
    unsigned int FrameRate;                       //帧率
    unsigned int BitRateType;                     //码率类型
    unsigned int VideoBitRate;                    //视频码率
};

struct VideoParamAttributeSetting
{
    unsigned int StreamNumber;                    //码流序号
    char VideoFormat[VIDEO_PARAM_TEXT_LEN];       //视频编码格式
    char Resolution[VIDEO_PARAM_TEXT_LEN];        //分辨率
    unsigned int FrameRate;                       //帧率
    unsigned int BitRateType;                     //码率类型
    unsigned int VideoBitRate;                    //视频码率
};

//?????????????
struct ImageSetting
{
    char FlipMode[32];                 //???????????
};

#define MAX_OSD_TEXT_NUM 8
#define OSD_TEXT_LEN 64

struct OsdTextItem
{
    char Text[OSD_TEXT_LEN];
    unsigned int X;
    unsigned int Y;
};

struct CfgOsdConfig
{
    unsigned int Length;
    unsigned int Width;
    unsigned int TimeX;
    unsigned int TimeY;
    unsigned int TimeEnable;
    unsigned int TimeType;
    unsigned int TextEnable;
    unsigned int SumNum;
    OsdTextItem Item[MAX_OSD_TEXT_NUM];
};

struct OsdSetting
{
    unsigned int Length;
    unsigned int Width;
    unsigned int TimeX;
    unsigned int TimeY;
    unsigned int TimeEnable;
    unsigned int TimeType;
    unsigned int TextEnable;
    unsigned int SumNum;
    OsdTextItem Item[MAX_OSD_TEXT_NUM];
};

//?????????????
struct ROIDescri
{
    unsigned int ROISeq;		//感兴趣区域编号
    unsigned int TopLeft;		//感兴趣区域左上角坐标 0-19683
    unsigned int BottomRight;	//感兴趣区域右下角坐标 0-19683
    unsigned int ROIQP;		//ROI区域编码质量等级 0:一般 1:较好 2:好 3:很好
};

//感兴趣区域参数配置
struct ROIParam
{
	unsigned int ROIFlag;						//感兴趣区域开关  0：关闭，1：打开
    unsigned int ROINum;						//感兴趣区域数量
    ROIDescri*    aryParam;		                //具体的感兴趣区域描述
    unsigned int BackGroundQP;					//背景区域编码质量等级 0:一般 1:较好 2:好 3:很好
    unsigned int BackGroundSkipFlag;			//背景跳过开关  0:关闭  1:打开
};

//SVC编码参数
struct SVCEncodeParam
{ 
    unsigned int SVCSpaceDomainMode;	//空域编码方式 *
    unsigned int SVCTimeDoaminMode;	//时域编码方式 *
    unsigned int SVCSpaceSUpportMode;	//空域编码能力 *
    unsigned int SVCTimeSUpportMode;	//时域编码能力 *
};

//SVC解码参数
struct SVCDecodeParam
{
    unsigned int SVCSpaceSUpportMode;	//空域编码能力 *
    unsigned int SVCTimeSUpportMode;	//时域编码能力 *
};

//监控专用信息参数类型
enum SurveilType
{	
        kUnknowSurveil,
        kTimeFlagSurveil,
        kEventFlagSurveil,
        kAlarmFalgSurveil
};

//监控专用信息参数结构体
struct SurveillanceParam
{
    unsigned int TimeShowFlag;		//绝对时间信息开关  0:关闭  1:打开 *
    unsigned int EventShowFlag;	//监控事件信息开关  0:关闭  1:打开 *
    unsigned int AlertShowFlag;	//报警信息开关	0:关闭  1:打开 *
};

//音频参数结构体
struct AudioParam
{
	unsigned int AudioRecognitionFlag;  //声音识别特征参数开关  0：关闭 1：打开
};

//监控专用信息参数的联合体
typedef union
{
    unsigned int TimeFlag;		//绝对时间信息开关  0:关闭  1:打开
    unsigned int EventFlag;	//监控事件信息开关  0:关闭  1:打开
    unsigned int AlertFlag;	//报警信息开关	0:关闭  1:打开
}UnionSurveilParam;

//每一项要配置的监控专用参数
struct SurveilDescri
{
        SurveilType eSurveilType;
	UnionSurveilParam unionParam;
};

#define MAX_SURVEILPARAM_NUM 3

//要配置的监控专用信息参数
struct SurveillanceSetting
{
    unsigned int ParamNum;			//参数总数
    SurveilDescri aryParamDescri[MAX_SURVEILPARAM_NUM];
};


//回复查询请求的SVAC编码配置参数
struct CfgSVACEncode
{
    ROIParam stuRoiParam;					//感兴趣区域参数配置
    SVCEncodeParam stuSvcParam;				//SVC参数
    SurveillanceParam stuSurveilParam;		                //监控专用信息参数
    AudioParam AudioRecognitionFlag;			//音频参数
};

//要设置的SVAC编码配置参数
struct SVACEncodeSetting
{
	ROIParam stuRoiParam;				//感兴趣区域参数设置
	SVCEncodeParam stuSvcParam;				//SVC参数
	SurveillanceParam stuSurveilParam;		                //监控专用信息参数
	AudioParam AudioRecognitionParam;			//音频参数
};

//回复查询请求的SVAC解码配置参数
struct CfgSVACDecode
{
    SVCDecodeParam stuSvcParam;				//SVC参数
    SurveillanceParam stuSurveilParam;		               //监控专用信息参数
};

//要设置的SVAC解码配置参数
struct SVACDecodeSetting
{
	SVCDecodeParam stuSvcParam;				//SVC参数
	SurveillanceParam stuSurveilParam;		               //监控专用信息参数
};

//配置查询结果的联合体
union  UnionConfigParam
{
        CfgBasicParam      CfgBasic;				//基本参数配置
        CfgVideoParamOpt   CfgVideoOpt;		//视频参数配置范围
        CfgSVACDecode     CfgDecode;				//SVAC解码配置
        CfgSVACEncode     CfgEncode;				//SVAC编码配置
        CfgOsdConfig      CfgOsd;                //前端OSD配置
        CfgVideoParamAttribute CfgVideoAttr;     //GB/T 28181-2022 视频参数属性
};



//配置设置参数的联合体
union UnionSettingParam
{
    BasicSetting              Basic;			            //基本参数设置
    SVACDecodeSetting   DecodeSetting;		    //SVAC解码设置
    SVACEncodeSetting    EncodeSetting;		    //SVAC编码设置
    ImageSetting            Image;                  //???????????
    OsdSetting              Osd;                    //前端OSD设置
    VideoParamAttributeSetting VideoAttr;          //GB/T 28181-2022 视频参数属性设置
};


//设备的配置参数
struct ConfigParam
{
     ConfigType CfgType;
     UnionConfigParam UnionCfgParam;
};

//要设置的配置项类型
enum SettingType
{
    kUnknowSetting,
    kBasicSetting,
    kEncodeSetting,
    kDecodeSetting,
    kImageSetting,
    kOsdSetting,
    kVideoParamAttributeSetting
};

//进行设备配置设置的参数
struct SettingParam
{
    SettingType SetType;
    UnionSettingParam unionSetParam;
};

//query response
struct DeviceConfigDownload
{
    char GBCode[GB_ID_LEN];
    bool  Result;								           //结果是否正常 *
    unsigned int Num;							            //配置数目
    ConfigParam* CfgParam;	                                           //具体的配置参数
};

//设备配置设置参数
struct ConfigSetParam
{
    unsigned int Num;
    SettingParam* arySetParam;
};

//云台控制命令参数
struct PTZSpeed
{
    unsigned int SpeedPan;			//水平速度，范围0-256
    unsigned int SpeedTilt;		//垂直速度，范围0-256
};

//巡航控制命令参数
struct Cruparam
{
    unsigned int CruID;			//巡航组编号，范围0-256

    unsigned int PosID;			//预置位编号，范围0-256
                                    //当操作类型为kPtzCruDel(删除巡航)，设置预置位编码为0，则会删除该巡航住所有的预置位

    unsigned int SpdOrTime;		//巡航速度或巡航停留时间(秒)，范围0-15
};

//扫描控制命令参数
struct ScanParam
{
    unsigned int ScanID;			//扫描组编号，范围0-256
    unsigned int BorderID;			//边界编号，范围0-256
    unsigned int Speed;			//扫描速度，范围0-15
};

//拉框控制命令参数
struct ZoomCmd
{
    bool in;
    int Length;		//播放窗口长度像素值 *
    int Width;		//播放窗口宽度像素值 *
    int MidPointX;	//拉框中心的横轴坐标像素值 *
    int MidPointY;	//拉框中心的纵轴坐标像素值 *
    int LengthX;		//拉框长度像素值 *
    int LengthY;		//拉框宽度像素值 *
};


//控制命令联合体
union PtzParam
{
    unsigned int    speedZoom;		//变倍控制速度，范围0-15
    unsigned int    speedIris;		//光圈速度，范围0-256
    unsigned int    speedFocus;		//聚焦速度，范围0-256
    unsigned int    posNo;			//预置位编号，范围0-256
    unsigned int    aidNo;			//辅助开关编码
    PTZSpeed        ptzSpeed;		//摄像头移动速度，范围
    Cruparam        cruparam;		//巡航命令参数
    ScanParam      scanParam;          //扫描命令参
};


//控制命令参数
struct PtzCmd
{
    PTZType                      cmdType;			//操作类型
    PtzParam                     param;		                //详细命令参数
    unsigned int                 ctrlType;			//0:停止 1:开始
    unsigned int                 address;
};

//播放控制类型
enum PlayCtrlType
{
    kPlayPause,		//暂停
    kPlayStart ,	     //播放
    kPlayFast,			//快放
    kPlaySlow,			//慢放
    kPlayDarg,			//随机拖动
    kPlayStop			//????
};

//播放控制命令
struct PlayCtrlCmd
{
    char GBCode[GB_ID_LEN];
    PlayCtrlType Type;		        //控制类型
    float           Scale;			//快进、慢放的倍数
    unsigned int Npt;		        //smpte相对时间戳
};

struct HomePositionCmd
{
    unsigned int enable;		//看守位使能  1:开启  0:关闭 *
    unsigned int resetTime;	//自动归位时间间隔，开启看守位时使用，单位:秒
    unsigned int presetIndex;	//调用预置位编号，开启看守位时使用，取值0-255
};

struct DeviceUpgradeCmd
{
    char Firmware[STR_LEN];
    char FileURL[URI_LEN * 2];
    char Manufacturer[STR_LEN];
    char SessionID[STR_LEN];
    unsigned int FileSize;
    char Checksum[STR_LEN];
    unsigned int ForceUpgrade;
};


enum QueryType
{
     kUnknowQuery,
     kDeviceInfoQuery,
     kDeviceStatusQuery,
     kDeviceCatalogQuery,
     kDeviceRecordQuery,
     kDeviceConfigQuery,
     kDevicePresetQuery
};

enum ControlType
{
    kUnknowControl,
    kPtzControl,
    kTeleBootControl,
    kDeviceUpgradeControl,
    kDragZoomInControl,
    kDragZoomOutControl,
    kIFameControl = 10,
    kRecordControl,
    kGurdControl,
    kAlarmResetControl,
    kDevConfigControl,
    kHomePositionControl
};


union  UnionQueryDescri
{
        RecordParam                record_index;
        TimeInterval               catalog_period;
        ConfigDownloadQuery        config_param;
};


struct QueryParam
{
        UnionQueryDescri   query_descri;
        QueryType     type;
        char             GBCode[GB_ID_LEN];
};

union  UnionDevControl
{
        bool                         gurd_cmd;
        bool                         record_cmd;
        AlarmResetInfo          alarm_reset_cmd;
        HomePositionCmd      homeposition_cmd;
        DeviceUpgradeCmd    device_upgrade_cmd;
        PtzCmd                     ptz_cmd;
        ZoomCmd                  zoom_cmd;
		ConfigSetParam          config_set_param;
};


struct DevControlCmd
{
        UnionDevControl   control_param;
        char              GBCode[GB_ID_LEN];
        ControlType       type;
};

struct MediaStatusNotify
{
	StreamHandle      handle;
	int               type;
};


union UnionNotifyMessage
{
    KeepaliveInfo            keepalive;
    AlarmNotifyInfo          alarm_notify;
    MobilePositionInfo       moblie_notify;
    DeviceCatalogList        catalog_list;
	MediaStatusNotify        media_status;
	BroadcastInfo            broadcast_info;
};


enum NotifyType
{
        kUnkonwNotify,
        kKeepaliveNotify,
        kMediaStatusNotify,
        kAlarmNotify,
        kAlarmForSubNotify,
        kCatalogNotify,
        kMobilePositionNotify,
        kBroadcastNotify,
		kSubscribeExpireNotify,
 };

struct NotifyInfo
{
        UnionNotifyMessage    notify_message;
        char                  GBCode[GB_ID_LEN];
        NotifyType            type;
};



enum SubscribeType
{
     kCatalogSubscribe,
     kAlarmSubscribe,
     kMobilePositionSubscribe,
     kCatalogSubscribeExpire,
	 kAlarmSubscribeExpire,
	 kMobilePositionSubscribeExpire,
 };

//目录更新事件
#define EVENT_NONE			"NONE"
#define EVENT_ADD			"ADD"
#define EVENT_DEL			"DEL"
#define EVENT_UPDATE		"UPDATE"		//更新
#define EVENT_ON			"ON"			//上线
#define EVENT_OFF			"OFF"			//下线
#define EVENT_VLOST			"VLOST"			//视频丢失
#define EVENT_DEFECT		"DEFECT"		//故障


class GBClientReceiver
{
   public:
   GBClientReceiver(){}
   virtual ~GBClientReceiver(){}

   virtual bool OnDeviceControl(ResponseHandle handle , const DevControlCmd* cmd) {  return  true;}
   virtual bool OnQuery( ResponseHandle  handle, const QueryParam* param ){return true;}
   virtual bool OnNotify(NotifyType type,  const char* gb_code,  void* info  ){return true;}
   virtual bool OnBroadcastResponse(const char* gb_code, void* info, bool ok) { return true; }
   virtual bool OnSubscribe(SubscribeHandle handle, SubscribeType type,  const char* gb_code,  void* info  ){return true;}
   virtual bool OnStreamRequest(StreamHandle handle, const char* gb_code, StreamRequestType type, const MediaInfo* input  ){return true;}
   virtual bool OnPlayControl(StreamHandle handle , const PlayCtrlCmd* cmd){return true;}
   virtual bool OnQueryResponse( QueryType type, const char* gb_code, void* msg ){return true;}
};


class GBServerReceiver
{
   public:
   GBServerReceiver(){}
   virtual ~GBServerReceiver(){}

   virtual bool OnRegist(SessionHandle handle, const GBRegistParam* param, const ConnectParam* connect ){return true;}
   virtual bool OnQueryResponse( QueryType type, const char* gb_code, void* msg ){return true;}
   virtual bool OnNotify(NotifyType type,  const char* gb_code,  void* info  ){return true;}
   virtual bool OnQuery( ResponseHandle  handle, const QueryParam* param ){return true;}
   virtual bool OnStreamRequest(StreamHandle handle, const char* gb_code, StreamRequestType type, const MediaInfo* input  ){return true;}
};

#endif // End GB28181DEFS_DEF_H
