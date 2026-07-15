#ifndef __DEMO_STREAM_H__
#define __DEMO_STREAM_H__


/* 读取媒体流的文件路径 */
#define TEST_MEDIA_FILE_265_STREAM_0 "./media_file/test_media_00_H265.cflv"
#define TEST_MEDIA_FILE_264_STREAM_0 "./media_file/test_media_00_H264.cflv"
#define TEST_MEDIA_FILE_265_STREAM_1 "./media_file/test_media_01_H265.cflv"
#define TEST_MEDIA_FILE_264_STREAM_1 "./media_file/test_media_01_H264.cflv"

/* 读取缩略图的文件路径 */
#define TEST_THUMBNAIL_FILE_PATH_STREAM_0 "./media_file/test_thumb_00.jpg"
#define TEST_THUMBNAIL_FILE_PATH_STREAM_1 "./media_file/test_thumb_01.jpg"

typedef struct
{
    cmiot_uint8_t type; // 1:audio, 2:video, 3:sps/pps, 201,200:SD卡回放开始/结束
    cmiot_uint8_t sync; // 0:非关键帧, 1:关键帧(针对视频而言)
    cmiot_uint8_t streamId;	// 流通道号
	cmiot_uint8_t resv;		    // 预留
    cmiot_uint32_t seq;         // 帧序列号,音视频分别编号,溢出之后重新从 0 开始
    cmiot_uint64_t startTime;   // 音视频时间戳,溢出之后重新从 0 开始,单位:毫秒
    cmiot_uint64_t ipcamTime;   // 摄像机 UTC 时间,单位:毫秒(该时间用于在 App 上展示)
    cmiot_uint32_t controlFlag; // 预留字段，暂时不用
    cmiot_uint32_t dataSize;    // 数据大小，单位：字节
    cmiot_int8_t data[0];       // 音视频数据
} _mediaData_t;

/* SD卡录制模式 */
typedef enum
{
    SD_CARD_RECORD_TYPE_ALL_DAY = 1,              // 全天录制 
    SD_CARD_RECORD_TYPE_EVENT = 2,                // 事件录制
} externalsdcardRecordType_e;


extern void *demo_cflv_file_open(cmiot_char_t *cflvFile);
extern cmiot_int32_t demo_cflv_file_read(void *opt, void* data);
extern void demo_cflv_file_close(void *opt);
extern cmiot_int32_t demo_cflv_file_move_spspps(void *opt, cmiot_uint64_t time);
extern cmiot_int32_t demo_external_sd_record_stream_save(cmiot_char_t* extSDPath, _mediaData_t* mData, void* readOpt, pthread_mutex_t* wLock);
extern cmiot_int32_t demo_external_sd_record_get_file_coded_format_by_time(cmiot_char_t* extSDPath, cmiot_uint64_t startTime);
extern cmiot_int32_t demo_external_sd_record_get_normal_timeline(cmiot_char_t* extSdPath, cmiot_uint64_t startTime, cmiot_uint64_t endTime, cmiot_uint32_t pageSize, cmiotTimelineInfo_t* data);
extern cmiot_int32_t demo_external_sd_record_find_first_file_within_time(cmiot_char_t* extSDPath, cmiot_uint64_t startTime, cmiot_char_t *videoFile, cmiot_uint32_t videoFileLen);
extern cmiot_int32_t demo_external_sd_record_find_next_file_by_time(cmiot_char_t *extSDPath, cmiot_uint64_t baseTime, cmiot_char_t *nextFile, cmiot_uint32_t nextFileLen);
extern cmiot_int32_t demo_cflv_read_end(void *opt);
extern cmiot_int32_t demo_external_sd_record_file_move_spspps(void *opt, cmiot_uint64_t time);
extern cmiot_int32_t demo_external_sd_record_get_file_coded_format_by_file(void *opt, cmiot_int32_t *videoType);
extern void demo_clear_external_sd_record_file_param(void);
extern void demo_external_event_record_process(cmiot_char_t *extSDPath, pthread_mutex_t* wLock);
extern cmiot_int32_t demo_external_event_record_write_audio_data(cmiotAudioData_t *data, cmiot_uint32_t streamId, cmiot_bool_t externalSdRecord, cmiot_bool_t externalSdRecordStatus);
extern cmiot_int32_t demo_external_event_record_write_video_data(cmiotVideoData_t *data, cmiot_uint32_t streamId, cmiot_bool_t externalSdRecord, cmiot_bool_t externalSdRecordStatus);
extern void demo_external_event_record_buf_deinit(void);


cmiot_uint64_t get_utc_time_ms(void);
cmiot_uint32_t get_utc_time_s(void);
cmiot_int32_t demo_set_media_thread(void);
cmiot_int32_t demo_get_thumbnail_data(cmiot_char_t *thumbFileName, cmiotPicData_t* picData);
void demo_set_media_thread_stop(void);
#ifdef FEATURE_SDCARD
cmiot_int32_t demo_set_sd_external_replay_thread(cmiot_uint64_t startTime);
void demo_set_sd_external_replay_stop(void);
#endif



#endif