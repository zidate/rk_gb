/************************************************************
Copyright (C), 2017-2021, CMIOT.
FileName: cmiot_define.h
Description: sdk 数据结构定义
***********************************************************/

#ifndef __CMIOT_DEFINE_H__
#define __CMIOT_DEFINE_H__

#ifdef __cplusplus
  extern "C" {
#endif

#define CMIOT_MAX_PATH_LEN                    256     /* 最大路径长度 */
#define CMIOT_MAX_MAC_LEN                     32      /* MAC地址最大长度 */
#define CMIOT_MAX_DEV_ID_LEN                  64      /* deviceId最大长度 */
#define CMIOT_MAX_ACCESS_KEY_LEN              64      /* key最大长度 */
#define CMIOT_MAX_ACCESS_SECRET_LEN           64      /* secret最大长度 */
#define CMIOT_MAX_MOD_ID_LEN                  64      /* modelId最大长度 */
#define CMIOT_MAX_VERSION_LEN                 32      /* 版本号最大长度 */
#define CMIOT_MAX_SCHEDULE_TIME_LEN           8       /* 计划任务时间最大长度 */
#define CMIOT_MAX_URL_LEN                     256     /* URL最大长度 */
#define CMIOT_MAX_WIFI_SSID_LEN               128     /* wifi ssid最大长度 */
#define CMIOT_MAX_WIFI_BSSID_LEN              128     /* wifi bssid最大长度 */
#define CMIOT_MAX_WIFI_ENCRYPTION_LEN         16      /* wifi加密方式最大长度 */
#define CMIOT_MAX_WIFI_KEY_LEN                128     /* wifi密码最大长度 */
#define CMIOT_MAX_SD_PART_NAME_LEN            16      /* sd卡分区名称最大长度 */
#define CMIOT_MAX_SD_PART_FORMAT_LEN          16      /* sd卡分区格式最大长度 */
#define CMIOT_MAX_AUDIO_ID_LEN                64      /* 音频id最大长度 */
#define CMIOT_MAX_MD5_LEN                     64      /* MD5校验码最大长度 */
#define CMIOT_MAX_VENDOR_INFO_LEN             16      /* 厂商信息最大长度 */
#define CMIOT_MAX_PRODUCT_MOD_LEN             16      /* 产品型号型号信息最大长度 */
#define CMIOT_MAX_OSD_CONTENT_LEN             128     /* OSD文本内容最大长度 */
#define CMIOT_SOUND_LIGHT_ALARM_CFG_NEW_MAX   5       /* 新声光告警配置支持最大组数 */


#define CMIOT_FALSE  0        /* 基础类型定义 */
#define CMIOT_TRUE   1        /* 基础类型定义 */

typedef char                    cmiot_bool_t;       /* 基础类型定义 */
typedef unsigned char           cmiot_uint8_t;     /* 基础类型定义 */
typedef char                    cmiot_int8_t;      /* 基础类型定义 */
typedef unsigned short          cmiot_uint16_t;     /* 基础类型定义 */
typedef short                   cmiot_int16_t;      /* 基础类型定义 */
typedef unsigned int            cmiot_uint32_t;     /* 基础类型定义 */
typedef int                     cmiot_int32_t;      /* 基础类型定义 */
typedef unsigned long long      cmiot_uint64_t;     /* 基础类型定义 */
typedef long long               cmiot_int64_t;      /* 基础类型定义 */
typedef char                    cmiot_char_t;       /* 基础类型定义 */
typedef unsigned char           cmiot_uchar_t;      /* 基础类型定义 */
typedef float                   cmiot_float_t;      /* 基础类型定义 */

/* 设备绑定类型 */
typedef enum {
    CMIOT_BIND_MODE_WIRED    = 1,   /* 支持有线绑定 */
    CMIOT_BIND_MODE_WIRELESS = 2,   /* 支持无线绑定（摄像头扫描APP生成的二维码 / AEC无感配网） */
    CMIOT_BIND_MODE_BOTH     = 3,   /* 同时支持有线绑定和无线绑定 */
} cmiotBindMode_e;

/* 多目摄像头流分辨率参数 */
typedef struct
{
    cmiot_int32_t streamId;             /* 流通道ID，从0开始 */
    cmiot_int32_t videoWidth;           /* 该通道流分辨率宽 */
    cmiot_int32_t videoHeight;          /* 该通道流分辨率高 */
} cmiotVideoReso_t;

/* 多目摄像头视频流参数 */
typedef struct
{
    cmiot_uint32_t num;              /* 多目摄像头流通道数，至少为2，至多为8 */
    cmiotVideoReso_t *reso;          /* 多目摄像头流分辨率 */
} cmiotMultiCamVideoReso_t;

/* 多目摄像头码流缓存参数 */
typedef struct
{
    cmiot_int32_t streamId;             /* 流通道ID，从0开始 */
    cmiot_int32_t streamSpace;          /* 码流缓存区大小，单位字节，至少1MByte */
} cmiotVideoStreamParam_t;

/* 多目摄像头码流缓存参数 */
typedef struct
{
    cmiot_uint32_t num;              /* 多目摄像头流通道数，至少为2，至多为8 */
    cmiotVideoStreamParam_t *stream; /* 多目摄像头流缓存参数 */
} cmiotMultiCamVideoStreamParam_t;

/* 音视频流相关参数 */
typedef struct
{
    cmiot_int32_t videoType;           /* 1: h264  2:h265，对于多目摄像头，多路流的视频编码格式保持一致 */
    cmiot_int32_t videoWidth;          /* 分辨率宽，单目摄像头以此参数为准 */
    cmiot_int32_t videoHeight;         /* 分辨率高，单目摄像头以此参数为准 */
    cmiot_int32_t audioType;           /* 1：G726（已废弃，不再支持），2：G711A，3：AAC，对于多目摄像头，多路流的音频编码格式保持一致 */
    cmiot_int32_t audioKhz;            /* 音频采样率例如采样率为8khz，此处值即为8，对于多目摄像头，多路流的音频采样率保持一致 */
    cmiot_int32_t audioChannel;        /* 音频声道数，对于多目摄像头，多路流的音频声道数保持一致 */
    cmiotMultiCamVideoReso_t multiCamReso;  /* 多目摄像头流分辨率参数，多目摄像头以此参数为准 */
    cmiotMultiCamVideoStreamParam_t multiCamStream;
    cmiot_int32_t multiCamPrimaryStreamId;   /* 设备类型为多目摄像头CMIOT_DEV_TYPE_MULTI_CAM时，此参数表示主要流ID，
                                                 配置检测区域等功能时默认展示该流ID的画面，其他设备类型该参数无效 */
} cmiotStreamParams_t;

/* 设备类型 */
typedef enum {
    CMIOT_DEV_TYPE_IPC,            /* 长上电摄像头 */
    CMIOT_DEV_TYPE_DOORBELL,       /* 门铃 */
    CMIOT_DEV_TYPE_NVR,            /* NVR */
    CMIOT_DEV_TYPE_LOWPOWER_CAM,   /* 低功耗摄像头 */
    CMIOT_DEV_TYPE_MULTI_CAM,      /* 多目摄像头 */
} cmiotDevType_e;

/* 设备固化信息 */
typedef struct
{
    cmiot_char_t wiredMac[CMIOT_MAX_MAC_LEN];         /* 有线网卡MAC地址，格式：54:F6:C5:98:75:FB，英文字符大写，不支持传空字符串 */
    cmiot_char_t wifiMac[CMIOT_MAX_MAC_LEN];          /* WiFi网卡MAC地址，格式：54:F6:C5:98:75:FB，英文字符大写，不支持传空字符串 */
    cmiot_char_t cellularMac[CMIOT_MAX_MAC_LEN];      /* 蜂窝网卡MAC地址，格式：54:F6:C5:98:75:FB，英文字符大写，不支持传空字符串 */
    cmiot_uint32_t ramSize;                           /* 内存总大小，单位兆比特（Mb） */
    cmiot_uint32_t flashSize;                         /* 闪存总大小，单位兆比特（Mb） */
} cmiotDevFixedInfo_t;

/* 设备初始化参数定义 */
typedef struct
{
    cmiot_char_t mac[CMIOT_MAX_MAC_LEN];                  /* 设备入库MAC地址，不带冒号，全小写 */
    cmiot_char_t devId[CMIOT_MAX_DEV_ID_LEN];             /* 设备device id，即IMEI/CMEI */
    cmiot_char_t key[CMIOT_MAX_ACCESS_KEY_LEN];           /* access key */
    cmiot_char_t secret[CMIOT_MAX_ACCESS_SECRET_LEN];     /* access secret */
    cmiot_char_t modelId[CMIOT_MAX_MOD_ID_LEN];           /* 型号 id */
    cmiot_char_t vendor[CMIOT_MAX_VENDOR_INFO_LEN];       /* 厂商信息 */
    cmiot_char_t productModel[CMIOT_MAX_PRODUCT_MOD_LEN]; /* 产品型号 */
    cmiot_char_t camAppVer[CMIOT_MAX_VERSION_LEN];        /* 应用程序版本 */
    cmiot_char_t fwVer[CMIOT_MAX_VERSION_LEN];            /* 固件版本 */
    cmiot_char_t configPath[CMIOT_MAX_PATH_LEN];          /* 配置文件路径 */
    cmiot_char_t sdCardPath[CMIOT_MAX_PATH_LEN];          /* SD卡路径，本地录像保存路径 */
    cmiot_char_t logPath[CMIOT_MAX_PATH_LEN];             /* 日志保存路径，如插入SD卡，则需将日志保存至SD卡，参考指导手册日志系统相关说明 */
    cmiot_int32_t streamSpace;                            /* 码流缓存区大小，至少4MByte，设备类型为多目摄像头CMIOT_DEV_TYPE_MULTI_CAM时无效 */
    cmiotStreamParams_t stream;                           /* 音视频流相关参数 */
    cmiotDevType_e devType;                               /* 设备类型 */
    cmiotBindMode_e bindMode;                             /* 设备支持的绑定类型 */
    cmiot_uint32_t logMaxSize;                            /* 单个日志文件的大小限制，单位KByte，默认值1024KB，最大不超过1024KB，若传入0，则使用默认值1024KB */
    cmiot_uint32_t logRotate;                             /* 缓存日志文件的个数（循环覆盖），至少为1，最大不超过80，若传入0，则使用默认值5 */
    cmiot_bool_t supportGAT1400;                          /* 是否支持1400协议 true-支持，false-不支持，若支持，通过CMIOT_RUNNING_STATUS_NOTIFY_1400_CONFIG回调下发零配置 */  
    cmiot_bool_t wakeupInit;                              /* 设备类型为低功耗类摄像头CMIOT_DEV_TYPE_LOWPOWER_CAM、CMIOT_DEV_TYPE_DOORBELL时，true-表示由模组唤醒启动 false-表示其他情况，用于休眠唤醒时快速上线 */
    cmiot_uint32_t idleBeforeSleep;                       /* 设备类型为低功耗类摄像头CMIOT_DEV_TYPE_LOWPOWER_CAM、CMIOT_DEV_TYPE_DOORBELL时该参数有效，
                                                             在（idleBeforeSleep * 5）秒无流传输任务后，SDK回调通知设备允许进入休眠，若为0，则使用默认值4，即20秒 */
    cmiot_bool_t externalSdRecord;                        /* 外部录制，设备自行录制sd本地录像，而无需SDK内部录制；true-外部录制，false-sdk内部录制。使用外部录制时，sdCardPath可为空，
                                                             同时需实现回调CMIOT_CMD_GET_DEV_SD_TIMELINE，CMIOT_CMD_GET_DEV_SD_CODED_FORMAT_BY_TIME，CMIOT_CMD_CONTROL_SD_REPLAY_STRAT，CMIOT_CMD_CONTROL_SD_REPLAY_END */
    cmiotDevFixedInfo_t devfixedInfo;                     /* 设备网卡MAC、内存闪存容量等固化信息 */
    cmiot_int32_t      capacityPercentLimit;            /* 触发设备循环存储限制，可以为限制容量.单位：MByte 范围:>=256MB, 也可以为限制百分比，范围：1-100*/
    cmiotStreamParams_t subStream;                         /* 子码流参数 */
} cmiotDevParams_t;

/* 视频帧类型 */
typedef enum {
    CMIOT_FRAME_TYPE_I,                  /* I帧，关键帧 */
    CMIOT_FRAME_TYPE_P,                  /* P帧 */
    CMIOT_FRAME_TYPE_B                   /* B帧 */
} cmiotFrameType_e;

/* 视频数据结构体 */
typedef struct
{
    cmiotFrameType_e frameType;      /* 帧类型 */
    cmiot_bool_t isSPSPPS;           /* 是否为sps/pps帧 */
    cmiot_char_t *data;              /* 视频数据 */
    cmiot_uint32_t dataSize;         /* 数据大小，单位：字节 */
    cmiot_uint32_t seqNum;           /* 帧序列号，溢出之后重新从0开始 */
    cmiot_uint32_t ts;               /* 视频时间戳，溢出之后重新从0开始，单位：毫秒 */
    cmiot_uint64_t utcms;            /* 当前UTC时间，单位：毫秒 */
} cmiotVideoData_t;

/* 音频数据结构体 */
typedef struct
{
    cmiot_char_t *data;             /* 音频数据 */
    cmiot_uint32_t dataSize;        /* 数据大小，单位：字节 */
    cmiot_uint32_t seqNum;          /* 帧序列号，溢出之后重新从0开始 */
    cmiot_uint32_t ts;              /* 音频时间戳，溢出之后重新从0开始，单位：毫秒，间隔20ms或40ms */
    cmiot_uint64_t utcms;           /* 当前UTC时间，单位：毫秒 */
} cmiotAudioData_t;

/* 图片数据结构体 */
typedef struct
{
    cmiot_uint32_t streamId;        /* 通道ID，从0开始*/
    cmiot_char_t *data;             /* 图片数据 */
    cmiot_uint32_t dataSize;        /* 数据大小，单位：字节 */
    cmiot_uint32_t ts;              /* 设备启动相对时间，单位：毫秒 */
    cmiot_uint64_t utcms;           /* 当前UTC时间，单位：毫秒 */
} cmiotPicData_t;


/* 设备固定能力集 CMIOT_TRUE-支持，CMIOT_FALSE-不支持 */
typedef struct {
    cmiot_bool_t wifi;            /* wifi */
    cmiot_bool_t ptz;             /* 云台基础功能（支持上、下、左、右移动） */
    cmiot_bool_t ptzEnhance;      /* 云台增强功能（支持左上、左下、右上、右下移动） */
    cmiot_bool_t ptzSpeed;        /* 云台速度控制 */
    cmiot_bool_t microphone;      /* 麦克风 */
    cmiot_bool_t fisheye;         /* 鱼眼摄像头 */
    cmiot_bool_t speaker;         /* 扬声器 */
    cmiot_bool_t cellular;        /* 蜂窝网络，4G、5G等 */
} cmiotFixedCapability_t;

/* 设备可变能力集 CMIOT_TRUE-支持，CMIOT_FALSE-不支持 */
typedef struct {
    cmiot_bool_t motionRegion;                /* 运动检测区域设置 */
    cmiot_bool_t duplexAudioTalk;             /* 双向语音通话 */
    cmiot_bool_t sdcard;                      /* SD卡 */
    cmiot_bool_t sdcardPlayback;              /* SD卡全天录制、回放 */
    cmiot_bool_t sdcardEventPlayback;         /* SD卡事件录制、回放，字段保留以兼容老设备，新设备以新字段 sdcardEventPlaybackV2为准 */
    cmiot_bool_t sdcardEventPlaybackV2;       /* SD卡事件录制、回放，千里眼平台暂不支持，此能力集需关闭；对于其他平台设备，使用内部录制时，若需要支持此能力集，
                                                 将额外消耗内存用于音视频缓存，大小与初始配置传入的streamSpace一致，如设备内存资源不足，可关闭此能力集 */                                      
    cmiot_bool_t motionTrack;                 /* 运动跟踪 */
    cmiot_bool_t peopleDetection;             /* 人形检测 */
    cmiot_bool_t wideDynamic;                 /* 宽动态 */
    cmiot_bool_t wifiSignal;                  /* 信号强度 */
    cmiot_bool_t battery;                     /* 电池 */
    cmiot_bool_t volumeMute;                  /* 声音采集 */
    cmiot_bool_t motionTrackBackTime;         /* 运动跟踪归位时长设置 */
    cmiot_bool_t fullColorNightVision;        /* 全彩夜视 */
    cmiot_bool_t fullColorNightVisionModel;   /* 全彩夜视模式设置 */
    cmiot_bool_t voiceBroadcast;              /* 云广播 */
    cmiot_bool_t lightIntensity;              /* 白光灯亮度调节 */
    cmiot_bool_t peopleTrack;                 /* 人形运动跟踪 */
    cmiot_bool_t statusLight;                 /* 状态指示灯 */
    cmiot_bool_t remoteReboot;                /* 远程重启 */
    cmiot_bool_t g711a;                       /* 支持g711a音频编码 */
    cmiot_bool_t aac;                         /* 支持aac音频编码 */
    cmiot_bool_t h264;                        /* 支持h264视频编码 */
    cmiot_bool_t h265;                        /* 支持h265视频编码 */
    cmiot_uchar_t aiAbilityFaceCapture;       /* AI人脸抓拍能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_uchar_t aiAbilityFaceContrast;      /* AI人脸比对能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_uchar_t aiAbilityVehicleCapture;    /* AI车辆抓拍能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_bool_t aiAbilityAiSensitivity;      /* AI检测灵敏度设置 */
    cmiot_bool_t aiAbilityRegions;            /* AI检测区域设置 */
    cmiot_uchar_t osd;                        /* OSD自定义模式，0表示不支持，其他值表示自定义模式下支持OSD水印的条数（不包含日期水印） */
    cmiot_uchar_t aiAbilityMaskDetect;        /* AI口罩检测能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_uchar_t aiAbilityEbikeCapture;       /* AI电瓶车抓拍能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_uchar_t aiAbilityPassengerStatistics;  /* AI客流统计能力算力值，百分比[0-100] 0表示不支持 */
    cmiot_uchar_t aiAbilityPassengerStatisticsRule;  /* AI客流统计规则，0表示不支持，1表示支持过线统计，2表示支持区域统计，3表示过线和区域统计都支持 */
    cmiot_bool_t aiFaceCaptureSen;          /* AI人脸抓拍灵敏度 */
    cmiot_bool_t aiFaceContrastSen;         /* AI人脸比对灵敏度 */
    cmiot_bool_t aiVehicleCaptureSen;       /* AI车辆抓拍灵敏度 */
    cmiot_bool_t aiMaskDetectSen;           /* AI口罩检测灵敏度 */
    cmiot_bool_t aiEbikeCaptureSen;         /* AI电瓶车抓拍灵敏度 */
    cmiot_bool_t aiPassengerStatisticsSen;  /* AI客流统计灵敏度 */
    cmiot_bool_t aiFaceCaptureRegion;       /* AI人脸抓拍区域 */
    cmiot_bool_t aiFaceContrastRegion;      /* AI人脸比对区域 */
    cmiot_bool_t aiVehicleCaptureRegion;    /* AI车辆抓拍区域 */
    cmiot_bool_t aiMaskDetectRegion;        /* AI口罩检测区域 */
    cmiot_bool_t aiEbikeCaptureRegion;      /* AI电瓶车抓拍区域 */
    cmiot_bool_t batMode;                   /* 低功耗电池模式切换 */
    cmiot_bool_t simInfo;                   /* 蜂窝设备卡信息上报 */
    cmiot_bool_t cellularSignal;            /* 蜂窝信号强度 */
    cmiot_bool_t speakerVolume;             /* 扬声器音量 */
    cmiot_bool_t babyCry;                   /* 哭声检测 */
    cmiot_bool_t aec;                       /* AEC配网 */
    cmiot_bool_t presetPoint;               /* 云台预置点 */
    cmiot_bool_t cruise;                    /* 云台巡航路线 */
    cmiot_bool_t clickAlarm;                /* 按键报警 */
    cmiot_bool_t batAlarm;                  /* 电池电量告警 */
    cmiot_bool_t cellularAlarm;             /* 蜂窝信号告警 */
    cmiot_uint16_t multiCam;                /* 多目摄像头 2字节16bit数值。0表示不支持。高8bit表示支持的摄像头个数，范围[2-8]，即至少2个，
                                               至多8个。低8bit表示每路流对应摄像头是否支持云台，0-不支持，1-支持，对应关系bit0-stream_id0, 
                                               bit1-stream_id1…bit7-stream_id7。如0x0306（十进制值774）表示支持3个摄像头，其中stream_id0
                                               对应的摄像头不支持云台，stream_id1和stream_id2对应的摄像头支持云台。*/
    cmiot_bool_t aperture;                   /* 光圈缩放 */
    cmiot_bool_t focusing;                   /* 焦距调整 */
    cmiot_bool_t zoom;                       /* 变倍缩放 */
    cmiot_bool_t oneTimeSoundAlarm;          /* 一次性声音告警 */
    cmiot_bool_t oneTimeLightAlarm;          /* 一次性灯光告警 */
    cmiot_bool_t focalPoint;                 /* 焦点调整 */
    cmiot_bool_t aiAlgoDispatch;             /* 算法下发 */
    cmiot_bool_t nightVision;                /* 黑白夜视 */
    cmiot_bool_t voiceBroadcastSchedule;     /* 定时云广播 */

    /* 以下四项为OSD新增功能，对接时，如千里眼平台对应功能还未上线，osdStatus、osdGb需传入0，
       osdFontSize、osdFontColor需传入千里眼平台已支持的设置项，即osdFontSize=69(0b0000 0000 0100 0101，支持16,32,64px)，
       osdFontColor=12(0b0000 1100，支持黑白自动，RGB自定义) */
    cmiot_bool_t osdStatus;                  /* OSD总开关 */
    cmiot_uint16_t osdGb;                    /* OSD国标模式，2字节16bit数值。0表示不支持，高8bit表示辖区信息支持的行数，
                                                低8bit表示附加信息支持行数。如0x0406（十进制1030）表示辖区信息支持4行，
                                                附加信息支持6行。缺省表示不支持 */
    cmiot_uint16_t osdFontSize;              /* OSD字体大小设置，2字节16bit数值。按比特位表示对应字体大小的支持情况：
                                                bit0-16px，bit1-24px，bit2-32px，bit3-40px，bit4-48px，bit5-56px，bit6-64px，
                                                bit7-72px，bit8-80px，bit9-88px，bit10-96px，bit11-104px,bit12-112px, 
                                                bit13-120px,bit14-128px。如0b0100 0100 0100 0101（十进制17477）表示支持16,32,64,96,128px */
    cmiot_uchar_t osdFontColor;              /* OSD字体颜色设置，1字节8bit数值。按比特位表示对应字体颜色的支持情况：bit0-黑色，bit1-白色，
                                                bit2-黑白自动，bit3-RGB自定义。如0b0000 0111（十进制7）表示支持黑色、白色和黑白自动 */

    cmiot_bool_t regionInvasion;              /* 区域入侵检测 */
    cmiot_bool_t soundLightAlarm;             /* 声光报警开关 */
    cmiot_bool_t soundLightAlarmVolume;       /* 声光报警音量设置 */
    cmiot_uchar_t soundLightAlarmModel;        /* 声光报警模式设置  0代表不支持、1代表全部支持、2代表支持声音的能力集（响声模式）、3代表支持光的能力集（闪光模式、路灯模式）*/
    cmiot_bool_t soundLightAlarmSoundContext; /* 声光报警声音内容设置 */
    cmiot_bool_t soundLightAlarmSoundTimes;   /* 声光报警声音播放次数 */
    cmiot_bool_t soundLightAlarmLightTime;    /* 声光报警灯光持续时间*/
    cmiot_bool_t regionInvasionBox;          /* 区域入侵实时检测框 */
    cmiot_uchar_t soundLightAlarmCfgNew;     /* 新声光报警配置，0表示不支持，其他值表示支持声光报警配置的组数，最多支持5组
                                                每组配置可单独设置时间段，声音的内容、播报次数、音量，灯光的闪烁方式和时长 */
    cmiot_bool_t soundLightAlarmLightContinuous;   /* 声光报警灯光持续闪烁或持续亮灯 */
    cmiot_bool_t soundLightAlarmSoundContinuous;   /* 声光报警语音持续播报 */

    /* 区域入侵检测模式为新增功能，对接时，如千里眼平台对应功能还未上线，需传入0 */
    cmiot_uchar_t regionInvasionMode;        /* 区域入侵检测模式, bit0-人体检测, bit1-运动检测; 0-均不支持 1-仅人体 2-仅运动 3-均支持 */

    cmiot_bool_t restoreSettings;           /* 支持远程恢复默认设置 */
    cmiot_uchar_t cameraImageFlip;          /* 视频画面翻转，bit0-水平翻转, bit1-垂直翻转; 0-均不支持 1-仅水平翻转 2-仅垂直翻转 3-均支持 */
    cmiot_bool_t microphoneVolume;          /* 麦克风音量设置 */
    cmiot_bool_t whiteLight;                /* 白光灯开关设置 */
    cmiot_bool_t eventPushStatus;           /* 事件上报总开关，开启时设备产生的事件、AI事件正常上报，关闭时所有事件均不上报 */
    cmiot_bool_t activeDeviceLogReport;     /* 设备日志主动上报，如支持，需实现回调 CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT */
    cmiot_uint32_t commonSoundAlarm;    /* 通用声音告警，4字节32bit数值，按比特位表示不同功能可设置的声音告警配置组数，0表示不支持
                                           bit0-bit2: 按键告警支持的声音告警配置组数，最大5组，0表示按键告警不支持联动声音
                                           bit3-bit31: 保留 */
    cmiot_uint32_t commonLightAlarm;    /* 通用灯光告警，4字节32bit数值，按比特位表示不同功能可设置的声音告警配置组数，0表示不支持
                                           bit0-bit2: 按键告警支持的灯光告警配置组数，最大5组，0表示按键告警不支持联动灯光
                                           bit3-bit31: 保留 */
    cmiot_uchar_t subStreamRecording;   /*辅码流录制，1 字节 8bit 数值，按比特位表示辅码流分辨率的支持情况，0 表示不支持
                                          bit0：240P，bit1：360P，bit2：480P，bit3：720P，
                                          bit4：960P，bit5：1080P，bit5-bit7：保留*/
} cmiotDynamicCapability_t;

/* 事件定义 */
typedef enum
{
    CMIOT_EVENT_MOTION_DETECT = 1,       /* 动态检测事件 */
    CMIOT_EVENT_SOUND_DETECT,            /* 声音检测事件 */
    CMIOT_EVENT_FACE_DETECT,             /* 人脸检测事件 */
    CMIOT_EVENT_PEOPLE_DETECT,           /* 人形检测事件 */
    CMIOT_EVENT_FACE_CONTRAST,           /* 人脸比对事件 */
    CMIOT_EVENT_VEHICLE_CAPTURE,         /* 车辆抓拍事件 */
    CMIOT_EVENT_EBIKE_CAPTURE,           /* 电瓶车抓拍事件 */
    CMIOT_EVENT_DISMANTLE_ALARM,         /* 强拆告警 */
    CMIOT_EVENT_INFRARED,                /* 红外告警 */
    CMIOT_EVENT_WANDERING,               /* 徘徊事件 */
    CMIOT_EVENT_DOORBELL_KEY,            /* 门铃按键事件 */
    CMIOT_EVENT_SOUND_LIGHT_ALARM,       /* 声光报警，设备实际产生声光报警时上报 */
    CMIOT_EVENT_BABYCRY_DETECT,          /* 哭声检测事件*/
    CMIOT_EVENT_REGION_HACK,             /* 区域入侵事件，仅上报开始 */
    CMIOT_EVENT_PASSENGER_FLOW_COUNT,    /* 客流统计事件，仅上报开始 */
    CMIOT_EVENT_MASK_DETECTION,          /* 口罩检测告警事件，仅上报开始 */
    CMIOT_EVENT_CLICK_ALARM,             /* 按键报警，仅上报开始 */
    CMIOT_EVENT_BAT_ALARM,               /* 电池电量告警事件，仅上报开始，根据电池电量告警规则进行上报 */
    CMIOT_EVENT_CELLULAR_ALARM,          /* 蜂窝信号告警事件，仅上报开始，根据蜂窝信号告警规则进行上报 */

    CMIOT_EVENT_AI = 100,                      /* AI事件 */
} cmiotEventType_e;

/* 客流统计数据定义 */
typedef struct
{
    cmiot_uint32_t startTime;  /* 统计开始时间（单位：秒, UTC时间） */
    cmiot_uint32_t stopTime;   /* 统计结束时间（单位：秒，UTC时间） */
    cmiot_uint32_t count;      /* 客流进入区域的数量 客流统计规则中enableRegion为1时上报该字段 */
    cmiot_uint32_t side1;      /* 客流穿过直线到side1的数量 客流统计规则中enableLine为1且mode中包含side1时上报该字段 */
    cmiot_uint32_t side2;      /* 客流穿过直线到side2的数量 客流统计规则中enableLine为1且mode中包含side2时上报该字段 */
} cmiotPassengerFlowData_t;

/* 升级状态定义 */
typedef enum
{
    CMIOT_UPGRADE_STATUS_START_DOWNLOAD    = 1,     /* 开始下载 */
    CMIOT_UPGRADE_STATUS_DOWNLOADING       = 2,     /* 正在下载 */
    CMIOT_UPGRADE_STATUS_DOWNLOAD_COMPLETE = 3,     /* 下载成功 */
    CMIOT_UPGRADE_STATUS_DOWNLOAD_FAILED   = 4,     /* 下载失败 */
    CMIOT_UPGRADE_STATUS_START_INSTALL     = 5,     /* 开始安装 */
    CMIOT_UPGRADE_STATUS_INSTALLING        = 6,     /* 正在安装 */
    CMIOT_UPGRADE_STATUS_INSTALL_COMPLETE  = 7,     /* 安装成功 */
    CMIOT_UPGRADE_STATUS_INSTALL_FAILED    = 8,     /* 安装失败 */
} cmiotUpgradeStatus_e;

/* 升级上报信息定义 */
typedef struct
{
    cmiot_uint32_t type;                            /* 1:固件升级 2：Camera应用升级 */
    cmiotUpgradeStatus_e status;                    /* 升级状态 */
    int devStatus;                                  /* status为7或8时需要填此参数,表示设备升级之前的开关状态, 0-OFF, 1-ON */
    cmiot_uint32_t curSize;                         /* 已下载字节数 */
    cmiot_uint32_t totalSize;                       /* 总字节数 */
    cmiot_char_t version[CMIOT_MAX_VERSION_LEN];    /* 目标版本的版本号 */
} cmiotUpgradeRptInfo_t;

/* 运动检测区域信息定义 */
typedef struct
{
    cmiot_uint32_t id;                 /* 区域id */
    cmiot_uint32_t left;               /* 区域左边框坐标，范围0~4 */
    cmiot_uint32_t right;              /* 区域右边框坐标，范围1~5 */
    cmiot_uint32_t top;                /* 区域上边框坐标，范围0~4 */
    cmiot_uint32_t bottom;             /* 区域下边框坐标，范围1~5 */
} cmiotMotionRegionInfo_t;

/* 运动检测区域列表 */
typedef struct
{
    cmiot_uint32_t regionNum;          /* 区域个数，范围1-25 */
    cmiotMotionRegionInfo_t *region;   /* 区域坐标信息数组指针 */
} cmiotMotionRegionList_t;

/* 计划任务信息 */
typedef struct
{
    cmiot_uint32_t index;                                    /* 计划序号 */
    cmiot_uint32_t enable;                                   /* 0-生效，1-不生效 */
    cmiot_uint32_t repeat;                                   /* 低7位有效，按二进制从低位到高位依次表示周日到周六：
                                    SUN:0x0001, MON:0x0002, TUE:0x0004, WED:0x0008, THU:0x0010, FRI:0x0020, SATU:0x0040 */
    cmiot_char_t startTime[CMIOT_MAX_SCHEDULE_TIME_LEN];     /* 开始时间，格式为 HH:mm */
    cmiot_char_t endTime[CMIOT_MAX_SCHEDULE_TIME_LEN];       /* 结束时间，格式为 HH:mm */
} cmiotScheduleInfo_t;

/* 计划任务列表 */
typedef struct
{
    cmiot_uint32_t scheduleNum;         /* 计划个数，范围1-25 */
    cmiotScheduleInfo_t *schedule;      /* 计划信息数组指针 */
} cmiotScheduleList_t;

/* 坐标点定义，以左上角为坐标原点（x:0,y:0） */
typedef struct
{
    cmiot_uint32_t x;               /* x为点所在的列，范围0~1000 */
    cmiot_uint32_t y;               /* y为点所在的行，范围0~1000 */
} cmiotPointInfo_t;

/* 矩形区域信息 */
typedef struct
{
    cmiotPointInfo_t point[4];   /* 矩形区域四个角的坐标 */
} cmiotRectangleRegionInfo_t;

/* 声光报警模式 */
typedef enum
{
    CMIOT_SOUND_LIGHT_ALARM_MODE_MONITER = 1,   /* 监控模式：发现人形或异动，播放语音报警，灯光闪烁 */
    CMIOT_SOUND_LIGHT_ALARM_MODE_SOUND   = 2,   /* 响声模式：发现人形或异常运动，播放语音报告 */
    CMIOT_SOUND_LIGHT_ALARM_MODE_FLASH   = 3,   /* 闪光模式：发现人形或异常运动，仅灯光闪烁 */
    CMIOT_SOUND_LIGHT_ALARM_MODE_LAMP    = 4,   /* 路灯模式：发现人形或异常运动，持续亮灯 */
} cmiotSoundLightAlarmMode_e;

/* 入侵检测区域信息 */
typedef struct
{
    cmiot_uint32_t regionNum;               /* 区域个数，范围1-25 */
    cmiotRectangleRegionInfo_t *region;     /* 区域数组指针 */
} cmiotInvadeRegionList_t;

/* wifi信息 */
typedef struct
{
    cmiot_char_t bssid[CMIOT_MAX_WIFI_BSSID_LEN];                  /* bssid */
    cmiot_char_t ssid[CMIOT_MAX_WIFI_SSID_LEN];                    /* ssid，只支持UTF8编码格式 */
    cmiot_char_t encryption[CMIOT_MAX_WIFI_ENCRYPTION_LEN];        /* 加密方式 */
    cmiot_uint32_t signal;                                    /* 信号强度，范围1~5 */
    cmiot_uint32_t connected;                                 /* 0:未连接 1:已连接 */
} cmiotWifiInfo_t;

/* wifi列表 */
typedef struct
{
    cmiot_uint32_t wifiNum;     /* wifi个数 */
    cmiotWifiInfo_t *wifi;      /* wifi信息数组指针 */
} cmiotWifiList_t;

/* SD卡容量信息 */
typedef struct
{
    cmiot_uint64_t freesize;     /* SD卡剩余容量，单位：字节 */
    cmiot_uint64_t totalsize;    /* SD卡总容量，单位：字节 */
} cmiotSdcardSpaceInfo_t;

/* SD卡分区信息 */
typedef struct
{
    cmiot_char_t name[CMIOT_MAX_SD_PART_NAME_LEN];    /* 分区名称 */
    cmiot_char_t fs[CMIOT_MAX_SD_PART_FORMAT_LEN];      /* 分区格式 */
    cmiot_uint64_t total;   /* 分区总容量，单位：字节 */
    cmiot_uint64_t remain;  /* 分区剩余容量，单位：字节 */
} cmiotSdcardPartInfo_t;

/* SD卡分区列表 */
typedef struct
{
    cmiot_uint32_t partNum;             /* 分区个数 */
    cmiotSdcardPartInfo_t *part;   /* 分区数组指针 */
} cmiotSdcardPartList_t;

/* 当前连接的wifi信息 */
typedef struct
{
    cmiot_char_t ssid[CMIOT_MAX_WIFI_SSID_LEN];        /* ssid */
    cmiot_uint32_t signal;                        /* 信号强度，范围0~100 */
} cmiotConWifiInfo_t;

/* 电量信息 */
typedef struct
{
    cmiot_uint8_t battery;     /* 1表示低电量；2表示正在充电；3表示电池充满了；4表示其它正常状态 */
    cmiot_uint8_t percent;     /* 电池电量的百分比 */
} cmiotBatteryInfo_t;

/* 修改wifi请求信息 */
typedef struct
{
    cmiot_char_t bssid[CMIOT_MAX_WIFI_BSSID_LEN];                  /* bssid */
    cmiot_char_t ssid[CMIOT_MAX_WIFI_SSID_LEN];                    /* ssid */
    cmiot_char_t encryption[CMIOT_MAX_WIFI_ENCRYPTION_LEN];        /* 加密方式 */
    cmiot_char_t key[CMIOT_MAX_WIFI_KEY_LEN];                      /* 密码 */
} cmiotModifyWifiReqInfo_t;

/* 缩略图上传请求信息 */
typedef struct
{
    cmiot_uint32_t width;    /* 缩略图的宽度 */
    cmiot_uint32_t height;   /* 缩略图的高度 */
} cmiotThumbnailReqInfo_t;


/* 分辨率信息 */
typedef struct
{
    cmiot_uint32_t width;    /* 画面宽度 */
    cmiot_uint32_t height;   /* 画面高度 */
} cmiotVideoQualityInfo_t;

/* 云台位置信息 */
typedef struct
{
    cmiot_int32_t pan;     /* 水平位置坐标，最左端-1000000，最右端1000000 */
    cmiot_int32_t tilt;    /* 垂直位置坐标，最下端-1000000，最上端1000000 */
} cmiotPtzPositionInfo_t;

/* 云台转动信息 */
typedef struct
{
    cmiot_int32_t value;    /* 1-方向向左 2-方向向右 3-方向向上 4-方向向下 5-方向左上 6-方向右上 7-方向左下 8-方向右下 0-步进转动时表示回到原始位置，连续转动时表示停止转动 */
    cmiot_int32_t speed;           /* speedVertical为空时或不支持水平、垂直速度分开设置的设备同时表示水平、垂直速度，反之则表示水平速度，取值范围1~7，速率递增，如为0，则表示未设置速度，以默认速度转动 */
    cmiot_int32_t speedVertical;   /* 垂直速度，对支持水平、垂直速度分开设置的设备生效，取值范围1~7，速率递增，如为0，则表示未设置速度，以默认速度转动 */
    cmiot_uint64_t ts;        /* 命令触发的 UTC 时间戳，单位毫秒，用于设备侧判断转动信令是否乱序，乱序的信令不执行 */ 
} cmiotPtzMoveInfo_t;

/* 云广播信息 */
typedef struct
{
    cmiot_char_t id[CMIOT_MAX_AUDIO_ID_LEN];    /* 语音id */
    cmiot_char_t url[CMIOT_MAX_URL_LEN];        /* 音频下载地址，设备需主动下载此语音内容（格式：G711A，采样率：8K，16bit） */
    cmiot_uint32_t times;                       /* 播报次数，若为0，则播放一次 */
    cmiot_uint32_t span;                        /* 多次播报的间隔时间，单位秒 */
} cmiotAudioPlayInfo_t;

/* 设备升级信息 */
typedef struct
{
    cmiot_char_t url[CMIOT_MAX_URL_LEN];            /* 更新包链接，可以是Fw，也可以是CamApp */
    cmiot_char_t checkSum[CMIOT_MAX_MD5_LEN];       /* 校验值，校验文件的完整性，这里是更新包的MD5 */
    cmiot_char_t version[CMIOT_MAX_VERSION_LEN];    /* 升级包版本号 */
} cmiotUpgradeInfo_t;

/* 返回值定义 */
typedef enum
{
    CMIOT_RETURN_CODE_SUCCESS = 0,             /* 成功 */
    CMIOT_RETURN_CODE_FAILED,                  /* 失败 */
} cmiotReturnCode_e;


/* 以下配置在运行过程中产生变化时，需调用api通知平台 */
typedef enum
{
    /* 【入参数据类型】int  【取值范围】1 - On, 0 - Off */
    CMIOT_UPDATE_CFG_STATUS = 0,              /* 设备开关 */

    /* 【入参数据类型】int  【取值范围】1 - On, 0 - Off */
    CMIOT_UPDATE_CFG_PERSON_STATUS,           /* 客流统计开关 */

    /* 【入参数据类型】char[] */
    CMIOT_UPDATE_CFG_WIFI_SSID,               /* WiFi的SSID: 设备上电连接Wifi成功时，上报一次，后续WiFi切换时上报 */

    /* 【入参数据类型】cmiotSdCardInfo_t */
    CMIOT_UPDATE_CFG_SD_STATUS,               /* 存储卡状态: 当设备上电时，上报一次，后续SD卡状态变化时上报 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_MOTION_TARCK,            /*运动跟踪开关 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_VOLUME_MUTE,             /* 声音采集开关 */

    /* 【入参数据类型】int */
    CMIOT_UPDATE_CFG_MOTION_TRACK_BACK_TIME,  /* 运动跟踪回到原来位置的时间（秒） */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM,       /* 声光报警开关，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】0~100 */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_VOLUME,  /* 声光报警音量，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】1-监控模式 2-响声模式 3-闪光模式 4-路灯模式  5-上报报警模式*/
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_MODE,   /* 声光报警模式，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION,   /* 全彩夜视开关，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】1-黑白夜视 2-全彩夜视 3-智能夜视 */
    CMIOT_UPDATE_CFG_FULL_COLOR_NIGHT_VISION_MODE,  /* 全彩夜视模式，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】1-h264 2-h265 */
    CMIOT_UPDATE_CFG_CODED_FORMAT,            /* 设备当前视频编码格式，设备绑定之后需要上报一次。设备若同时支持 h264/h265视频编码，
                                                默认采用h265；设备视频编码发生变化或切换编码格式失败时，需上报当前视频编码格式 */
    
    /* 【入参数据类型】int  【取值范围】1-g711a 2-aac */
    CMIOT_UPDATE_CFG_AUDIO_CODEC_FORMAT,      /* 设备当前音频编码格式，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】0~100 */
    CMIOT_UPDATE_CFG_LIGHT_INTENSITY,         /* 白光灯亮度，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_PEOPLE_TRACK,            /* 人形运动跟踪开关 */

    /* 【入参数据类型】int  【取值范围】0~600，0：声光告警灯光持续闪烁或持续亮灯 */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_LIGHT_TIME,   /* 声光报警 灯光持续时长，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】0~10，0：声光告警语音持续播报 */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_SOUND_TIMES,  /* 声光报警 语音播报次数，如支持该能力，设备绑定之后需要上报一次 */

    /* 【入参数据类型】char[] */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_SOUND_URL,    /* 声光报警 语音包下载地址，本地现存语音包信息发生变化或下载失败时，上报本地现存的语音包信息 */

    /* 【入参数据类型】char[] */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_ALARM_SOUND_ID,     /* 声光报警 语音包ID，本地现存语音包信息发生变化或下载失败时，上报本地现存的语音包信息 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_FACE_CAPTURE,               /* 人脸抓拍开关状态 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_FACE_CONTRAST,              /* 人脸比对开关状态 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_VEHICLE_CAPTURE,            /* 车辆抓拍开关状态 */

    /* 【入参数据类型】int 【取值范围】0-100 */
    CMIOT_UPDATE_CFG_AI_SENSITIVITY,                /* AI检测灵敏度 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_MASK_DETECT,                /* 口罩检测开关状态 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_EBIKE_CAPTURE,              /* 电瓶车抓拍开关状态 */

    /* 【入参数据类型】int 【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_AI_PASSENGER_STAT,             /* 客流统计开关状态 */

    /* 【入参数据类型】cmiotBattLowPowerMode_t */
    CMIOT_UPDATE_CFG_BAT_MODE,                      /* 低功耗电池模式，设备绑定之后需要上报一次 */

    /* 【入参数据类型】cmiotSimInfo_t */
    CMIOT_UPDATE_CFG_SIM_INFO,                      /* 蜂窝设备卡信息，设备注册之后需要上报一次 */

    /* 【入参数据类型】int  【取值范围】0~100 */
    CMIOT_UPDATE_CFG_SPEAKER_VOLUME,                /* 扬声器音量 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_MOTION_DETECTION_STATUS,            /*运动检测开关 */

    /* 【入参数据类型】int  【取值范围】1：打开  0：关闭 */
    CMIOT_UPDATE_CFG_SOUND_DETECTION_STATUS,            /*声音检测开关 */

    /* 【入参数据类型】cmiotBatAlarmRuleInfo_t */
    CMIOT_UPDATE_CFG_BAT_ALARM_RULE,                    /* 电池电量告警规则，设备绑定之后需要上报一次，告警开关打开时，必须上传rule和span字段 */

    /* 【入参数据类型】cmiotCellAlarmRuleInfo_t*/
    CMIOT_UPDATE_CFG_CELLULAR_ALARM_RULE,               /* 蜂窝信号告警规则，设备绑定之后需要上报一次，告警开关打开时，必须上传threshold和span字段*/

    /* 【入参数据类型】int  【取值范围】0-关闭  1-打开 */
    CMIOT_UPDATE_CFG_REGION_INVASION_BOX_STATUS,         /* 区域入侵实时检测框开关 */

    /* 【入参数据类型】int  【取值范围】bit0-人体检测, bit1-运动检测; 0-均不支持 1-仅人体 2-仅运动 3-均支持 */
    CMIOT_UPDATE_CFG_REGION_INVASION_MODE,              /* 区域入侵检测模式 */

    /* 【入参数据类型】char[] * 【说明】宽*高，如双目摄像头 2K分辨率竖向向拼接："2560*2880" */
    CMIOT_UPDATE_MULTICAM_STITCH_RESO,              /* 多目拼接摄像头拼接后的画面分辨率，设备绑定首次上线之后需要上报一次 */

    /* 【入参数据类型】cmiotSoundLightAlarmCfgNewInfo_t */
    CMIOT_UPDATE_CFG_SOUND_LIGHT_CFG_NEW,               /* 配置新声光告警计划，配置发生变化或下发的新声光告警计划配置无法生效或语音包下载失败时，上报当前配置 */

    /* 【入参数据类型】int  【取值范围】0-视频画面未发生翻转 1-视频画面为水平翻转状态 2-视频画面为垂直翻转状态 3-视频画面为水平和垂直同时翻转状态 */
    CMIOT_UPDATE_CFG_CAMERA_IMAGE_FLIP,                 /* 视频画面翻转 */

    /* 【入参数据类型】int  【取值范围】0-100 */
    CMIOT_UPDATE_CFG_MICROPHONE_VOLUME,                 /* 麦克风音量 */

    /* 【入参数据类型】int  【取值范围】0-关闭  1-打开 */
    CMIOT_UPDATE_CFG_WHITE_LIGHT,                      /* 摄像头白光灯开关 */  

    /* 【入参数据类型】int  【取值范围】0-关闭  1-打开 */
    CMIOT_UPDATE_CFG_EVENT_PUSH_STATUS,                /* 事件上报开关 */

    /* 【入参数据类型】 cmiotCommonSoundAlarmCfgs_t */
    CMIOT_UPDATE_CFG_COMMON_SOUND_ALARM,               /* 通用声音告警配置，配置发生改变，或下发的配置无法生效，或语音包下载失败时，上报当前配置 */

    /* 【入参数据类型】 cmiotCommonLightAlarmCfgs_t */
    CMIOT_UPDATE_CFG_COMMON_LIGHT_ALARM,               /* 通用灯光告警配置，配置发生改变，或下发的配置无法生效时，上报当前配置 */

    /* 【入参数据类型】int  【取值范围】0：240P，1：360P，2：480P，3：720P，4：960P，5：1080P */
    CMIOT_UPDATE_CFG_SUBCODING_RESOLUTION,
    CMIOT_UPDATE_CFG_END
} cmiotUpdateCfgType_e;

/* sd卡状态 */
typedef enum
{
    CMIOT_SD_CARD_STATUS_READY,                          /**< 设备sd卡插入，且状态正常 */
    CMIOT_SD_CARD_STATUS_NOT_READY,                      /**< 设备无sd卡存储   */
    CMIOT_SD_CARD_STATUS_ABNORMAL,                       /**< 设备sd卡存储异常 */
    CMIOT_SD_CARD_STATUS_UNFORMATTED,                    /**< 设备SD卡未格式化 */
    CMIOT_SD_CARD_STATUS_FORMATTING,                     /**< 设备SD卡正在格式化 */
    CMIOT_SD_CARD_FILE_SYSTEM_NOT_SUPPORT,               /**< 不支持的文件系统 */
} cmiotSdCardStatus_e;

/* 网络接入信息 */
typedef enum
{
    CMIOT_ACCESS_WIRED,                                   /**< 有线接入 */
    CMIOT_ACCESS_WIFI,                                    /**< WiFi接入 */
    CMIOT_ACCESS_FOURTH_GENERATION_NET,                   /**< 4G蜂窝接入 */ 
    CMIOT_ACCESS_FIFTH_GENERATION_NET,                    /**< 5G蜂窝接入 */ 
} cmiotAccessMethod_e;

/* WiFi接入频段 */
typedef enum
{
    CMIOT_BAND_2_4GHZ,                                    /**< 2.4GHz频段 */
    CMIOT_BAND_5GHZ,                                      /**< 5GHz频段 */
} cmiotAccessWifiFrequency_e;

/* sd卡状态和路径信息 */
typedef struct
{
    cmiotSdCardStatus_e status;
    cmiot_char_t path[CMIOT_MAX_PATH_LEN];      /* 保留 */
} cmiotSdCardInfo_t;

/* 零配置注册信息，用于接入其他平台的配置信息 */
typedef struct
{
    cmiot_char_t registerProtocolType[16];         /* 0：GB/T28181，1：Onvif，2：Ehome，3：海康，4：大华，5：华为，6：ISUP，7：GA/T 1400，8：千里眼私有协议 */
                                                   /* 该字段可能包含多种协议类型，如设备同时支持国标和私有协议，则为"0,8"，中间以英文逗号分隔 */
    cmiot_char_t deviceRegisterPassword[128];      /* 注册密码，密钥 */
    cmiot_char_t deviceCode[128];                  /* 设备编码 */
    cmiot_char_t SIPServiceId[128];                /* registerProtocolType=0时必选，SIP服务器ID */
    cmiot_char_t SIPServiceDomain[128];            /* registerProtocolType=0时必选，SIP服务器域 */
    cmiot_char_t SIPIPAddress[64];                 /* registerProtocolType=0时必选，SIP服务器地址 */
    cmiot_uint32_t SIPIPPort;                      /* registerProtocolType=0时必选，SIP服务器ID */
    cmiot_char_t SIPProtocol[8];                   /* registerProtocolType=0时必选，通信协议（UDP、TCP） */
    cmiot_uint32_t bps;                            /* 最大码率，单位kbps，0则无限制 */
} cmiotZeroCfgInfo_t;

/* 1400零配置信息 */
typedef struct
{
    cmiot_char_t GAT1400IPAddress[64];             /* 1400协议地址 (IPV4/IPV6/域名) */
    cmiot_uint32_t GAT1400IPPort;                  /* 1400协议IP地址端口 */
    cmiot_char_t GAT1400Id[48];                    /* 1400编码ID */
    cmiot_char_t GAT1400RegisterPassword[48];      /* 1400密码 */
    cmiot_char_t GAT1400RegisterAccount[48];       /* 1400视图库用户名 */
    cmiot_char_t GAT1400RegisterChannelCode[48];   /* 1400通道ID */
    cmiot_uint32_t GAT1400KeepaliveCycle;          /* 1400心跳周期，单位秒 */
    cmiot_uint32_t GAT1400MaxKeepaliveTimeoutNum;  /* 1400最大心跳超时次数 */
    cmiot_char_t   GAT1400IPV6Address[64];         /* 1400协议IPV6地址 (IPV6/域名) */
    cmiot_uint32_t GAT1400IPV6Port;                /* 1400协议IPV6地址端口 */

    /* GAT1400IPV6Address和GAT1400IPV6Port的值有效时，设备需要优先使用其提供的IPV6地址/域名和端口连接GAT1400平台，无法连接时再使用IPV4地址 */
    /* 若GAT1400平台被配置为单栈IPV6，GAT1400IPAddress 也将返回IPV6地址 */
    /* 若GAT1400平台被配置为域名，GAT1400IPAddress 和 GAT1400IPV6Address 都返回域名，设备需自行解析地址，并在IPV6网络可用时优先使用V6连接 */
} cmiot1400CfgInfo_t;

/* OSD自定义模式下水印位置对齐模式 */
typedef enum
{
    CMIOT_OSD_POS_ALIGN_NOT_SET,    /* 未设置 */
    CMIOT_OSD_POS_ALIGN_LEFT,       /* 左对齐 */
    CMIOT_OSD_POS_ALIGN_RIGHT,      /* 右对齐 */
} cmiotOsdPosAlignSetting_e;

/* OSD自定义模式水印位置定义，以画面左上角为坐标原点（x:0, y:0），给出本条水印对齐设置和左上角位置的对应坐标，坐标范围0~10000，设备根据实际分辨率做好适配 */
typedef struct
{
    cmiot_uint32_t x;                       /* 水印位置x轴坐标，范围0~10000 */
    cmiot_uint32_t y;                       /* 水印位置y轴坐标，范围0~10000 */
    cmiotOsdPosAlignSetting_e alignSet;     /* 对齐模式，当对齐模式为未设置时，根据x轴坐标，y轴坐标设置水印位置 */
                                            /* 当对齐模式为左对齐或右对齐时，根据对齐模式、水印边距、y轴坐标设置水印位置 */
    cmiot_float_t alignPos;                 /* 水印边距，单位字符，精确到小数点后1位。对齐模式为左对齐或右对齐时，该字段有效 */
                                            /* 对齐模式为左对齐时，表示水印的左边距，对齐模式为右对齐时，表示水印的右边距 */
} cmiotOsdPosInfo_t;

/* OSD国标模式水印位置定义 */
typedef struct
{
    cmiot_float_t sideSpace;  // 左右边距，单位字符，精确到小数点后1位，对于日期、辖区信息为右边距，对于附加信息为左边距
    cmiot_float_t vertSpace;  // 上下边距，单位字符，精确到小数点后1位，对于日期为上边距，对于辖区、附加信息为下边距
    cmiot_float_t lineSpace;  // 行间距，单位字符，精确到小数点后1位，当辖区、附加信息有多行时生效，日期水印忽略此参数
} cmiotGBOsdPosInfo_t;

/* OSD日期水印配置 */
typedef struct
{
    cmiot_char_t status[4];           // 日期水印开关，Off：关闭，On：开启
    cmiot_char_t dateFormat[24];      // 日期格式，YYYY-MM-DD、YYYY.MM.DD、YYYY/MM/DD、YYYY年MM月DD日
    cmiot_uint16_t timeFormat;        // 时间格式，12：12小时制，24：24小时制，跟随在日期后显示
    cmiot_uint16_t weekday;           // 0-不显示星期 1-显示星期 跟随在时间后显示
    cmiotOsdPosInfo_t pos;            // 日期水印位置，自定义模式下此参数生效，日期水印显示在对应坐标位置
    cmiotGBOsdPosInfo_t gbPos;        // 日期水印的上边距和右边距，国标模式下此参数生效，位置固定在右上角
} cmiotOsdDateInfo_t;

/* OSD自定义模式文本水印内容 */
typedef struct
{
    cmiot_char_t content[CMIOT_MAX_OSD_CONTENT_LEN + 1];        //文本内容，最大128字符
    cmiotOsdPosInfo_t pos;            //文本水印坐标位置
} cmiotOsdTextInfo_t;

/* OSD国标模式文本水印内容 */
typedef struct
{
    cmiot_char_t content[CMIOT_MAX_OSD_CONTENT_LEN + 1];        //文本内容，最大128字符
} cmiotGBOsdTextInfo_t;

/* OSD自定义模式文本水印列表 */
typedef struct
{
    cmiot_uint32_t textNum;          //文本水印条数
    cmiotOsdTextInfo_t *text;        //文本水印内容
} cmiotOsdTextInfoList_t;

/* osd国标模式文本水印列表 */
typedef struct
{
    cmiot_uint32_t        textNum;  // 文本水印条数
    cmiotGBOsdTextInfo_t  *text;    // 文本水印内容
    cmiotGBOsdPosInfo_t   textPos;  // 文本水印边距
} cmiotOsdGBTextInfoList_t;

/* osd国标模式辖区、附加信息 */
typedef struct
{
    cmiotOsdGBTextInfoList_t districtText;  // 辖区信息文本，位置固定在右下角，右对齐，按数组顺序由上至下显示
    cmiotOsdGBTextInfoList_t additionText;  // 附加信息文本，位置固定在左下角，左对齐，按数组顺序由上至下显示
} cmiotOsdGBInfoList_t;

/* OSD水印设置 */
typedef struct
{
    cmiot_bool_t osdSwitch;           // OSD功能总开关，1-开启，0-关闭；开启时设备应用平台水印设置，覆盖本地web设置的水印；
                                      // 关闭时设备清除平台下发的水印，如存在本地web水印设置，则应用本地web水印设置
    cmiot_uint8_t mode;               // OSD模式，1-自定义模式 2-国标模式
    cmiotOsdDateInfo_t date;          // 日期水印设置
    cmiot_uint32_t fontSize;          // 字体大小，对所有水印内容生效，16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128
    cmiot_char_t fontColor[16];       // 字体颜色，对所有水印内容生效，black-黑色 white-白色 auto-黑白自动 其他值-自定义色彩，RGB HEX格式（#rrggbb，如黑色为#000000）
    union
    {
        cmiotOsdTextInfoList_t customText;  // 文本水印，自定义模式下此参数生效
        cmiotOsdGBInfoList_t   gbText;      // 文本水印，国标模式下此参数生效
    } osdText;
} cmiotOSDInfo_t;

/* 电池低功耗模式 */
typedef struct
{
    cmiot_uint8_t mode;         // 低功耗工作模式 1-常电模式 默认凌晨0点至凌晨6点设备处于工作状态，其余时间自动切换为省电模式；
                                // 2-混电模式 持续充电1小时，自动切换成常电模式。持续耗电1小时，自动切换成省电模式；
                                // 3-省电模式 设备处于休眠状态，当有人或其他物体经过，设备自动唤醒进入工作状态
    cmiot_char_t  schedule[6];   // 工作时间 mode为1时，该字段有效。如"22-6"表示第一天晚上10点到第二天凌晨6点设备处于正常工作状态，"0-6"表示凌晨0点至凌晨6点设备处于正常工作状态
} cmiotBattLowPowerMode_t;

/* 系统运行信息 */
typedef struct
{
    cmiot_char_t model[32];           //芯片型号
    cmiot_char_t producer[32];        //芯片制造商
    cmiot_char_t systemEnv[64];       //芯片运行系统环境
    cmiot_char_t power[16];           //芯片算力
    cmiot_uint32_t totalMemory;       //总运行内存,单位MB
    cmiot_uint32_t remainMemory;      //剩余运行内存,单位MB
    cmiot_uint32_t totalStorage;      //总存储空间,单位MB
    cmiot_uint32_t remainStorage;     //剩余可用存储空间,单位MB
    cmiot_char_t networkType[16];     //网络类型
    cmiot_float_t packetLossRate;     //网络丢包率,最多保留两位小数，单位%
} cmiotSystemStatInfo_t;

// ip端口
typedef struct
{
	cmiot_char_t    ip[256];
	cmiot_uint32_t  port;
} cmiotIpList_t;

/* 低功耗保活信息 */
typedef struct
{
	cmiot_uint32_t    pingSpan;        // pingSpan: 保活心跳包间隔
    cmiot_uint8_t     pingDataLen;     // 心跳包内容长度
    cmiot_uint8_t     *pingData;       // 心跳包内容，设备通信模块发往服务器，
    cmiot_uint8_t     wakeDataLen;     // 唤醒包内容长度
	cmiot_uint8_t     *wakeData;       // 唤醒包内容，服务器发往设备通信模块，接收后唤醒设备
    cmiot_uint8_t     iplistNum;       // 可用服务器个数
	cmiotIpList_t     *iplist;         // ipList: 保活服务器信息
} cmiotKeepaliveInfo_t;

typedef struct
{
    cmiot_uint32_t x1;   //线端点1 x坐标 坐标范围0~1000
    cmiot_uint32_t y1;   //线端点1 y坐标 坐标范围0~1000
    cmiot_uint32_t x2;   //线端点2 x坐标 坐标范围0~1000
    cmiot_uint32_t y2;   //线端点2 y坐标 坐标范围0~1000
    cmiot_uint8_t mode;  // 1-表示需统计客流穿过直线到side1的数量 2-表示需统计客流穿过直线到side2的数量 3-表示需同时统计客流穿过直线到两侧的数量
                         // side1与side2标志着直线两侧，对于非水平线，左侧为side1，右侧为side2，对于水平线上侧为side1，下侧为side2。
} cmiotPassengerStatLine_t;

/* 客流统计规则 */
typedef struct
{
    cmiot_bool_t               enableLine;        //过线客流统计开关 0：表示不开启 1：表示开启
    cmiot_bool_t               enableRegion;      //区域客流统计开关 0：表示不开启 1：表示开启
    cmiot_uint32_t             lineCountCycle;    //过线客流统计周期 单位s 仅当enableLine为1时，该字段有效
    cmiot_uint32_t             regionCountCycle;  //区域客流统计周期 单位s 仅当enableRegion为1时，该字段有效
    cmiotPassengerStatLine_t   lineRule;          //过线客流统计规则 仅当enableLine为1时，该字段有效
    cmiotRectangleRegionInfo_t regionRule;        //区域客流统计规则 仅当enableRegion为1时，该字段有效
} cmiotPassengerStatRule_t;


/* 蜂窝设备卡信息 */
typedef struct
{
    cmiot_int32_t carrier;   /* 卡所属运营商 1-移动，2-电信，3-联通，4-广电*/
    cmiot_char_t iccid[24];  /* 卡ICCID */
} cmiotSimInfo_t;

typedef struct
{
    cmiot_uint8_t repeat;       // 重复计划，低7bit有效，从低位到高位依次表示周日到周六，例：每天重复值为0x7F(127)
                                // 0x01 0x02 0x04 0x08 0x10 0x20 0x40
                                // SUN  MON  TUE  WED  THU  FRI  SAT
    cmiot_char_t  startTime[6]; // 开始时间，单位HH:mm
    cmiot_char_t  endTime[6];   // 结束时间，单位HH:mm
} cmiotAIScheduleParam_t;

typedef struct
{
    cmiot_uint8_t flag;              // 配置项标识，对应位表示是否要配置该项
                                     // 0x01   0x02  0x04     0x08      0x10
                                     // status times audio.id audio.url schedule
    cmiot_bool_t status;             // 语音播报开关
    cmiot_uint32_t times;            // 语音播报次数
    cmiot_char_t id[CMIOT_MAX_AUDIO_ID_LEN];    // 音频id
    cmiot_char_t url[CMIOT_MAX_URL_LEN];        // 音频下载地址，设备需主动下载此音频内容（格式：G711A，采样率：8K，16bit） */
    cmiotAIScheduleParam_t schedule; // 功能开启时间计划，status为On时该字段有效，缺省为7*24全天开启
} cmiotAIConfigAudioParam_t;

typedef struct
{
    cmiot_uint8_t flag;              // 配置项标识，对应位表示是否要配置该项
                                     // 0x01   0x02 0x04 0x08
                                     // status time mode schedule
    cmiot_bool_t status;             // 灯光警示开关
    cmiot_uint32_t time;             // 灯光警示（闪烁或长亮）持续时长，单位秒
    cmiot_uint32_t mode;             // 灯光警示模式，0-闪烁 1-长亮
    cmiotAIScheduleParam_t schedule; // 功能开启时间计划，status为On时该字段有效，缺省为7*24全天开启
} cmiotAIConfigLightParam_t;

typedef struct
{
    cmiot_uint32_t aiAlgoType; //AI算法类型

    //Common
    cmiot_bool_t statusExist;                   // 是否存在算法开关
    cmiot_bool_t status;                        // 算法开关

    cmiot_bool_t sensitivityExist;              // 是否存在灵敏度
    cmiot_uint16_t sensitivity;                 // 灵敏度 取值范围0-100

    cmiot_bool_t regionExist;                   // 是否存在检测区域
    cmiotRectangleRegionInfo_t region;          // 检测区域
    
    cmiot_bool_t audioExist;                    // 是否存在语音播报警示
    cmiotAIConfigAudioParam_t audio;            // 语音播报警示设置，如支持联动语音警示，需在传入AI能力集时包含audio字段，详细定义见对接文档
    
    cmiot_bool_t lightExist;                    // 是否存在灯光警示
    cmiotAIConfigLightParam_t light;            // 灯光警示设置，如支持联动灯光警示，需在传入AI能力集时包含audio字段，详细定义见对接文档
    
    cmiot_bool_t countCycleExist;               // 是否存在统计周期
    cmiot_uint32_t countCycle;                  // 统计周期 取值范围{5,10,15,20,30,60} 统计时需以整点为基准对齐

    //Specific
    cmiot_bool_t specificExist;                 // 是否存在特殊配置
    cmiot_uint32_t specificLen;                 // 特殊配置JSON字符串长度
    cmiot_char_t* specificStr;                  // 特殊配置JSON字符串
       
} cmiotAIConfigParam_t;

typedef struct
{
    cmiot_char_t logPath[CMIOT_MAX_PATH_LEN];   //日志文件路径
} cmiotLogPath_t;

typedef struct
{
    cmiot_uint32_t startTime;             // 日志文件上报时间范围的开始时间（单位：秒，UTC时间），如为0，则表示返回endTime之前的所有日志
    cmiot_uint32_t endTime;               // 日志文件上报时间范围的结束时间（单位：秒，UTC时间）
} cmiotLogReportTimeRange_t;

typedef struct
{
    cmiot_uint32_t logNum;                // 时间范围内上报日志文件的个数
    cmiot_uint64_t logSize;               // 时间范围内上报日志文件的总大小（单位：byte）
    cmiotLogPath_t *pathInfo;             // 时间范围内符合条件的日志文件的路径
} cmiotLogReportInfo_t;

typedef struct
{
    cmiot_uint32_t presetPointCodeNum;     // 预置点编码数组中元素个数
    cmiot_uint32_t *codes;                 // 预置点编码数组指针
} cmiotPresetPointCodeInfo_t;

typedef struct
{
    cmiot_uint32_t cruiseCodeNum;     // 巡航路线编码数组中元素个数
    cmiot_uint32_t *codes;            // 巡航路线编码数组指针
}cmiotCruiseCodeInfo_t;

typedef struct
{
    cmiot_uint32_t action;                 // 操作码 1-新增 2-修改 3-删除 4-跳转
    cmiot_uint32_t presetPointCode;        // 预置点编码 取值范围 [1~255]
} cmiotPtzPresetPointInfo_t; 

typedef struct
{
    cmiot_uint32_t action;                   // 操作码 1-新增 2-修改 3-删除
    cmiot_uint32_t cruiseCode;               // 巡航路线编码 取值范围 [1~255]
    cmiot_uint32_t speed;                    // 巡航速度 取值范围 [1~4095]
    cmiot_uint32_t stayTime;                 // 停留时间 取值范围 [1~4095]，单位：秒
    cmiotPresetPointCodeInfo_t codesInfo;    // 巡航路线预置点列表信息（按列表顺序巡航）
} cmiotPtzCruiseInfo_t;

typedef struct
{
    cmiot_uint8_t repeat;       // 重复计划，低7bit有效，从低位到高位依次表示周日到周六，例：每天重复值为0x7F(127)
                                // 0x01 0x02 0x04 0x08 0x10 0x20 0x40
                                // SUN  MON  TUE  WED  THU  FRI  SAT
    cmiot_uint32_t  startTime;  // 巡航开始时间，单位秒，取值范围 [0-86400]；0表示每天0点，86400表示每天24点，到达开始时间时，立即开始巡航
    cmiot_uint32_t  endTime;    // 巡航结束时间，单位秒，取值范围 [0-86400]；0表示每天0点，86400表示每天24点，到达结束时间时，立即停止巡航
} cmiotPtzCruiseScheduleParam_t;

typedef struct
{
    cmiot_uint8_t flag;                     // 配置项标识，对应位表示是否含有该配置项
                                            // 0x08
                                            // schedule
    cmiot_uint32_t action;                  // 操作码 0-开始 1-停止
    cmiot_uint32_t cruiseCode;              // 巡航路线编码 取值范围 [1~255]
    cmiotPtzCruiseScheduleParam_t schedule; // 定时巡航时间，不含有该配置项时表示全天
}cmiotPtzCruiseControlInfo_t;

/* 镜头控制（变焦、变倍、光圈）参数 */
typedef struct
{
    cmiot_uint32_t action;           /* 操作码 1-焦距变大 2-焦距变小 3-光圈扩大 4-光圈缩小 5-图像放大 6-图像缩小 7-焦点前调 8-焦点后调*/
    cmiot_uint32_t speed;            /* 速度，取值范围1~7，速率递增 */
    cmiot_uint32_t move;             /* 控制动作 0-开始 1-停止 2-单步控制 */
}cmiotLensControlInfo_t;

/* 一次性声光报警配置 */
typedef struct
{
    cmiot_char_t   soundUrl[CMIOT_MAX_URL_LEN]; // 声音报警参数 灯光持续时长
    cmiot_uint32_t soundTimes;                  // 声音报警参数 声音播放次数
    cmiot_uint8_t  lightMode;                   // 灯光报警参数 模式 0-闪烁 1-常亮
    cmiot_uint32_t lightTime;                   // 灯光报警参数 灯光持续时长 单位秒
}cmiotSoundLightAlarmOnce_t;

typedef enum
{
    CMIOT_RTN_VIDEO_RESOLUTION_INVALID = 0,
    CMIOT_RTN_VIDEO_RESOLUTION_240P,  // 320  * 240
    CMIOT_RTN_VIDEO_RESOLUTION_360P,  // 640  * 360
    CMIOT_RTN_VIDEO_RESOLUTION_480P,  // 640  * 480
    CMIOT_RTN_VIDEO_RESOLUTION_720P,  // 1280 * 720
    CMIOT_RTN_VIDEO_RESOLUTION_960P,  // 1280 * 960
    CMIOT_RTN_VIDEO_RESOLUTION_1080P, // 1920 * 1080
    CMIOT_RTN_VIDEO_RESOLUTION_1296P, // 2304 * 1296
    CMIOT_RTN_VIDEO_RESOLUTION_1920P, // 2560 * 1920
    CMIOT_RTN_VIDEO_RESOLUTION_2K, 	  // 2560 * 1440
} cmiotRtnVideoReso_e;


/* RTN通道推流时可能触发的视频配置参数 */
typedef struct 
{
	cmiotRtnVideoReso_e resolution;  //分辨率 
    cmiot_int32_t gop;         //关键帧间隔
    cmiot_int32_t fps;         //每秒帧率
    cmiot_int32_t bps;         //每秒比特率
	cmiot_int32_t initQP;     //初始量化参数
	cmiot_int32_t ImaxQP;      //I帧（关键帧）的最大和最小量化参数
	cmiot_int32_t IminQP;
	cmiot_int32_t PmaxQP;      //P帧（关键帧）的最大和最小量化参数
	cmiot_int32_t PminQP;      
    cmiot_int32_t bVbr;        //cbr: 0 vbr: 1
} cmiotRtnVideoParams_t;

/* RTN通道推流时可能触发的视频RC配置参数 */
typedef struct
{
    cmiot_int32_t bitrate;
    cmiot_int32_t IminQP;
    cmiot_int32_t ImaxQP;
    cmiot_int32_t PminQP;
    cmiot_int32_t PmaxQP;
    cmiot_int32_t fps;
    cmiot_int32_t maxpercent;
} cmiotRtnRcChangeParams_t;

/* 延时开启关闭白光灯相关结构体：开关转换和延迟时间 */
typedef struct
{
    cmiot_uint32_t status;    //开关状态，0-关闭，1-开启
    cmiot_int32_t delayTime;  //延迟时间，单位：秒，0表示立即开启或关闭白光灯
} cmiotWhiteLightDelaySwitch_t ;

/* 电池电量告警触发规则 */
typedef struct
{
    cmiot_uint32_t fullBatTrigger;       // 满电告警触发值F1，百分比，默认为100，电量充至F1时产生告警
    cmiot_uint32_t fullBatLowerTrigger;  // 满电下沿触发值F2，百分比，默认为90，电量充至F1后，电量降低到F2后再度充至F1方可再次产生告警；
    cmiot_uint32_t lowBatTrigger;        // 低电量告警触发值L，百分比，默认为20，电量降低至L时告警，如果升高至L，则不进行告警；
    cmiot_uint32_t batExhaustTrigger;    // 电量耗尽告警触发值D，百分比，默认为5，电量降低至D时产生告警，如果升高至D，则不进行告警；
}cmiotBatAlarmTriggerRule_t;

/* 电池电量告警规则信息 */
typedef struct
{
    cmiot_uint32_t status;              // 告警开关，1-打开，0-关闭

    cmiot_bool_t ruleExist;             // 是否存在告警规则
    cmiotBatAlarmTriggerRule_t rule;    // 告警规则

    cmiot_bool_t spanExist;             // 是否存在告警间隔
    cmiot_uint32_t span;                // 告警间隔，距上次告警时间间隔达到此数值时，方可再次上报对应的告警事件
                                        // 仅对告警规则中低电量告警和电量耗尽告警生效
}cmiotBatAlarmRuleInfo_t;

/* 蜂窝信号告警规则信息 */
typedef struct
{
    cmiot_uint32_t status;                // 告警开关，1-打开，0-关闭

    cmiot_bool_t thresholdExist;          // 是否存在告警阈值
    cmiot_uint32_t threshold;             // 告警阈值，信号强度低于此值时产生告警，取值范围 [0~100]。默认值50

    cmiot_bool_t spanExist;               // 是否存在告警间隔
    cmiot_uint32_t span;                  // 告警间隔，单位分钟，距上次告警时间间隔达到此数值时，方可再次上报告警事件。默认值30
}cmiotCellAlarmRuleInfo_t;

/* 缩略图上报信息 */
typedef struct
{
    cmiot_uint32_t num;                 // 上传缩略图数量
    cmiotPicData_t *thumbInfo;          // 缩略图信息
}cmiotThumbUploadInfo_t;

/* SD卡存储视频流信息 */
typedef struct
{
    cmiot_int32_t videoType;           /* 1: h264  2:h265，需与主码流编码格式保持一致 */
    cmiot_int32_t videoWidth;          /* 分辨率宽 */
    cmiot_int32_t videoHeight;         /* 分辨率高 */
    cmiot_int32_t audioType;           /* 1：G726（已废弃，不再支持），2：G711A，3：AAC */
    cmiot_int32_t audioKhz;            /* 音频采样率例如采样率为8khz，此处值即为8 */
    cmiot_int32_t audioChannel;        /* 音频声道数 */
}cmiotSdStreamParam_t;


/* 共享内存写入参数 */
typedef struct
{
    void *handle;      /* 共享内存句柄 */
    cmiot_uint32_t width;   /* 图像分辨率宽 如为0则表示不指定分辨率 */
    cmiot_uint32_t height;  /* 图像分辨率高 如为0则表示不指定分辨率 */
    cmiot_uint32_t fps;     /* 帧率 */
} cmiotShmemWriteInfo_t;

/* 共享内存写入数据定义 */
typedef struct
{
    cmiot_uint32_t size;    // 数据大小
    cmiot_char_t  *data;   // 数据内容
    cmiot_uint64_t time;    // 帧写缓存utc时间，单位毫秒
    cmiot_uint64_t pts;     // 帧生成的时间戳，单位毫秒
} cmiotShmemData_t;


/* 声音告警参数 */
typedef struct
{
    cmiot_char_t audioPath[CMIOT_MAX_PATH_LEN]; /* 声音文件路径 */
    cmiot_uint32_t times;   /* 播放次数 */
} cmiotSoundAlarmInfo_t;

/* 灯光告警参数 */
typedef struct
{
    cmiot_uint32_t mode;    /* 灯光警示模式，0-闪烁 1-长亮 */
    cmiot_uint32_t time;   /* 灯光警示（闪烁或长亮）持续时长，单位秒 */
} cmiotLightAlarmInfo_t;

/* 算法管理参数 */
typedef struct
{
    cmiot_char_t picPath[CMIOT_MAX_PATH_LEN];        /* 下发算法APP抓图保存路径 */
    cmiot_char_t logPath[CMIOT_MAX_PATH_LEN];        /* 下发算法APP日志保存路径 */
    cmiot_char_t configPath[CMIOT_MAX_PATH_LEN];     /* 下发算法APP配置保存路径 */
    cmiot_char_t aiAlgoPath[CMIOT_MAX_PATH_LEN];     /* AI算法包保存路径 */
}cmiotAlgoManagerParams_t;

/* 定时云广播计划信息 */
typedef struct
{
    cmiot_uint32_t mode;            /* 定时模式，1-按星期播放，2-按日期播放 */
    cmiot_uint32_t loop;            /* 播放模式，0-单次播放，1-循环播放 */
    cmiot_uint32_t week;            /* 播放星期，低7位有效，按二进制从低位到高位依次表示周日到周六：
                                    SUN:0x0001, MON:0x0002, TUE:0x0004, WED:0x0008, THU:0x0010, FRI:0x0020, SATU:0x0040
                                    mode为1时该字段有效 */
    cmiot_char_t timePeriod[5][12]; /* 时间段，播放当天的指定时间段内进行播放，单次播放在到达时间段的开始时间时播放一次，
                                       循环播放在时间段内循环播放，格式HH:mm-HH:mm，最多五个时间段
                                       如时间段均为空，表示关闭定时任务 */
    cmiot_char_t days[22];          /* 播放日期，日期范围格式yyyy/MM/dd-yyyy/MM/dd，mode为2时该字段有效 */
    cmiot_uint32_t span;            /* 间隔时间，播放时间段内，重复播放时的间隔时间，单位秒，取值范围0-86400，loop为1时该字段有效 */
    cmiot_char_t id[CMIOT_MAX_AUDIO_ID_LEN];    /* 语音id，设备需要保存此ID并比对已有ID，若不一致，访问url进行下载 */
    cmiot_char_t url[CMIOT_MAX_URL_LEN];        /* 音频下载地址，设备需主动下载此语音内容（格式：G711A，采样率：8K，16bit，音频时长不超过1min） */
}cmiotVoiceBroadcastScheduleInfo_t;

/* 获取timeline入参 */
typedef struct
{
	cmiot_uint64_t startTime;      /* 开始时间 utc时间, 毫秒  */
	cmiot_uint64_t endTime;        /* 结束时间 utc时间, 毫秒 */
    cmiot_uint32_t pageSize;       /* 返回的最大条数 */
} cmiotTimelineRange_t;

/* section信息 */
typedef struct
{
    cmiot_uint64_t startTime;     /* section开始时间 */
    cmiot_uint64_t endTime;       /* section结束时间 */
    cmiotEventType_e alarmType;   /* 告警类型，上报告警timeline section信息时，该字段必传 */
} cmiotSectionInfo_t;

/* 获取timeline出参 */
typedef struct
{
    cmiot_uint32_t sectionNum;         /* section数量, 在时间段内，有多少个timeline分段 */
    cmiotSectionInfo_t *sections;      /* section信息, 按照时间顺序排列；内存由设备申请，sdk内部释放*/
    cmiot_bool_t hasMore;              /* 起止时间内的是否还有录像列表未上报 */
} cmiotTimelineInfo_t;

/* 获取告警timeline入参 */
typedef struct
{
    cmiotTimelineRange_t rangeInfo;    /* 时间段信息 */
    cmiot_uint32_t alarmTypeNum;       /* 查询的告警类型数量，当值为0时，表示全量查询 */
    cmiotEventType_e *alarmTypeList;   /* 查询的告警类型 */
} cmiotAlarmTimelineRange_t;

/* 获取告警timeline出参 */
typedef cmiotTimelineInfo_t cmiotAlarmTimelineInfo_t;

typedef cmiotAIScheduleParam_t cmiotSoundLightNewSchedule_t;

/* 新声光报警配置单组计划 */
typedef struct
{
    cmiotSoundLightNewSchedule_t schedule;              /* 时间段计划 */ 
    cmiotSoundLightAlarmMode_e mode;                    /* 声光报警模式 */
    cmiot_uint32_t volume;                              /* 声光告警音量，当mode值为1、2时，该字段有效 */ 
    cmiot_uint32_t lightTime;                           /* 声光报警灯光闪烁或持续时长，单位秒，当该字段值为0时，表示声光报警灯光持续闪烁或持续亮灯。当mode值为1、3、4时，该字段有效*/ 
    cmiot_uint32_t soundTimes;                          /* 声光报警语音播报次数，当该字段值为0时，表示声光报警语音持续播报。当mode值为1、2时，该字段有效 */
    cmiot_char_t soundUrl[CMIOT_MAX_URL_LEN];           /* 声光报警语音包的下载地址，设备需主动下载此语音内容（格式：G711A，采样率：8K，16bit，音频时长不超过1min）。当mode值为1、2时，该字段有效 */
    cmiot_char_t soundId[CMIOT_MAX_AUDIO_ID_LEN];       /* 声光报警语音包id，设备需要保存此ID并比对已有ID，若不一致，访问soundUrl进行下载，当mode值为1、2时，该字段有效 */
} cmiotSoundLightCfgNewSingleInfo_t;

/* 新声光报警配置计划 */
typedef struct
{
    cmiot_uint32_t num;                                                                   /* 新声光报警配置组数 */
    cmiotSoundLightCfgNewSingleInfo_t cfgInfo[CMIOT_SOUND_LIGHT_ALARM_CFG_NEW_MAX];       /* 新声光报警配置信息 */
} cmiotSoundLightAlarmCfgNewInfo_t;

typedef struct
{
    cmiot_bool_t reportWiredInfo;                       /* 是否上报有线接入信息*/
    cmiot_uint32_t networkSpeed;                        /* 有线接入时与路由器协商的有线网口速率，单位Mbps */
} cmiotAccessWiredInfo_t;

typedef struct
{
    cmiot_bool_t reportWifiInfo;                        /* 是否上报WiFi接入信息 */
    cmiot_uint32_t linkSpeed;                           /* WiFi接入时无线热点的连接速度，单位Mbps */
    cmiot_int32_t wifiSignal;                           /* WiFi信号强度，单位dBm */
    cmiotAccessWifiFrequency_e frequency;               /* WiFi接入连接热点的频段 */
} cmiotAccessWifiInfo_t;

typedef struct
{
    cmiot_bool_t reportCellularInfo;                    /* 是否上报蜂窝接入信息 */
    cmiot_int32_t cellularSignal;                       /* 蜂窝信号强度，单位dBm */
} cmiotAccessCellularInfo_t;

/* 网络接入网络配置参数 */
typedef struct
{
    cmiot_char_t dns[64];                               /* DNS地址 */
    cmiot_char_t ipv6Dns[64];                           /* ipv6的DNS地址 */
    cmiot_char_t ipv4Addr[64];                          /* 终端ipv4地址 */
    cmiot_char_t ipv6Addr[64];                          /* 终端ipv6地址 */
    cmiot_char_t gateway[64];                           /* 网关地址 */
    cmiot_char_t gatewayMac[CMIOT_MAX_MAC_LEN];         /* 网关MAC */
} cmiotAccessNetParamInfo_t;

/* 网络接入信息 */
typedef struct
{
    cmiotAccessMethod_e accessMethod;                   /* 网络接入方式 */
    cmiotAccessNetParamInfo_t accessNetParams;          /* 网络配置参数 */
    union
    {
        cmiotAccessWiredInfo_t accessWiredInfo;         /* 有线连接信息，网络通过有线接入时填充 */
        cmiotAccessWifiInfo_t  accessWifiInfo;          /* Wifi连接信息，网络通过WiFi接入时填充 */
        cmiotAccessCellularInfo_t accessCellularInfo;   /* 蜂窝网络连接信息，网络通过蜂窝接入时填充 */
    } netInfo;
} cmiotAccessNetInfo_t;


typedef cmiotAIScheduleParam_t cmiotCommonSoundLightSchedule_t;

typedef struct
{
    cmiot_int32_t type;              // 此声音告警配置联动的功能，1-按键告警  其他值-预留
    cmiot_bool_t status;             // 语音播报开关
    cmiot_int32_t times;             // 语音播报次数，0表示满足条件时，持续播报
    cmiot_int32_t volume;            // 语音播报音量，取值范围 0~100
    cmiot_char_t id[CMIOT_MAX_AUDIO_ID_LEN];    // 音频id
    cmiot_char_t url[CMIOT_MAX_URL_LEN];        // 音频下载地址，设备需主动下载此音频内容（格式：G711A，采样率：8K，16bit） */
    cmiotCommonSoundLightSchedule_t schedule; // 功能开启时间计划，status开启时该字段有效，缺省为7*24全天开启
} cmiotCommonSoundAlarmCfg_t;

typedef struct
{
    cmiot_int32_t num;
    cmiotCommonSoundAlarmCfg_t cfg[1];    // 柔性数组，实际按num长度进行解析
} cmiotCommonSoundAlarmCfgs_t;

typedef struct
{
    cmiot_int32_t type;             // 此灯光告警配置联动的功能，1-按键告警  其他值-预留
    cmiot_bool_t status;            // 灯光警示开关
    cmiot_int32_t mode;             // 灯光警示模式，0-闪烁 1-长亮
    cmiot_int32_t time;             // 灯光警示（闪烁或长亮）持续时长，单位秒，0表示满足条件时，持续闪烁/常亮
    cmiotCommonSoundLightSchedule_t schedule; // 功能开启时间计划，status开启时该字段有效，缺省为7*24全天开启
} cmiotCommonLightAlarmCfg_t;

typedef struct
{
    cmiot_int32_t num;
    cmiotCommonLightAlarmCfg_t cfg[1];    // 柔性数组，实际按num长度进行解析
} cmiotCommonLightAlarmCfgs_t;

#ifdef __cplusplus
}
#endif

#endif