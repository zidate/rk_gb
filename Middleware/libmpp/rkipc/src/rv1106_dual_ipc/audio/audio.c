// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "log.h"
#include "rtsp.h"
// #include "storage.h"

#include <rk_debug.h>
#include <rk_mpi_aenc.h>
#include <rk_mpi_adec.h>
#include <rk_mpi_ai.h>
#include <rk_mpi_ao.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_sys.h>
#include <rk_comm_adec.h>
#include <rk_mpi_amix.h>
#include <rk_defines.h>

#include "PAL/libdmc.h"
#include "PAL/Audio_coder.h"
extern int AudioInBuffer_aac(unsigned char* pBuffer, int buffLen);

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio.c"

#if 0
//aac
#include "fdk-aac/aacenc_lib.h"
#define ACAP_FRAME_SIZE (640)
#define ACAP_MEN_NODE_SIZE (25)
static HANDLE_AACENCODER hAacEncoder = NULL;
#endif

pthread_t save_ai_tid, save_aenc_tid, ai_get_detect_result_tid;
static int ai_dev_id = 0;
static int ai_chn_id = 0;
static int aenc_dev_id = 0;
static int aenc_chn_id = 0;
static int g_audio_run_ = 1;
static int enable_aed, enable_bcd, enable_vqe;
MPP_CHN_S ai_chn, aenc_chn;

static int s_mic_enable = 1;

static void *ai_get_detect_result(void *arg);

void *save_ai_thread(void *ptr) 
{
	RK_S32 ret = 0;
	RK_S32 s32MilliSec = -1;
	AUDIO_FRAME_S frame;

	unsigned long long timestamp;
	struct timeval tv = {0};

	#if 0
	rk_audio_aac_encode_init();
	unsigned char acc_data[1024] = {0};
	int aac_data_size = 0;
	#endif
	RK_U32 u32PtNumPerFrm = rk_param_get_int("audio.0:frame_size", 320);
	char g711u_data[u32PtNumPerFrm];
	while (g_audio_run_) 
    {
		ret = RK_MPI_AI_GetFrame(ai_dev_id, ai_chn_id, &frame, RK_NULL, s32MilliSec);
		if (ret == RK_SUCCESS) 
		{
			if (0 == s_mic_enable)
			{
				RK_MPI_AI_ReleaseFrame(ai_dev_id, ai_chn_id, &frame, RK_NULL);
				continue;
			}
			
			void *data = RK_MPI_MB_Handle2VirAddr(frame.pMbBlk);
			if (data)
			{
				gettimeofday(&tv, NULL);
				timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);

				//发送到aac编码buffer
				// AudioInBuffer_aac(data, frame.u32Len);
				
				dmc_input(	DMC_MEDIA_AUDIO_FRIST_STREAM,
							DMC_MEDIA_TYPE_AUDIO,
							DMC_MEDIA_SUBTYPE_PCM,
							timestamp,
							data,
							frame.u32Len,
							/*0*/(frame.u64TimeStamp/1000));
				
				int g711u_len = DG_encode_g711u((char *)data, (char *)g711u_data, frame.u32Len);
				// printf("======frame.u32Len = %d,g711u_len=%d\n",frame.u32Len,g711u_len);
				timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
				if (g711u_len > 0)
				{
					dmc_input(	DMC_MEDIA_AUDIO_SECOND_STREAM,
								DMC_MEDIA_TYPE_AUDIO,
								DMC_MEDIA_SUBTYPE_ULAW,
								timestamp,
								g711u_data,
								g711u_len,
								/*0*/(frame.u64TimeStamp/1000));
				}

				#if 0
				aac_data_size = rk_audio_aac_encode((char *)data,frame.u32Len, (char *)acc_data,sizeof(acc_data));
				LOG_INFO("get aac frame data = %p, size = %d, pts is %u\n", acc_data,aac_data_size, timestamp);
				if (aac_data_size > 0)
				{
					dmc_input(	DMC_MEDIA_AUDIO_SECOND_STREAM,
								DMC_MEDIA_TYPE_AUDIO,
								DMC_MEDIA_SUBTYPE_AAC,
								timestamp,
								acc_data,
								aac_data_size,
								end_flag);
				}
				#endif
			}
			RK_MPI_AI_ReleaseFrame(ai_dev_id, ai_chn_id, &frame, RK_NULL);
		}
		usleep(5000); //延时5ms，正常是音频帧是按照采样率一帧一帧过来，有时候底层会阻塞导致会缓存多帧数据，此时通过接口获取的时候再打时间戳有可能帧时间错就一样了，会导致录像异常
	}
#if 0
	rk_audio_aac_encode_deinit();
#endif	
	return RK_NULL;
}

void *save_aenc_thread(void *ptr) 
{
	prctl(PR_SET_NAME, "save_aenc_thread", 0, 0, 0);
	RK_S32 s32ret = 0;
	AUDIO_STREAM_S pstStream;

	unsigned long long timestamp;
	struct timeval tv = {0};

	while (g_audio_run_) 
    {
		s32ret = RK_MPI_AENC_GetStream(aenc_chn_id, &pstStream, 1000);
		if (s32ret == RK_SUCCESS) 
        {
			MB_BLK bBlk = pstStream.pMbBlk;
			void *buffer = RK_MPI_MB_Handle2VirAddr(bBlk);
			if (buffer) 
            {
				// LOG_INFO("get frame data = %p, size = %d, pts is %lld, seq is %d\n", buffer,
				// 		pstStream.u32Len, pstStream.u64TimeStamp, pstStream.u32Seq);

				gettimeofday(&tv, NULL);
				timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
				dmc_input(	DMC_MEDIA_AUDIO_THIRD_STREAM,
							DMC_MEDIA_TYPE_AUDIO,
							DMC_MEDIA_SUBTYPE_ULAW,
							timestamp,
							buffer,
							pstStream.u32Len,
							0);
			}
			RK_MPI_AENC_ReleaseStream(aenc_chn_id, &pstStream);
		} 
        else 
        {
			LOG_ERROR("fail to get aenc frame\n");
		}
	}

	return RK_NULL;
}

int rkipc_audio_aed_init() 
{
	int result;
	AI_AED_CONFIG_S ai_aed_config;

	ai_aed_config.fSnrDB = 10.0f;
	ai_aed_config.fLsdDB = -25.0f;
	ai_aed_config.s32Policy = 1;
	result = RK_MPI_AI_SetAedAttr(ai_dev_id, ai_chn_id, &ai_aed_config);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_SetAedAttr(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetAedAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableAed(ai_dev_id, ai_chn_id);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_EnableAed(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableAed(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

int rkipc_audio_bcd_init() 
{
	int result;
	AI_BCD_CONFIG_S ai_bcd_config;

	ai_bcd_config.mFrameLen = 120;
	ai_bcd_config.mBlankFrameMax = 50;
	ai_bcd_config.mCryEnergy = -1.25f;
	ai_bcd_config.mJudgeEnergy = -0.75f;
	ai_bcd_config.mCryThres1 = 0.70f;
	ai_bcd_config.mCryThres2 = 0.55f;
	result = RK_MPI_AI_SetBcdAttr(ai_dev_id, ai_chn_id, &ai_bcd_config);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_SetBcdAttr(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetBcdAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableBcd(ai_dev_id, ai_chn_id);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_EnableBcd(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableBcd(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

int rkipc_audio_vqe_init() 
{
	int result;
	AI_VQE_CONFIG_S stAiVqeConfig;
	const char *pVqeCfgPath = rk_param_get_string("audio.0:vqe_cfg", "/oem/usr/share/vqefiles/config_aivqe.json");
	int vqe_gap_ms = 16;
	if (vqe_gap_ms != 16 && vqe_gap_ms != 10) 
    {
		LOG_ERROR("Invalid gap: %d, just supports 16ms or 10ms for AI VQE", vqe_gap_ms);
		return RK_FAILURE;
	}

	memset(&stAiVqeConfig, 0, sizeof(AI_VQE_CONFIG_S));
	stAiVqeConfig.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
	memcpy(stAiVqeConfig.aCfgFile, pVqeCfgPath,strlen(pVqeCfgPath));

	// const char *vqe_cfg = rk_param_get_string("audio.0:vqe_cfg", "/oem/usr/share/vqefiles/config_aivqe.json");
	// memcpy(stAiVqeConfig.aCfgFile, vqe_cfg, strlen(vqe_cfg) + 1);
	// memset(stAiVqeConfig.aCfgFile + strlen(vqe_cfg) + 1, '\0', sizeof(char));
	// LOG_INFO("stAiVqeConfig.aCfgFile = %s\n", stAiVqeConfig.aCfgFile);

	stAiVqeConfig.s32WorkSampleRate = rk_param_get_int("audio.0:sample_rate", 8000);
	stAiVqeConfig.s32FrameSample = rk_param_get_int("audio.0:sample_rate", 8000) * vqe_gap_ms / 1000;
	result = RK_MPI_AI_SetVqeAttr(ai_dev_id, ai_chn_id, 0, 0, &stAiVqeConfig);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_SetVqeAttr(%d,%d) failed with %#x", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetVqeAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableVqe(ai_dev_id, ai_chn_id);
	if (result != RK_SUCCESS) 
    {
		LOG_ERROR("RK_MPI_AI_EnableVqe(%d,%d) failed with %#x", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableVqe(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

static void *ai_get_detect_result(void *arg) 
{
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "ai_get_detect_result", 0, 0, 0);
	int result;

	while (g_audio_run_) 
    {
		usleep(1000 * 1000);
		AI_AED_RESULT_S aed_result;
		AI_BCD_RESULT_S bcd_result;
		memset(&aed_result, 0, sizeof(aed_result));
		memset(&bcd_result, 0, sizeof(bcd_result));
		if (enable_aed) 
        {
			result = RK_MPI_AI_GetAedResult(ai_dev_id, ai_chn_id, &aed_result);
			if (result == 0) 
            {
				RK_LOGD("aed_result: %d, %d", aed_result.bAcousticEventDetected,
						aed_result.bLoudSoundDetected);
			}
		}
		if (enable_bcd) 
        {
			result = RK_MPI_AI_GetBcdResult(ai_dev_id, ai_chn_id, &bcd_result);
			if (result == 0) 
            {
				RK_LOGD("bcd_result: %d", bcd_result.bBabyCry);
			}
		}
	}

	return 0;
}
//声音质量增强
RK_S32 test_init_ai_vqe(RK_S32 s32SampleRate) {
	AI_VQE_CONFIG_S stAiVqeConfig, stAiVqeConfig2;
	RK_S32 result;
	RK_S32 s32VqeGapMs = 16;
	int s32DevId = 0;
	int s32ChnIndex = 0;
	const char *pVqeCfgPath = "/oem/usr/share/vqefiles/config_aivqe.json";

	// Need to config enCfgMode to VQE attr even the VQE is not enabled
	memset(&stAiVqeConfig, 0, sizeof(AI_VQE_CONFIG_S));
	if (pVqeCfgPath != RK_NULL) {
		stAiVqeConfig.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
		memcpy(stAiVqeConfig.aCfgFile, pVqeCfgPath, strlen(pVqeCfgPath));
	}

	if (s32VqeGapMs != 16 && s32VqeGapMs != 10) {
		LOG_ERROR("Invalid gap: %d, just supports 16ms or 10ms for AI VQE", s32VqeGapMs);
		return RK_FAILURE;
	}

	stAiVqeConfig.s32WorkSampleRate = s32SampleRate;
	stAiVqeConfig.s32FrameSample = s32SampleRate * s32VqeGapMs / 1000;
	result = RK_MPI_AI_SetVqeAttr(s32DevId, s32ChnIndex, 0, 0, &stAiVqeConfig);
	if (result != RK_SUCCESS) {
		LOG_ERROR("%s: SetVqeAttr(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	result = RK_MPI_AI_GetVqeAttr(s32DevId, s32ChnIndex, &stAiVqeConfig2);
	if (result != RK_SUCCESS) {
		LOG_ERROR("%s: GetVqeAttr(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	result = memcmp(&stAiVqeConfig, &stAiVqeConfig2, sizeof(AI_VQE_CONFIG_S));
	if (result != RK_SUCCESS) {
		LOG_ERROR("%s: set/get vqe config is different: %d", __FUNCTION__, result);
		return result;
	}

	result = RK_MPI_AI_EnableVqe(s32DevId, s32ChnIndex);
	if (result != RK_SUCCESS) {
		LOG_ERROR("%s: EnableVqe(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	return RK_SUCCESS;
}
RK_S32 ai_set_other(RK_S32 s32SetVolume) {
	printf("\n=======%s=======\n", __func__);
	int s32DevId = 0;

	RK_MPI_AI_SetVolume(s32DevId, s32SetVolume);

	//双声道，左声道为MIC拾⾳数据，右声道为播放的右声道的回采数据
	RK_MPI_AI_SetTrackMode(s32DevId, AUDIO_TRACK_NORMAL);
	AUDIO_TRACK_MODE_E trackMode;
	RK_MPI_AI_GetTrackMode(s32DevId, &trackMode);
	LOG_INFO("test info : get track mode = %d", trackMode);

	return 0;
}
int rkipc_ai_init() 
{
#if 1
	LOG_DEBUG("%s\n", __func__);
	int ret;
	AUDIO_DEV aiDevId = ai_dev_id;
	AIO_ATTR_S aiAttr;
	AI_CHN_PARAM_S pstParams;
	int aiChn = ai_chn_id;
	//char period_size_str[16];
	//int period_size = rk_param_get_int("audio.0:rt_audio_period_size", 1024);
	//snprintf(period_size_str, sizeof(period_size_str), "%d", period_size);
	//setenv("rt_audio_period_size", period_size_str, 1);

	memset(&aiAttr, 0, sizeof(AIO_ATTR_S));
	const char *card_name = rk_param_get_string("audio.0:card_name", "hw:0,0");
	snprintf(aiAttr.u8CardName, sizeof(aiAttr.u8CardName), "%s", card_name);
	LOG_INFO("aiAttr.u8CardName is %s\n", aiAttr.u8CardName);

	aiAttr.soundCard.channels = 2;
	aiAttr.soundCard.sampleRate = rk_param_get_int("audio.0:sample_rate", 8000);
	const char *format = rk_param_get_string("audio.0:format", "S16");
	if (!strcmp(format, "S16")) 
    {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} 
    else if (!strcmp(format, "U8")) 
    {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
	}
	// aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
	// aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	aiAttr.enSamplerate = rk_param_get_int("audio.0:sample_rate", 8000);
	aiAttr.u32PtNumPerFrm = rk_param_get_int("audio.0:frame_size", 320);
	aiAttr.u32FrmNum = 4;
	aiAttr.u32EXFlag = 0;
	aiAttr.u32ChnCnt = 2;

	if (rk_param_get_int("audio.0:channels", 2) == 2)
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;

	ret = RK_MPI_AI_SetPubAttr(ai_dev_id, &aiAttr);
	if (ret != 0) 
    {
		LOG_ERROR("ai set attr fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	printf("RK_MPI_AMIX_SetControl start\n");
	ret = RK_MPI_AMIX_SetControl(aiDevId, "I2STDM Digital Loopback Mode", (char *)"Mode2");
	if (ret != RK_SUCCESS) 
    {
		LOG_ERROR("ai set I2STDM Digital Loopback Mode fail, reason = %x", ret);
		return RK_FAILURE;
	}

	ret = RK_MPI_AMIX_SetControl(aiDevId, "ADC ALC Left Volume", (char *)"22");
	if (ret != RK_SUCCESS) 
    {
		LOG_ERROR("ai set alc left voulme fail, reason = %x", ret);
		return RK_FAILURE;
	}

	ret = RK_MPI_AMIX_SetControl(aiDevId, "ADC ALC Right Volume", (char *)"22");
	if (ret != RK_SUCCESS) 
    {
		LOG_ERROR("ai set alc right voulme fail, reason = %x", ret);
		return RK_FAILURE;
	}
	printf("RK_MPI_AMIX_SetControl end\n");

	ret = RK_MPI_AI_Enable(ai_dev_id);
	if (ret != 0) 
    {
		LOG_ERROR("ai enable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	memset(&pstParams, 0, sizeof(AI_CHN_PARAM_S));
	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	pstParams.s32UsrFrmDepth = 4;
	ret = RK_MPI_AI_SetChnParam(aiDevId, aiChn, &pstParams);
	if (ret != RK_SUCCESS) 
    {
		LOG_ERROR("ai set channel params, aiChn = %d", aiChn);
		return RK_FAILURE;
	}

	// aed bcd vqe
	enable_aed = rk_param_get_int("audio.0:enable_aed", 0);
	enable_bcd = rk_param_get_int("audio.0:enable_bcd", 0);
	enable_vqe = rk_param_get_int("audio.0:enable_vqe", 1);

	if (enable_aed)
		rkipc_audio_aed_init();
	if (enable_bcd)
		rkipc_audio_bcd_init();
	if (enable_vqe)
		rkipc_audio_vqe_init();
	
	if (enable_aed || enable_bcd)
		pthread_create(&ai_get_detect_result_tid, RK_NULL, ai_get_detect_result, NULL);

	ret = RK_MPI_AI_EnableChn(ai_dev_id, ai_chn_id);
	if (ret != 0) 
    {
		LOG_ERROR("ai enable channel fail, aoChn = %d, reason = %x\n", ai_chn_id, ret);
		return RK_FAILURE;
	}

	// ret = RK_MPI_AI_EnableReSmp(ai_dev_id, ai_chn_id,
	//                               (AUDIO_SAMPLE_RATE_E)params->s32SampleRate);
	// if (ret != 0) {
	//     LOG_ERROR("ai enable resample fail, reason = %x, aoChn = %d\n", ret, ai_chn_id);
	//     return RK_FAILURE;
	// }

	RK_MPI_AI_SetVolume(ai_dev_id, rk_param_get_int("audio.0:volume", 50));

	//双声道，左声道为MIC拾⾳数据，右声道为播放的右声道的回采数据
	RK_MPI_AI_SetTrackMode(ai_dev_id, AUDIO_TRACK_NORMAL);
	AUDIO_TRACK_MODE_E trackMode;
	RK_MPI_AI_GetTrackMode(ai_dev_id, &trackMode);
	LOG_INFO("test info : get track mode = %d", trackMode);

	// if (rk_param_get_int("audio.0:channels", 2) == 1) {
	// 	RK_MPI_AI_SetTrackMode(ai_dev_id, AUDIO_TRACK_FRONT_LEFT);
	// }

	pthread_create(&save_ai_tid, RK_NULL, save_ai_thread, NULL);

	return 0;
#else

	RK_S32 InputSampleRate = 8000;
	RK_S32 OutputSampleRate = 8000;
	RK_S32 u32FrameCnt = 320;
    RK_S32 vqeEnable = 1;
	printf("\n=======%s=======\n", __func__);
	AIO_ATTR_S aiAttr;
	AI_CHN_PARAM_S pstParams;
	RK_S32 result;
	int aiDevId = 0;
	int aiChn = 0;
	memset(&aiAttr, 0, sizeof(AIO_ATTR_S));

	RK_BOOL needResample = (InputSampleRate != OutputSampleRate) ? RK_TRUE : RK_FALSE;
#ifdef RV1126_RV1109
	//这是RV1126 声卡打开设置，RV1106设置无效，可以不设置
	result = RK_MPI_AMIX_SetControl(aiDevId, "Capture MIC Path", (char *)"Main Mic");
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set Capture MIC Path fail, reason = %x", result);
		goto __FAILED;
	}
#endif

	sprintf((char *)aiAttr.u8CardName, "%s", "hw:0,0");

	// s32DeviceSampleRate和s32SampleRate,s32SampleRate可以使用其他采样率，需要调用重采样函数。默认一样采样率。
	aiAttr.soundCard.channels = 2;
	aiAttr.soundCard.sampleRate = InputSampleRate;
	aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
	aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	aiAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)OutputSampleRate;
	aiAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	aiAttr.u32PtNumPerFrm = u32FrameCnt;
	//以下参数无特殊需求，无需变动，保持默认值即可
	aiAttr.u32FrmNum = 4;
	aiAttr.u32EXFlag = 0;
	aiAttr.u32ChnCnt = 2;

	result = RK_MPI_AI_SetPubAttr(aiDevId, &aiAttr);
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set attr fail, reason = %x", result);
		goto __FAILED;
	}

	//这是RV1106 回采设置，适用于左mic，右回采
	// RV1126设置无效，可以不设置，RV1126需要配置asound.conf文件或者内核驱动配置软件回采
	result =
	    RK_MPI_AMIX_SetControl(aiDevId, "I2STDM Digital Loopback Mode", (char *)"Mode2");
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set I2STDM Digital Loopback Mode fail, reason = %x", result);
		goto __FAILED;
	}

	//这是RV1106 ALC设置，而RV1126设置无效，可以不设置
	result = RK_MPI_AMIX_SetControl(aiDevId, "ADC ALC Left Volume", (char *)"22");
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set alc left voulme fail, reason = %x", result);
		goto __FAILED;
	}

	result = RK_MPI_AMIX_SetControl(aiDevId, "ADC ALC Right Volume", (char *)"22");
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set alc right voulme fail, reason = %x", result);
		goto __FAILED;
	}

	result = RK_MPI_AI_Enable(aiDevId);
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai enable fail, reason = %x", result);
		goto __FAILED;
	}

	memset(&pstParams, 0, sizeof(AI_CHN_PARAM_S));
	pstParams.s32UsrFrmDepth = 4;
	result = RK_MPI_AI_SetChnParam(aiDevId, aiChn, &pstParams);
	if (result != RK_SUCCESS) {
		LOG_ERROR("ai set channel params, aiChn = %d", aiChn);
		return RK_FAILURE;
	}

	//使用声音增强功能，默认开启
	if (vqeEnable)
		test_init_ai_vqe(OutputSampleRate);

	result = RK_MPI_AI_EnableChn(aiDevId, aiChn);
	if (result != 0) {
		LOG_ERROR("ai enable channel fail, aiChn = %d, reason = %x", aiChn, result);
		return RK_FAILURE;
	}

	//重采样功能
	if (needResample == RK_TRUE) {
		RK_LOGI("need to resample %d -> %d", InputSampleRate, OutputSampleRate);
		result =
		    RK_MPI_AI_EnableReSmp(aiDevId, aiChn, (AUDIO_SAMPLE_RATE_E)OutputSampleRate);
		if (result != 0) {
			LOG_ERROR("ai enable channel fail, reason = %x, aiChn = %d", result, aiChn);
			return RK_FAILURE;
		}
	}

	ai_set_other(100);
	pthread_create(&save_ai_tid, RK_NULL, save_ai_thread, NULL);
	return RK_SUCCESS;
__FAILED:
	return RK_FAILURE;
#endif
}

int rkipc_ai_deinit() 
{
	pthread_join(save_ai_tid, RK_NULL);
	// RK_MPI_AI_DisableReSmp(ai_dev_id, ai_chn_id);
	int ret = RK_MPI_AI_DisableChn(ai_dev_id, ai_chn_id);
	if (ret != 0) 
    {
		LOG_ERROR("ai disable channel fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_AI_DisableChn success\n");

	ret = RK_MPI_AI_Disable(ai_dev_id);
	if (ret != 0) 
    {
		LOG_ERROR("ai disable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_AI_Disable success\n");

	return 0;
}

int rkipc_aenc_init() 
{
	LOG_DEBUG("%s\n", __func__);
	AENC_CHN_ATTR_S stAencAttr;
	const char *encode_type = rk_param_get_string("audio.0:encode_type", "G711U");
	if (!strcmp(encode_type, "MP2")) 
    {
		stAencAttr.enType = RK_AUDIO_ID_MP2;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_MP2;
	} 
    else if (!strcmp(encode_type, "G711A")) 
    {
		stAencAttr.enType = RK_AUDIO_ID_PCM_ALAW;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_ALAW;
	}
	else if (!strcmp(encode_type, "G711U")) 
    {
		stAencAttr.enType = RK_AUDIO_ID_PCM_MULAW;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_MULAW;
	}
	else if (!strcmp(encode_type, "PCM")) 
    {
		stAencAttr.enType = RK_AUDIO_ID_PCM_S16LE;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_S16LE;
	}
    else 
    {
		LOG_ERROR("not support %s\n", encode_type);
	}
	stAencAttr.stCodecAttr.u32Channels = rk_param_get_int("audio.0:channels", 1);
	stAencAttr.stCodecAttr.u32SampleRate = rk_param_get_int("audio.0:sample_rate", 8000);
	const char *format = rk_param_get_string("audio.0:format", "S16");
	if (!strcmp(format, "S16")) 
    {
		stAencAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} 
    else if (!strcmp(format, "U8")) 
    {
		stAencAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} 
    else 
    {
		LOG_ERROR("not support %s\n", format);
	}

	stAencAttr.u32BufCount = 4;

	int ret = RK_MPI_AENC_CreateChn(aenc_chn_id, &stAencAttr);
	if (ret) 
    {
		LOG_ERROR("create aenc chn %d err:0x%x\n", aenc_chn_id, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("create aenc chn %d success\n", aenc_chn_id);

	pthread_create(&save_aenc_tid, RK_NULL, save_aenc_thread, NULL);

	return 0;
}

int rkipc_aenc_deinit() 
{
	int ret = RK_MPI_AENC_DestroyChn(aenc_chn_id);
	if (ret)
		LOG_ERROR("RK_MPI_AI_DisableChn fail\n");

	LOG_DEBUG("RK_MPI_AI_DisableChn success\n");

	return 0;
}

int rkipc_ao_init() 
{
	LOG_DEBUG("%s\n", __func__);
	int ret;
	AIO_ATTR_S aoAttr;
	AO_CHN_PARAM_S pstParams;
	memset(&aoAttr, 0, sizeof(AIO_ATTR_S));
	memset(&pstParams, 0, sizeof(AO_CHN_PARAM_S));

	const char *card_name = rk_param_get_string("audio.0:card_name", "hw:0,0");
	snprintf(aoAttr.u8CardName, sizeof(aoAttr.u8CardName), "%s", card_name);
	LOG_INFO("aoAttr.u8CardName is %s\n", aoAttr.u8CardName);

	aoAttr.soundCard.channels = 2;
	aoAttr.soundCard.sampleRate = rk_param_get_int("audio.0:sample_rate", 8000);

	const char *format = rk_param_get_string("audio.0:format", "S16");
	if (!strcmp(format, "S16")) 
    {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} 
    else if (!strcmp(format, "U8")) 
    {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} 
    else 
    {
		LOG_ERROR("not support %s\n", format);
	}

	aoAttr.enSamplerate = rk_param_get_int("audio.0:sample_rate", 8000);
	if (rk_param_get_int("audio.0:channels", 2) == 2)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;

	aoAttr.u32PtNumPerFrm = rk_param_get_int("audio.0:frame_size", 320);
	aoAttr.u32FrmNum = 4;
	aoAttr.u32EXFlag = 0;
	aoAttr.u32ChnCnt = 2;

	ret = RK_MPI_AO_SetPubAttr(0, &aoAttr);
	if (ret)
		LOG_ERROR("RK_MPI_AO_SetPubAttr fail %#x\n", ret);
	ret = RK_MPI_AO_Enable(0);
	if (ret)
		LOG_ERROR("RK_MPI_AO_Enable fail %#x\n", ret);

	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	ret = RK_MPI_AO_SetChnParams(0, 0, &pstParams);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("ao set channel params, aoChn = %d", 0);
		return RK_FAILURE;
	}

	ret = RK_MPI_AO_EnableChn(0, 0);
	if (ret)
		LOG_ERROR("RK_MPI_AO_EnableChn fail %#x\n", ret);

	// set sample rate of input data
	// ret = RK_MPI_AO_EnableReSmp(0, 0, (AUDIO_SAMPLE_RATE_E)s32SampleRate);
	// if (result != 0) {
	// 	RK_LOGE("ao enable channel fail, reason = %x, aoChn = %d", ret, 0);
	// 	return RK_FAILURE;
	// }
	RK_S32 volume = 0;
	RK_MPI_AO_SetVolume(0, rk_param_get_int("audio.0:volume", 50));
	RK_MPI_AO_GetVolume(0, &volume);
	LOG_INFO("test info : get volume = %d", volume);

	ret = RK_MPI_AO_SetTrackMode(0, AUDIO_TRACK_OUT_STEREO);
	if (ret)
		LOG_ERROR("RK_MPI_AO_SetTrackMode fail %#x\n", ret);

	AUDIO_TRACK_MODE_E trackMode;
	RK_MPI_AO_GetTrackMode(0, &trackMode);
	LOG_INFO("test info : get track mode = %d", trackMode);

	//adec
	ADEC_CHN AdChn = 0;
	ADEC_CHN_ATTR_S pstChnAttr;
	memset(&pstChnAttr, 0, sizeof(ADEC_CHN_ATTR_S));

    RK_CODEC_ID_E  enType;
    const char *encode_type = rk_param_get_string("audio.0:encode_type", "G711U");
    if (!strcmp(encode_type, "MP2")) 
    {
		enType = RK_AUDIO_ID_MP2;
	} 
    else if (!strcmp(encode_type, "G711A")) 
    {
		enType = RK_AUDIO_ID_PCM_ALAW;
	}
	else if (!strcmp(encode_type, "G711U")) 
    {
		enType = RK_AUDIO_ID_PCM_MULAW;
	}
	else if (!strcmp(encode_type, "PCM")) 
    {
		enType = RK_AUDIO_ID_PCM_S16LE;
	} 
    else 
    {
		LOG_ERROR("not support %s\n", encode_type);
	}

	pstChnAttr.stCodecAttr.enType = enType;
	pstChnAttr.stCodecAttr.u32Channels = 1; // default 1
	pstChnAttr.stCodecAttr.u32SampleRate = rk_param_get_int("audio.0:sample_rate", 8000);
	pstChnAttr.stCodecAttr.u32BitPerCodedSample = 4;

	pstChnAttr.enType = enType;
	pstChnAttr.enMode = ADEC_MODE_STREAM; // ADEC_MODE_PACK
	pstChnAttr.u32BufCount = 4;
	pstChnAttr.u32BufSize = 50 * 1024;
	ret = RK_MPI_ADEC_CreateChn(AdChn, &pstChnAttr);
	if (ret) 
    {
		LOG_ERROR("create adec chn %d err:0x%x\n", AdChn, ret);
		return RK_FAILURE;
	}

	// adec bind ao
	MPP_CHN_S stSrcChn, stDestChn;
	stSrcChn.enModId = RK_ID_ADEC;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;

	stDestChn.enModId = RK_ID_AO;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = 0;

	// 3. bind ADEC-AO
	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret) 
    {
		printf("Bind ADEC[0] to AO[0] failed! ret=%d\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_ao_deinit() 
{
	int ret;
	ret = RK_MPI_AO_DisableChn(0, 0);
	if (ret)
		LOG_ERROR("RK_MPI_AO_DisableChn fail %#x\n", ret);
	ret = RK_MPI_AO_Disable(0);
	if (ret)
		LOG_ERROR("RK_MPI_AO_Disable fail %#x\n", ret);

	return 0;
}
static int adec_data_free(void *opaque) {
	if (opaque) {
		free(opaque);
		opaque = RK_NULL;
	}
	return 0;
}
int rkipc_ao_write(unsigned char *data, int data_len) 
{
	int ret;
	// AUDIO_FRAME_S frame;
	MB_EXT_CONFIG_S extConfig;
	AUDIO_STREAM_S stAudioStream;
	static RK_S32 count = 0;
	static RK_U64 timeStamp = 0;

	// memset(&frame, 0, sizeof(frame));
	// frame.u32Len = data_len;
	// frame.u64TimeStamp = 0;
	// frame.enBitWidth = AUDIO_BIT_WIDTH_16;
	// frame.enSoundMode = AUDIO_SOUND_MODE_STEREO;
	// frame.bBypassMbBlk = RK_FALSE;
	
	unsigned char *srcData = RK_NULL;
	srcData = calloc(data_len, sizeof(unsigned char));
	if (!srcData)
	{
		LOG_ERROR("calloc error!");
	}
	memset(srcData, 0, data_len);
	memcpy(srcData,data,data_len);

	stAudioStream.u32Len = data_len;
	stAudioStream.u64TimeStamp = timeStamp;
	stAudioStream.u32Seq = ++count;
	stAudioStream.bBypassMbBlk = RK_TRUE;
	memset(&extConfig, 0, sizeof(extConfig));
	extConfig.pFreeCB = adec_data_free;
	extConfig.pOpaque = srcData;
	extConfig.pu8VirAddr = srcData;
	extConfig.u64Size = data_len;
	RK_MPI_SYS_CreateMB(&(stAudioStream.pMbBlk), &extConfig);

	// ret = RK_MPI_AO_SendFrame(0, 0, &frame, 1000);
	// if (ret)
	// 	LOG_ERROR("send frame fail, result = %#x\n", ret);
	ret = RK_MPI_ADEC_SendStream(0, &stAudioStream, RK_TRUE);
	if (ret != RK_SUCCESS) 
    {
		LOG_ERROR("fail to send adec stream.");
	}

	RK_MPI_MB_ReleaseMB(stAudioStream.pMbBlk);
	timeStamp++;
	if (data_len <= 0) 
    {
		LOG_INFO("eof\n");
		RK_MPI_AO_WaitEos(0, 0, -1);
		timeStamp = 0;
		count = 0;
	}

	return 0;
}

int rkipc_audio_init() 
{
	LOG_DEBUG("%s\n", __func__);
	int ret = rkipc_ai_init();
	
#if 0
	printf("==================rkipc_audio_init rkipc_aenc_init==================\n");
	ret |= rkipc_aenc_init();
	// bind ai to aenc
	ai_chn.enModId = RK_ID_AI;
	ai_chn.s32DevId = ai_dev_id;
	ai_chn.s32ChnId = ai_chn_id;

	aenc_chn.enModId = RK_ID_AENC;
	aenc_chn.s32DevId = aenc_dev_id;
	aenc_chn.s32ChnId = aenc_chn_id;

	ret |= RK_MPI_SYS_Bind(&ai_chn, &aenc_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_Bind fail %x\n", ret);
	}
	LOG_DEBUG("RK_MPI_SYS_Bind success\n");
#endif
	return ret;
}

int rkipc_audio_deinit() 
{
	LOG_DEBUG("%s\n", __func__);
	int ret;
	g_audio_run_ = 0;
	if (enable_aed || enable_bcd)
		pthread_join(ai_get_detect_result_tid, NULL);

	// if (enable_aed)
	// 	rkipc_audio_aed_deinit();
	// if (enable_bcd)
	// 	rkipc_audio_bcd_deinit();
	// if (enable_vqe)
	// 	rkipc_audio_vqe_deinit();
#if 0
	pthread_join(save_aenc_tid, RK_NULL);
	ret = RK_MPI_SYS_UnBind(&ai_chn, &aenc_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_UnBind fail %x\n", ret);
	}
	LOG_DEBUG("RK_MPI_SYS_UnBind success\n");
	ret |= rkipc_aenc_deinit();
#endif
	ret |= rkipc_ai_deinit();

	return ret;
}

// export api

int rk_audio_restart() 
{
	int ret;
	ret |= rkipc_audio_deinit();
	ret |= rkipc_audio_init();

	return ret;
}

int rk_audio_get_bit_rate(int stream_id, int *value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:bit_rate", stream_id);
	// *value = rk_param_get_int(entry, 16000);

	return 0;
}

int rk_audio_set_bit_rate(int stream_id, int value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:bit_rate", stream_id);
	// rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_sample_rate(int stream_id, int *value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:sample_rate", stream_id);
	// *value = rk_param_get_int(entry, 8000);

	return 0;
}

int rk_audio_set_sample_rate(int stream_id, int value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:sample_rate", stream_id);
	// rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_volume(int stream_id, int *value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:volume", stream_id);
	// *value = rk_param_get_int(entry, 50);

	return 0;
}

int rk_audio_set_volume(int stream_id, int value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:volume", stream_id);
	// rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_enable_vqe(int stream_id, int *value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:enable_vqe", stream_id);
	// *value = rk_param_get_int(entry, 1);

	return 0;
}

int rk_audio_set_enable_vqe(int stream_id, int value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:enable_vqe", stream_id);
	// rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_encode_type(int stream_id, const char **value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:encode_type", stream_id);
	// *value = rk_param_get_string(entry, "G711A");

	return 0;
}

int rk_audio_set_encode_type(int stream_id, const char *value) 
{
	// char entry[128] = {'\0'};
	// snprintf(entry, 127, "audio.%d:encode_type", stream_id);
	// rk_param_set_string(entry, value);

	return 0;
}

int rk_audio_set_ai_volume(int ai)
{
	RK_S32 volume = ai;
	RK_MPI_AI_SetVolume(0, volume);
	volume = 0;
	RK_MPI_AI_GetVolume(0, &volume);
	// LOG_INFO("rk_audio_set_ai_volume : get volume = %d", volume);
	return 0;
}
int rk_audio_set_ao_volume(int ao)
{
	RK_S32 volume = ao;
	RK_MPI_AO_SetVolume(0, volume);
	volume = 0;
	RK_MPI_AO_GetVolume(0, &volume);
	// LOG_INFO("rk_audio_set_ao_volume : get volume = %d", volume);
	return 0;
}

void rk_audio_set_mic_enable(int en)
{
	s_mic_enable = en;
}



#if 0
//aac
int rk_audio_aac_encode_deinit()
{
	if (hAacEncoder)
		aacEncClose(&hAacEncoder);

	return 0;
}
int rk_audio_aac_encode_init()
{
	LOG_INFO("in AencAACStreamProc\n");
	int ret;
	unsigned long long timestamp;
	struct timeval tv = {0};
	unsigned char buffer[ACAP_FRAME_SIZE];
	unsigned int frameSize;

	unsigned char *ptr = NULL;
	int numRemainInSamples;
	// allocate encoder instance
	int bitrate = 128000;
	int samplerate = 8000;
	int channel = 1;
	CHANNEL_MODE mode = MODE_1;
	int aot = 2;
	//	int bitrate_mode = 0;  //cbr
	int bitrate_mode = 1; // cbr
	const char *infile, *outfile, *lengthfile;
	int input_size;

	AACENC_BufDesc in_buf = {0}, out_buf = {0};
	AACENC_InArgs in_args = {0};
	AACENC_OutArgs out_args = {0};
	int in_identifier = IN_AUDIO_DATA;
	int in_size, in_elem_size;
	int out_identifier = OUT_BITSTREAM_DATA;
	int out_size, out_elem_size;
	int read, i;
	void *in_ptr, *out_ptr;
	uint8_t outbuf[1024];
	AACENC_ERROR err;

	AACENC_InfoStruct encInfo;

	if (aacEncOpen(&hAacEncoder, 0, channel) != AACENC_OK) // 1 channel
	{
		LOG_INFO("----- aacEncOpen failed...\n");
		goto aac_exit;
	}

	// set params: MPEG-4 AAC Low Complexity
	if (aacEncoder_SetParam(hAacEncoder, AACENC_AOT, aot) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}
	// set params: bitrate
	if (aacEncoder_SetParam(hAacEncoder, AACENC_BITRATEMODE, bitrate_mode) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}
	/*
	if (aacEncoder_SetParam(hAacEncoder, AACENC_BITRATE, bitrate) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}
	*/
	if (aacEncoder_SetParam(hAacEncoder, AACENC_SAMPLERATE, samplerate) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}
	if (aacEncoder_SetParam(hAacEncoder, AACENC_CHANNELMODE, mode) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}
	// adts encode
	if (aacEncoder_SetParam(hAacEncoder, AACENC_TRANSMUX, TT_MP4_ADTS) != AACENC_OK)
	{
		LOG_INFO("---- aacEncoder_SetParam failed.\n");
		goto aac_exit;
	}

	// init encoder
	if (aacEncEncode(hAacEncoder, NULL, NULL, NULL, NULL) != AACENC_OK)
	{
		LOG_INFO("----- aacEncEncode faield/\n");
		goto aac_exit;
	}

	// get encinfo
	aacEncInfo(hAacEncoder, &encInfo);

	input_size = channel * 2 * encInfo.frameLength;
	LOG_INFO("info.frameLength = %d\n", encInfo.frameLength);
	LOG_INFO("samplerate = %d\n", samplerate);
	LOG_INFO("aot = %d\n", aot);
	LOG_INFO("channel_mode = %d\n", mode);
	LOG_INFO("bitrate = %d\n", bitrate);

	return 0;
aac_exit:
	rk_audio_aac_encode_deinit();
	return -1;
}

int rk_audio_aac_encode(char * inbuf,int inbuf_size, char * outbuf,int outbuf_size)
{
	AACENC_BufDesc in_buf = {0}, out_buf = {0};
	AACENC_InArgs in_args = {0};
	AACENC_OutArgs out_args = {0};
	int in_identifier = IN_AUDIO_DATA;
	int in_size, in_elem_size;
	int out_identifier = OUT_BITSTREAM_DATA;
	int out_size, out_elem_size;

	void *in_ptr, *out_ptr;

	AACENC_ERROR err;

	AACENC_InfoStruct encInfo;

	in_ptr = inbuf;
	in_size = inbuf_size;
	in_elem_size = 2;

	in_args.numInSamples = inbuf_size/2;
	in_buf.numBufs = 1;
	in_buf.bufs = &in_ptr;
	in_buf.bufferIdentifiers = &in_identifier;
	in_buf.bufSizes = &in_size;
	in_buf.bufElSizes = &in_elem_size;

	out_ptr = outbuf;
	out_size = outbuf_size;
	out_elem_size = 1;
	out_buf.numBufs = 1;
	out_buf.bufs = &out_ptr;
	out_buf.bufferIdentifiers = &out_identifier;
	out_buf.bufSizes = &out_size;
	out_buf.bufElSizes = &out_elem_size;

	if ((err = aacEncEncode(hAacEncoder, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK)
	{
		LOG_INFO("aacEncEncode failed. err : 0x%x\n", err);
		return -1;
	}
	//			LOG_INFO("numInSamples=%d\n", out_args.numInSamples);
	//			LOG_INFO("size=%d\n", out_args.numOutBytes);

	// if (out_args.numOutBytes > 0)
	// {
		//				LOG_INFO("enc aac head : %02X %02X %02X %02X %02X %02X %02X\n", out_ptr[0], out_ptr[1], out_ptr[2], out_ptr[3], out_ptr[4], out_ptr[5], out_ptr[6]);
		//				LOG_INFO("total pcm size : %d\n", s_count);

		// gettimeofday(&tv, NULL);
		// timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
		// AudioInBuffer(out_ptr, out_args.numOutBytes, 0, timestamp);
		//				LOG_INFO("aac capture timestamp : %lld\n", timestamp);
	// }
	return out_args.numOutBytes;
}
#endif