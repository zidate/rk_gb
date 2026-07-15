#ifndef __DEMO_CALLBACK_H__
#define __DEMO_CALLBACK_H__


cmiot_int32_t demo_warp_cmiot_update_device_config_send(void);
void ptz_timer_handle(union sigval arg);
cmiot_int32_t demo_dev_config_callback(cmiotDevConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_get_Info_callback(cmiotGetInfoCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_dev_control_callback(cmiotDevControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_ptz_control_callback(cmiotPtzControlCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_audio_callback(cmiotAudioCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_upgrade_callback(cmiotUpgradeCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_running_status_callback(cmiotRunningStatus_e cmd, cmiot_uint32_t streamId, void* input, void* output);
cmiot_int32_t demo_dev_ai_config_callback(cmiotDevAIConfigCmd_e cmd, cmiot_uint32_t streamId, void* input, void* output);

#endif

