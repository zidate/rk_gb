/************************************************************ 
Copyright (C), 2017-2021, CMIOT.
FileName: cmiot_api.h
Description: sdk API接口文档
             定义SDK中接口函数，并进行说明
***********************************************************/

#ifndef __CMIOT_API_H__
#define __CMIOT_API_H__

#ifdef __cplusplus
  extern "C" {
#endif

#include "cmiot_define.h"
#include "cmiot_callback.h"

/**
 * SDK初始化函数，启动后调用的第一个函数
 * @param [in] devParams   设备初始化参数
 * @param [in] cbList      回调函数列表指针
 * @param [in] fixedCap    固定能力集
 * @param [in] dynamicCap  可变能力集
 * @param [in] aiCap       AI能力集，JSON字符串格式，字段定义参考指导手册。若不支持AI能力集，则传NULL
 * @param [in] amParams    算法管理参数，如不支持算法下发能力集aiAlgoDispatch，则传NULL
 * @return 返回函数执行结果                                      
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_sdk_init(cmiotCallbackList_t *cbList, cmiotDevParams_t *devParams, cmiotFixedCapability_t *fixedCap, \
                             cmiotDynamicCapability_t *dynamicCap, cmiot_char_t *aiCap, cmiotAlgoManagerParams_t *amParams);

/**
 * SDK反初始化函数
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_sdk_deinit(void);

/**
 * 推送视频流数据，设备绑定上线成功后持续推流
 * 注意：每个关键帧前都必须要推送SPS/PPS，且SPS/PPS中的时间戳（ts和utcms）需实时更新，不能设为固定值
 * @param [in] data  视频数据
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_video_data(cmiotVideoData_t *data, cmiot_uint32_t streamId);

/**
 * 推送音频流数据，设备绑定上线成功后持续推流
 * @param [in] data  音频数据
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_audio_data(cmiotAudioData_t *data, cmiot_uint32_t streamId);


/**
 * 推送辅码流录制视频流数据，当设备的sd卡录制状态是辅码流录制时才开始推流
 * 注意：每个关键帧前都必须要推送SPS/PPS，且SPS/PPS中的时间戳（ts和utcms）需实时更新，不能设为固定值
 * @param [in] data  视频数据
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sub_video_data(cmiotVideoData_t *data, cmiot_uint32_t streamId);

/**
 * 推送辅码流录制音频流数据，当设备的sd卡录制状态是辅码流录制时才开始推流
 * @param [in] data  音频数据
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sub_audio_data(cmiotAudioData_t *data, cmiot_uint32_t streamId);

/**
 * 事件开始，上报平台
 * @param [in] type  事件类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] utcms 事件开始时刻的utc时间，单位ms (注意：设备上报类型A事件结束的时间 >= 最后一次上报类型A事件开始时间+10秒)
 * @param [in] eventMsg 对于人脸检测事件、人脸比对事件、车辆抓拍事件、电瓶车抓拍事件、口罩检测告警事件，
 *     如设备支持1400协议，并将当前事件抓拍的图片上传至1400网关时，需上报imageID，数据类型：char[]，用于标记该事件在1400视图库中对应的图片ID；
 *     客流统计需上报数据类型：cmiotPassengerFlowData_t
 *     蜂窝信号告警需上报数据类型：int  取值范围：信号值[0-100]
 *     电池电量告警需上报数据类型：int  取值范围：电量百分比[0-100]
 * @param [in] pic 对于动态检测事件、声音检测事件、哭声检测事件、区域入侵事件，支持通过该参数上传事件抓拍图（JPG格式）数据，可选，如无需上报则传NULL
 * @param [in] picSize 抓拍图片数据长度
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_event_start(cmiotEventType_e type, cmiot_uint32_t streamId, cmiot_uint64_t utcms,
                                void *eventMsg, cmiot_uint8_t *pic, cmiot_uint32_t picSize);


/**
 * 事件结束，上报平台
 * @param [in] type  事件类型
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @param [in] utcms 事件结束时刻的utc时间，单位ms (注意：设备上报类型A事件结束的时间 >= 最后一次上报类型A事件开始时间+10秒)
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_event_stop(cmiotEventType_e type, cmiot_uint32_t streamId, cmiot_uint64_t utcms);


/**
 * 上报升级进度
 * @param [in] info  升级信息
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_report_upgrade_step(cmiotUpgradeRptInfo_t *info);


/**
 * cmiotUpdateCfgType_e中的配置产生变化时，调用该接口将配置添加到待更新配置列表
 * 注意：此接口仅将配置放入待更新列表中，随后需要调用cmiot_update_device_config_send待更新列表中的配置合并为一条消息上报
 * @param [in] type  更新的配置类型
 * @param [in] data  配置信息
 * @param [in] size  data的长度
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_update_device_config(cmiotUpdateCfgType_e type, cmiot_char_t *data, cmiot_uint32_t size);

/**
 * 将待更新配置列表中的配置合并为一条消息上报到平台进行缓存，设备未绑定状态下调用此接口将返回失败
 * 为减轻平台侧压力，在设备未上线之前，需要将多条配置更新到平台的情况（比如SDK内部拉取到在线配置后，调用了若干回调），
 * 先多次调用cmiot_update_device_config将配置添加到更新配置列表中，然后在收到上线回调后，调用该接口合并上报。
 * 而对于运行过程中产生的配置变化，为保证实时性，可在调用cmiot_update_device_config后立即调用此接口。
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_update_device_config_send(void);


/**
 * 切换视频编码格式前，调用该接口更新配置
 * @param [in] params  媒体流配置
 * @return void
 */
void cmiot_set_stream_param(cmiotStreamParams_t *params);

/**
 * 支持休眠的低功耗设备，上线后可调用该接口获取低功耗保活信息
 * 异步接口，获取结果通过回调CMIOT_CMD_SET_LOW_POWER_KEEPALIVE通知
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_get_lowpower_keepalive(void);

/**
 * 上报AI能力集，能力集发生改变后通过该接口上报最新能力集，支持增量更新。
 * @param [in] aiCap   AI能力集，JSON字符串格式，字段定义参考指导手册。
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_ai_capability_update(cmiot_char_t *aiCap);

/**
 * 上报 AI 场景检测事件
 * @param [in] eventCode   AI事件类型，取值参见应用开发指导手册
 * @param [in] eventMsg    AI事件msg json文本，格式与内容参见应用开发指导手册
 * @param [in] eventMsgLen AI事件msg json文本长度
 * @param [in] pic AI事件抓拍图（JPG格式）数据，无需上报则传NULL。
 *                 抓拍图片有两种上传途径：1.图片上传1400网关后，将imageID放入eventMsg中上报；2.图片数据放入data上报；
 * @param [in] picSize AI事件抓拍图片数据长度
 * @param [in] streamId  摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_ai_event_report(cmiot_uint32_t eventCode, cmiot_char_t *eventMsg, cmiot_uint32_t eventMsgLen, 
                                    cmiot_uint8_t *pic, cmiot_uint32_t picSize, cmiot_uint32_t streamId);

/**
 * 修改本地日志系统参数
 * @param [in] logPath  日志存储路径，该路径必须已经存在
 * @param [in] logMaxSize  单个日志文件的大小限制，单位KByte，默认值1024KB，最大不超过1024KB，若传入0，则使用默认值1024KB
 * @param [in] logMaxRotate  缓存日志文件的个数（循环覆盖），至少为1，最大不超过80，若传入0，则使用默认值5
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_set_log_config(const char* logPath, cmiot_uint32_t logMaxSize, cmiot_uint16_t logMaxRotate);


/**
 * 通知sdk设备reset按键被按下，设备被重置。重置时调用此接口即可，厂商无需操作ini文件。
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：ini文件删除成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_notify_reset(void);


/**
 * 设置SDK日志控制台输出开关
 * @param [in] status  CMIOT_FALSE-关闭控制台输出，仅输出到日志文件；CMIOT_TRUE-同时输出到控制台和日志文件。默认为开启。
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_set_log_console_output(cmiot_bool_t status);


/**
 * 设备视频流分辨率产生改变时，通知平台
 * @param [in] resolution 流通道视频分辨率
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_notify_resolution_change(cmiotVideoReso_t *resolution);

/**
 * @brief 修改第一跳接入地址，以接入不同平台。需在cmiot_sdk_init前调用，一般无需调用
 *        变更地址时，需要先重置设备，再调用此接口
 * @param server 域名或IP
 * @param port 端口号
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_set_server(cmiot_char_t *server, cmiot_int32_t port);

/**
 * 设置SD卡存储视频流独立推送，需在cmiot_sdk_init前调用。此接口暂不支持多目摄像头
 * 注意：此接口调用后，需调用cmiot_push_sd_video_data和cmiot_push_sd_audio_data推送单独一路流保存至SD卡
 * 接口cmiot_push_video_data和cmiot_push_audio_data推送的视频流数据不再保存至SD卡，仅用于直播和云存储。
 * 推荐在支持RTN时才调用此接口，避免码流参数随回调CMIOT_CMD_CONTROL_RTN_VIDEO_PARAMS、CMIOT_CMD_CONTROL_RTN_VIDEO_FPS、
 * CMIOT_CMD_CONTROL_RTN_VIDEO_RC_CHANGE_PARAMS动态调整时，影响SD卡存储的数据流，否则无需调用。
 * @param sdStreamParams sd卡视频流参数
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_set_sd_stream_param(cmiotSdStreamParam_t *sdStreamParams);

/**
 * 调用cmiot_set_sd_stream_param接口后，通过此接口推送的流数据保存至SD卡，cmiot_push_video_data推送的流数据不再保存至SD卡，仅用于直播和云存储。
 * 注意：每个关键帧前都必须要推送SPS/PPS，且SPS/PPS中的时间戳（ts和utcms）需实时更新，不能设为固定值
 * @param [in] data  视频数据
 * @param [in] streamId  摄像头流通道id，固定为0，此接口暂不支持多目摄像头
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sd_video_data(cmiotVideoData_t *data, cmiot_uint32_t streamId);

/**
 * 调用cmiot_set_sd_stream_param接口后，通过此接口推送的流数据保存至SD卡，cmiot_push_audio_data推送的流数据不再保存至SD卡，仅用于直播和云存储。
 * @param [in] data  音频数据
 * @param [in] streamId  摄像头流通道id，固定为0，此接口暂不支持多目摄像头
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sd_audio_data(cmiotAudioData_t *data, cmiot_uint32_t streamId);


/**
 * 下发算法启动后，收到回调CMIOT_CMD_CONTROL_SHMEM_WRITE_START时，调用该接口向共享内存写入指定格式的YUV帧数据
 * @param [in] handle 共享内存句柄
 * @param [in] frame 帧数据
 * @param [in] timeout 写入超时
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_shmem_write(void *handle, cmiotShmemData_t *frame, cmiot_uint32_t timeout);

/**
 * externalSdRecord打开使用外部录制时，调用此接口推送sd卡回放流
 * 注意：每个关键帧前都必须要推送SPS/PPS，且SPS/PPS中的时间戳 （ts和utcms）需实时更新，不能设为固定值
 * @param [in] data  视频数据
 * @param [in] streamId  摄像头流通道id，单目摄像头固定为0
 * @param [in] videoType 视频编码格式， 1-H264 2-H265
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sd_replay_video_data(cmiotVideoData_t *data, cmiot_uint32_t streamId, cmiot_int32_t videoType);

/**
 * externalSdRecord打开使用外部录制时，调用此接口推送sd卡回放流
 * @param [in] data  音频数据
 * @param [in] streamId  摄像头流通道id，单目摄像头固定为0
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_push_sd_replay_audio_data(cmiotAudioData_t *data, cmiot_uint32_t streamId);

/**
 * externalSdRecord打开使用外部录制，推送回放流停止后，需调用此接口通知SDK
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_notify_sd_replay_stop(void);

/**
 * 上传设备日志
 * CMIOT_CMD_CONTROL_ACTIVE_DEV_LOG_REPORT回调打开时，设备日志每写满一个文件则调用此接口上传一次
 * @param [in] deviceLogInfo    设备日志信息
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_device_log_report(cmiotLogReportInfo_t *deviceLogInfo);

/**
 * 发送校时请求
 * 设备在专网环境无法通过公网 NTP 服务校时时，调用此接口请求校时，校时响应通过CMIOT_CMD_CONTROL_TIME_CALIBRATION回调通知
 * @return 返回函数执行结果
 * - CMIOT_RETURN_CODE_SUCCESS：成功
 * - 其他值：失败
 */
cmiot_int32_t cmiot_request_time_calibration(void);

/**
 * 修改sd卡录制模式，主辅码切换
 * 设备切换主码流和辅码流录制的时候，需要调用此接口更新配置
 * @return 无
 */
void cmiot_set_main_sub_recording(cmiot_int32_t streamType);

#ifdef __cplusplus
}
#endif

#endif