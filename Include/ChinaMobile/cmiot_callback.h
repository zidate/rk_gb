#ifndef __CMIOT_CALLBACK_H__
#define __CMIOT_CALLBACK_H__

#ifdef __cplusplus
  extern "C" {
#endif

#include "cmiot_define.h"

/* 设备配置命令定义 */
typedef enum {
    CMIOT_CMD_SET_START = 2000,                      /* 配置命令开始 */

    //【入参数据类型】int  【取值范围】30、50、80、90、100  【说明】默认是80，值越大灵敏度越高
    CMIOT_CMD_SET_MOTION_DETECTION_SENSITIVITY,      /* 配置运动检测灵敏度 */

    //【入参数据类型】int  【取值范围】80、85、90、95、100  【说明】默认是90，值越大灵敏度越高
    CMIOT_CMD_SET_SOUND_DETECTION_SENSITIVITY,       /* 配置声音检测灵敏度 */

    //【入参数据类型】int  【取值范围】0-静音  1-打开
    CMIOT_CMD_SET_MIC_STATUS,                        /* 配置麦克风开关 */

    //【入参数据类型】int  【取值范围】0-不旋转  180-旋转180°
    CMIOT_CMD_SET_VIDEO_ROTATE,                      /* 配置视频画面旋转 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_LED_STATUS,                        /* 配置指示灯开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开  2-自动
    CMIOT_CMD_SET_NIGHTVISIGON_MODE,                 /* 配置夜视模式 */

    //【入参数据类型】int  【取值范围】50-表示50HZ  60-表示60HZ
    CMIOT_CMD_SET_ANTIFLICKER_VALUE,                 /* 配置防闪烁频率 */

    //【入参数据类型】int  【取值范围】1-打开  4-强制关闭  【说明】关闭时停止推流
    CMIOT_CMD_SET_DEVICE_STATUS,                     /* 配置设备开关状态 */

    //【入参数据类型】cmiotMotionRegionList_t  【说明】如果所有区域全选，区域个数为0，数组指针为NULL
    CMIOT_CMD_SET_MOTION_DETECTION_REGION,           /* 配置运动检测区域 */

    //【入参数据类型】cmiotScheduleList_t  【说明】如果startTime > endTime，则表示跨天，关闭时停止推流
    CMIOT_CMD_SET_DEVICE_CLOSE_SCHEDULE,             /* 配置设备关闭计划 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_MOTION_TRACKING_STATUS,            /* 配置运动跟踪开关 */

    //【入参数据类型】int  【说明】配置运动跟踪多少秒内无画面变化，摄像机自动回到旋转前位置
    CMIOT_CMD_SET_MOTION_TRACKING_RESET_TIMEOUT,     /* 配置运动跟踪归位时间 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_HDR_STATUS,                        /* 配置宽动态能力开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_STATUS,          /* 配置声光报警开关 */

    //【入参数据类型】int  【取值范围】0~100
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_VOL,             /* 配置声光报警音量 */

    //【入参数据类型】cmiotSoundLightAlarmMode_e
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_MODE,            /* 配置声光报警模式 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_STATUS,    /* 配置全彩夜视开关 */

    //【入参数据类型】int  【取值范围】1-黑白夜视  2-全彩夜视  3-智能夜视，产生告警则夜视画面彩色
    CMIOT_CMD_SET_FULL_COLOR_NIGHTVISIGON_MODE,      /* 配置全彩夜视模式 */

    //【入参数据类型】cmiotInvadeRegionList_t  【说明】设备需根据设备分辨率按比例做好适配，根据四个点的坐标计算出区域
    CMIOT_CMD_SET_INVADE_DETECTION_REGION,           /* 配置区域入侵检测区域 */

    //【入参数据类型】int  【取值范围】0~100
    CMIOT_CMD_SET_LIGHT_BRIGHTNESS,                  /* 配置白光灯亮度 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_HUMAN_TRACKING_STATUS,             /* 配置人形运动跟踪开关 */

    //【入参数据类型】int  【取值范围】1-主码流  2-辅码流  【说明】设备向平台上传实时音视频数据包过程中，立即生成一个关键帧发送
    CMIOT_CMD_SET_I_FRAME,                           /* 强制设备生成关键帧 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_FACE_CAPTURE,                    /* 设置AI人脸抓拍开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_FACE_CONTRAST,                   /* 设置AI人脸比对开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_VEHICLE_CAPTURE,                 /* 设置AI车辆抓拍开关 */

    //【入参数据类型】int  【取值范围】0到100,灵敏度越高，报警消息越多
    CMIOT_CMD_SET_AI_SENSITIVITY,                     /* 设置AI检测灵敏度 */

    //【入参数据类型】cmiotRectangleRegionInfo_t  【取值范围】设备需根据设备分辨率做好适配，根据四个点的坐标计算出区域，如传入NULL则表示全区域检测
    CMIOT_CMD_SET_AI_REGIONS,                         /* 设置AI检测区域 */

    //【入参数据类型】cmiotOSDInfo_t 【说明】设置OSD
    CMIOT_CMD_SET_OSD,                               /* 设置OSD */

    //【入参数据类型】int  【取值范围】1-h264 / 2-h265
    //【说明】接收该回调时，先停止推流，再调用cmiot_set_stream_param更新配置，然后切换编码格式，若切换成功返回CMIOT_RETURN_CODE_SUCCESS
    //       若切换失败，调用cmiot_set_stream_param恢复配置，再调用cmiot_update_device_config(CMIOT_UPDATE_CFG_CODED_FORMAT,...)上报当前生效的编码格式，返回CMIOT_RETURN_CODE_FAILED
    CMIOT_CMD_SET_CODED_FORMAT,                      /* 设置视频编码格式 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_MASK_DETECTION_STATUS,          /* 配置AI口罩检测开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_EBIKE_CAPTURE_STATUS,           /* 配置AI电动车检测开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_AI_PASSENGER_STAT_STATUS,          /* 配置AI客流统计开关 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_FACE_CAPTURE_SEN,               /* 配置AI人脸抓拍灵敏度 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_FACE_CONTRAST_SEN,              /* 配置AI人脸比对灵敏度 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_SEN,            /* 配置AI车辆抓拍灵敏度 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_MASK_DETECTION_SEN,             /* 配置AI口罩检测灵敏度 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_EBIKE_CAPTURE_SEN,              /* 配置AI电瓶车抓拍灵敏度 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_AI_PASSENGER_STAT_SEN,             /* 配置AI客流统计灵敏度 */

    //【入参数据类型】cmiotRectangleRegionInfo_t  【取值范围】设备需根据设备分辨率做好适配，根据四个点的坐标计算出区域，以下同理
    CMIOT_CMD_SET_AI_FACE_CAPTURE_REGION,            /* 配置AI人脸抓拍区域 */

    //【入参数据类型】cmiotRectangleRegionInfo_t
    CMIOT_CMD_SET_AI_FACE_CONTRAST_REGION,           /* 配置AI人脸比对区域 */

    //【入参数据类型】cmiotRectangleRegionInfo_t
    CMIOT_CMD_SET_AI_VEHICLE_CAPTURE_REGION,         /* 配置AI车辆抓拍区域 */

    //【入参数据类型】cmiotRectangleRegionInfo_t
    CMIOT_CMD_SET_AI_MASK_DETECTION_REGION,          /* 配置AI口罩检测区域 */

    //【入参数据类型】cmiotRectangleRegionInfo_t
    CMIOT_CMD_SET_AI_EBIKE_CAPTURE_REGION,           /* 配置AI电瓶车抓拍区域 */

    //【入参数据类型】cmiotPassengerStatRule_t
    CMIOT_CMD_SET_AI_PASSENGER_STAT_RULE,            /* 配置AI客流统计规则 */

    //【入参数据类型】cmiotBattLowPowerMode_t
    CMIOT_CMD_SET_BATT_LOW_POWER_MODE,               /* 配置电池低功耗模式 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_SPEAKER_VOLUME,                    /* 配置扬声器音量 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_MOTION_DETECTION_STATUS,           /* 配置运动检测开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_SOUND_DETECTION_STATUS,            /* 配置声音检测开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_BABYCRY_DETECTION_STATUS,          /* 配置哭声检测开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_CLICK_ALARM_STATUS,                /* 配置按键报警开关 */

    //【入参数据类型】cmiotBatAlarmRuleInfo_t
    //【说明】告警开关为打开时，告警规则和告警间隔必存在
    //        告警开关为关闭时，告警规则和告警间隔根据是否存在进行配置
    CMIOT_CMD_SET_BAT_ALARM_RULE,                    /* 配置电池电量告警规则 */

    //【入参数据类型】cmiotCellAlarmRuleInfo_t  
    //【说明】告警开关为打开时，告警阈值和告警间隔必存在
    //       告警开关为关闭时，告警阈值和告警间隔根据是否存在进行配置
    CMIOT_CMD_SET_CELLULAR_ALARM_RULE,              /* 配置蜂窝信号告警规则 */

    //【入参数据类型】cmiotVoiceBroadcastScheduleInfo_t 
    CMIOT_CMD_SET_VOICE_BROADCAST_SCHEDULE,         /* 配置定时云广播计划 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_REGION_INVASION_BOX_STATUS,       /* 配置区域入侵实时检测框开关 */

    //【入参数据类型】int  【取值范围】bit0-人体检测, bit1-运动检测; 0-均不支持 1-仅人体 2-仅运动 3-均支持
    CMIOT_CMD_SET_REGION_INVASION_MODE,             /* 配置区域入侵检测模式 */

    //【入参数据类型】cmiotSoundLightAlarmCfgNewInfo_t
    //【说明】当CMIOT_CMD_SET_SOUND_LIGHT_ALARM_STATUS打开时，此配置生效
    // 收到此回调时，如本机已存在CMIOT_CMD_SET_SOUND_LIGHT_ALARM_VOL、CMIOT_CMD_SET_SOUND_LIGHT_ALARM_MODE、CMIOT_CMD_SET_SOUND_LIGHT_ALARM_LIGHT_TIME、CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_TIMES、CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_URL、CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_ID的配置，需删除以上配置，以此新配置为准
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_CFG_NEW,        /* 配置新声光告警计划 */
       
    // 无出参入参
    //【说明】配置项恢复默认设置后，需先通过cmiot_update_device_config接口上报发生变化的设置项，最后调用cmiot_update_device_config_send接口
    CMIOT_CMD_SET_RESTORE_SETTINGS,                 /* 配置所有设置项恢复默认设置 */

    //【入参数据类型】int  【取值范围】0-视频画面未发生翻转 1-视频画面为水平翻转状态 2-视频画面为垂直翻转状态 3-视频画面为水平和垂直同时翻转状态
    CMIOT_CMD_SET_CAMERA_IMAGE_FLIP,                /* 配置视频画面翻转状态 */

    //【入参数据类型】int  【取值范围】0-100
    CMIOT_CMD_SET_MICROPHONE_VOLUME,                /* 配置麦克风音量 */

    //【入参数据类型】cmiotWhiteLightDelaySwitch_t
    CMIOT_CMD_SET_WHITE_LIGHT,                      /* 配置白光灯开关 */

    //【入参数据类型】int  【取值范围】0-关闭  1-打开
    CMIOT_CMD_SET_EVENT_PUSH_STATUS,                /* 配置事件上报开关 */

    //【入参数据类型】 cmiotCommonSoundAlarmCfgs_t
    CMIOT_CMD_SET_COMMON_SOUND_ALARM_CFG,           /* 设置通用声音告警配置 */

    //【入参数据类型】 cmiotCommonLightAlarmCfgs_t
    CMIOT_CMD_SET_COMMON_LIGHT_ALARM_CFG,           /* 设置通用灯光告警配置 */

    //【入参数据类型】int
    CMIOT_CMD_SET_SUBCODING_RESOLUTION,             /* 配置辅码流分辨率配置 */

    //【入参数据类型】int
    CMIOT_CMD_SET_SD_REC_STREAM_TYPE,             /* 配置SD卡录制码流类型*/

    //【入参数据类型】int 
    //【说明】当入参值为0时，设备声光报警须配置为灯光持续闪烁或持续亮灯
    //       设备需要保存此参数，并在触发声光报警时应用
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_LIGHT_TIME,      /* 配置声光报警效果 灯光持续时长*/

    //【入参数据类型】int 
    //【说明】当入参值为0时，设备声光报警须配置为语音持续播报
    //       设备需要保存此参数，并在触发声光报警时应用
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_TIMES,     /* 配置声光报警效果 语音播放次数*/

    //【入参数据类型】char[] 【说明】设备需要主动从该地址下载语音包，并根据ID适时更新
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_URL,       /* 配置声光报警效果 语音包下载地址*/

    //【入参数据类型】char[] 【说明】设备需要保存此ID并比对已有ID，若不一致则需根据下载地址取得新的语音包
    CMIOT_CMD_SET_SOUND_LIGHT_ALARM_SOUND_ID,        /* 配置声光报警效果 语音包ID*/

    //【入参数据类型】int  【取值范围】1-g711a / 2-aac  【说明】设置音频编码格式
    CMIOT_CMD_SET_AUDIO_CODEC_FORMAT,                /* 设置音频编码格式 */

    //【入参数据类型】cmiotKeepaliveInfo_t 【说明】支持休眠的低功耗设备，上线后可调用api cmiot_get_lowpower_keepalive获取低功耗保活信息
    // 获取结果通过本回调通知
    CMIOT_CMD_SET_LOW_POWER_KEEPALIVE,              /* 低功耗保活信息 */

}cmiotDevConfigCmd_e;

typedef enum {
    //【入参数据类型】cmiotAIConfigParam_t 【说明】该场景包含的信息与含义详见demo和开发指导手册，下同
    CMIOT_CMD_SET_AI_FACE_SCENE_CFG,                /* 配置AI人脸识别场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_VEHICLE_SCENE_CFG,             /* 配置AI车辆场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_BASIC_SCENE_CFG,               /* 配置AI基础行为分析场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_CROWDED_SCENE_CFG,             /* 配置AI人群态势分析场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_PASSFLOW_SCENE_CFG,            /* 配置AI客流统计场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_KITCHEN_SCENE_CFG,             /* 配置AI智慧厨房场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_ELEVATOR_SCENE_CFG,            /* 配置AI智慧电梯场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_FALLINGOBJ_SCENE_CFG,          /* 配置AI高空抛物场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_SMOKEFIRE_SCENE_CFG,           /* 配置AI烟火检测场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_CITYOFFICER_SCENE_CFG,         /* 配置AI城管场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_WORKSITE_SCENE_CFG,            /* 配置AI工地场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_GASSTATION_SCENE_CFG,          /* 配置AI加油站场景配置 */

    //【入参数据类型】cmiotAIConfigParam_t
    CMIOT_CMD_SET_AI_SAFEDRIVING_SCENE_CFG,         /* 配置AI安全驾驶场景配置 */

}cmiotDevAIConfigCmd_e;

/* 设备查询命令定义 */
typedef enum{
    CMIOT_CMD_GET_START = 2100,            /* 查询命令开始 */

    //【出参数据类型】cmiotWifiList_t
    CMIOT_CMD_GET_WIFI_LIST,               /* 查询WiFi列表 */

    //【出参数据类型】cmiotSdcardSpaceInfo_t
    CMIOT_CMD_GET_SD_CARD_SPACE,           /* 查询SD卡容量 */

    //【出参数据类型】cmiotSdcardPartList_t
    CMIOT_CMD_GET_SD_CARD_PARTITION,       /* 查询SD卡分区信息 */

    //【出参数据类型】cmiotConWifiInfo_t
    CMIOT_CMD_GET_WIFI_SIGNAL,             /* 查询WiFi信号强度 */

    //【出参数据类型】cmiotSystemStatInfo_t
    CMIOT_CMD_GET_SYSTEM_STATUS,           /* 获取系统信息 */

    //【出参数据类型】cmiot_uint8_t 【说明】蜂窝信号强度，范围0~100
    CMIOT_CMD_GET_CELLULAR_SIGNAL,         /* 获取蜂窝信号强度 */

    //【出参数据类型】cmiotBatteryInfo_t
    CMIOT_CMD_GET_BATTERY_STATUS,          /* 获取电量信息 */

    //【入参数据类型】cmiotLogReportTimeRange_t    【出参类型】cmiotLogReportInfo_t
    //【说明】需返回相应时间范围内的设备日志文件路径（单个日志文件大小不要超过1M），参考指导手册相关说明和demo示例代码。
    CMIOT_CMD_GET_DEV_LOG,                 /* 获取设备日志 */

    // 【入参数据类型】 cmiotTimelineRange_t    【出参类型】 cmiotTimelineInfo_t
    // 【说明】externalSdRecord打开使用外部录制时，通过此回调查找时间范围内的普通timeline，设备按规定格式返回
    // cmiotTimelineInfo_t类型的出参在SDK内申请内存，存在timeline时sections字段由设备申请内存，SDK释放
    CMIOT_CMD_GET_DEV_SD_NORMAL_TIMELINE,         /* 获取本地存储普通timeline */

    // 【入参数据类型】 uint64_t    utc时间，单位毫秒  【出参数据类型】 int32_t  视频编码格式：1-h264  2-h265
    // 【说明】externalSdRecord打开使用外部录制时，通过此回调查找指定时间位置所属录像文件中视频流的编码格式
    CMIOT_CMD_GET_DEV_SD_CODED_FORMAT_BY_TIME,     /* 获取录像编码格式 */

    //【出参数据类型】cmiotAccessNetInfo_t
    CMIOT_CMD_GET_DEV_ACCESS_NET_INFO,      /* 获取设备网络接入信息 */

    // 【入参数据类型】 cmiotAlarmTimelineRange_t    【出参类型】 cmiotAlarmTimelineInfo_t
    // 【说明】externalSdRecord打开使用外部录制并且支持sdcardEventPlaybackV2能力时，通过此回调，根据查询开始结束时间、通道号以及告警类型，查找符合条件的告警timeline设备按规定格式返回
    // cmiotAlarmTimelineInfo_t类型的出参在SDK内申请内存，存在告警timeline时sections字段由设备申请内存，SDK释放
    CMIOT_CMD_GET_DEV_SD_ALARM_TIMELINE,      /* 获取告警timeline */
}cmiotGetInfoCmd_e;

/* 设备控制命令定义 */
typedef enum{
    CMIOT_CMD_CONTROL_START = 2200,        /* 控制命令开始 */

    //【入参数据类型】cmiotModifyWifiReqInfo_t 【说明】WiFi为隐藏网络时，encryption字段不为空
    CMIOT_CMD_CONTROL_MODIFY_WIFI,         /* 修改WiFi */

    //无入参出参
    CMIOT_CMD_CONTROL_SD_CARD_FORMAT,      /* 格式化SD卡 */

    //【入参数据类型】cmiotThumbnailReqInfo_t  【出参数据类型】cmiotThumbUploadInfo_t
    //【说明】多目摄像头width, height字段同时对多路画面的缩略图生效
    CMIOT_CMD_CONTROL_THUMBNAIL_PUSH,      /* 控制上传缩略图 */

    //【入参类型】cmiot_uint64_t
    //【说明】设备在专网环境无法通过公网NTP服务校时时，调用API cmiot_request_time_calibration请求校时，通过此回调通知UTC时间戳，单位毫秒
    CMIOT_CMD_CONTROL_TIME_CALIBRATION,    /* 校时响应 */

    //无入参出参
    CMIOT_CMD_CONTROL_DEVICE_REBOOT,       /* 设备重启 */

    //无入参出参
    CMIOT_CMD_CONTROL_IPC_REBOOT,          /* 摄像头重启 */

    //【入参数据类型】int  【取值范围】1-HD高清（设备支持的最大分辨率）  0-SD低清（VGA）
    //【说明】切换分辨率后，设备发送的音视频数据时间戳需要重新从0开始计算。切换时，要从关键帧开始切；切换之后，关键帧前要上报正确的sps pps数据
    // 切换完成后，调用API cmiot_notify_resolution_change通知平台当前的分辨率
    CMIOT_CMD_CONTROL_VIDEO_QUALITY,       /* 改变分辨率 */

    //【入参数据类型】cmiotSoundLightAlarmOnce_t 【说明】设备收到后立即根据参数进行声音报警
    CMIOT_CMD_CONTROL_SOUND_ALARM_ONCE,    /* 触发一次性声音报警 */

    //【入参数据类型】cmiotSoundLightAlarmOnce_t 【说明】设备收到后立即根据参数进行灯光报警
    CMIOT_CMD_CONTROL_LIGHT_ALARM_ONCE,    /* 触发一次性灯光报警 */

    //【入参数据类型】cmiot_char_t [1025] 【说明】设备识别命令成功后，执行对应操作
    CMIOT_CMD_CONTROL_DEVICE_COMMON_CMD,   /* 设备通用命令下发 */

    // 以下为不包含在 HEDGW_CTRL_* 中的控制类型

    //【入参数据类型】uint32_t  【取值范围】unix时间戳，单位s
    //【说明】ntp未完成同步时，本地时间与平台时间差值大，连接平台会返回鉴权失败，
    // 此回调将平台侧返回的时间同步到设备，让设备快速接入平台，后续设备仍需周期性进行ntp同步确保时间不偏移
    CMIOT_CMD_CONTROL_SYNC_TIME,     /* 同步时间 */

    //【入参数据类型】 cmiotRtnVideoParams_t  
    //【说明】设备通过RTN通道推流时，可能触发此回调。设备需按照回调参数对视频流编码参数进行调整。此回调设置后，立即刷新I帧。
    // 多目摄像头需对streamId对应的画面进行调整。如分辨率发生变化，需调用cmiot_notify_resolution_change通知平台。
    CMIOT_CMD_CONTROL_RTN_VIDEO_PARAMS,     /* 设置视频参数 */

    //【入参数据类型】int32_t
    //【说明】设备通过RTN通道推流时，可能触发此回调。设备需按照回调参数对视频流FPS进行调整。此回调设置后，不可立即刷新I帧。
    CMIOT_CMD_CONTROL_RTN_VIDEO_FPS,       /* 设置视频FPS */

    //【入参数据类型】 cmiotRtnRcChangeParams_t
    //【说明】设备通过RTN通道推流时，可能触发此回调。设备需按照回调参数对视频流rc参数进行调整。此回调设置后，不可立即刷新I帧。
    CMIOT_CMD_CONTROL_RTN_VIDEO_RC_CHANGE_PARAMS,   /* 设置视频参数 */

    //【入参数据类型】cmiotShmemWriteInfo_t
    //【说明】开始向指定共享内存句柄中写入数据
    // 平台下发AI算法包后，设备需按规定格式，调用API cmiot_shmem_write向共享内存中写入YUV帧数据
    CMIOT_CMD_CONTROL_SHMEM_WRITE_START,   /* 开始向共享内存写入数据 */

    //【入参数据类型】void * 共享内存句柄
    //【说明】停止向指定共享内存句柄中写入数据
    CMIOT_CMD_CONTROL_SHMEM_WRITE_STOP,   /* 停止向共享内存写入数据 */

    // 【入参数据类型】cmiotSoundAlarmInfo_t
    // 【说明】设备读取声音文件，控制扬声器播放指定次数进行声音告警
    CMIOT_CMD_CONTROL_SOUND_ALARM_AI,       /*下发的AI算法触发声音告警 */

    // 【入参数据类型】cmiotLightAlarmInfo_t
    // 【说明】设备控制闪光灯闪烁或长亮指定时长进行灯光告警
    CMIOT_CMD_CONTROL_LIGHT_ALARM_AI,       /* 下发的AI算法触发触发灯光告警 */

    // 【入参数据类型】 cmiot_uint64_t 开始回放的时间, utc时间, 单位毫秒
    // 【说明】externalSdRecord打开使用外部录制时，通过此回调开始回放，设备需seek到开始时间前最近的i帧，调用
    // cmiot_push_sd_replay_video_data和cmiot_push_sd_replay_video_data推送回放流，推流时设备自行控制间隔
    CMIOT_CMD_CONTROL_SD_REPLAY_STRAT,      /* 本地回放开始 */

    // 【说明】externalSdRecord打开使用外部录制时，通过此回调停止回放，设备需停止调用cmiot_push_sd_replay_video_data和
    // cmiot_push_sd_replay_video_data，然后调用cmiot_notify_sd_replay_stop通知SDK
    CMIOT_CMD_CONTROL_SD_REPLAY_END,        /* 本地回放结束 */

    // 【入参数据类型】 cmiot_int32_t 1-打开 0-关闭
    // 【说明】externalSdRecord打开使用外部录制时，通过此回调通知设备开启和关闭SD卡录制
    CMIOT_CMD_CONTROL_SD_RECORD_STATUS,     /* SD卡录制开关 */

    // 【入参数据类型】 cmiot_int32_t 1-打开 0-关闭
    // 【说明】 打开时，设备日志每写满一个文件则调用 cmiot_device_log_report 上传一次
    CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT,    /* 设备主动上报日志开关 */

    // 【入参数据类型】 cmiot_int32_t 1-全天录制 2-事件录制
    // 【说明】 externalSdRecord打开使用外部录制时，通过此回调通知设备设置SD卡录制模式
    CMIOT_CMD_CONTROL_SD_RECORD_TYPE,      /* SD卡录制模式 */
}cmiotDevControlCmd_e;

/* 云台控制命令定义 */
typedef enum{
    CMIOT_CMD_PTZ_START = 2300,            /* 云台控制命令开始 */

    //【出参数据类型】cmiotPtzPositionInfo_t
    CMIOT_CMD_PTZ_GET_POSITION,            /* 获取云台机当前位置 */

    //【入参数据类型】cmiotPtzPositionInfo_t
    CMIOT_CMD_PTZ_MOVE_TO_POSITION,        /* 设置云台机绝对位置 */

    //【入参数据类型】cmiotPtzMoveInfo_t
    CMIOT_CMD_PTZ_MOVE,                    /* 控制PTZ开始连续转动 */

    //【入参数据类型】cmiotPtzMoveInfo_t
    CMIOT_CMD_PTZ_MOVE_BY_STEP,            /* 控制PTZ步进转动 */

    //【入参数据类型】cmiotPtzPresetPointInfo_t
    //【说明】 当action取值为1-新增、2-修改、3-删除时，根据presetPointCode值，对本地保存的预置点编码列表进行对应操作
    //         当action取值为4-跳转时，将云台跳转到presetPointCode处
    CMIOT_CMD_PTZ_SET_PRESET_POINT,        /* 设置云台预置点 */

    //【入参数据类型】cmiotPtzCruiseInfo_t
    //【说明】根据action，对本地保存的巡航路线编码和巡航路线编码对应的预置点编码列表进行对应操作
    CMIOT_CMD_PTZ_SET_CRUISE,              /* 设置云台巡航路线 */

    //【入参数据类型】cmiotPtzCruiseControlInfo_t 
    //【说明】根据action和streamId控制云台开始或停止对应的巡航路线
    CMIOT_CMD_PTZ_CRUISE_CONTROL,          /* 云台巡航控制 */

    // 【出参数据类型】cmiotPresetPointCodeInfo_t
    CMIOT_CMD_PTZ_GET_PRESET_POINT_LIST,   /* 获取云台预置点列表 */

    // 【出参数据类型】cmiotCruiseCodeInfo_t
    CMIOT_CMD_PTZ_GET_CRUISE_LIST,         /* 获取云台巡航路线列表 */

    // 【出参数据类型】cmiotLensControlInfo_t
    CMIOT_CMD_PTZ_SET_LENS,                /* 镜头控制（变焦、变倍、光圈） */
}cmiotPtzControlCmd_e;

/* 音频命令定义 */
typedef enum{
    CMIOT_CMD_AUDIO_START = 2400,          /* 云广播命令开始 */

    //【入参数据类型】cmiotAudioData_t
    //【说明】该命令主要用于双向语音通话过程中，设备端接收并播放客户端采集的声音。
    //        音频格式为G711 a-law，采样率为：8kHz，通道数为1
    //        dataSize为0时，表示本次语音对讲结束
    CMIOT_CMD_AUDIO_TALK,                  /* 实时语音对讲 */

    //【入参数据类型】cmiotAudioPlayInfo_t
    //【说明】设备需保存音频文件以及音频文件对应的语音id。
    //        收到开始播放请求后，对比消息中的语音id和本地语音id是否相同，不相同时需下载保存新的语音及语音id。
    CMIOT_CMD_AUDIO_PLAY_START,            /* 开始播放云广播音频 */

    //无入参出参
    CMIOT_CMD_AUDIO_PLAY_STOP,             /* 停止播放云广播音频 */
}cmiotAudioCmd_e;

/* 升级命令定义 */
typedef enum{
    CMIOT_CMD_UPGRADE_START = 2500,        /* 升级命令开始 */

    //【入参数据类型】cmiotUpgradeInfo_t
    //【说明】设备下载后进行升级，下载及升级过程中，需通过API cmiot_report_upgrade_step向服务器反馈进度
    CMIOT_CMD_UPGRADE_FW,                  /* 固件升级 */

    //【入参数据类型】cmiotUpgradeInfo_t
    //【说明】设备下载后进行升级，下载及升级过程中，需通过API cmiot_report_upgrade_step向服务器反馈进度
    CMIOT_CMD_UPGRADE_APP,                 /* 应用升级 */
}cmiotUpgradeCmd_e;

/* 运行状态定义 */
typedef enum{
    CMIOT_RUNNING_STATUS_START = 2600,             /* 运行状态开始 */

    //【入参数据类型】cmiot_uint32_t 【取值范围】1-开始有线绑定，厂家阻塞直到确认有线网络连接成功后返回 2-开始无线绑定，设备进入无线绑定状态
    CMIOT_RUNNING_STATUS_BIND_START,               /* 开始绑定 */

    //获取二维码信息，设备若未处于扫码配网模式，则不处理该回调，直接返回失败即可，否则启动扫码
    //设备会持续收到此回调，直到解析正确，二维码信息包括结束符最长128字节，SDK解析到WiFi后将通过CMIOT_RUNNING_STATUS_BIND_CONNECT_WIFI通知设备连接
    //【出参数据类型】cmiot_char_t [128]  【说明】该内存由SDK内申请
    CMIOT_RUNNING_STATUS_BIND_GET_QRCODE,          /* 获取二维码信息 */

    //设备连接目标WIFI，建议在连接WIFI成功并且获取到IP地址之后返回
    //【入参数据类型】cmiotModifyWifiReqInfo_t
    CMIOT_RUNNING_STATUS_BIND_CONNECT_WIFI,        /* 连接WiFi */

    //设备连接AEC AP进行无感配网，设备未处于AEC配网模式时，则不处理该回调，直接返回失败即可，否则进行连接
    //设备需在连接AEC AP成功并且获取到IP地址之后返回，SDK向AP请求到真实WiFi后将通过CMIOT_RUNNING_STATUS_BIND_CONNECT_WIFI通知设备连接
    //设备当前的WiFi配网模式（扫码配网 or AEC配网）由设备自行决定，如上电默认扫码配网，扫码->AEC：连续按两次reset，AEC->扫码：长按reset
    //【入参数据类型】cmiotModifyWifiReqInfo_t 【出参数据类型】cmiot_char_t[32]  返回AEC WiFi的网关地址
    CMIOT_RUNNING_STATUS_BIND_CONNECT_AEC_WIFI,    /* 连接AEC AP */

    //设备收到该回调后应播报相应提示音频提示客户重启设备，并且修改指示灯状态
    CMIOT_RUNNING_STATUS_BIND_STOP,                /* 退出绑定状态 */

    //设备需要播放相应音频，并且修改指示灯状态
    CMIOT_RUNNING_STATUS_BIND_SUCCESS,             /* 绑定成功 */

    //设备需要播放相应音频，并且重新进入绑定状态
    CMIOT_RUNNING_STATUS_BIND_FAILED,              /* 绑定失败 */

    //设备收到该回调后停止推流，设备参数恢复默认
    CMIOT_RUNNING_STATUS_UNBIND,                   /* 设备被解绑 */

    // 已从平台获取到零配置信息，设备根据协议类型registerProtocolType，使用相关参数，连接到相应平台
    // 如后续不使用千里眼私有协议接入，则调用cmiot_sdk_deinit释放资源
    //【入参数据类型】cmiotZeroCfgInfo_t
    CMIOT_RUNNING_STATUS_NOTIFY_ZERO_CONFIG,       /* 零配置，（对于支持GB/T28181、ONvif等协议的设备，平台会下发相应配置参数以快速接入） */

    // 已从平台获取到1400配置信息，设备使用相关参数，连接到1400网关
    // 如后续不使用千里眼私有协议接入，则调用cmiot_sdk_deinit释放资源
    //【入参数据类型】cmiot1400CfgInfo_t
    CMIOT_RUNNING_STATUS_NOTIFY_1400_CONFIG,       /* GAT1400配置 */

    CMIOT_RUNNING_STATUS_SERVER_ONLINE,            /* 与服务器建立连接 */
    CMIOT_RUNNING_STATUS_SERVER_OFFLINE,           /* 与服务器连接断开 */

    CMIOT_RUNNING_STATUS_LOWPOWER_SLEEP,           /* 若设备类型为CMIOT_DEV_TYPE_LOWPOWER_CAM或CMIOT_DEV_TYPE_DOORBELL时，系统空闲时产生该回调通知设备，允许休眠 */
    
}cmiotRunningStatus_e;

/* 时间回调类型 */
typedef enum{
    // 【入参数据类型】 无
    // 【出参数据类型】 struct timeval *
    // 【说明】返回当前utc时间
    CMIOT_CMD_GET_TIMEOFDAY,               /* gettimeofday */

    // 【入参数据类型】 time_t *
    // 【出参数据类型】 struct tm *
    // 【说明】将入参传入的utc时间，转化为本地时间
    CMIOT_CMD_GET_LOCALTIME,               /* localtime */

    // 【入参数据类型】 struct tm *
    // 【出参数据类型】 time_t *
    // 【说明】将入参传入的本地时间，转化为utc时间
    CMIOT_CMD_GET_MKTIME,                  /* mktime */
}cmiotTimeCmd_e;

/**
 * 设备配置回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_dev_config_callback_t)(cmiotDevConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 查询回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_get_Info_callback_t)(cmiotGetInfoCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 设备控制回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_dev_control_callback_t)(cmiotDevControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 云台控制回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_ptz_control_callback_t)(cmiotPtzControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 语音回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_audio_callback_t)(cmiotAudioCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 升级回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_upgrade_callback_t)(cmiotUpgradeCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 运行状态回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_running_status_callback_t)(cmiotRunningStatus_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 设备AI配置回调函数定义
 * @param [in] cmd  命令类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_dev_ai_config_callback_t)(cmiotDevAIConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

/**
 * 获取时间回调，仅当gettimeofday、localtime、mktime系统调用为非标准实现时
 * （如gettimeofday返回时区时间而非utc时间，localtime返回结果不与时区关联），才需实现
 * 一般情况下无需关注，置为NULL
 * @param [in] cmd  回调类型
 * @param [in] input 入参指针，根据不同的回调类型，转成不同的数据结构进行处理
 * @param [out] output 出参指针
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
typedef cmiot_int32_t (*cmiot_time_callback_t)(cmiotTimeCmd_e cmd, void* input, void* output);


/** 回调函数指针列表 */
typedef struct {
    cmiot_dev_config_callback_t        dev_config_cb;          /* 参数设置回调 */
    cmiot_get_Info_callback_t          get_info_cb;            /* 查询回调 */
    cmiot_dev_control_callback_t       dev_ctrl_cb;            /* 设备控制回调 */
    cmiot_ptz_control_callback_t       ptz_ctrl_cb;            /* PTZ控制回调 */
    cmiot_audio_callback_t             audio_cb;               /* 语音回调 */
    cmiot_upgrade_callback_t           upgrade_cb;             /* 服务状态通知回调 */
    cmiot_running_status_callback_t    running_status_cb;      /* 固件升级回调 */
    cmiot_dev_ai_config_callback_t     dev_ai_config_cb;       /* AI参数配置回调 */
    cmiot_time_callback_t              time_cb;                /* 获取时间回调，仅当时间相关系统调用为非标准实现时，才需实现，否则传入NULL */
} cmiotCallbackList_t;

#ifdef __cplusplus
}
#endif

#endif
