// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video.h"
#include "audio.h"
#include "rockiva.h"

#define HAS_VO 0
#if HAS_VO
#include <rk_mpi_vo.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.c"

#define RKISP_MAINPATH 0
#define RKISP_SELFPATH 1
#define RKISP_FBCPATH 2
#define VIDEO_PIPE_0 0
#define VIDEO_PIPE_1 1
#define VIDEO_PIPE_2 2
#define VIDEO_PIPE_3 3
#define JPEG_VENC_CHN 2
#define DRAW_NN_VENC_CHN_ID 0
#define VPSS_ROTATE 6
#define VPSS_BGR 0
#define DRAW_NN_OSD_ID 7
#define DRAW_NN_OSD_EX_ID 6
#define RED_COLOR 0x0000FF
#define BLUE_COLOR 0xFF0000

#define RK3588_VO_DEV_HDMI 0
#define RK3588_VO_DEV_MIPI 3
#define RK3588_VOP_LAYER_CLUSTER0 0

#define RTSP_URL_0 "/live/0"
#define RTSP_URL_1 "/live/1"
#define RTSP_URL_2 "/live/2"
#define RTMP_URL_0 "rtmp://127.0.0.1:1935/live/mainstream"
#define RTMP_URL_1 "rtmp://127.0.0.1:1935/live/substream"
#define RTMP_URL_2 "rtmp://127.0.0.1:1935/live/thirdstream"

// -----------------------------------------------------------------------
// 新增代码 开始

static unsigned int uiMainStreamEncodeFrameCount_g = 0;
static unsigned int uiSubStreamEncodeFrameCount_g = 0;
//sd
static float stitch_distance = 5;
//qrcode
static QrCodeDecodeCallback QrCodeDecodeCb_g = NULL;
static int g_vi_qrcode_run_ = 0;
static pthread_t get_vi_qrcode_thread;

//ivs
static int g_ivs_run_ = 0;
static int ivs_start = 0;
static int ivs_sensibility = 1;
static motion_detect_callback ivs_cb = NULL;

//det
static int iva_start = 0;
static CaptureDetectCallback person_det_cb = NULL;
static CaptureDetectCallback vehicle_det_cb = NULL;
static CaptureDetectCallback non_vehicle_det_cb = NULL;
//motion tracker
static CaptureMotionTrackerCallback motion_tracker_cb = NULL;
//meanluma
float g_MeanLuma = 0.0f;
static char *jpeg_buffer = NULL;
static int jpeg_size = 0;

extern int g_video_chn_0_enc_param_inited;
extern int g_video_chn_0_enc_type;			//0-264 1-265
extern int g_video_chn_0_bit_rate;			//码率
extern int g_video_chn_0_frmae_rate; 		//帧率
extern int g_video_chn_0_gop;				//I帧间隔
extern int g_video_chn_1_enc_param_inited;
extern int g_video_chn_1_enc_type;			//0-264 1-265
extern int g_video_chn_1_bit_rate;			//码率
extern int g_video_chn_1_frmae_rate; 		//帧率
extern int g_video_chn_1_gop;				//I帧间隔

typedef struct {	
	uint32_t max_width;
	uint32_t max_height;
	uint32_t width;
	uint32_t height;
	uint32_t buf_cnt;
	uint32_t depth;
	int32_t frmae_rate;
} VI_CHAN_PARAM_T;

typedef struct {
	uint32_t max_width;
	uint32_t max_height;
	uint32_t width;
	uint32_t height;
	const char *enc_type;
	const char *rc_mode;
	const char *rc_quality;
	const char *profile;
	uint32_t frame_rate_den;
	uint32_t frame_rate_num;
	uint32_t gop;
	const char *gop_mode;
	uint32_t smartp_viridrlen;
	uint32_t bit_rate;
	const char *smart;
	uint32_t enable_motion_deblur; 
	uint32_t motion_deblur_strength; 
	uint32_t enable_motion_static_switch;
	uint32_t enable_debreath_effect;
	uint32_t debreath_effect_strength;
	const char *thrd_i;
	const char *thrd_p;
	const char *aq_step_i;
	const char *aq_step_p;
	uint32_t qbias_enable;
	uint32_t qbias_i;
	uint32_t qbias_p;
	uint32_t flt_str_i;
	uint32_t flt_str_p;
	uint32_t frame_min_i_qp;
	uint32_t frame_min_qp;
	uint32_t frame_max_i_qp;
	uint32_t frame_max_qp;
	uint32_t cu_dqp;
	uint32_t anti_ring;
	uint32_t anti_line;
	uint32_t lambds;
	uint32_t scalinglist;
	uint32_t buffer_count;
	uint32_t buffer_size;
	uint32_t enable_refer_buffer_share;
} H264_H265_VENC_CHAN_PARAM_T;

typedef struct {
	int channel;
	uint32_t max_width;
	uint32_t max_height;
	uint32_t width;
	uint32_t height;
	uint32_t buffer_count;
	uint32_t buffer_size;
	uint32_t jpeg_qfactor;
} JPEG_VENC_CHAN_PARAM_T;

typedef struct {
	RGN_HANDLE handle;
	uint32_t max_width;
	uint32_t max_height;
	uint32_t width;
	uint32_t height;
	int32_t x;
	int32_t y;
	int32_t alignment;
	char text[128];
	uint32_t font_color;
	uint32_t show;
	uint32_t inited;
	uint32_t changed;
	pthread_mutex_t mutex;
} RGN_OSD_PARAM_T;

int g_sensor_fps = 30;

static VI_CHAN_PARAM_T s_vi_chan_param[3] = {
	{
		.max_width = 2560,
		.max_height = 1440,
		.width = 2560,
		.height = 1440,
		.buf_cnt = 2,
		.depth = 1,
		.frmae_rate = 15,
	},
	{
		.max_width = 1280,
		.max_height = 720,
		.width = 1280,
		.height = 720,
		.buf_cnt = 2,
		.depth = 1,
		.frmae_rate = 15,
	},
	{
		.max_width = 960,
		.max_height = 540,
		.width = 640,
		.height = 360,
		.buf_cnt = 2,
		.depth = 1,
		.frmae_rate = 15,
	}
};

static H264_H265_VENC_CHAN_PARAM_T s_stream_venc_chan_param[2] = {
	{
		.max_width = 2560,
		.max_height = 1440,
		.width = 2560,
		.height = 1440,
		.enc_type = "H.265",
		.rc_mode = "VBR",
		.rc_quality = "high",
		.profile = "high",
		.frame_rate_den = 1,
		.frame_rate_num = 15,
		.gop = 30,
		.gop_mode = "normalP",
		.smartp_viridrlen = 25,
		.bit_rate = 2048,
		.smart = "close",
		.enable_motion_deblur = 0, 
		.motion_deblur_strength = 3, 
		.enable_motion_static_switch = 0,
		.enable_debreath_effect = 0,
		.debreath_effect_strength = 16,
		.thrd_i = NULL,
		.thrd_p = NULL,
		.aq_step_i = NULL,
		.aq_step_p = NULL,
		.qbias_enable = 0,
		.qbias_i = 0,
		.qbias_p = 0,
		.flt_str_i = 0,
		.flt_str_p = 0,
		.frame_min_i_qp = 26,
		.frame_min_qp = 28,
		.frame_max_i_qp = 51,
		.frame_max_qp = 51,
		.cu_dqp = 1,
		.anti_ring = 2,
		.anti_line = 2,
		.lambds = 4,
		.scalinglist = 0,
		.buffer_count = 4,
		.buffer_size = 1036800,
		.enable_refer_buffer_share = 1,
	},
	{
		.max_width = 1280,
		.max_height = 720,
		.width = 1280,
		.height = 720,
		.enc_type = "H.265",
		.rc_mode = "VBR",
		.rc_quality = "high",
		.profile = "high",
		.frame_rate_den = 1,
		.frame_rate_num = 15,
		.gop = 30,
		.gop_mode = "normalP",
		.smartp_viridrlen = 25,
		.bit_rate = 1024,
		.smart = "close",
		.enable_motion_deblur = 0, 
		.motion_deblur_strength = 3, 
		.enable_motion_static_switch = 0,
		.enable_debreath_effect = 0,
		.debreath_effect_strength = 16,
		.thrd_i = NULL,
		.thrd_p = NULL,
		.aq_step_i = NULL,
		.aq_step_p = NULL,
		.qbias_enable = 0,
		.qbias_i = 0,
		.qbias_p = 0,
		.flt_str_i = 0,
		.flt_str_p = 0,
		.frame_min_i_qp = 26,
		.frame_min_qp = 28,
		.frame_max_i_qp = 51,
		.frame_max_qp = 51,
		.cu_dqp = 1,
		.anti_ring = 2,
		.anti_line = 2,
		.lambds = 4,
		.scalinglist = 0,
		.buffer_count = 4,
		.buffer_size = 261120,
		.enable_refer_buffer_share = 1,
	}
};

static JPEG_VENC_CHAN_PARAM_T s_jpeg_venc_chan_param[2] = {
	{
		.channel = 2,
		.max_width = 1280,
		.max_height = 720,
		.width = 1280,
		.height = 720,
		.buffer_count = 2,
		.buffer_size = 256*1024,
		.jpeg_qfactor = 70,
	},
	{
		.channel = 3,
		.max_width = 100,
		.max_height = 100,
		.width = 100,
		.height = 100,
		.buffer_count = 4,
		.buffer_size = 10*1024,
		.jpeg_qfactor = 70,
	}
};

RGN_OSD_PARAM_T s_rgn_osd_param[8] = {
	{
		.handle = 0,
		.max_width = 0,
		.max_height = 0,
		.width = 0,
		.height = 0,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "24hour CHR-YYYY-MM-DD",
		.font_color = 0xfff799,
		.show = 1,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 1,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 2,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 3,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 4,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 5,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 6,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
	{
		.handle = 7,
		.max_width = 800,
		.max_height = 80,
		.width = 800,
		.height = 80,
		.x = 0,
		.y = 0,
		.alignment = 0,
		.text = "",
		.font_color = 0xfff799,
		.show = 0,
		.inited = 0,
		.changed = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	},
};

// -----------------------------------------------------------------------
// 新增代码 结束


static int send_jpeg_cnt = 0;
static int get_jpeg_cnt = 0;
static int enable_ivs, enable_jpeg, enable_venc_0, enable_venc_1, enable_rtsp, enable_rtmp;
static int g_enable_vo, g_vo_dev_id, g_vi_chn_id, enable_npu, enable_osd;
static int g_video_run_ = 1;
static int g_nn_osd_run_ = 0;
static int pipe_id_ = 0;
static int dev_id_ = 0;
static int cycle_snapshot_flag = 0;
static const char *tmp_output_data_type = "H.264";
static const char *tmp_rc_mode;
static const char *tmp_h264_profile;
static const char *tmp_smart;
static const char *tmp_gop_mode;
static const char *tmp_rc_quality;
static int get_venc_0_running;
static int get_venc_1_running;
static int get_venc_2_running;
static pthread_t vi_thread_1, venc_thread_0, venc_thread_1, venc_thread_2, jpeg_venc_thread_id,
    vpss_thread_rgb, cycle_snapshot_thread_id, get_nn_update_osd_thread_id,
    get_vi_send_jpeg_thread_id, get_vi_2_send_thread, get_ivs_result_thread;

static MPP_CHN_S vi_chn, vpss_bgr_chn, vpss_rotate_chn, vo_chn, vpss_out_chn[4], venc_chn, ivs_chn;
static VO_DEV VoLayer = RK3588_VOP_LAYER_CLUSTER0;

typedef enum rkCOLOR_INDEX_E {
	RGN_COLOR_LUT_INDEX_0 = 0,
	RGN_COLOR_LUT_INDEX_1 = 1,
} COLOR_INDEX_E;

#if HAS_VO
static void *get_vi_send_vo(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "get_vi_send_vo", 0, 0, 0);
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_1, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			LOG_ERROR("RK_MPI_VI_GetChnFrame ok:data %p loop:%d pts:%" PRId64 " ms\n", data,
			          loopCount, stViFrame.stVFrame.u64PTS / 1000);
			// 6.get the channel status
			// ret = RK_MPI_VI_QueryChnStatus(pipe_id_, VIDEO_PIPE_1, &stChnStatus);
			// LOG_ERROR("RK_MPI_VI_QueryChnStatus ret %x, "
			//           "w:%d,h:%d,enable:%d,lost:%d,framerate:%d,vbfail:%d\n",
			//           ret, stChnStatus.stSize.u32Width, stChnStatus.stSize.u32Height,
			//           stChnStatus.bEnable, stChnStatus.u32LostFrame, stChnStatus.u32FrameRate,
			//           stChnStatus.u32VbFail);

			// send vo
			ret = RK_MPI_VO_SendFrame(VoLayer, 0, &stViFrame, 1000);
			if (ret)
				LOG_ERROR("RK_MPI_VO_SendFrame timeout %x\n", ret);

			// 7.release the frame
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_1, &stViFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
		usleep(10 * 1000);
	}

	return 0;
}
#endif

static void *rkipc_get_venc_0(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcVenc0", 0, 0, 0);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
//	int loopCount = 0;
	int ret = 0;
	// FILE *fp = fopen("/data/venc.h265", "wb");
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
	if (!stFrame.pstPack)
		return NULL;

	unsigned long long timestamp;
	struct timeval tv = {0};
	int subtype = 0;
	int chan = DMC_MEDIA_VIDEO_MAIN_STREAM;
	int end_flag = 0;
	int media_type;
	void *data = NULL;

	while (get_venc_0_running) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_0, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			// fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			// fflush(fp);
			// LOG_DEBUG("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
//			rkipc_rtsp_write_video_frame(0, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
			    subtype = DMC_MEDIA_SUBTYPE_IFRAME;
//				rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
//				                             stFrame.pstPack->u64PTS, 1);
//				if (enable_rtmp)
//					rk_rtmp_write_video_frame(0, data, stFrame.pstPack->u32Len,
//					                          stFrame.pstPack->u64PTS, 1);
			} else {
				subtype = DMC_MEDIA_SUBTYPE_PFRAME;
//				rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
//				                             stFrame.pstPack->u64PTS, 0);
//				if (enable_rtmp)
//					rk_rtmp_write_video_frame(0, data, stFrame.pstPack->u32Len,
//					                          stFrame.pstPack->u64PTS, 0);
			}

			gettimeofday(&tv, NULL);
			//---------------------------
			if(stFrame.pstPack->u32Len > (384 *1024))
			{
				printf("\033[1;35m	 main stream frame is too large,size = %u	\033[0m\n",stFrame.pstPack->u32Len);
			}
			//---------------------------
			timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
			if (!strcmp(s_stream_venc_chan_param[0].enc_type, "H.264"))
				media_type = DMC_MEDIA_TYPE_H264;
			else
				media_type = DMC_MEDIA_TYPE_H265;
			
			data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			if (data)
			{
				dmc_input(	chan,
							media_type,
							subtype,
							timestamp,
							data,
							stFrame.pstPack->u32Len,
							end_flag);
			}
			else
				LOG_ERROR("RK_MPI_MB_Handle2VirAddr failed\n");
#if 0
			//帧率统计
			{
				if( stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE )
				{
					LOG_INFO("main Iframe size : %d \n", stFrame.pstPack->u32Len);
				}
				
				static int main_stream_count = 0;
				static unsigned int main_bit_rate_count = 0;
				static int main_gop_count = 0;
				static int last_time = 0;
				int now_time = time(0);;

				main_stream_count++;
				main_gop_count++;
				main_bit_rate_count += stFrame.pstPack->u32Len;

				if (last_time==0)
				{
					last_time = now_time;
				}
				int tt = now_time - last_time;
				if (tt >= 2)
				{
					LOG_INFO("main frame rate  : %dFPS \n", main_stream_count/tt);
					LOG_INFO("main bit rate    : %u KB/s \n", (main_bit_rate_count/1024)/tt);
					printf("\033[1;36m	main frame rate  : %dFPS  \033[0m\n", main_stream_count/tt);
					printf("\033[1;36m	main bit rate	 : %u Kb/s	\033[0m\n", (main_bit_rate_count*8/1024)/tt);
					last_time = now_time;
					main_stream_count = 0;
					main_bit_rate_count = 0;
				}
				if (DMC_MEDIA_SUBTYPE_IFRAME == subtype)
				{
					printf("\033[1;36m	main frame gop   : %d  \033[0m\n", main_gop_count);
					main_gop_count = 0;
				}
			}
#endif
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_0, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
//			loopCount++;
			uiMainStreamEncodeFrameCount_g++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
	// if (fp)
	// fclose(fp);

	return 0;
}

static void *rkipc_get_vi_send_jpeg(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcSendJPEG", 0, 0, 0);
	int jpeg_width, jpeg_height, ret;

	TDE_HANDLE hHandle;
	TDE_SURFACE_S pstSrc, pstDst;
	TDE_RECT_S pstSrcRect, pstDstRect;

	VIDEO_FRAME_INFO_S stViFrame, DstFrame;
	PIC_BUF_ATTR_S Dst_stPicBufAttr;
	MB_PIC_CAL_S Dst_stMbPicCalResult;
	VENC_CHN_ATTR_S pstChnAttr;
	MB_BLK dstBlk = RK_NULL;

	Dst_stPicBufAttr.u32Width = rk_param_get_int("video.0:max_width", 2304);
	Dst_stPicBufAttr.u32Height = rk_param_get_int("video.0:max_height", 1296);
	Dst_stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	Dst_stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;

	ret = RK_MPI_CAL_TDE_GetPicBufferSize(&Dst_stPicBufAttr, &Dst_stMbPicCalResult);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("get picture buffer size failed. err 0x%x\n", ret);
		return;
	}
	ret = RK_MPI_SYS_MmzAlloc(&dstBlk, RK_NULL, RK_NULL, Dst_stMbPicCalResult.u32MBSize);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_MmzAlloc err 0x%x\n", ret);
		return;
	}

	pstSrc.enColorFmt = RK_FMT_YUV420SP;
	pstSrc.enComprocessMode = COMPRESS_MODE_NONE;
	pstSrcRect.s32Xpos = 0;
	pstSrcRect.s32Ypos = 0;

	pstDst.enColorFmt = RK_FMT_YUV420SP;
	pstDst.enComprocessMode = COMPRESS_MODE_NONE;
	pstDstRect.s32Xpos = 0;
	pstDstRect.s32Ypos = 0;

	memset(&DstFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
	DstFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
	DstFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;

	ret = RK_TDE_Open();
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_TDE_Open fail %x\n", ret);
		RK_MPI_SYS_Free(dstBlk);
		return;
	}
	while (g_video_run_) {
		if (!send_jpeg_cnt) {
			usleep(300 * 1000);
			continue;
		}
		pstSrc.u32Width = rk_param_get_int("video.0:width", -1);
		pstSrc.u32Height = rk_param_get_int("video.0:height", -1);
		pstSrcRect.u32Width = rk_param_get_int("video.0:width", -1);
		pstSrcRect.u32Height = rk_param_get_int("video.0:height", -1);
		jpeg_width = rk_param_get_int("video.jpeg:width", 1920);
		jpeg_height = rk_param_get_int("video.jpeg:height", 1080);
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			// tde begin job
			hHandle = RK_TDE_BeginJob();
			if (RK_ERR_TDE_INVALID_HANDLE == hHandle) {
				LOG_ERROR("start job fail\n");
				RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame);
				continue;
			}
			// tde quick resize
			pstSrc.pMbBlk = stViFrame.stVFrame.pMbBlk;
			pstDst.pMbBlk = dstBlk;
			pstDst.u32Width = jpeg_width;
			pstDst.u32Height = jpeg_height;
			pstDstRect.u32Width = jpeg_width;
			pstDstRect.u32Height = jpeg_height;
			ret = RK_TDE_QuickResize(hHandle, &pstSrc, &pstSrcRect, &pstDst, &pstDstRect);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_TDE_QuickResize failed. err 0x%x\n", ret);
				RK_TDE_CancelJob(hHandle);
				RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame);
				continue;
			}
			// tde end job
			ret = RK_TDE_EndJob(hHandle, RK_FALSE, RK_TRUE, 10);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_TDE_EndJob failed. err 0x%x\n", ret);
				RK_TDE_CancelJob(hHandle);
				RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame);
				continue;
			}
			ret = RK_TDE_WaitForDone(hHandle);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_TDE_WaitForDone fail %x\n", ret);
			// set jpeg venc w,h
			ret = RK_MPI_VENC_GetChnAttr(JPEG_VENC_CHN, &pstChnAttr);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_GetChnAttr fail %x\n", ret);
			pstChnAttr.stVencAttr.u32PicWidth = pstDst.u32Width;
			pstChnAttr.stVencAttr.u32PicHeight = pstDst.u32Height;
			ret = RK_MPI_VENC_SetChnAttr(JPEG_VENC_CHN, &pstChnAttr);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_SetChnAttr fail %x\n", ret);
			// send frame to jpeg venc
			DstFrame.stVFrame.pMbBlk = dstBlk;
			DstFrame.stVFrame.u32Width = pstDst.u32Width;
			DstFrame.stVFrame.u32Height = pstDst.u32Height;
			DstFrame.stVFrame.u32VirWidth = pstDst.u32Width;
			DstFrame.stVFrame.u32VirHeight = pstDst.u32Height;
			ret = RK_MPI_VENC_SendFrame(JPEG_VENC_CHN, &DstFrame, 1000);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_SendFrame fail %x\n", ret);
			// release the frame
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", ret);
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
		send_jpeg_cnt--;
	}
	RK_TDE_Close();
	RK_MPI_SYS_Free(dstBlk);

	return NULL;
}

static int rga_nv12_border(rga_buffer_t buf, int x, int y, int width, int height, int line_pixel,
                           int color) {
	im_rect rect_up = {x, y, width, line_pixel};
	im_rect rect_buttom = {x, y + height - line_pixel, width, line_pixel};
	im_rect rect_left = {x, y, line_pixel, height};
	im_rect rect_right = {x + width - line_pixel, y, line_pixel, height};
	IM_STATUS STATUS = imfill(buf, rect_up, color);
	STATUS |= imfill(buf, rect_buttom, color);
	STATUS |= imfill(buf, rect_left, color);
	STATUS |= imfill(buf, rect_right, color);
	return STATUS == IM_STATUS_SUCCESS ? 0 : 1;
}

static void *rkipc_get_vi_draw_send_venc(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcVi2Venc", 0, 0, 0);
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	int line_pixel = 2;
	long long last_ba_result_time;
	RockIvaBaResult ba_result;
	im_handle_param_t param;
	RockIvaBaObjectInfo *object;
	rga_buffer_handle_t handle;
	rga_buffer_t src;

	memset(&ba_result, 0, sizeof(ba_result));
	memset(&param, 0, sizeof(im_handle_param_t));
	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_1, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			uint64_t phy_data = RK_MPI_MB_Handle2PhysAddr(stViFrame.stVFrame.pMbBlk);
			// LOG_DEBUG("phy_data %p, loop:%d pts:%" PRId64 " ms\n", phy_data, loopCount,
			//           stViFrame.stVFrame.u64PTS / 1000);

			ret = rkipc_rknn_object_get(&ba_result);
			if ((!ret && ba_result.objNum) ||
			    ((ret == -1) && (rkipc_get_curren_time_ms() - last_ba_result_time < 300))) {
				// LOG_DEBUG("ret is %d, ba_result.objNum is %d\n", ret, ba_result.objNum);
				handle = importbuffer_physicaladdr(phy_data, &param);
				src = wrapbuffer_handle_t(handle, stViFrame.stVFrame.u32Width,
				                          stViFrame.stVFrame.u32Height, stViFrame.stVFrame.u32Width,
				                          stViFrame.stVFrame.u32Height, RK_FORMAT_YCbCr_420_SP);
				if (!ret)
					last_ba_result_time = rkipc_get_curren_time_ms();
				for (int i = 0; i < ba_result.objNum; i++) {
					int x, y, w, h;
					object = &ba_result.triggerObjects[i];
					LOG_DEBUG("topLeft:[%d,%d], bottomRight:[%d,%d],"
					          "objId is %d, frameId is %d, score is %d, type is %d\n",
					          object->objInfo.rect.topLeft.x, object->objInfo.rect.topLeft.y,
					          object->objInfo.rect.bottomRight.x,
					          object->objInfo.rect.bottomRight.y, object->objInfo.objId,
					          object->objInfo.frameId, object->objInfo.score, object->objInfo.type);
					x = stViFrame.stVFrame.u32Width * object->objInfo.rect.topLeft.x / 10000;
					y = stViFrame.stVFrame.u32Height * object->objInfo.rect.topLeft.y / 10000;
					w = stViFrame.stVFrame.u32Width *
					    (object->objInfo.rect.bottomRight.x - object->objInfo.rect.topLeft.x) /
					    10000;
					h = stViFrame.stVFrame.u32Height *
					    (object->objInfo.rect.bottomRight.y - object->objInfo.rect.topLeft.y) /
					    10000;
					x = x / 2 * 2;
					y = y / 2 * 2;
					w = w / 2 * 2;
					h = h / 2 * 2;
					while (x + w + line_pixel >= stViFrame.stVFrame.u32Width) {
						w -= 8;
					}
					while (y + h + line_pixel >= stViFrame.stVFrame.u32Height) {
						h -= 8;
					}
					LOG_DEBUG("i is %d, x,y,w,h is %d,%d,%d,%d\n", i, x, y, w, h);
					rga_nv12_border(src, x, y, w, h, line_pixel, 0x000000ff);
					// LOG_INFO("draw rect time-consuming is %lld\n",(rkipc_get_curren_time_ms() -
					// last_ba_result_time));
					// LOG_INFO("triggerRules is %d, ruleID is %d, triggerType is %d\n",
					//          object->triggerRules,
					//          object->firstTrigger.ruleID,
					//          object->firstTrigger.triggerType);
				}
				releasebuffer_handle(handle);
			}

			// send venc
			ret = RK_MPI_VENC_SendFrame(VIDEO_PIPE_1, &stViFrame, 1000);
			if (ret)
				LOG_ERROR("RK_MPI_VENC_SendFrame timeout %x\n", ret);
			// 7.release the frame
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_1, &stViFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", ret);
			}

			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
	}

	return 0;
}

static void *rkipc_get_venc_1(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcVenc1", 0, 0, 0);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
//	int loopCount = 0;
	int ret = 0;
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
	if (!stFrame.pstPack)
		return NULL;

	unsigned long long timestamp;
	struct timeval tv = {0};
	int subtype = 0;
	int chan = DMC_MEDIA_VIDEO_SUB_STREAM;
	int end_flag = 0;
	int media_type;
	void *data = NULL;

	while (get_venc_1_running) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_1, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
//			rkipc_rtsp_write_video_frame(1, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
			    subtype = DMC_MEDIA_SUBTYPE_IFRAME;
//				rk_storage_write_video_frame(1, data, stFrame.pstPack->u32Len,
//				                             stFrame.pstPack->u64PTS, 1);
//				if (enable_rtmp)
//					rk_rtmp_write_video_frame(1, data, stFrame.pstPack->u32Len,
//					                          stFrame.pstPack->u64PTS, 1);
			} else {
				subtype = DMC_MEDIA_SUBTYPE_PFRAME;
//				rk_storage_write_video_frame(1, data, stFrame.pstPack->u32Len,
//				                             stFrame.pstPack->u64PTS, 0);
//				if (enable_rtmp)
//					rk_rtmp_write_video_frame(1, data, stFrame.pstPack->u32Len,
//					                          stFrame.pstPack->u64PTS, 0);
			}

			gettimeofday(&tv, NULL);
			timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
			if (!strcmp(s_stream_venc_chan_param[1].enc_type, "H.264"))
				media_type = DMC_MEDIA_TYPE_H264;
			else
				media_type = DMC_MEDIA_TYPE_H265;
			data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			if (data)
			{
				dmc_input(	chan,
							media_type,
							subtype,
							timestamp,
							data,
							stFrame.pstPack->u32Len,
							end_flag);
			}
			else
				LOG_ERROR("RK_MPI_MB_Handle2VirAddr failed\n");
#if 0
			//帧率统计
			{
				if( stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE )
				{
					LOG_INFO("sub Iframe size  : %d\n", stFrame.pstPack->u32Len);
				}
				
				static int sub_stream_count = 0;
				static unsigned int sub_bit_rate_count = 0;
				static int sub_gop_count = 0;
				static int last_time = 0;
				int now_time = time(0);;

				sub_stream_count++;
				sub_gop_count++;
				sub_bit_rate_count += stFrame.pstPack->u32Len;

				if (last_time==0)
				{
					last_time = now_time;
				}
				int tt = now_time - last_time;
				if (tt >= 2)
				{
					LOG_INFO("sub frame rate   : %dFPS	 \n", sub_stream_count/tt);
					LOG_INFO("sub bit rate	   : %u KB/s \n", (sub_bit_rate_count/1024)/tt);
					printf("\033[1;35m	sub frame rate	: %dFPS  \033[0m\n", sub_stream_count/tt);
					printf("\033[1;35m	sub bit rate	: %u Kb/s  \033[0m\n", (sub_bit_rate_count*8/1024)/tt);
					last_time = now_time;
					sub_stream_count = 0;
					sub_bit_rate_count = 0;
				}
				if (DMC_MEDIA_SUBTYPE_IFRAME == subtype)
				{
					printf("\033[1;35m	sub frame gop 	: %d  \033[0m\n", sub_gop_count);
					sub_gop_count = 0;
				}
			}
#endif

			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_1, &stFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
//			loopCount++;
			uiSubStreamEncodeFrameCount_g++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

static void *rkipc_get_jpeg(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcGetJpeg", 0, 0, 0);
#if 1
	VENC_STREAM_S stFrame;
	int ret = 0;

	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
	while (get_venc_2_running) {
		ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			if (get_jpeg_cnt > 0)
			{
				if ( (jpeg_size >= stFrame.pstPack->u32Len) && (jpeg_buffer != NULL))
				{
					memcpy(jpeg_buffer,data,stFrame.pstPack->u32Len);
					jpeg_size = stFrame.pstPack->u32Len;
				}
				else
				{
					jpeg_size = 0;
				}
			}
#if 1
			//帧率统计
			{
				
				// LOG_INFO("jpeg frame size : %d \n", stFrame.pstPack->u32Len);
				
				static int sub_stream_count = 0;
				static unsigned int sub_bit_rate_count = 0;
				static int last_time = 0;
				int now_time = time(0);;

				sub_stream_count++;
				sub_bit_rate_count += stFrame.pstPack->u32Len;

				if (last_time==0)
				{
					last_time = now_time;
				}
				int tt = now_time - last_time;
				if (tt >= 30)
				{
					LOG_INFO("jpeg frame rate  : %dFPS \n", sub_stream_count/tt);
					LOG_INFO("jpeg	 bit rate  : %u KB/s \n", (sub_bit_rate_count/1024)/tt);
					last_time = now_time;
					sub_stream_count = 0;
					sub_bit_rate_count = 0;
				}
			}
#endif

			// release the frame
			ret = RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
		if (get_jpeg_cnt > 0)
			get_jpeg_cnt--;
		//RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_2);
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
#else
	VENC_STREAM_S stFrame;
	int loopCount = 0;
	int ret = 0;
	char file_name[128] = {0};
	char record_path[256];

	memset(&record_path, 0, sizeof(record_path));
	strcat(record_path, rk_param_get_string("storage:mount_path", "/userdata"));
	strcat(record_path, "/");
	strcat(record_path, rk_param_get_string("storage.0:folder_name", "video0"));

	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
	// drop first frame
	ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
	if (ret == RK_SUCCESS)
		RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
	else
		LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
	while (g_video_run_) {
		if (!get_jpeg_cnt) {
			usleep(300 * 1000);
			continue;
		}
		// get the frame
		ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			LOG_DEBUG("Count:%d, Len:%d, PTS is %" PRId64 ", enH264EType is %d\n", loopCount,
			          stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			          stFrame.pstPack->DataType.enH264EType);
			// save jpeg file
			time_t t = time(NULL);
			struct tm tm = *localtime(&t);
			snprintf(file_name, 128, "%s/%d%02d%02d%02d%02d%02d.jpeg", record_path,
			         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
			         tm.tm_sec);
			LOG_DEBUG("file_name is %s, u32Len is %d\n", file_name, stFrame.pstPack->u32Len);
			FILE *fp = fopen(file_name, "wb");
			if (fp == NULL) {
				LOG_ERROR("fp is NULL\n");
			} else {
				fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			}
			// release the frame
			ret = RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
			if (fp) {
				fflush(fp);
				fclose(fp);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
		get_jpeg_cnt--;
		RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
#endif

	return 0;
}

static void *rkipc_cycle_snapshot(void *arg) {
	LOG_INFO("start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcCycleSnapshot", 0, 0, 0);

	while (g_video_run_ && cycle_snapshot_flag) {
		usleep(rk_param_get_int("video.jpeg:snapshot_interval_ms", 1000) * 1000);
		rk_take_photo();
	}
	LOG_INFO("exit %s thread, arg:%p\n", __func__, arg);

	return 0;
}

static void *rkipc_get_vi_2_send(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcGetVi2", 0, 0, 0);
	int ret;
	int32_t loopCount = 0;
	VIDEO_FRAME_INFO_S stViFrame;
	int npu_cycle_time_ms = 1000 / 5;//rk_param_get_int("video.source:npu_fps", 10);

	long long before_time, cost_time;
	while (g_video_run_) {
		before_time = rkipc_get_curren_time_ms();
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_2, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
//			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
//			uint8_t *phy_addr = (uint8_t *)RK_MPI_MB_Handle2PhysAddr(stViFrame.stVFrame.pMbBlk);
//			rkipc_rockiva_write_nv12_frame_by_phy_addr(
//			    stViFrame.stVFrame.u32Width, stViFrame.stVFrame.u32Height, loopCount, phy_addr);

			int32_t fd = RK_MPI_MB_Handle2Fd(stViFrame.stVFrame.pMbBlk);
			rkipc_rockiva_write_nv12_frame_by_fd(stViFrame.stVFrame.u32Width,stViFrame.stVFrame.u32Height, loopCount, fd);
				
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_2, &stViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x", ret);
		}
		cost_time = rkipc_get_curren_time_ms() - before_time;
		if ((cost_time > 0) && (cost_time < npu_cycle_time_ms))
			usleep((npu_cycle_time_ms - cost_time) * 1000);
	}
	return NULL;
}

static void *rkipc_get_vpss_bgr(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcGetVpssBgr", 0, 0, 0);
	VIDEO_FRAME_INFO_S frame;
	VI_CHN_STATUS_S stChnStatus;
	int32_t loopCount = 0;
	int ret = 0;

	while (g_video_run_) {
		ret = RK_MPI_VPSS_GetChnFrame(VPSS_BGR, 0, &frame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(frame.stVFrame.pMbBlk);
			// LOG_INFO("data:%p, u32Width:%d, u32Height:%d, PTS is %" PRId64 "\n", data,
			//          frame.stVFrame.u32Width, frame.stVFrame.u32Height, frame.stVFrame.u64PTS);
			// rkipc_rockiva_write_rgb888_frame(frame.stVFrame.u32Width, frame.stVFrame.u32Height,
			//                                  data);
			int32_t fd = RK_MPI_MB_Handle2Fd(frame.stVFrame.pMbBlk);
#if 0
			FILE *fp = fopen("/data/test.bgr", "wb");
			fwrite(data, 1, frame.stVFrame.u32Width * frame.stVFrame.u32Height * 3, fp);
			fflush(fp);
			fclose(fp);
			exit(1);
#endif
			// long long last_nn_time = rkipc_get_curren_time_ms();
			rkipc_rockiva_write_rgb888_frame_by_fd(frame.stVFrame.u32Width,
			                                       frame.stVFrame.u32Height, loopCount, fd);
			// LOG_DEBUG("nn time-consuming is %lld\n",(rkipc_get_curren_time_ms() - last_nn_time));

			ret = RK_MPI_VPSS_ReleaseChnFrame(VPSS_BGR, 0, &frame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VPSS_ReleaseChnFrame fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VPSS_GetChnFrame timeout %x\n", ret);
		}
	}

	return 0;
}

static void *rkipc_ivs_get_results(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcGetIVS", 0, 0, 0);
	int ret, i;
	IVS_RESULT_INFO_S stResults;
	int resultscount = 0;
	int count = 0;
//	int md = rk_param_get_int("ivs:md", 0);
//	int od = rk_param_get_int("ivs:od", 0);
	int md = 1;
	int od = 0;
	int width = rk_param_get_int("video.2:width", 960);
	int height = rk_param_get_int("video.2:height", 540);
	int md_area_threshold = width * height * 0.3;

#if 1
	while (g_ivs_run_) {
		ret = RK_MPI_IVS_GetResults(0, &stResults, 1000);
		if (ret >= 0) {
			if (md == 1) {
				if (1 == ivs_sensibility) { //低
					md_area_threshold = width * height * 0.3;
				} else if (2 == ivs_sensibility) { //中
					md_area_threshold = width * height * 0.2;
				} else if (3 == ivs_sensibility) { //高
					md_area_threshold = width * height * 0.1;
				}
				if (stResults.pstResults->stMdInfo.u32Square > md_area_threshold) {
					LOG_INFO("MD: md_area is %d, md_area_threshold is %d\n",
							stResults.pstResults->stMdInfo.u32Square, md_area_threshold);
					if (ivs_cb)
					{
						ivs_cb(1);
					}
				}
				else {
					if (ivs_cb)
					{
						ivs_cb(0);
					}
				}
			}
			if (od == 1) {
				if (stResults.s32ResultNum > 0) {
					if (stResults.pstResults->stOdInfo.u32Flag)
						LOG_INFO("OD flag:%d\n", stResults.pstResults->stOdInfo.u32Flag);
				}
			}
			RK_MPI_IVS_ReleaseResults(0, &stResults);
		} else {
			LOG_ERROR("get chn %d fail %d\n", 0, ret);
			usleep(50000llu);
		}
	}
#else
	while (g_video_run_) {
		ret = RK_MPI_IVS_GetResults(0, &stResults, 1000);
		if (ret >= 0) {
			resultscount++;
			if (md == 1) {
				if (stResults.pstResults->stMdInfo.u32Square > md_area_threshold) {
					LOG_INFO("MD: md_area is %d, md_area_threshold is %d\n",
					         stResults.pstResults->stMdInfo.u32Square, md_area_threshold);
				}
			}
			if (od == 1) {
				if (stResults.s32ResultNum > 0) {
					if (stResults.pstResults->stOdInfo.u32Flag)
						LOG_INFO("OD flag:%d\n", stResults.pstResults->stOdInfo.u32Flag);
				}
			}
			RK_MPI_IVS_ReleaseResults(0, &stResults);
		} else {
			LOG_ERROR("get chn %d fail %d\n", 0, ret);
			usleep(50000llu);
		}
	}
#endif
	return NULL;
}

int rkipc_rtmp_init() {
	int ret = 0;
	ret |= rk_rtmp_init(0, RTMP_URL_0);
	ret |= rk_rtmp_init(1, RTMP_URL_1);
	// ret |= rk_rtmp_init(2, RTMP_URL_2);

	return ret;
}

int rkipc_rtmp_deinit() {
	int ret = 0;
	ret |= rk_rtmp_deinit(0);
	ret |= rk_rtmp_deinit(1);
	// ret |= rk_rtmp_deinit(2);

	return ret;
}

int rkipc_vi_dev_init() {
	LOG_DEBUG("%s\n", __func__);
	int ret = 0;
	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(dev_id_, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(dev_id_, &stDevAttr);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(dev_id_);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(dev_id_);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = pipe_id_;
		stBindPipe.PipeId[0] = pipe_id_;
		ret = RK_MPI_VI_SetDevBindPipe(dev_id_, &stBindPipe);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int rkipc_vi_dev_deinit() {
	RK_MPI_VI_DisableDev(pipe_id_);

	return 0;
}

int rkipc_vi_chan_init(int chan, VI_CHAN_PARAM_T *p_chan_param) {
	int ret;
	// VI
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = p_chan_param->buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = p_chan_param->max_width;
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = p_chan_param->max_height;
	vi_chn_attr.stSize.u32Width = p_chan_param->width;
	vi_chn_attr.stSize.u32Height = p_chan_param->height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.u32Depth = p_chan_param->depth;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
	vi_chn_attr.stFrameRate.s32SrcFrameRate = g_sensor_fps;
	vi_chn_attr.stFrameRate.s32DstFrameRate = p_chan_param->frmae_rate;
	
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, chan, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VI chan[%d] error! ret=%d\n", chan, ret);
		return ret;
	}

	ret = RK_MPI_VI_EnableChn(pipe_id_, chan);
	if (ret) {
		LOG_ERROR("ERROR: enable VI chan[%d] error! ret=%d\n", chan, ret);
		return ret;
	}
	return 0;
}

int rkipc_vi_chan_deinit(int chan) {
	int ret;
	// VI
	ret = RK_MPI_VI_DisableChn(pipe_id_, chan);
	if (ret)
		LOG_ERROR("ERROR: Destroy VI chan[%d] error! ret=%#x\n", chan, ret);

	return 0;
}

int rkipc_venc_chan_init(int chan, H264_H265_VENC_CHAN_PARAM_T *p_chan_param) {
	int ret;
	// VENC
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
		
	if ((p_chan_param->enc_type == NULL) || (p_chan_param->rc_mode == NULL)) {
		LOG_ERROR("venc chan[%d] enc_type or rc_mode is NULL\n", chan);
		return -1;
	}
	LOG_DEBUG("venc chan[%d] enc_type is %s, rc_mode is %s, profile is %s\n",
	          chan, p_chan_param->enc_type, p_chan_param->rc_mode, p_chan_param->profile);

	printf("===============================\n");
	printf("video venc param.\n");
	printf("chan[%d] enc_type       : %s\n", chan, p_chan_param->enc_type);
	printf("chan[%d] bit_rate       : %d\n", chan, p_chan_param->bit_rate);
	printf("chan[%d] frame_rate_num : %d\n", chan, p_chan_param->frame_rate_num);
	printf("chan[%d] gop            : %d\n", chan, p_chan_param->gop);
	printf("===============================\n");
			  
	if (!strcmp(p_chan_param->enc_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(p_chan_param->profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(p_chan_param->profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(p_chan_param->profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("venc chan[%d] profile is %s\n", chan, p_chan_param->profile);
		if (!strcmp(p_chan_param->rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = p_chan_param->gop;
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = p_chan_param->bit_rate;
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = p_chan_param->gop;
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = p_chan_param->bit_rate / 3 * 2;
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = p_chan_param->bit_rate;
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = p_chan_param->bit_rate / 3;
		}
	} else if (!strcmp(p_chan_param->enc_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(p_chan_param->rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = p_chan_param->gop;
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = p_chan_param->bit_rate;
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = p_chan_param->gop;
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = p_chan_param->frame_rate_den;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = p_chan_param->frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = p_chan_param->bit_rate / 3 * 2;
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = p_chan_param->bit_rate;
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = p_chan_param->bit_rate / 3;
		}
	} else {
		LOG_ERROR("venc chan[%d] enc_type is %s, not support\n", chan, p_chan_param->enc_type);
		return -1;
	}

	if (!strcmp(p_chan_param->gop_mode, "normalP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	} else if (!strcmp(p_chan_param->gop_mode, "smartP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
		venc_chn_attr.stGopAttr.s32VirIdrLen = p_chan_param->smartp_viridrlen;
		venc_chn_attr.stGopAttr.u32MaxLtrCount = 1; // long-term reference frame ltr is fixed to 1
	} else if (!strcmp(p_chan_param->gop_mode, "TSVC4")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.0:gop", -1);
	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32MaxPicWidth = p_chan_param->max_width;
	venc_chn_attr.stVencAttr.u32MaxPicHeight = p_chan_param->max_height;
	venc_chn_attr.stVencAttr.u32PicWidth = p_chan_param->width;
	venc_chn_attr.stVencAttr.u32PicHeight = p_chan_param->height;
	venc_chn_attr.stVencAttr.u32VirWidth = p_chan_param->width;
	venc_chn_attr.stVencAttr.u32VirHeight = p_chan_param->height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = p_chan_param->buffer_count;
	venc_chn_attr.stVencAttr.u32BufSize = p_chan_param->buffer_size;
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(chan, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC chan[%d] error! ret=%#x\n", chan, ret);
		return -1;
	}

	if (!strcmp(p_chan_param->smart, "open"))
		RK_MPI_VENC_EnableSvc(chan, 1);

	if (p_chan_param->enable_motion_deblur) {
		ret = RK_MPI_VENC_EnableMotionDeblur(chan, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionDeblur chan[%d] error! ret=%#x\n", chan, ret);
		ret = RK_MPI_VENC_SetMotionDeblurStrength(chan, p_chan_param->motion_deblur_strength);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetMotionDeblurStrength chan[%d] error! ret=%#x\n", chan, ret);
	}
	if (p_chan_param->enable_motion_static_switch) {
		ret = RK_MPI_VENC_EnableMotionStaticSwitch(chan, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionStaticSwitch chan[%d] error! ret=%#x\n", chan, ret);
	}

	VENC_DEBREATHEFFECT_S debfrath_effect;
	memset(&debfrath_effect, 0, sizeof(VENC_DEBREATHEFFECT_S));
	if (p_chan_param->enable_debreath_effect) {
		debfrath_effect.bEnable = true;
		debfrath_effect.s32Strength1 = p_chan_param->debreath_effect_strength;
		ret = RK_MPI_VENC_SetDeBreathEffect(chan, &debfrath_effect);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetDeBreathEffect chan[%d] error! ret=%#x\n", chan, ret);
	}

	VENC_RC_PARAM2_S rc_param2;
	ret = RK_MPI_VENC_GetRcParam2(chan, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_GetRcParam2 chan[%d] error! ret=%#x\n", chan, ret);
	if (p_chan_param->thrd_i) {
		char *str = strdup(p_chan_param->thrd_i);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	if (p_chan_param->thrd_p) {
		char *str = strdup(p_chan_param->thrd_p);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	if (p_chan_param->aq_step_i) {
		char *str = strdup(p_chan_param->aq_step_i);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	if (p_chan_param->aq_step_p) {
		char *str = strdup(p_chan_param->aq_step_p);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	ret = RK_MPI_VENC_SetRcParam2(chan, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetRcParam2 chan[%d] error! ret=%#x\n", chan, ret);

	if (!strcmp(p_chan_param->enc_type, "H.264")) {
		VENC_H264_QBIAS_S qbias;
		qbias.bEnable = p_chan_param->qbias_enable;
		qbias.u32QbiasI = p_chan_param->qbias_i;
		qbias.u32QbiasP = p_chan_param->qbias_p;
		ret = RK_MPI_VENC_SetH264Qbias(chan, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH264Qbias chan[%d] error! ret=%#x\n", chan, ret);
	} else {
		VENC_H265_QBIAS_S qbias;
		qbias.bEnable = p_chan_param->qbias_enable;
		qbias.u32QbiasI = p_chan_param->qbias_i;
		qbias.u32QbiasP = p_chan_param->qbias_p;
		ret = RK_MPI_VENC_SetH265Qbias(chan, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH265Qbias chan[%d] error! ret=%#x\n", chan, ret);
	}

	VENC_FILTER_S pstFilter;
	RK_MPI_VENC_GetFilter(chan, &pstFilter);
	pstFilter.u32StrengthI = p_chan_param->flt_str_i;
	pstFilter.u32StrengthP = p_chan_param->flt_str_p;
	RK_MPI_VENC_SetFilter(chan, &pstFilter);

	// VENC_RC_PARAM_S h265_RcParam;
	// RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &h265_RcParam);
	// h265_RcParam.s32FirstFrameStartQp = 26;
	// h265_RcParam.stParamH265.u32StepQp = 8;
	// h265_RcParam.stParamH265.u32MaxQp = 51;
	// h265_RcParam.stParamH265.u32MinQp = 10;
	// h265_RcParam.stParamH265.u32MaxIQp = 46;
	// h265_RcParam.stParamH265.u32MinIQp = 24;
	// h265_RcParam.stParamH265.s32DeltIpQp = -4;
	// RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &h265_RcParam);

	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(chan, &venc_rc_param);
	if (!strcmp(p_chan_param->enc_type, "H.264")) {
		if (!strcmp(p_chan_param->rc_quality, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(p_chan_param->rc_quality, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(p_chan_param->rc_quality, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(p_chan_param->rc_quality, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(p_chan_param->rc_quality, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(p_chan_param->rc_quality, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
		venc_rc_param.stParamH264.u32FrmMinIQp = p_chan_param->frame_min_i_qp;
		venc_rc_param.stParamH264.u32FrmMinQp = p_chan_param->frame_min_qp;
		venc_rc_param.stParamH264.u32FrmMaxIQp = p_chan_param->frame_max_i_qp;
		venc_rc_param.stParamH264.u32FrmMaxQp = p_chan_param->frame_max_qp;
	} else if (!strcmp(p_chan_param->enc_type, "H.265")) {
		if (!strcmp(p_chan_param->rc_quality, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(p_chan_param->rc_quality, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(p_chan_param->rc_quality, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(p_chan_param->rc_quality, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(p_chan_param->rc_quality, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(p_chan_param->rc_quality, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
		venc_rc_param.stParamH265.u32FrmMinIQp = p_chan_param->frame_min_i_qp;
		venc_rc_param.stParamH265.u32FrmMinQp = p_chan_param->frame_min_qp;
		venc_rc_param.stParamH265.u32FrmMaxIQp = p_chan_param->frame_max_i_qp;
		venc_rc_param.stParamH265.u32FrmMaxQp = p_chan_param->frame_max_qp;
	} else {
		LOG_ERROR("chan[%d] enc_type is %s, not support\n", chan, p_chan_param->enc_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(chan, &venc_rc_param);

	if (!strcmp(p_chan_param->enc_type, "H.264")) {
		VENC_H264_TRANS_S pstH264Trans;
		RK_MPI_VENC_GetH264Trans(chan, &pstH264Trans);
		pstH264Trans.bScalingListValid = p_chan_param->scalinglist;
		RK_MPI_VENC_SetH264Trans(chan, &pstH264Trans);
	} else if (!strcmp(p_chan_param->enc_type, "H.265")) {
		VENC_H265_TRANS_S pstH265Trans;
		RK_MPI_VENC_GetH265Trans(chan, &pstH265Trans);
		pstH265Trans.bScalingListEnabled = p_chan_param->scalinglist;
		RK_MPI_VENC_SetH265Trans(chan, &pstH265Trans);
	}

	if (!strcmp(p_chan_param->enc_type, "H.265")) {
		VENC_H265_CU_DQP_S cu_dqp_s;
		RK_MPI_VENC_GetH265CuDqp(chan, &cu_dqp_s);
//		printf("RK_MPI_VENC_GetH265CuDqp -> cu_dqp_s.u32CuDqp: %d\n", cu_dqp_s.u32CuDqp);
//		printf("p_chan_param->cu_dqp: %d\n", p_chan_param->cu_dqp);
		cu_dqp_s.u32CuDqp = p_chan_param->cu_dqp;
//		printf("RK_MPI_VENC_SetH265CuDqp -> cu_dqp_s.u32CuDqp: %d\n", cu_dqp_s.u32CuDqp);
		RK_MPI_VENC_SetH265CuDqp(chan, &cu_dqp_s);
	}
	VENC_ANTI_RING_S anti_ring_s;
	RK_MPI_VENC_GetAntiRing(chan, &anti_ring_s);
	anti_ring_s.u32AntiRing = p_chan_param->anti_ring;
	RK_MPI_VENC_SetAntiRing(chan, &anti_ring_s);
	VENC_ANTI_LINE_S anti_line_s;
	RK_MPI_VENC_GetAntiLine(chan, &anti_line_s);
	anti_line_s.u32AntiLine = p_chan_param->anti_line;
	RK_MPI_VENC_SetAntiLine(chan, &anti_line_s);
	VENC_LAMBDA_S lambds_s;
	RK_MPI_VENC_GetLambda(chan, &lambds_s);
	lambds_s.u32Lambda = p_chan_param->lambds;
	RK_MPI_VENC_SetLambda(chan, &lambds_s);

	VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
	stVencChnRefBufShare.bEnable = p_chan_param->enable_refer_buffer_share;
	RK_MPI_VENC_SetChnRefBufShareAttr(chan, &stVencChnRefBufShare);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chan, &stRecvParam);
	return 0;
}

int rkipc_venc_chan_deinit(int chan) {
	int ret;
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(chan);
	ret |= RK_MPI_VENC_DestroyChn(chan);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC chan[%d] error! ret=%#x\n", chan, ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn chan[%d] success\n", chan);
	return 0;
}

int rkipc_venc_chan_start(int chan)
{
	int ret;

	if (0 == chan)
	{
		get_venc_0_running = 1;
		ret = pthread_create(&venc_thread_0, NULL, rkipc_get_venc_0, NULL);
		if (ret)
		{
			get_venc_0_running = 0;
			LOG_ERROR("create rkipc_get_venc_0 failed! ret=%d\n", ret);
		}
	}
	else if (1 == chan)
	{
		get_venc_1_running = 1;
		ret = pthread_create(&venc_thread_1, NULL, rkipc_get_venc_1, NULL);
		if (ret)
		{
			get_venc_1_running = 0;
			LOG_ERROR("create rkipc_get_venc_1 failed! ret=%d\n", ret);
		}
	}
	else
	{
		return -1;
	}
	// bind
	MPP_CHN_S vi_chn;
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = chan;
	MPP_CHN_S venc_chn;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = chan;
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI chan[%d] and VENC chan[%d] error! ret=%#x\n", vi_chn.s32ChnId, venc_chn.s32ChnId, ret);
	else
		LOG_DEBUG("Bind VI chan[%d] and VENC chan[%d] success\n", vi_chn.s32ChnId, venc_chn.s32ChnId);

	return 0;
}

int rkipc_venc_chan_stop(int chan)
{
	int ret;

	if (0 == chan)
	{
		if (get_venc_0_running)
		{
			get_venc_0_running = 0;
			pthread_join(venc_thread_0, NULL);
		}
	}
	else if (1 == chan)
	{
		if (get_venc_1_running)
		{
			get_venc_1_running = 0;
			pthread_join(venc_thread_1, NULL);
		}
	}
	else
	{
		return -1;
	}
	// unbind
	MPP_CHN_S vi_chn;
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = chan;
	MPP_CHN_S venc_chn;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = chan;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("UnBind VI chan[%d] and VENC chan[%d] error! ret=%#x\n", vi_chn.s32ChnId, venc_chn.s32ChnId, ret);
	else
		LOG_DEBUG("UnBind VI chan[%d] and VENC chan[%d] success\n", vi_chn.s32ChnId, venc_chn.s32ChnId);

	return 0;
}

int rkipc_venc_chan_jpeg_init(JPEG_VENC_CHAN_PARAM_T *p_jpeg_chan_param) {
	int ret;
	VENC_CHN_ATTR_S jpeg_chn_attr;
	memset(&jpeg_chn_attr, 0, sizeof(jpeg_chn_attr));

	jpeg_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP; // jpeg need VENC_RC_MODE_MJPEGFIXQP
	jpeg_chn_attr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
	jpeg_chn_attr.stRcAttr.stH264Cbr.u32Gop = 60;

	jpeg_chn_attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
	jpeg_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	jpeg_chn_attr.stVencAttr.u32MaxPicWidth = p_jpeg_chan_param->max_width;
	jpeg_chn_attr.stVencAttr.u32MaxPicHeight = p_jpeg_chan_param->max_height;
	jpeg_chn_attr.stVencAttr.u32PicWidth = p_jpeg_chan_param->width;
	jpeg_chn_attr.stVencAttr.u32PicHeight = p_jpeg_chan_param->height;
	jpeg_chn_attr.stVencAttr.u32VirWidth = p_jpeg_chan_param->width;
	jpeg_chn_attr.stVencAttr.u32VirHeight = p_jpeg_chan_param->height;
	jpeg_chn_attr.stVencAttr.u32StreamBufCnt = p_jpeg_chan_param->buffer_count;
	jpeg_chn_attr.stVencAttr.u32BufSize = p_jpeg_chan_param->buffer_size;
	// jpeg_chn_attr.stVencAttr.u32Depth = 1;
	jpeg_chn_attr.stVencAttr.enMirror = MIRROR_NONE;

	jpeg_chn_attr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
	jpeg_chn_attr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
	jpeg_chn_attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;

	ret = RK_MPI_VENC_CreateChn(p_jpeg_chan_param->channel, &jpeg_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create jpeg VENC chan[%d] error! ret=%d\n", p_jpeg_chan_param->channel, ret);
		return -1;
	}

//	VENC_CHN_PARAM_S stParam;
//	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));
//	stParam.stFrameRate.bEnable = RK_TRUE;
//	stParam.stFrameRate.s32SrcFrmRateNum = 15;
//	stParam.stFrameRate.s32SrcFrmRateDen = 1;
//	stParam.stFrameRate.s32DstFrmRateNum = 5;
//	stParam.stFrameRate.s32DstFrmRateDen = 1;
//	ret = RK_MPI_VENC_SetChnParam(p_jpeg_chan_param->channel, &stParam);
//	if (ret) {
//		LOG_ERROR("ERROR: create jpeg VENC chan[%d] error! ret=%d\n", p_jpeg_chan_param->channel, ret);
//		return -1;
//	}

	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = p_jpeg_chan_param->jpeg_qfactor;
	RK_MPI_VENC_SetJpegParam(p_jpeg_chan_param->channel, &stJpegParam);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;//1;
	RK_MPI_VENC_StartRecvFrame(p_jpeg_chan_param->channel, &stRecvParam); // must, for no streams callback running failed

	printf("\n\n-------------venc jpeg chan: %d\n\n", p_jpeg_chan_param->channel);

	return ret;
}

int rkipc_venc_chan_jpeg_deinit(JPEG_VENC_CHAN_PARAM_T *p_jpeg_chan_param) {
	int ret = 0;
	ret = RK_MPI_VENC_StopRecvFrame(p_jpeg_chan_param->channel);
	ret |= RK_MPI_VENC_DestroyChn(p_jpeg_chan_param->channel);
	if (ret)
		LOG_ERROR("ERROR: Destroy jpeg VENC chan[%d] error! ret=%#x\n", p_jpeg_chan_param->channel, ret);
	else
		LOG_INFO("RK_MPI_VENC_DestroyChn chan[%d] success\n", p_jpeg_chan_param->channel);

	return ret;
}

int rkipc_pipe_0_init() {
	int ret;
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);
	int buffer_line = rk_param_get_int("video.source:buffer_line", video_height / 4);
	int rotation = rk_param_get_int("video.source:rotation", 0);
	int buf_cnt = 2;
	int frame_min_i_qp = rk_param_get_int("video.0:frame_min_i_qp", 26);
	int frame_min_qp = rk_param_get_int("video.0:frame_min_qp", 28);
	int frame_max_i_qp = rk_param_get_int("video.0:frame_max_i_qp", 51);
	int frame_max_qp = rk_param_get_int("video.0:frame_max_qp", 51);
	int scalinglist = rk_param_get_int("video.0:scalinglist", 0);

	// VI
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.0:max_width", 2560);
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.0:max_height", 1440);
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.u32Depth = 1;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, VIDEO_PIPE_0, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	ret = RK_MPI_VI_EnableChn(pipe_id_, VIDEO_PIPE_0);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	// VENC
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.0:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.0:h264_profile", NULL);

	int sensor_fps = rk_param_get_int("isp.0.adjustment:fps", 30);
	int dst_frame_rate_den = rk_param_get_int("video.0:dst_frame_rate_den", -1);
	int dst_frame_rate_num = rk_param_get_int("video.0:dst_frame_rate_num", -1);
	int gop = rk_param_get_int("video.0:gop", -1);
	int min_rate = rk_param_get_int("video.0:min_rate", -1);
	int mid_rate = rk_param_get_int("video.0:mid_rate", -1);
	int max_rate = rk_param_get_int("video.0:max_rate", -1);
	if (g_video_chn_0_enc_param_inited)
	{
		printf("===============================\n");
		printf("web config video param.\n");
		printf("chan[0] g_video_chn_0_enc_type  : %d\n", g_video_chn_0_enc_type);
		printf("chan[0] g_video_chn_0_bit_rate  : %d\n", g_video_chn_0_bit_rate);
		printf("chan[0] g_video_chn_0_frmae_rate: %d\n", g_video_chn_0_frmae_rate);
		printf("chan[0] g_video_chn_0_gop       : %d\n", g_video_chn_0_gop);
		printf("===============================\n");
		tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
		if (!strcmp(tmp_rc_mode, "CBR")) {
			max_rate = g_video_chn_0_bit_rate;
		} else {
			min_rate = g_video_chn_0_bit_rate / 3;
			mid_rate = g_video_chn_0_bit_rate / 3 * 2;
			max_rate = g_video_chn_0_bit_rate;
		}
		dst_frame_rate_den = 1;
		dst_frame_rate_num = g_video_chn_0_frmae_rate;
		gop = g_video_chn_0_gop;
		
	}
	
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_DEBUG("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	          tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = gop;//rk_param_get_int("video.0:gop", -1);
//			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = max_rate;//rk_param_get_int("video.0:max_rate", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = gop;//rk_param_get_int("video.0:gop", -1);
//			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = mid_rate;//rk_param_get_int("video.0:mid_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = max_rate;//rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = min_rate;//rk_param_get_int("video.0:min_rate", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = gop;//rk_param_get_int("video.0:gop", -1);
//			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = max_rate;//rk_param_get_int("video.0:max_rate", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = gop;//rk_param_get_int("video.0:gop", -1);
//			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = mid_rate;//rk_param_get_int("video.0:mid_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = max_rate;//rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = min_rate;//rk_param_get_int("video.0:min_rate", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}

	tmp_gop_mode = rk_param_get_string("video.0:gop_mode", NULL);
	if (!strcmp(tmp_gop_mode, "normalP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	} else if (!strcmp(tmp_gop_mode, "smartP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
		venc_chn_attr.stGopAttr.s32VirIdrLen = rk_param_get_int("video.0:smartp_viridrlen", 25);
		venc_chn_attr.stGopAttr.u32MaxLtrCount = 1; // long-term reference frame ltr is fixed to 1
	} else if (!strcmp(tmp_gop_mode, "TSVC4")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.0:gop", -1);
	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.0:max_width", 2560);
	venc_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.0:max_height", 1440);
	venc_chn_attr.stVencAttr.u32PicWidth = video_width;
	venc_chn_attr.stVencAttr.u32PicHeight = video_height;
	venc_chn_attr.stVencAttr.u32VirWidth = video_width;
	venc_chn_attr.stVencAttr.u32VirHeight = video_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = rk_param_get_int("video.0:buffer_count", 4);
	venc_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.0:buffer_size", 1843200);
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_0, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
//	rk_video_reset_frame_rate(VIDEO_PIPE_0);
	char str_frame_rate[4];
	snprintf(str_frame_rate, 4, "%d", dst_frame_rate_num);
	rk_video_set_frame_rate(VIDEO_PIPE_0, str_frame_rate);

	tmp_smart = rk_param_get_string("video.0:smart", "close");
	if (!strcmp(tmp_smart, "open"))
		RK_MPI_VENC_EnableSvc(VIDEO_PIPE_0, 1);

	if (rk_param_get_int("video.0:enable_motion_deblur", 0)) {
		ret = RK_MPI_VENC_EnableMotionDeblur(VIDEO_PIPE_0, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionDeblur error! ret=%#x\n", ret);
		ret = RK_MPI_VENC_SetMotionDeblurStrength(
		    VIDEO_PIPE_0, rk_param_get_int("video.0:motion_deblur_strength", 3));
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetMotionDeblurStrength error! ret=%#x\n", ret);
	}
	if (rk_param_get_int("video.0:enable_motion_static_switch", 0)) {
		ret = RK_MPI_VENC_EnableMotionStaticSwitch(VIDEO_PIPE_0, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionStaticSwitch error! ret=%#x\n", ret);
	}

	VENC_DEBREATHEFFECT_S debfrath_effect;
	memset(&debfrath_effect, 0, sizeof(VENC_DEBREATHEFFECT_S));
	if (rk_param_get_int("video.0:enable_debreath_effect", 0)) {
		debfrath_effect.bEnable = true;
		debfrath_effect.s32Strength1 = rk_param_get_int("video.0:debreath_effect_strength", 16);
		ret = RK_MPI_VENC_SetDeBreathEffect(VIDEO_PIPE_0, &debfrath_effect);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetDeBreathEffect error! ret=%#x\n", ret);
	}

	VENC_RC_PARAM2_S rc_param2;
	ret = RK_MPI_VENC_GetRcParam2(VIDEO_PIPE_0, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_GetRcParam2 error! ret=%#x\n", ret);
	const char *strings = rk_param_get_string("video.0:thrd_i", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.0:thrd_p", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.0:aq_step_i", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.0:aq_step_p", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	ret = RK_MPI_VENC_SetRcParam2(VIDEO_PIPE_0, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetRcParam2 error! ret=%#x\n", ret);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_QBIAS_S qbias;
		qbias.bEnable = rk_param_get_int("video.0:qbias_enable", 0);
		qbias.u32QbiasI = rk_param_get_int("video.0:qbias_i", 0);
		qbias.u32QbiasP = rk_param_get_int("video.0:qbias_p", 0);
		ret = RK_MPI_VENC_SetH264Qbias(VIDEO_PIPE_0, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH264Qbias error! ret=%#x\n", ret);
	} else {
		VENC_H265_QBIAS_S qbias;
		qbias.bEnable = rk_param_get_int("video.0:qbias_enable", 0);
		qbias.u32QbiasI = rk_param_get_int("video.0:qbias_i", 0);
		qbias.u32QbiasP = rk_param_get_int("video.0:qbias_p", 0);
		ret = RK_MPI_VENC_SetH265Qbias(VIDEO_PIPE_0, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH265Qbias error! ret=%#x\n", ret);
	}

	VENC_FILTER_S pstFilter;
	RK_MPI_VENC_GetFilter(VIDEO_PIPE_0, &pstFilter);
	pstFilter.u32StrengthI = rk_param_get_int("video.0:flt_str_i", 0);
	pstFilter.u32StrengthP = rk_param_get_int("video.0:flt_str_p", 0);
	RK_MPI_VENC_SetFilter(VIDEO_PIPE_0, &pstFilter);

	// VENC_RC_PARAM_S h265_RcParam;
	// RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &h265_RcParam);
	// h265_RcParam.s32FirstFrameStartQp = 26;
	// h265_RcParam.stParamH265.u32StepQp = 8;
	// h265_RcParam.stParamH265.u32MaxQp = 51;
	// h265_RcParam.stParamH265.u32MinQp = 10;
	// h265_RcParam.stParamH265.u32MaxIQp = 46;
	// h265_RcParam.stParamH265.u32MinIQp = 24;
	// h265_RcParam.stParamH265.s32DeltIpQp = -4;
	// RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &h265_RcParam);

	tmp_rc_quality = rk_param_get_string("video.0:rc_quality", NULL);
	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
		venc_rc_param.stParamH264.u32FrmMinIQp = frame_min_i_qp;
		venc_rc_param.stParamH264.u32FrmMinQp = frame_min_qp;
		venc_rc_param.stParamH264.u32FrmMaxIQp = frame_max_i_qp;
		venc_rc_param.stParamH264.u32FrmMaxQp = frame_max_qp;
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
		venc_rc_param.stParamH265.u32FrmMinIQp = frame_min_i_qp;
		venc_rc_param.stParamH265.u32FrmMinQp = frame_min_qp;
		venc_rc_param.stParamH265.u32FrmMaxIQp = frame_max_i_qp;
		venc_rc_param.stParamH265.u32FrmMaxQp = frame_max_qp;
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &venc_rc_param);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_TRANS_S pstH264Trans;
		RK_MPI_VENC_GetH264Trans(VIDEO_PIPE_0, &pstH264Trans);
		pstH264Trans.bScalingListValid = scalinglist;
		RK_MPI_VENC_SetH264Trans(VIDEO_PIPE_0, &pstH264Trans);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_TRANS_S pstH265Trans;
		RK_MPI_VENC_GetH265Trans(VIDEO_PIPE_0, &pstH265Trans);
		pstH265Trans.bScalingListEnabled = scalinglist;
		RK_MPI_VENC_SetH265Trans(VIDEO_PIPE_0, &pstH265Trans);
	}

	if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_CU_DQP_S cu_dqp_s;
		RK_MPI_VENC_SetH265CuDqp(VIDEO_PIPE_0, &cu_dqp_s);
		cu_dqp_s.u32CuDqp = rk_param_get_int("video.0:cu_dqp", 1);
		RK_MPI_VENC_SetH265CuDqp(VIDEO_PIPE_0, &cu_dqp_s);
	}
	VENC_ANTI_RING_S anti_ring_s;
	RK_MPI_VENC_GetAntiRing(VIDEO_PIPE_0, &anti_ring_s);
	anti_ring_s.u32AntiRing = rk_param_get_int("video.0:anti_ring", 2);
	RK_MPI_VENC_SetAntiRing(VIDEO_PIPE_0, &anti_ring_s);
	VENC_ANTI_LINE_S anti_line_s;
	RK_MPI_VENC_GetAntiLine(VIDEO_PIPE_0, &anti_line_s);
	anti_line_s.u32AntiLine = rk_param_get_int("video.0:anti_line", 2);
	RK_MPI_VENC_SetAntiLine(VIDEO_PIPE_0, &anti_line_s);
	VENC_LAMBDA_S lambds_s;
	RK_MPI_VENC_GetLambda(VIDEO_PIPE_0, &lambds_s);
	lambds_s.u32Lambda = rk_param_get_int("video.0:lambds", 4);
	RK_MPI_VENC_SetLambda(VIDEO_PIPE_0, &lambds_s);

	VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
	stVencChnRefBufShare.bEnable = rk_param_get_int("video.0:enable_refer_buffer_share", 0);
	RK_MPI_VENC_SetChnRefBufShareAttr(VIDEO_PIPE_0, &stVencChnRefBufShare);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_270);
	}

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_0, &stRecvParam);
	get_venc_0_running = 1;
	ret = pthread_create(&venc_thread_0, NULL, rkipc_get_venc_0, NULL);
	if (ret)
	{
		get_venc_0_running = 0;
		LOG_ERROR("create rkipc_get_venc_0 failed! ret=%d\n", ret);
	}
	// bind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_0;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_0;
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Bind VI and VENC success\n");

	return 0;
}

int rkipc_pipe_0_deinit() {
	int ret;
	// unbind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_0;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_0;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Unbind VI and VENC success\n");
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_0);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_0);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn success\n");
	// VI
	ret = RK_MPI_VI_DisableChn(pipe_id_, VIDEO_PIPE_0);
	if (ret)
		LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);

	return 0;
}

int rkipc_pipe_1_init() {
	int ret;
	int video_width = rk_param_get_int("video.1:width", 1920);
	int video_height = rk_param_get_int("video.1:height", 1080);
	int buf_cnt = rk_param_get_int("video.1:input_buffer_count", 2);
	int rotation = rk_param_get_int("video.source:rotation", 0);
	int frame_min_i_qp = rk_param_get_int("video.1:frame_min_i_qp", 26);
	int frame_min_qp = rk_param_get_int("video.1:frame_min_qp", 28);
	int frame_max_i_qp = rk_param_get_int("video.1:frame_max_i_qp", 51);
	int frame_max_qp = rk_param_get_int("video.1:frame_max_qp", 51);
	int scalinglist = rk_param_get_int("video.1:scalinglist", 0);

	// VI
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.1:max_width", 704);
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.1:max_height", 576);
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.u32Depth = 1;//0;
	printf("vi_chn_attr.u32Depth: %u\n", vi_chn_attr.u32Depth);
	if (g_enable_vo)
		vi_chn_attr.u32Depth += 1;
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, VIDEO_PIPE_1, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(pipe_id_, VIDEO_PIPE_1);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	// VENC
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.1:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.1:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.1:h264_profile", NULL);

	int sensor_fps = rk_param_get_int("isp.0.adjustment:fps", 30);
	int dst_frame_rate_den = rk_param_get_int("video.1:dst_frame_rate_den", -1);
	int dst_frame_rate_num = rk_param_get_int("video.1:dst_frame_rate_num", -1);
	int gop = rk_param_get_int("video.1:gop", -1);
	int min_rate = rk_param_get_int("video.1:min_rate", -1);
	int mid_rate = rk_param_get_int("video.1:mid_rate", -1);
	int max_rate = rk_param_get_int("video.1:max_rate", -1);
	if (g_video_chn_1_enc_param_inited)
	{
		printf("===============================\n");
		printf("web config video param.\n");
		printf("chan[1] g_video_chn_1_enc_type  : %d\n", g_video_chn_1_enc_type);
		printf("chan[1] g_video_chn_1_bit_rate  : %d\n", g_video_chn_1_bit_rate);
		printf("chan[1] g_video_chn_1_frmae_rate: %d\n", g_video_chn_1_frmae_rate);
		printf("chan[1] g_video_chn_1_gop       : %d\n", g_video_chn_1_gop);
		printf("===============================\n");
		tmp_output_data_type = (0 == g_video_chn_1_enc_type) ? "H.264" : "H.265";
		if (!strcmp(tmp_rc_mode, "CBR")) {
			max_rate = g_video_chn_1_bit_rate;
		} else {
			min_rate = g_video_chn_1_bit_rate / 3;
			mid_rate = g_video_chn_1_bit_rate / 3 * 2;
			max_rate = g_video_chn_1_bit_rate;
		}
		dst_frame_rate_den = 1;
		dst_frame_rate_num = g_video_chn_1_frmae_rate;
		gop = g_video_chn_1_gop;
	}

	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_DEBUG("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	          tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = gop;//rk_param_get_int("video.1:gop", -1);
//			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = max_rate;//rk_param_get_int("video.1:max_rate", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = gop;//rk_param_get_int("video.1:gop", -1);
//			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = mid_rate;//rk_param_get_int("video.1:mid_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = max_rate;//rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = min_rate;//rk_param_get_int("video.1:min_rate", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = gop;//rk_param_get_int("video.1:gop", -1);
//			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = max_rate;//rk_param_get_int("video.1:max_rate", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = gop;//rk_param_get_int("video.1:gop", -1);
//			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
//			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = sensor_fps;
//			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = dst_frame_rate_den;
//			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = dst_frame_rate_num;
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = mid_rate;//rk_param_get_int("video.1:mid_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = max_rate;//rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = min_rate;//rk_param_get_int("video.1:min_rate", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}

	tmp_gop_mode = rk_param_get_string("video.1:gop_mode", NULL);
	if (!strcmp(tmp_gop_mode, "normalP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	} else if (!strcmp(tmp_gop_mode, "smartP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
		venc_chn_attr.stGopAttr.s32VirIdrLen = rk_param_get_int("video.1:smartp_viridrlen", 25);
		venc_chn_attr.stGopAttr.u32MaxLtrCount = 1; // long-term reference frame ltr is fixed to 1
	} else if (!strcmp(tmp_gop_mode, "TSVC4")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.1:gop", -1);

	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.1:max_width", 704);
	venc_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.1:max_height", 576);
	venc_chn_attr.stVencAttr.u32PicWidth = video_width;
	venc_chn_attr.stVencAttr.u32PicHeight = video_height;
	venc_chn_attr.stVencAttr.u32VirWidth = video_width;
	venc_chn_attr.stVencAttr.u32VirHeight = video_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = rk_param_get_int("video.1:buffer_count", 4);
	venc_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.1:buffer_size", 202752);
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_1, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
//	rk_video_reset_frame_rate(VIDEO_PIPE_1);
	char str_frame_rate[4];
	snprintf(str_frame_rate, 4, "%d", dst_frame_rate_num);
	rk_video_set_frame_rate(VIDEO_PIPE_1, str_frame_rate);

	tmp_smart = rk_param_get_string("video.1:smart", "close");
	if (!strcmp(tmp_smart, "open"))
		RK_MPI_VENC_EnableSvc(VIDEO_PIPE_1, 1);

	if (rk_param_get_int("video.1:enable_motion_deblur", 0)) {
		ret = RK_MPI_VENC_EnableMotionDeblur(VIDEO_PIPE_1, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionDeblur error! ret=%#x\n", ret);
		ret = RK_MPI_VENC_SetMotionDeblurStrength(
		    VIDEO_PIPE_1, rk_param_get_int("video.1:motion_deblur_strength", 3));
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetMotionDeblurStrength error! ret=%#x\n", ret);
	}
	if (rk_param_get_int("video.1:enable_motion_static_switch", 0)) {
		ret = RK_MPI_VENC_EnableMotionStaticSwitch(VIDEO_PIPE_1, true);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionStaticSwitch error! ret=%#x\n", ret);
	}

	VENC_DEBREATHEFFECT_S debfrath_effect;
	memset(&debfrath_effect, 0, sizeof(VENC_DEBREATHEFFECT_S));
	if (rk_param_get_int("video.1:enable_debreath_effect", 0)) {
		debfrath_effect.bEnable = true;
		debfrath_effect.s32Strength1 = rk_param_get_int("video.1:debreath_effect_strength", 16);
		ret = RK_MPI_VENC_SetDeBreathEffect(VIDEO_PIPE_1, &debfrath_effect);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetDeBreathEffect error! ret=%#x\n", ret);
	}

	VENC_RC_PARAM2_S rc_param2;
	ret = RK_MPI_VENC_GetRcParam2(VIDEO_PIPE_1, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_GetRcParam2 error! ret=%#x\n", ret);
	const char *strings = rk_param_get_string("video.1:thrd_i", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.1:thrd_p", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.1:aq_step_i", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	strings = rk_param_get_string("video.1:aq_step_p", NULL);
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	ret = RK_MPI_VENC_SetRcParam2(VIDEO_PIPE_1, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetRcParam2 error! ret=%#x\n", ret);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_QBIAS_S qbias;
		qbias.bEnable = rk_param_get_int("video.1:qbias_enable", 0);
		qbias.u32QbiasI = rk_param_get_int("video.1:qbias_i", 0);
		qbias.u32QbiasP = rk_param_get_int("video.1:qbias_p", 0);
		ret = RK_MPI_VENC_SetH264Qbias(VIDEO_PIPE_1, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH264Qbias error! ret=%#x\n", ret);
	} else {
		VENC_H265_QBIAS_S qbias;
		qbias.bEnable = rk_param_get_int("video.1:qbias_enable", 0);
		qbias.u32QbiasI = rk_param_get_int("video.1:qbias_i", 0);
		qbias.u32QbiasP = rk_param_get_int("video.1:qbias_p", 0);
		ret = RK_MPI_VENC_SetH265Qbias(VIDEO_PIPE_1, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH265Qbias error! ret=%#x\n", ret);
	}

	VENC_FILTER_S pstFilter;
	RK_MPI_VENC_GetFilter(VIDEO_PIPE_1, &pstFilter);
	pstFilter.u32StrengthI = rk_param_get_int("video.1:flt_str_i", 0);
	pstFilter.u32StrengthP = rk_param_get_int("video.1:flt_str_p", 0);
	RK_MPI_VENC_SetFilter(VIDEO_PIPE_1, &pstFilter);

	tmp_rc_quality = rk_param_get_string("video.1:rc_quality", NULL);
	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(VIDEO_PIPE_1, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
		venc_rc_param.stParamH264.u32FrmMinIQp = frame_min_i_qp;
		venc_rc_param.stParamH264.u32FrmMinQp = frame_min_qp;
		venc_rc_param.stParamH264.u32FrmMaxIQp = frame_max_i_qp;
		venc_rc_param.stParamH264.u32FrmMaxQp = frame_max_qp;
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
		venc_rc_param.stParamH265.u32FrmMinIQp = frame_min_i_qp;
		venc_rc_param.stParamH265.u32FrmMinQp = frame_min_qp;
		venc_rc_param.stParamH265.u32FrmMaxIQp = frame_max_i_qp;
		venc_rc_param.stParamH265.u32FrmMaxQp = frame_max_qp;
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(VIDEO_PIPE_1, &venc_rc_param);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_TRANS_S pstH264Trans;
		RK_MPI_VENC_GetH264Trans(VIDEO_PIPE_1, &pstH264Trans);
		pstH264Trans.bScalingListValid = scalinglist;
		RK_MPI_VENC_SetH264Trans(VIDEO_PIPE_1, &pstH264Trans);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_TRANS_S pstH265Trans;
		RK_MPI_VENC_GetH265Trans(VIDEO_PIPE_1, &pstH265Trans);
		pstH265Trans.bScalingListEnabled = scalinglist;
		RK_MPI_VENC_SetH265Trans(VIDEO_PIPE_1, &pstH265Trans);
	}

	if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_CU_DQP_S cu_dqp_s;
		RK_MPI_VENC_SetH265CuDqp(VIDEO_PIPE_1, &cu_dqp_s);
		cu_dqp_s.u32CuDqp = rk_param_get_int("video.1:cu_dqp", 1);
		RK_MPI_VENC_SetH265CuDqp(VIDEO_PIPE_1, &cu_dqp_s);
	}
	VENC_ANTI_RING_S anti_ring_s;
	RK_MPI_VENC_GetAntiRing(VIDEO_PIPE_1, &anti_ring_s);
	anti_ring_s.u32AntiRing = rk_param_get_int("video.1:anti_ring", 2);
	RK_MPI_VENC_SetAntiRing(VIDEO_PIPE_1, &anti_ring_s);
	VENC_ANTI_LINE_S anti_line_s;
	RK_MPI_VENC_GetAntiLine(VIDEO_PIPE_1, &anti_line_s);
	anti_line_s.u32AntiLine = rk_param_get_int("video.1:anti_line", 2);
	RK_MPI_VENC_SetAntiLine(VIDEO_PIPE_1, &anti_line_s);
	VENC_LAMBDA_S lambds_s;
	RK_MPI_VENC_GetLambda(VIDEO_PIPE_1, &lambds_s);
	lambds_s.u32Lambda = rk_param_get_int("video.1:lambds", 4);
	RK_MPI_VENC_SetLambda(VIDEO_PIPE_1, &lambds_s);

	VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
	stVencChnRefBufShare.bEnable = rk_param_get_int("video.1:enable_refer_buffer_share", 0);
	RK_MPI_VENC_SetChnRefBufShareAttr(VIDEO_PIPE_1, &stVencChnRefBufShare);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_270);
	}

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_1, &stRecvParam);
	get_venc_1_running = 1;
	ret = pthread_create(&venc_thread_1, NULL, rkipc_get_venc_1, NULL);
	if (ret)
	{
		get_venc_1_running = 0;
		LOG_ERROR("create rkipc_get_venc_1 failed! ret=%d\n", ret);
	}

	// pthread_create(&vi_thread_1, NULL, rkipc_get_vi_draw_send_venc, NULL);
	// bind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_1;
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Bind VI and VENC success\n");

	if (!g_enable_vo)
		return 0;
#if HAS_VO
	// VO
	VO_PUB_ATTR_S VoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CSC_S VideoCSC;
	VO_CHN_ATTR_S VoChnAttr;
	RK_U32 u32DispBufLen;
	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&VideoCSC, 0, sizeof(VO_CSC_S));
	memset(&VoChnAttr, 0, sizeof(VoChnAttr));

	if (g_vo_dev_id == 0) {
		VoPubAttr.enIntfType = VO_INTF_HDMI;
		VoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	} else {
		VoPubAttr.enIntfType = VO_INTF_MIPI;
		VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	}
	ret = RK_MPI_VO_SetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetPubAttr %x\n", ret);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_SetPubAttr success\n");

	ret = RK_MPI_VO_Enable(g_vo_dev_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_Enable err is %x\n", ret);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_Enable success\n");

	ret = RK_MPI_VO_GetLayerDispBufLen(VoLayer, &u32DispBufLen);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("Get display buf len failed with error code %d!\n", ret);
		return ret;
	}
	LOG_DEBUG("Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);
	u32DispBufLen = 3;
	ret = RK_MPI_VO_SetLayerDispBufLen(VoLayer, u32DispBufLen);
	if (ret != RK_SUCCESS) {
		return ret;
	}
	LOG_DEBUG("Agin Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);

	/* get vo attribute*/
	ret = RK_MPI_VO_GetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_GetPubAttr fail!\n");
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_GetPubAttr success\n");
	if ((VoPubAttr.stSyncInfo.u16Hact == 0) || (VoPubAttr.stSyncInfo.u16Vact == 0)) {
		if (g_vo_dev_id == RK3588_VO_DEV_HDMI) {
			VoPubAttr.stSyncInfo.u16Hact = 1920;
			VoPubAttr.stSyncInfo.u16Vact = 1080;
		} else {
			VoPubAttr.stSyncInfo.u16Hact = 1080;
			VoPubAttr.stSyncInfo.u16Vact = 1920;
		}
	}

	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stDispRect.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	stLayerAttr.stImageSize.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stImageSize.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	LOG_DEBUG("stLayerAttr W=%d, H=%d\n", stLayerAttr.stDispRect.u32Width,
	          stLayerAttr.stDispRect.u32Height);

	stLayerAttr.u32DispFrmRt = 25;
	stLayerAttr.enPixFormat = RK_FMT_RGB888;
	VideoCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	VideoCSC.u32Contrast = 50;
	VideoCSC.u32Hue = 50;
	VideoCSC.u32Luma = 50;
	VideoCSC.u32Satuature = 50;
	RK_S32 u32VoChn = 0;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(VoLayer, g_vo_dev_id, VO_LAYER_MODE_GRAPHIC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_BindLayer VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_BindLayer success\n");

	ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerAttr VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_SetLayerAttr success\n");

	ret = RK_MPI_VO_EnableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_EnableLayer VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_EnableLayer success\n");

	ret = RK_MPI_VO_SetLayerCSC(VoLayer, &VideoCSC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerCSC error\n");
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_SetLayerCSC success\n");

	ret = RK_MPI_VO_EnableChn(RK3588_VOP_LAYER_CLUSTER0, u32VoChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create RK3588_VOP_LAYER_CLUSTER0 layer %d ch vo failed!\n", u32VoChn);
		return ret;
	}
	LOG_DEBUG("RK_MPI_VO_EnableChn success\n");

	VoChnAttr.bDeflicker = RK_FALSE;
	VoChnAttr.u32Priority = 1;
	VoChnAttr.stRect.s32X = 0;
	VoChnAttr.stRect.s32Y = 0;
	VoChnAttr.stRect.u32Width = stLayerAttr.stDispRect.u32Width;
	VoChnAttr.stRect.u32Height = stLayerAttr.stDispRect.u32Height;
	ret = RK_MPI_VO_SetChnAttr(VoLayer, 0, &VoChnAttr);

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, get_vi_send_vo, NULL);
#endif

	return 0;
}

int rkipc_pipe_1_deinit() {
	int ret;
	// unbind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_1;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Unbind VI and VENC success\n");
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_1);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_1);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn success\n");
	// VI
	ret = RK_MPI_VI_DisableChn(pipe_id_, VIDEO_PIPE_1);
	if (ret)
		LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);

	return 0;
}

int rkipc_pipe_2_init() {
	int ret;
	int video_width = rk_param_get_int("video.2:width", -1);
	int video_height = rk_param_get_int("video.2:height", -1);
	int buf_cnt = 2;

	// VI
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	if (enable_npu) // ensure vi and ivs have two buffer ping-pong
		buf_cnt += 1;
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.2:max_width", 960);
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.2:max_height", 540);
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
	vi_chn_attr.u32Depth = 0;
	if (enable_npu)
		vi_chn_attr.u32Depth += 1;
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, VIDEO_PIPE_2, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	ret = RK_MPI_VI_EnableChn(pipe_id_, VIDEO_PIPE_2);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	if (enable_ivs) {
		rkipc_ivs_init();
		// bind
		vi_chn.enModId = RK_ID_VI;
		vi_chn.s32DevId = 0;
		vi_chn.s32ChnId = VIDEO_PIPE_2;
		ivs_chn.enModId = RK_ID_IVS;
		ivs_chn.s32DevId = 0;
		ivs_chn.s32ChnId = 0;
		ret = RK_MPI_SYS_Bind(&vi_chn, &ivs_chn);
		if (ret)
			LOG_ERROR("Bind VI and IVS error! ret=%#x\n", ret);
		else
			LOG_DEBUG("Bind VI and IVS success\n");
	}
	if (enable_npu) {
		pthread_create(&get_vi_2_send_thread, NULL, rkipc_get_vi_2_send, NULL);
		rkipc_osd_draw_nn_init();
	}
}

int rkipc_pipe_2_deinit() {
	int ret;
	if (enable_npu) {
		rkipc_osd_draw_nn_deinit();
		pthread_join(get_vi_2_send_thread, NULL);
	}
	if (enable_ivs) {
		// unbind
		vi_chn.enModId = RK_ID_VI;
		vi_chn.s32DevId = 0;
		vi_chn.s32ChnId = VIDEO_PIPE_2;
		ivs_chn.enModId = RK_ID_IVS;
		ivs_chn.s32DevId = 0;
		ivs_chn.s32ChnId = 0;
		ret = RK_MPI_SYS_UnBind(&vi_chn, &ivs_chn);
		if (ret)
			LOG_ERROR("Unbind VI and IVS error! ret=%#x\n", ret);
		else
			LOG_DEBUG("Unbind VI and IVS success\n");
		rkipc_ivs_deinit();
	}
	ret = RK_MPI_VI_DisableChn(pipe_id_, VIDEO_PIPE_2);
	if (ret)
		LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);

	return 0;
}

int rkipc_pipe_jpeg_init() {
	// jpeg resolution same to video.1
	int ret;
	int video_width = rk_param_get_int("video.jpeg:width", 1920);
	int video_height = rk_param_get_int("video.jpeg:height", 1080);
//	int rotation = rk_param_get_int("video.source:rotation", 0);
	// VENC[3] init
	VENC_CHN_ATTR_S jpeg_chn_attr;
	memset(&jpeg_chn_attr, 0, sizeof(jpeg_chn_attr));
	jpeg_chn_attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
	jpeg_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	jpeg_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.1:max_width", 2560);
	jpeg_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.1:max_height", 1440);
	jpeg_chn_attr.stVencAttr.u32PicWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32PicHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32VirWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32VirHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32StreamBufCnt = 2;
	jpeg_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.jpeg:jpeg_buffer_size", 204800);
	// jpeg_chn_attr.stVencAttr.u32Depth = 1;
	jpeg_chn_attr.stVencAttr.enMirror = MIRROR_NONE;

	jpeg_chn_attr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
	jpeg_chn_attr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
	jpeg_chn_attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;

	ret = RK_MPI_VENC_CreateChn(JPEG_VENC_CHN, &jpeg_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}

	VENC_CHN_PARAM_S stParam;
	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));
	stParam.stFrameRate.bEnable = RK_TRUE;
	stParam.stFrameRate.s32SrcFrmRateNum = 15;
	stParam.stFrameRate.s32SrcFrmRateDen = 1;
	stParam.stFrameRate.s32DstFrmRateNum = 5;
	stParam.stFrameRate.s32DstFrmRateDen = 1;
	ret = RK_MPI_VENC_SetChnParam(JPEG_VENC_CHN, &stParam);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}

	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = rk_param_get_int("video.jpeg:jpeg_qfactor", 70);
	RK_MPI_VENC_SetJpegParam(JPEG_VENC_CHN, &stJpegParam);
//	if (rotation == 0) {
//		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_0);
//	} else if (rotation == 90) {
//		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_90);
//	} else if (rotation == 180) {
//		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_180);
//	} else if (rotation == 270) {
//		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_270);
//	}

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;//1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN,
	                           &stRecvParam); // must, for no streams callback running failed

	get_venc_2_running = 1;
	ret = pthread_create(&jpeg_venc_thread_id, NULL, rkipc_get_jpeg, NULL);
	if (ret)
	{
		get_venc_2_running = 0;
		LOG_ERROR("create rkipc_get_jpeg failed! ret=%d\n", ret);
	}
//	pthread_create(&get_vi_send_jpeg_thread_id, NULL, rkipc_get_vi_send_jpeg, NULL);
//	if (rk_param_get_int("video.jpeg:enable_cycle_snapshot", 0)) {
//		cycle_snapshot_flag = 1;
//		pthread_create(&cycle_snapshot_thread_id, NULL, rkipc_cycle_snapshot, NULL);
//	}

	// bind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = JPEG_VENC_CHN;
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Bind VI and VENC success\n");

	return ret;
}

int rkipc_pipe_jpeg_deinit() {
	int ret = 0;
//	if (rk_param_get_int("video.jpeg:enable_cycle_snapshot", 0)) {
//		cycle_snapshot_flag = 0;
//		pthread_join(cycle_snapshot_thread_id, NULL);
//	}
//	pthread_join(get_vi_send_jpeg_thread_id, NULL);

	// unbind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = JPEG_VENC_CHN;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Unbind VI and VENC success\n");

	pthread_join(jpeg_venc_thread_id, NULL);
	ret = RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	ret |= RK_MPI_VENC_DestroyChn(JPEG_VENC_CHN);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_INFO("RK_MPI_VENC_DestroyChn success\n");

	return ret;
}

int rkipc_ivs_init() {
	int ret;
	int video_width = rk_param_get_int("video.2:width", -1);
	int video_height = rk_param_get_int("video.2:height", -1);
	int buf_cnt = 2;
//	int smear = rk_param_get_int("ivs:smear", 1);
//	int weightp = rk_param_get_int("ivs:weightp", 1);
//	int md = rk_param_get_int("ivs:md", 0);
//	int od = rk_param_get_int("ivs:od", 0);
	int smear = 0;
	int weightp = 1;
	int md = 1;
	int od = 0;
	if (ivs_start)
	{
		LOG_ERROR("ivs aleady start\n");
		return 0;
	}

	if (!smear && !weightp && !md && !od) {
		LOG_INFO("no pp function enabled! end\n");
		return -1;
	}

	// IVS
	IVS_CHN_ATTR_S attr;
	memset(&attr, 0, sizeof(attr));
	attr.enMode = IVS_MODE_MD_OD;
	attr.u32PicWidth = video_width;
	attr.u32PicHeight = video_height;
	attr.enPixelFormat = RK_FMT_YUV420SP;
	attr.s32Gop = rk_param_get_int("video.0:gop", 30);
	attr.bSmearEnable = smear;
	attr.bWeightpEnable = weightp;
	attr.bMDEnable = md;
	attr.s32MDInterval = 5;
	attr.bMDNightMode = RK_TRUE;
	attr.u32MDSensibility = rk_param_get_int("ivs:md_sensibility", 3);
	attr.bODEnable = od;
	attr.s32ODInterval = 1;
	attr.s32ODPercent = 6;
	ret = RK_MPI_IVS_CreateChn(0, &attr);
	if (ret) {
		LOG_ERROR("ERROR: RK_MPI_IVS_CreateChn error! ret=%#x\n", ret);
		return -1;
	}

	IVS_MD_ATTR_S stMdAttr;
	memset(&stMdAttr, 0, sizeof(stMdAttr));
	ret = RK_MPI_IVS_GetMdAttr(0, &stMdAttr);
	if (ret) {
		LOG_ERROR("ERROR: RK_MPI_IVS_GetMdAttr error! ret=%#x\n", ret);
		return -1;
	}
	stMdAttr.s32ThreshSad = 40;
	stMdAttr.s32ThreshMove = 2;
	stMdAttr.s32SwitchSad = 0;
	ret = RK_MPI_IVS_SetMdAttr(0, &stMdAttr);
	if (ret) {
		LOG_ERROR("ERROR: RK_MPI_IVS_SetMdAttr error! ret=%#x\n", ret);
		return -1;
	}

	ivs_start = 1;

	if (md == 1 || od == 1)
	{
		g_ivs_run_ = 1;
		pthread_create(&get_ivs_result_thread, NULL, rkipc_ivs_get_results, NULL);
	}

	return 0;
}

int rkipc_ivs_deinit() {
	int ret;

	if (!ivs_start)
	{
		LOG_ERROR("ivs aleady stop\n");
		return 0;
	}
	ivs_start = 0;
	g_ivs_run_ = 0;
	
	pthread_join(get_ivs_result_thread, NULL);
	// IVS
	ret = RK_MPI_IVS_DestroyChn(0);
	if (ret)
		LOG_ERROR("ERROR: Destroy IVS error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_IVS_DestroyChn success\n");

	return 0;
}

static RK_U8 rgn_color_lut_0_left_value[4] = {0x03, 0xf, 0x3f, 0xff};
static RK_U8 rgn_color_lut_0_right_value[4] = {0xc0, 0xf0, 0xfc, 0xff};
static RK_U8 rgn_color_lut_1_left_value[4] = {0x02, 0xa, 0x2a, 0xaa};
static RK_U8 rgn_color_lut_1_right_value[4] = {0x80, 0xa0, 0xa8, 0xaa};
RK_S32 draw_rect_2bpp(RK_U8 *buffer, RK_U32 width, RK_U32 height, int rgn_x, int rgn_y, int rgn_w,
                      int rgn_h, int line_pixel, COLOR_INDEX_E color_index) {
	int i;
	RK_U8 *ptr = buffer;
	RK_U8 value = 0;
	if (color_index == RGN_COLOR_LUT_INDEX_0)
		value = 0xff;
	if (color_index == RGN_COLOR_LUT_INDEX_1)
		value = 0xaa;

	if (line_pixel > 4) {
		printf("line_pixel > 4, not support\n", line_pixel);
		return -1;
	}

	// printf("YUV %dx%d, rgn (%d,%d,%d,%d), line pixel %d\n", width, height, rgn_x, rgn_y, rgn_w,
	// rgn_h, line_pixel); draw top line
	ptr += (width * rgn_y + rgn_x) >> 2;
	for (i = 0; i < line_pixel; i++) {
		memset(ptr, value, (rgn_w + 3) >> 2);
		ptr += width >> 2;
	}
	// draw letft/right line
	for (i = 0; i < (rgn_h - line_pixel * 2); i++) {
		if (color_index == RGN_COLOR_LUT_INDEX_1) {
			*ptr = rgn_color_lut_1_left_value[line_pixel - 1];
			*(ptr + ((rgn_w + 3) >> 2)) = rgn_color_lut_1_right_value[line_pixel - 1];
		} else {
			*ptr = rgn_color_lut_0_left_value[line_pixel - 1];
			*(ptr + ((rgn_w + 3) >> 2)) = rgn_color_lut_0_right_value[line_pixel - 1];
		}
		ptr += width >> 2;
	}
	// draw bottom line
	for (i = 0; i < line_pixel; i++) {
		memset(ptr, value, (rgn_w + 3) >> 2);
		ptr += width >> 2;
	}
	return 0;
}

static void *rkipc_get_nn_update_osd(void *arg) {
	g_nn_osd_run_ = 1;
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcNpuOsd", 0, 0, 0);

	int ret = 0;
	int line_pixel = 2;
	int change_to_nothing_flag = 0;
	int video_width = 0;
	int video_height = 0;
	int video_width1 = 0;
	int video_height1 = 0;
	int rotation = 0;
	long long last_ba_result_time;
	RockIvaBaResult ba_result;
	RockIvaBaObjectInfo *object;
	RGN_HANDLE RgnHandle = DRAW_NN_OSD_ID;
	RGN_CANVAS_INFO_S stCanvasInfo;

	RGN_HANDLE RgnHandle1 = DRAW_NN_OSD_EX_ID;
	RGN_CANVAS_INFO_S stCanvasInfo1;

	memset(&stCanvasInfo, 0, sizeof(RGN_CANVAS_INFO_S));
	memset(&stCanvasInfo1, 0, sizeof(RGN_CANVAS_INFO_S));
	memset(&ba_result, 0, sizeof(ba_result));
	while (g_nn_osd_run_) {
		usleep(40 * 1000);
		rotation = rk_param_get_int("video.source:rotation", 0);
		if (rotation == 90 || rotation == 270) {
			video_width = rk_param_get_int("video.0:height", -1);
			video_height = rk_param_get_int("video.0:width", -1);
		} else {
			video_width = rk_param_get_int("video.0:width", -1);
			video_height = rk_param_get_int("video.0:height", -1);
		}
		if (rotation == 90 || rotation == 270) {
			video_width1 = rk_param_get_int("video.1:height", -1);
			video_height1 = rk_param_get_int("video.1:width", -1);
		} else {
			video_width1 = rk_param_get_int("video.1:width", -1);
			video_height1 = rk_param_get_int("video.1:height", -1);
		}
		ret = rkipc_rknn_object_get(&ba_result);
		// LOG_DEBUG("ret is %d, ba_result.objNum is %d\n", ret, ba_result.objNum);

		if ((ret == -1) && (rkipc_get_curren_time_ms() - last_ba_result_time > 300))
			ba_result.objNum = 0;
		if (ret == 0)
			last_ba_result_time = rkipc_get_curren_time_ms();
		//0
		ret = RK_MPI_RGN_GetCanvasInfo(RgnHandle, &stCanvasInfo);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_RGN_GetCanvasInfo failed with %#x!", ret);
			continue;
		}
		if ((stCanvasInfo.stSize.u32Width != UPALIGNTO16(video_width)) ||
		    (stCanvasInfo.stSize.u32Height != UPALIGNTO16(video_height))) {
			LOG_WARN("canvas 0 is %d*%d, not equal %d*%d, maybe in the process of switching,"
			         "skip this time\n",
			         stCanvasInfo.stSize.u32Width, stCanvasInfo.stSize.u32Height,
			         UPALIGNTO16(video_width), UPALIGNTO16(video_height));
			continue;
		}
		memset((void *)stCanvasInfo.u64VirAddr, 0,
		       stCanvasInfo.u32VirWidth * stCanvasInfo.u32VirHeight >> 2);
		
		//1
		ret = RK_MPI_RGN_GetCanvasInfo(RgnHandle1, &stCanvasInfo1);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_RGN_GetCanvasInfo1 failed with %#x!", ret);
			continue;
		}
		if ((stCanvasInfo1.stSize.u32Width != UPALIGNTO16(video_width1)) ||
		    (stCanvasInfo1.stSize.u32Height != UPALIGNTO16(video_height1))) {
			LOG_WARN("canvas 1 is %d*%d, not equal %d*%d, maybe in the process of switching,"
			         "skip this time\n",
			         stCanvasInfo1.stSize.u32Width, stCanvasInfo1.stSize.u32Height,
			         UPALIGNTO16(video_width1), UPALIGNTO16(video_height1));
			continue;
		}
		memset((void *)stCanvasInfo1.u64VirAddr, 0,
		       stCanvasInfo1.u32VirWidth * stCanvasInfo1.u32VirHeight >> 2);

		// draw 0
		for (int i = 0; i < ba_result.objNum; i++) {
			int x, y, w, h;
			object = &ba_result.triggerObjects[i];
			// LOG_INFO("topLeft:[%d,%d], bottomRight:[%d,%d],"
			// 			"objId is %d, frameId is %d, score is %d, type is %d\n",
			// 			object->objInfo.rect.topLeft.x, object->objInfo.rect.topLeft.y,
			// 			object->objInfo.rect.bottomRight.x,
			// 			object->objInfo.rect.bottomRight.y, object->objInfo.objId,
			// 			object->objInfo.frameId, object->objInfo.score, object->objInfo.type);
			x = video_width * object->objInfo.rect.topLeft.x / 10000;
			y = video_height * object->objInfo.rect.topLeft.y / 10000;
			w = video_width *
			    (object->objInfo.rect.bottomRight.x - object->objInfo.rect.topLeft.x) / 10000;
			h = video_height *
			    (object->objInfo.rect.bottomRight.y - object->objInfo.rect.topLeft.y) / 10000;
			x = x / 16 * 16;
			y = y / 16 * 16;
			w = (w + 3) / 16 * 16;
			h = (h + 3) / 16 * 16;

			while (x + w + line_pixel >= video_width) {
				w -= 8;
			}
			while (y + h + line_pixel >= video_height) {
				h -= 8;
			}
			if (x < 0 || y < 0 || w <= 0 || h <= 0) {
				continue;
			}
			// LOG_DEBUG("i is %d, x,y,w,h is %d,%d,%d,%d\n", i, x, y, w, h);
			if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_PERSON) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo.u64VirAddr, stCanvasInfo.u32VirWidth,
				               stCanvasInfo.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_1);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_FACE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo.u64VirAddr, stCanvasInfo.u32VirWidth,
				               stCanvasInfo.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_1);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_VEHICLE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo.u64VirAddr, stCanvasInfo.u32VirWidth,
				               stCanvasInfo.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_0);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_NON_VEHICLE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo.u64VirAddr, stCanvasInfo.u32VirWidth,
				               stCanvasInfo.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_0);
			}
			// LOG_INFO("draw rect time-consuming is %lld\n",(rkipc_get_curren_time_ms() -
			// 	last_ba_result_time));
			// LOG_INFO("triggerRules is %d, ruleID is %d, triggerType is %d\n",
			// 			object->triggerRules,
			// 			object->firstTrigger.ruleID,
			// 			object->firstTrigger.triggerType);
		}
		ret = RK_MPI_RGN_UpdateCanvas(RgnHandle);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_RGN_UpdateCanvas failed with %#x!", ret);
			continue;
		}

		// draw 1
		for (int i = 0; i < ba_result.objNum; i++) {
			int x, y, w, h;
			object = &ba_result.triggerObjects[i];
			// LOG_INFO("topLeft:[%d,%d], bottomRight:[%d,%d],"
			// 			"objId is %d, frameId is %d, score is %d, type is %d\n",
			// 			object->objInfo.rect.topLeft.x, object->objInfo.rect.topLeft.y,
			// 			object->objInfo.rect.bottomRight.x,
			// 			object->objInfo.rect.bottomRight.y, object->objInfo.objId,
			// 			object->objInfo.frameId, object->objInfo.score, object->objInfo.type);
			x = video_width1 * object->objInfo.rect.topLeft.x / 10000;
			y = video_height1 * object->objInfo.rect.topLeft.y / 10000;
			w = video_width1 *
			    (object->objInfo.rect.bottomRight.x - object->objInfo.rect.topLeft.x) / 10000;
			h = video_height1 *
			    (object->objInfo.rect.bottomRight.y - object->objInfo.rect.topLeft.y) / 10000;
			x = x / 16 * 16;
			y = y / 16 * 16;
			w = (w + 3) / 16 * 16;
			h = (h + 3) / 16 * 16;

			while (x + w + line_pixel >= video_width1) {
				w -= 8;
			}
			while (y + h + line_pixel >= video_height1) {
				h -= 8;
			}
			if (x < 0 || y < 0 || w <= 0 || h <= 0) {
				continue;
			}
			// LOG_DEBUG("i is %d, x,y,w,h is %d,%d,%d,%d\n", i, x, y, w, h);
			if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_PERSON) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo1.u64VirAddr, stCanvasInfo1.u32VirWidth,
				               stCanvasInfo1.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_1);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_FACE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo1.u64VirAddr, stCanvasInfo1.u32VirWidth,
				               stCanvasInfo1.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_1);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_VEHICLE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo1.u64VirAddr, stCanvasInfo1.u32VirWidth,
				               stCanvasInfo1.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_0);
			} else if (object->objInfo.type == ROCKIVA_OBJECT_TYPE_NON_VEHICLE) {
				draw_rect_2bpp((RK_U8 *)stCanvasInfo1.u64VirAddr, stCanvasInfo1.u32VirWidth,
				               stCanvasInfo1.u32VirHeight, x, y, w, h, line_pixel,
				               RGN_COLOR_LUT_INDEX_0);
			}
			// LOG_INFO("draw rect time-consuming is %lld\n",(rkipc_get_curren_time_ms() -
			// 	last_ba_result_time));
			// LOG_INFO("triggerRules is %d, ruleID is %d, triggerType is %d\n",
			// 			object->triggerRules,
			// 			object->firstTrigger.ruleID,
			// 			object->firstTrigger.triggerType);
		}
		ret = RK_MPI_RGN_UpdateCanvas(RgnHandle1);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_RGN_UpdateCanvas failed with %#x!", ret);
			continue;
		}
	}

	return 0;
}

int rkipc_osd_draw_nn_init() {
	LOG_DEBUG("start\n");
	int ret = 0;
	RGN_HANDLE RgnHandle = DRAW_NN_OSD_ID;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;
	int rotation = rk_param_get_int("video.source:rotation", 0);

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_2BPP;
	stRgnAttr.unAttr.stOverlay.u32CanvasNum = 1;
	if (rotation == 90 || rotation == 270) {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:max_height", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:max_width", -1);
	} else {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:max_width", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:max_height", -1);
	}
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_DEBUG("The handle: %d, create success\n", RgnHandle);
	// after malloc max size, it needs to be set to the actual size
	if (rotation == 90 || rotation == 270) {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:height", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:width", -1);
	} else {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:width", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:height", -1);
	}
	ret = RK_MPI_RGN_SetAttr(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_SetAttr (%d) failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}

	// display overlay regions to venc groups
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = RK_TRUE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 255;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = DRAW_NN_OSD_ID;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_0] = BLUE_COLOR;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_1] = RED_COLOR;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = DRAW_NN_VENC_CHN_ID;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");

	//for sub video stream
	RgnHandle = DRAW_NN_OSD_EX_ID;
	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_2BPP;
	stRgnAttr.unAttr.stOverlay.u32CanvasNum = 1;
	if (rotation == 90 || rotation == 270) {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.1:max_height", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.1:max_width", -1);
	} else {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.1:max_width", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.1:max_height", -1);
	}
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_DEBUG("The handle: %d, create success\n", RgnHandle);
	// after malloc max size, it needs to be set to the actual size
	if (rotation == 90 || rotation == 270) {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.1:height", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.1:width", -1);
	} else {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.1:width", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.1:height", -1);
	}
	ret = RK_MPI_RGN_SetAttr(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_SetAttr (%d) failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}

	// display overlay regions to venc groups
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = RK_TRUE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 255;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = DRAW_NN_OSD_EX_ID;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_0] = BLUE_COLOR;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_1] = RED_COLOR;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 1;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc1 success\n");

	pthread_create(&get_nn_update_osd_thread_id, NULL, rkipc_get_nn_update_osd, NULL);
	LOG_DEBUG("end\n");

	return ret;
}

int rkipc_osd_draw_nn_deinit() {
	LOG_DEBUG("%s\n", __func__);
	int ret = 0;
	if (g_nn_osd_run_) {
		g_nn_osd_run_ = 0;
		pthread_join(get_nn_update_osd_thread_id, NULL);
	}
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = DRAW_NN_OSD_ID;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = DRAW_NN_VENC_CHN_ID;
	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (RK_SUCCESS != ret)
		LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_DEBUG("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_draw_nn_change() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	int rotation = rk_param_get_int("video.source:rotation", 0);
	MPP_CHN_S stMppChn;
	RGN_ATTR_S stRgnAttr;
	RGN_CHN_ATTR_S stRgnChnAttr;
	RGN_HANDLE RgnHandle = DRAW_NN_OSD_ID;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = DRAW_NN_VENC_CHN_ID;
	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (RK_SUCCESS != ret)
		LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
	ret = RK_MPI_RGN_GetAttr(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_GetAttr (%d) failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}
	if (rotation == 90 || rotation == 270) {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:height", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:width", -1);
	} else {
		stRgnAttr.unAttr.stOverlay.stSize.u32Width = rk_param_get_int("video.0:width", -1);
		stRgnAttr.unAttr.stOverlay.stSize.u32Height = rk_param_get_int("video.0:height", -1);
	}
	ret = RK_MPI_RGN_SetAttr(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_SetAttr (%d) failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}

	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = RK_TRUE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 255;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = DRAW_NN_OSD_ID;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_0] = BLUE_COLOR;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[RGN_COLOR_LUT_INDEX_1] = RED_COLOR;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) failed with %#x!", RgnHandle, ret);
		return RK_FAILURE;
	}

	return ret;
}

// export API
int rk_video_get_gop(int stream_id, int *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:gop", stream_id);
//	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_gop(int stream_id, int value) {
//	char entry[128] = {'\0'};
//	VENC_CHN_ATTR_S venc_chn_attr;
//	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
//	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
//	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
//	tmp_output_data_type = rk_param_get_string(entry, "H.264");
//	tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
//	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
//	tmp_rc_mode = rk_param_get_string(entry, "CBR");
//	if (!strcmp(tmp_output_data_type, "H.264")) {
//		if (!strcmp(tmp_rc_mode, "CBR"))
//			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = value;
//		else
//			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = value;
//	} else if (!strcmp(tmp_output_data_type, "H.265")) {
//		if (!strcmp(tmp_rc_mode, "CBR"))
//			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = value;
//		else
//			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = value;
//	} else {
//		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
//		return -1;
//	}
//	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
//	snprintf(entry, 127, "video.%d:gop", stream_id);
//	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_max_rate(int stream_id, int *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:max_rate", stream_id);
//	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_max_rate(int stream_id, int value) {
//	VENC_CHN_ATTR_S venc_chn_attr;
//	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
//	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
//	tmp_output_data_type = rk_param_get_string(entry, "H.264");
//	tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
//	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
//	tmp_rc_mode = rk_param_get_string(entry, "CBR");
//	if (!strcmp(tmp_output_data_type, "H.264")) {
//		if (!strcmp(tmp_rc_mode, "CBR")) {
//			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = value;
//		} else {
//			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = value / 3;
//			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = value / 3 * 2;
//			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = value;
//		}
//	} else if (!strcmp(tmp_output_data_type, "H.265")) {
//		if (!strcmp(tmp_rc_mode, "CBR")) {
//			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = value;
//		} else {
//			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = value / 3;
//			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = value / 3 * 2;
//			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = value;
//		}
//	} else {
//		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
//		return -1;
//	}
//	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
//	snprintf(entry, 127, "video.%d:max_rate", stream_id);
//	rk_param_set_int(entry, value);
//	snprintf(entry, 127, "video.%d:mid_rate", stream_id);
//	rk_param_set_int(entry, value / 3 * 2);
//	snprintf(entry, 127, "video.%d:min_rate", stream_id);
//	rk_param_set_int(entry, value / 3);

	return 0;
}

int rk_video_get_RC_mode(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
//	*value = rk_param_get_string(entry, "CBR");

	return 0;
}

int rk_video_set_RC_mode(int stream_id, const char *value) {
//	char entry_output_data_type[128] = {'\0'};
//	char entry_gop[128] = {'\0'};
//	char entry_mid_rate[128] = {'\0'};
//	char entry_max_rate[128] = {'\0'};
//	char entry_min_rate[128] = {'\0'};
//	char entry_rc_mode[128] = {'\0'};
//	snprintf(entry_output_data_type, 127, "video.%d:output_data_type", stream_id);
//	snprintf(entry_gop, 127, "video.%d:gop", stream_id);
//	snprintf(entry_mid_rate, 127, "video.%d:mid_rate", stream_id);
//	snprintf(entry_max_rate, 127, "video.%d:max_rate", stream_id);
//	snprintf(entry_min_rate, 127, "video.%d:min_rate", stream_id);
//	snprintf(entry_rc_mode, 127, "video.%d:rc_mode", stream_id);
//
//	VENC_CHN_ATTR_S venc_chn_attr;
//	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
//	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
//	tmp_output_data_type = rk_param_get_string(entry_output_data_type, "H.264");
//	tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
//	if (!strcmp(tmp_output_data_type, "H.264")) {
//		if (!strcmp(value, "CBR")) {
//			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
//			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
//			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
//		} else {
//			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
//			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
//			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int(entry_mid_rate, -1);
//			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = rk_param_get_int(entry_max_rate, -1);
//			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = rk_param_get_int(entry_min_rate, -1);
//		}
//	} else if (!strcmp(tmp_output_data_type, "H.265")) {
//		if (!strcmp(value, "CBR")) {
//			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
//			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
//			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
//		} else {
//			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
//			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
//			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int(entry_mid_rate, -1);
//			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = rk_param_get_int(entry_max_rate, -1);
//			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = rk_param_get_int(entry_min_rate, -1);
//		}
//	} else {
//		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
//		return -1;
//	}
//	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
//	rk_param_set_string(entry_rc_mode, value);
//	rk_video_reset_frame_rate(stream_id);

	return 0;
}

int rk_video_get_output_data_type(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
//	*value = rk_param_get_string(entry, "H.265");

	return 0;
}

int rk_video_set_output_data_type(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
//	rk_param_set_string(entry, value);
//
//	rk_video_restart();

	return 0;
}

int rk_video_get_rc_quality(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:rc_quality", stream_id);
//	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_rc_quality(int stream_id, const char *value) {
//	char entry_rc_quality[128] = {'\0'};
//	char entry_output_data_type[128] = {'\0'};
//
//	snprintf(entry_rc_quality, 127, "video.%d:rc_quality", stream_id);
//	snprintf(entry_output_data_type, 127, "video.%d:output_data_type", stream_id);
//	tmp_output_data_type = rk_param_get_string(entry_output_data_type, "H.264");
//	tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
//
//	VENC_RC_PARAM_S venc_rc_param;
//	RK_MPI_VENC_GetRcParam(stream_id, &venc_rc_param);
//	if (!strcmp(tmp_output_data_type, "H.264")) {
//		if (!strcmp(value, "highest")) {
//			venc_rc_param.stParamH264.u32MinQp = 10;
//		} else if (!strcmp(value, "higher")) {
//			venc_rc_param.stParamH264.u32MinQp = 15;
//		} else if (!strcmp(value, "high")) {
//			venc_rc_param.stParamH264.u32MinQp = 20;
//		} else if (!strcmp(value, "medium")) {
//			venc_rc_param.stParamH264.u32MinQp = 25;
//		} else if (!strcmp(value, "low")) {
//			venc_rc_param.stParamH264.u32MinQp = 30;
//		} else if (!strcmp(value, "lower")) {
//			venc_rc_param.stParamH264.u32MinQp = 35;
//		} else {
//			venc_rc_param.stParamH264.u32MinQp = 40;
//		}
//	} else if (!strcmp(tmp_output_data_type, "H.265")) {
//		if (!strcmp(value, "highest")) {
//			venc_rc_param.stParamH265.u32MinQp = 10;
//		} else if (!strcmp(value, "higher")) {
//			venc_rc_param.stParamH265.u32MinQp = 15;
//		} else if (!strcmp(value, "high")) {
//			venc_rc_param.stParamH265.u32MinQp = 20;
//		} else if (!strcmp(value, "medium")) {
//			venc_rc_param.stParamH265.u32MinQp = 25;
//		} else if (!strcmp(value, "low")) {
//			venc_rc_param.stParamH265.u32MinQp = 30;
//		} else if (!strcmp(value, "lower")) {
//			venc_rc_param.stParamH265.u32MinQp = 35;
//		} else {
//			venc_rc_param.stParamH265.u32MinQp = 40;
//		}
//	} else {
//		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
//		return -1;
//	}
//	RK_MPI_VENC_SetRcParam(stream_id, &venc_rc_param);
//	rk_param_set_string(entry_rc_quality, value);

	return 0;
}

int rk_video_get_smart(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:smart", stream_id);
//	*value = rk_param_get_string(entry, "close");

	return 0;
}

int rk_video_set_smart(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:smart", stream_id);
//	rk_param_set_string(entry, value);
//	rk_video_restart();

	return 0;
}

int rk_video_get_gop_mode(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:gop_mode", stream_id);
//	*value = rk_param_get_string(entry, "normalP");

	return 0;
}

int rk_video_set_gop_mode(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:gop_mode", stream_id);
//	rk_param_set_string(entry, value);
//	rk_video_restart();

	return 0;
}

int rk_video_get_stream_type(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:stream_type", stream_id);
//	*value = rk_param_get_string(entry, "mainStream");

	return 0;
}

int rk_video_set_stream_type(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:stream_type", stream_id);
//	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_h264_profile(int stream_id, const char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
//	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_h264_profile(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
//	rk_param_set_string(entry, value);
//	rk_video_restart();

	return 0;
}

int rk_video_get_resolution(int stream_id, char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:width", stream_id);
//	int width = rk_param_get_int(entry, 0);
//	snprintf(entry, 127, "video.%d:height", stream_id);
//	int height = rk_param_get_int(entry, 0);
//	sprintf(*value, "%d*%d", width, height);

	return 0;
}

int rk_video_get_resolution_v10(int stream_id, int *w,int *h)
{
	if (!w || !h) 
    {
		LOG_ERROR("rk_video_get_resolution_v10 error,pointer is null\n");
		return -1;
	}
	if (stream_id < 0 || stream_id > 1)
	{
		LOG_ERROR("rk_video_get_resolution_v10 stream_id error!\n");
		return -1;
	}

	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:width", stream_id);
	snprintf(entry, 127, "video.%d:height", stream_id);
	
	*w = rk_param_get_int(entry, 1920);
	*h = rk_param_get_int(entry, 1080);

	return 0;
}

int rk_video_set_resolution(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	int width, height, ret;
//
//	sscanf(value, "%d*%d", &width, &height);
//	LOG_INFO("value is %s, width is %d, height is %d\n", value, width, height);
//
//	// unbind
//	vi_chn.enModId = RK_ID_VI;
//	vi_chn.s32DevId = 0;
//	vi_chn.s32ChnId = stream_id;
//	venc_chn.enModId = RK_ID_VENC;
//	venc_chn.s32DevId = 0;
//	venc_chn.s32ChnId = stream_id;
//	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
//	if (ret)
//		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
//	else
//		LOG_DEBUG("Unbind VI and VENC success\n");
//
//	snprintf(entry, 127, "video.%d:width", stream_id);
//	rk_param_set_int(entry, width);
//	snprintf(entry, 127, "video.%d:height", stream_id);
//	rk_param_set_int(entry, height);
//
//	VENC_CHN_ATTR_S venc_chn_attr;
//	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
//	venc_chn_attr.stVencAttr.u32PicWidth = width;
//	venc_chn_attr.stVencAttr.u32PicHeight = height;
//	venc_chn_attr.stVencAttr.u32VirWidth = width;
//	venc_chn_attr.stVencAttr.u32VirHeight = height;
//	ret = RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
//	if (ret)
//		LOG_ERROR("RK_MPI_VENC_SetChnAttr error! ret=%#x\n", ret);
//	VI_CHN_ATTR_S vi_chn_attr;
//	RK_MPI_VI_GetChnAttr(0, stream_id, &vi_chn_attr);
//	vi_chn_attr.stSize.u32Width = width;
//	vi_chn_attr.stSize.u32Height = height;
//	ret = RK_MPI_VI_SetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
//	if (ret)
//		LOG_ERROR("RK_MPI_VI_SetChnAttr error! ret=%#x\n", ret);
//
//	if (stream_id == DRAW_NN_VENC_CHN_ID && enable_npu)
//		rkipc_osd_draw_nn_change();
//	rk_osd_privacy_mask_restart();
//	rk_roi_set_all(); // update roi info, and osd cover attach vi, no update required
//	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
//	if (ret)
//		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);

	return 0;
}

int rk_video_get_frame_rate(int stream_id, char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
//	int den = rk_param_get_int(entry, -1);
//	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
//	int num = rk_param_get_int(entry, -1);
//	if (den == 1)
//		sprintf(*value, "%d", num);
//	else
//		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int den, num, sensor_fps;
	VI_CHN_ATTR_S vi_chn_attr;
	VENC_CHN_ATTR_S venc_chn_attr;

	if (strchr(value, '/') == NULL) {
		den = 1;
		sscanf(value, "%d", &num);
	} else {
		sscanf(value, "%d/%d", &num, &den);
	}
	LOG_INFO("num is %d, den is %d\n", num, den);
	sensor_fps = rk_param_get_int("isp.0.adjustment:fps", 30);
//	sensor_fps = 30;//shang

	RK_MPI_VI_GetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	LOG_INFO("old VI framerate is [%d:%d]\n", vi_chn_attr.stFrameRate.s32SrcFrameRate,
	         vi_chn_attr.stFrameRate.s32DstFrameRate);
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	//<shang>
	if (0 == stream_id && g_video_chn_0_enc_param_inited)
		tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
	if (1 == stream_id && g_video_chn_1_enc_param_inited)
		tmp_output_data_type = (0 == g_video_chn_1_enc_type) ? "H.264" : "H.265";
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");

	// if the frame rate is not an integer, use VENC frame rate control,
	// otherwise use VI frame rate control
	if (den != 1) {
		vi_chn_attr.stFrameRate.s32SrcFrameRate = sensor_fps;
		vi_chn_attr.stFrameRate.s32DstFrameRate = sensor_fps;

		if (!strcmp(tmp_output_data_type, "H.264")) {
			venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
			if (!strcmp(tmp_rc_mode, "CBR")) {
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = sensor_fps;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = num;
			} else {
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = sensor_fps;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = num;
			}
		} else if (!strcmp(tmp_output_data_type, "H.265")) {
			venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
			if (!strcmp(tmp_rc_mode, "CBR")) {
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = sensor_fps;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = num;
			} else {
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = sensor_fps;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = num;
			}
		} else {
			LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
			return -1;
		}
	} else {
		vi_chn_attr.stFrameRate.s32SrcFrameRate = sensor_fps;
		vi_chn_attr.stFrameRate.s32DstFrameRate = num; // den == 1

		if (!strcmp(tmp_output_data_type, "H.264")) {
			venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
			if (!strcmp(tmp_rc_mode, "CBR")) {
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = num;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = num;
			} else {
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = num;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = num;
			}
		} else if (!strcmp(tmp_output_data_type, "H.265")) {
			venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
			if (!strcmp(tmp_rc_mode, "CBR")) {
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = num;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = num;
			} else {
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = num;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = den;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = num;
			}
		} else {
			LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
			return -1;
		}
	}
	LOG_INFO("new VI framerate is [%d:%d]\n", vi_chn_attr.stFrameRate.s32SrcFrameRate,
	         vi_chn_attr.stFrameRate.s32DstFrameRate);
	RK_MPI_VI_SetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);

	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
	rk_param_set_int(entry, den);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
	rk_param_set_int(entry, num);

	return 0;
}

int rk_video_reset_frame_rate(int stream_id) {
	int ret = 0;
	char *value = malloc(20);
	ret |= rk_video_get_frame_rate(stream_id, &value);
	ret |= rk_video_set_frame_rate(stream_id, value);
	free(value);

	return 0;
}

int rk_video_get_frame_rate_in(int stream_id, char **value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
//	int den = rk_param_get_int(entry, -1);
//	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
//	int num = rk_param_get_int(entry, -1);
//	if (den == 1)
//		sprintf(*value, "%d", num);
//	else
//		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate_in(int stream_id, const char *value) {
//	char entry[128] = {'\0'};
//	int den, num;
//	if (strchr(value, '/') == NULL) {
//		den = 1;
//		sscanf(value, "%d", &num);
//	} else {
//		sscanf(value, "%d/%d", &num, &den);
//	}
//	LOG_INFO("num is %d, den is %d\n", num, den);
//	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
//	rk_param_set_int(entry, den);
//	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
//	rk_param_set_int(entry, num);
//	rk_video_restart();

	return 0;
}

int rk_video_get_rotation(int *value) {
//	char entry[128] = {'\0'};
//	snprintf(entry, 127, "video.source:rotation");
//	*value = rk_param_get_int(entry, 0);

	return 0;
}

extern int g_rotation;
int rk_video_set_rotation(int value) {
#if 1
	int ret = 0;
	int rotation = 0;

	if (value == 0) {
		rotation = ROTATION_0;
		g_rotation = 0;
	} else if (value == 90) {
		rotation = ROTATION_90;
		g_rotation = 90;
	} else if (value == 180) {
		rotation = ROTATION_180;
		g_rotation = 180;
	} else if (value == 270) {
		rotation = ROTATION_270;
		g_rotation = 270;
	}
	printf("g_rotation=%d\n",g_rotation);
	ret = RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation VIDEO_PIPE_0 error! ret=%#x\n", ret);
	ret = RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation VIDEO_PIPE_1 error! ret=%#x\n", ret);
	ret = RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation JPEG_VENC_CHN error! ret=%#x\n", ret);

	return 0;
#else
	int ret = 0;
	int rotation = 0;
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.source:rotation");
	rk_param_set_int(entry, value);
	if (value == 0) {
		rotation = ROTATION_0;
	} else if (value == 90) {
		rotation = ROTATION_90;
	} else if (value == 180) {
		rotation = ROTATION_180;
	} else if (value == 270) {
		rotation = ROTATION_270;
	}
	ret = RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation VIDEO_PIPE_0 error! ret=%#x\n", ret);
	ret = RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation VIDEO_PIPE_1 error! ret=%#x\n", ret);
	ret = RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, rotation);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetChnRotation JPEG_VENC_CHN error! ret=%#x\n", ret);
	rk_roi_set_all(); // update roi info
	// update osd info, cover currently attaches to VI
	rk_osd_privacy_mask_restart();
	if (enable_npu)
		rkipc_osd_draw_nn_change();

	return 0;
#endif
}

int rk_video_get_smartp_viridrlen(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:smartp_viridrlen", stream_id);
	*value = rk_param_get_int(entry, 25);

	return 0;
}

int rk_video_set_smartp_viridrlen(int stream_id, int value) {
	char entry[128] = {'\0'};

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	venc_chn_attr.stGopAttr.s32VirIdrLen = value;
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);

	snprintf(entry, 127, "video.%d:smartp_viridrlen", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_md_switch(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:md");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_md_switch(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:md");
	rk_param_set_int(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_md_sensebility(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:md_sensibility");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_md_sensebility(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:md_sensibility");
	rk_param_set_int(entry, value);
	rk_video_restart();
}

int rk_video_get_od_switch(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:od");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_od_switch(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "ivs:od");
	rk_param_set_int(entry, value);
	rk_video_restart();
}

int rkipc_osd_cover_create(int id, osd_data_s *osd_data) {
	LOG_DEBUG("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE coverHandle = id;
	RGN_ATTR_S stCoverAttr;
	MPP_CHN_S stCoverChn;
	RGN_CHN_ATTR_S stCoverChnAttr;
	int rotation = rk_param_get_int("video.source:rotation", 0);
	int video_0_width = rk_param_get_int("video.0:width", -1);
	int video_0_height = rk_param_get_int("video.0:height", -1);
	int video_0_max_width = rk_param_get_int("video.0:max_width", -1);
	int video_0_max_height = rk_param_get_int("video.0:max_height", -1);
	double video_0_w_h_rate = 1.0;

	// since the coordinates stored in the OSD module are of actual resolution,
	// 1106 needs to be converted back to the maximum resolution
	osd_data->origin_x = osd_data->origin_x * video_0_max_width / video_0_width;
	osd_data->origin_y = osd_data->origin_y * video_0_max_height / video_0_height;
	osd_data->width = osd_data->width * video_0_max_width / video_0_width;
	osd_data->height = osd_data->height * video_0_max_height / video_0_height;

	memset(&stCoverAttr, 0, sizeof(stCoverAttr));
	memset(&stCoverChnAttr, 0, sizeof(stCoverChnAttr));
	// create cover regions
	stCoverAttr.enType = COVER_RGN;
	ret = RK_MPI_RGN_Create(coverHandle, &stCoverAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", coverHandle, ret);
		RK_MPI_RGN_Destroy(coverHandle);
		return RK_FAILURE;
	}
	LOG_DEBUG("The handle: %d, create success\n", coverHandle);

	// when cover is attached to VI,
	// coordinate conversion of three angles shall be considered when rotating VENC
	video_0_w_h_rate = (double)video_0_max_width / (double)video_0_max_height;
	if (rotation == 90) {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X =
		    (double)osd_data->origin_y * video_0_w_h_rate;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y =
		    (video_0_max_height -
		     ((double)(osd_data->width + osd_data->origin_x) / video_0_w_h_rate));
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width =
		    (double)osd_data->height * video_0_w_h_rate;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height =
		    (double)osd_data->width / video_0_w_h_rate;
	} else if (rotation == 270) {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X =
		    (video_0_max_width -
		     ((double)(osd_data->height + osd_data->origin_y) * video_0_w_h_rate));
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y =
		    (double)osd_data->origin_x / video_0_w_h_rate;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width =
		    (double)osd_data->height * video_0_w_h_rate;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height =
		    (double)osd_data->width / video_0_w_h_rate;
	} else if (rotation == 180) {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X =
		    video_0_max_width - osd_data->width - osd_data->origin_x;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y =
		    video_0_max_height - osd_data->height - osd_data->origin_y;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = osd_data->width;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = osd_data->height;
	} else {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = osd_data->origin_x;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = osd_data->origin_y;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = osd_data->width;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = osd_data->height;
	}
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X =
	    UPALIGNTO16(stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X);
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y =
	    UPALIGNTO16(stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y);
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width =
	    UPALIGNTO16(stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width);
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height =
	    UPALIGNTO16(stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height);
	// because the rotation is done in the VENC,
	// and the cover and VI resolution are both before the rotation,
	// there is no need to judge the rotation here
	while (stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X +
	           stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width >
	       video_0_max_width) {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width -= 16;
	}
	while (stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y +
	           stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height >
	       video_0_max_height) {
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height -= 16;
	}

	// display cover regions to vi
	stCoverChn.enModId = RK_ID_VI;
	stCoverChn.s32DevId = 0;
	stCoverChn.s32ChnId = VI_MAX_CHN_NUM;
	stCoverChnAttr.bShow = osd_data->enable;
	stCoverChnAttr.enType = COVER_RGN;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0xffffffff;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = id;
	LOG_DEBUG("cover region to chn success\n");
	ret = RK_MPI_RGN_AttachToChn(coverHandle, &stCoverChn, &stCoverChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("vi pipe RK_MPI_RGN_AttachToChn (%d) failed with %#x\n", coverHandle, ret);
		return RK_FAILURE;
	}
	ret = RK_MPI_RGN_SetDisplayAttr(coverHandle, &stCoverChn, &stCoverChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("vi pipe RK_MPI_RGN_SetDisplayAttr failed with %#x\n", ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to vi 0 success\n");

	return ret;
}

int rkipc_osd_cover_destroy(int id) {
	LOG_DEBUG("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;

	stMppChn.enModId = RK_ID_VI;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = VI_MAX_CHN_NUM;
	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (RK_SUCCESS != ret)
		LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to vi pipe failed with %#x\n", RgnHandle, ret);

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_DEBUG("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_bmp_create(int id, osd_data_s *osd_data) {
	LOG_DEBUG("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.u32CanvasNum = 2;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = osd_data->width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = osd_data->height;
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_DEBUG("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = osd_data->enable;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = osd_data->origin_x;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = osd_data->origin_y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;

	// stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bEnable = true;
	// stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bForceIntra = false;
	// stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = false;
	// stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp = -3;
	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");
	}
	if (enable_venc_1) {
		stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		    UPALIGNTO16(osd_data->origin_x * rk_param_get_int("video.1:width", 1) /
		                rk_param_get_int("video.0:width", 1));
		stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		    UPALIGNTO16(osd_data->origin_y * rk_param_get_int("video.1:height", 1) /
		                rk_param_get_int("video.0:height", 1));
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc1 success\n");
	}
	if (enable_jpeg) {
		stMppChn.s32ChnId = JPEG_VENC_CHN;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to jpeg success\n");
	}

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_bmp_destroy(int id) {
	LOG_DEBUG("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
	}
	if (enable_venc_1) {
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
	}
	if (enable_jpeg) {
		stMppChn.s32ChnId = JPEG_VENC_CHN;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
	}

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_DEBUG("Destory handle:%d success\n", RgnHandle);

	return ret;
}

static int s_bOsdShow = 1;
int rkipc_osd_bmp_change(int id, osd_data_s *osd_data) {
	// LOG_DEBUG("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	BITMAP_S stBitmap;

	{
		static int bCurrOsdShow = -1;
		if (bCurrOsdShow != s_bOsdShow)
		{
			bCurrOsdShow = s_bOsdShow;
			
			MPP_CHN_S stMppChn;
			RGN_CHN_ATTR_S stChnAttr;
			stMppChn.enModId = RK_ID_VENC;
			stMppChn.s32DevId = 0;
			if (enable_venc_0) {
				stMppChn.s32ChnId = 0;
				ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
				if (RK_SUCCESS != ret)
					LOG_DEBUG("RK_MPI_RGN_GetDisplayAttr (%d) to venc0 failed with %#x\n", RgnHandle, ret);
				else
				{
					stChnAttr.bShow = bCurrOsdShow ? RK_TRUE : RK_FALSE;
					ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
					if (RK_SUCCESS != ret)
						LOG_DEBUG("RK_MPI_RGN_SetDisplayAttr (%d) to venc0 failed with %#x\n", RgnHandle, ret);
				}
			}
			if (enable_venc_1) {
				stMppChn.s32ChnId = 1;
				ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
				if (RK_SUCCESS != ret)
					LOG_DEBUG("RK_MPI_RGN_GetDisplayAttr (%d) to venc1 failed with %#x\n", RgnHandle, ret);
				else
				{
					stChnAttr.bShow = bCurrOsdShow ? RK_TRUE : RK_FALSE;
					ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
					if (RK_SUCCESS != ret)
						LOG_DEBUG("RK_MPI_RGN_SetDisplayAttr (%d) to venc1 failed with %#x\n", RgnHandle, ret);
				}
			}
			if (enable_jpeg) {
				stMppChn.s32ChnId = JPEG_VENC_CHN;
				ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
				if (RK_SUCCESS != ret)
					LOG_DEBUG("RK_MPI_RGN_GetDisplayAttr (%d) to jpeg failed with %#x\n", RgnHandle, ret);
				else
				{
					stChnAttr.bShow = bCurrOsdShow ? RK_TRUE : RK_FALSE;
					ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, &stMppChn, &stChnAttr);
					if (RK_SUCCESS != ret)
						LOG_DEBUG("RK_MPI_RGN_SetDisplayAttr (%d) to jpeg failed with %#x\n", RgnHandle, ret);
				}
			}
		}
	}

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_init() {
	rk_osd_cover_create_callback_register(rkipc_osd_cover_create);
	rk_osd_cover_destroy_callback_register(rkipc_osd_cover_destroy);
	rk_osd_bmp_create_callback_register(rkipc_osd_bmp_create);
	rk_osd_bmp_destroy_callback_register(rkipc_osd_bmp_destroy);
	rk_osd_bmp_change_callback_register(rkipc_osd_bmp_change);
	rk_osd_init();

	return 0;
}

int rkipc_osd_deinit() {
	rk_osd_deinit();
	rk_osd_cover_create_callback_register(NULL);
	rk_osd_cover_destroy_callback_register(NULL);
	rk_osd_bmp_create_callback_register(NULL);
	rk_osd_bmp_destroy_callback_register(NULL);
	rk_osd_bmp_change_callback_register(NULL);

	return 0;
}

int rkipc_osd_show(int bShow)
{
	LOG_DEBUG("%s\n", __func__);
	s_bOsdShow = bShow;
}

// jpeg
int rk_video_get_enable_cycle_snapshot(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:enable_cycle_snapshot");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_enable_cycle_snapshot(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:enable_cycle_snapshot");
	rk_param_set_int(entry, value);
	if (value && !cycle_snapshot_flag) {
		cycle_snapshot_flag = 1;
		pthread_create(&cycle_snapshot_thread_id, NULL, rkipc_cycle_snapshot, NULL);
	} else if (!value && cycle_snapshot_flag) {
		send_jpeg_cnt = 0;
		get_jpeg_cnt = 0;
		cycle_snapshot_flag = 0;
		pthread_join(cycle_snapshot_thread_id, NULL);
	}

	return 0;
}

int rk_video_get_image_quality(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:jpeg_qfactor");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_image_quality(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:jpeg_qfactor");
	rk_param_set_int(entry, value);

	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = value;
	RK_MPI_VENC_SetJpegParam(JPEG_VENC_CHN, &stJpegParam);

	return 0;
}

int rk_video_get_snapshot_interval_ms(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:snapshot_interval_ms");
	*value = rk_param_get_int(entry, 0);

	return 0;
}

int rk_video_set_snapshot_interval_ms(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:snapshot_interval_ms");
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_jpeg_resolution(char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:width");
	int width = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.jpeg:height");
	int height = rk_param_get_int(entry, 0);
	sprintf(*value, "%d*%d", width, height);

	return 0;
}

int rk_video_set_jpeg_resolution(const char *value) {
	int width, height, ret;
	char entry[128] = {'\0'};
	sscanf(value, "%d*%d", &width, &height);
	snprintf(entry, 127, "video.jpeg:width");
	rk_param_set_int(entry, width);
	snprintf(entry, 127, "video.jpeg:height");
	rk_param_set_int(entry, height);

	VENC_CHN_ATTR_S venc_chn_attr;
	RK_MPI_VENC_GetChnAttr(JPEG_VENC_CHN, &venc_chn_attr);
	venc_chn_attr.stVencAttr.u32PicWidth = width;
	venc_chn_attr.stVencAttr.u32PicHeight = height;
	venc_chn_attr.stVencAttr.u32VirWidth = width;
	venc_chn_attr.stVencAttr.u32VirHeight = height;
	ret = RK_MPI_VENC_SetChnAttr(JPEG_VENC_CHN, &venc_chn_attr);
	if (ret)
		LOG_ERROR("JPEG RK_MPI_VENC_SetChnAttr error! ret=%#x\n", ret);

	return 0;
}

int rk_take_photo() {
	LOG_DEBUG("start\n");
	if (send_jpeg_cnt || get_jpeg_cnt) {
		LOG_WARN("the last photo was not completed\n");
		return -1;
	}
	if (rkipc_storage_dev_mount_status_get() != DISK_MOUNTED) {
		LOG_WARN("dev not mount\n");
		return -1;
	}
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	send_jpeg_cnt++;
	get_jpeg_cnt++;

	return 0;
}
int rk_take_photo_v10(char *buffer, int size, int timeout_s)
{
	LOG_DEBUG("start\n");
	if (get_jpeg_cnt) 
	{
		LOG_WARN("the last photo was not completed\n");
		return -1;
	}
	jpeg_size = size;
	jpeg_buffer = buffer;
	if (!jpeg_buffer)
	{
		LOG_WARN("buffer is null\n");
		return -1;
	}
	// VENC_RECV_PIC_PARAM_S stRecvParam;
	// memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	// stRecvParam.s32RecvPicNum = 1;
	// RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	get_jpeg_cnt++;

	int timeout = timeout_s*1000;

	while(timeout > 0)
	{
		timeout -= 100;
		usleep(100*1000);
		if (!get_jpeg_cnt)
		{
			return jpeg_size;
		}
	}
	
	return 0;
}

int rk_roi_set(roi_data_s *roi_data) {
	int ret = 0;
	int venc_chn_num = 0;
	int rotation, video_width, video_height;
	int origin_x, origin_y;
	VENC_ROI_ATTR_S pstRoiAttr;
	char entry[128] = {'\0'};
	pstRoiAttr.u32Index = roi_data->id;
	pstRoiAttr.bEnable = roi_data->enabled;
	pstRoiAttr.bAbsQp = RK_FALSE;
	pstRoiAttr.bIntra = RK_FALSE;
	pstRoiAttr.stRect.s32X = roi_data->position_x;
	pstRoiAttr.stRect.s32Y = roi_data->position_y;
	pstRoiAttr.stRect.u32Width = roi_data->width;
	pstRoiAttr.stRect.u32Height = roi_data->height;
	switch (roi_data->quality_level) {
	case 6:
		pstRoiAttr.s32Qp = -16;
		break;
	case 5:
		pstRoiAttr.s32Qp = -14;
		break;
	case 4:
		pstRoiAttr.s32Qp = -12;
		break;
	case 3:
		pstRoiAttr.s32Qp = -10;
		break;
	case 2:
		pstRoiAttr.s32Qp = -8;
		break;
	case 1:
	default:
		pstRoiAttr.s32Qp = -6;
	}

	if (!strcmp(roi_data->stream_type, "mainStream") &&
	    rk_param_get_int("video.source:enable_venc_0", 0)) {
		venc_chn_num = 0;
	} else if (!strcmp(roi_data->stream_type, "subStream") &&
	           rk_param_get_int("video.source:enable_venc_1", 0)) {
		venc_chn_num = 1;
	} else if (!strcmp(roi_data->stream_type, "thirdStream") &&
	           rk_param_get_int("video.source:enable_venc_2", 0)) {
		venc_chn_num = 2;
	} else {
		LOG_DEBUG("%s is not exit\n", roi_data->stream_type);
		return -1;
	}

	if (pstRoiAttr.stRect.u32Height != 0 && pstRoiAttr.stRect.u32Width != 0 && roi_data->enabled) {
		snprintf(entry, 127, "video.%d:width", venc_chn_num);
		video_width = rk_param_get_int(entry, 0);
		snprintf(entry, 127, "video.%d:height", venc_chn_num);
		video_height = rk_param_get_int(entry, 0);
		rotation = rk_param_get_int("video.source:rotation", 0);
		origin_x = pstRoiAttr.stRect.s32X;
		origin_y = pstRoiAttr.stRect.s32Y;
		if (video_height < (origin_y + pstRoiAttr.stRect.u32Height))
			LOG_ERROR("illegal params! video height(%d) < y(%d) + h(%d)", video_height, origin_y,
			          pstRoiAttr.stRect.u32Height);
		if (video_width < (origin_x + pstRoiAttr.stRect.u32Width))
			LOG_ERROR("illegal params! video width(%d) < x(%d) + w(%d)", video_width, origin_x,
			          pstRoiAttr.stRect.u32Width);
		switch (rotation) {
		case 90:
			pstRoiAttr.stRect.s32X = video_height - origin_y - pstRoiAttr.stRect.u32Height;
			pstRoiAttr.stRect.s32Y = origin_x;
			RKIPC_SWAP(pstRoiAttr.stRect.u32Width, pstRoiAttr.stRect.u32Height);
			break;
		case 270:
			pstRoiAttr.stRect.s32X = origin_y;
			pstRoiAttr.stRect.s32Y = video_width - origin_x - pstRoiAttr.stRect.u32Width;
			RKIPC_SWAP(pstRoiAttr.stRect.u32Width, pstRoiAttr.stRect.u32Height);
			break;
		case 180:
			pstRoiAttr.stRect.s32X = video_width - origin_x - pstRoiAttr.stRect.u32Width;
			pstRoiAttr.stRect.s32Y = video_height - origin_y - pstRoiAttr.stRect.u32Height;
			break;
		default:
			break;
		}
		LOG_INFO("id %d, rotation %d, from [x(%d),y(%d),w(%d),h(%d)] "
		         "to [x(%d),y(%d),w(%d),h(%d)]\n",
		         roi_data->id, rotation, roi_data->position_x, roi_data->position_y,
		         roi_data->width, roi_data->height, pstRoiAttr.stRect.s32X, pstRoiAttr.stRect.s32Y,
		         pstRoiAttr.stRect.u32Width, pstRoiAttr.stRect.u32Height);
	}

	ret = RK_MPI_VENC_SetRoiAttr(venc_chn_num, &pstRoiAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_VENC_SetRoiAttr to venc %d failed with %#x\n", venc_chn_num, ret);
		return RK_FAILURE;
	}

	return ret;
}

// int rk_region_clip_set(int venc_chn, region_clip_data_s *region_clip_data) {
// 	int ret = 0;
// 	VENC_CHN_PARAM_S stParam;

// 	RK_MPI_VENC_GetChnParam(venc_chn, &stParam);
// 	if (RK_SUCCESS != ret) {
// 		LOG_ERROR("RK_MPI_VENC_GetChnParam to venc failed with %#x\n", ret);
// 		return RK_FAILURE;
// 	}
// 	LOG_DEBUG("RK_MPI_VENC_GetChnParam to venc success\n");
// 	LOG_DEBUG("venc_chn is %d\n", venc_chn);
// 	if (region_clip_data->enabled)
// 		stParam.stCropCfg.enCropType = VENC_CROP_ONLY;
// 	else
// 		stParam.stCropCfg.enCropType = VENC_CROP_NONE;
// 	stParam.stCropCfg.stCropRect.s32X = region_clip_data->position_x;
// 	stParam.stCropCfg.stCropRect.s32Y = region_clip_data->position_y;
// 	stParam.stCropCfg.stCropRect.u32Width = region_clip_data->width;
// 	stParam.stCropCfg.stCropRect.u32Height = region_clip_data->height;
// 	LOG_DEBUG("xywh is %d,%d,%d,%d\n", stParam.stCropCfg.stCropRect.s32X,
// stParam.stCropCfg.stCropRect.s32Y, 				stParam.stCropCfg.stCropRect.u32Width,
// stParam.stCropCfg.stCropRect.u32Height); 	ret = RK_MPI_VENC_SetChnParam(venc_chn, &stParam);
// if
// (RK_SUCCESS != ret) { 		LOG_ERROR("RK_MPI_VENC_SetChnParam to venc failed with %#x\n", ret);
// return RK_FAILURE;
// 	}
// 	LOG_DEBUG("RK_MPI_VENC_SetChnParam to venc success\n");

// 	return ret;
// }

int rk_video_init() {
	LOG_DEBUG("begin\n");
	int ret = 0;
	enable_ivs = rk_param_get_int("video.source:enable_ivs", 1);
	enable_jpeg = rk_param_get_int("video.source:enable_jpeg", 1);
	enable_venc_0 = rk_param_get_int("video.source:enable_venc_0", 1);
	enable_venc_1 = rk_param_get_int("video.source:enable_venc_1", 1);
	enable_rtsp = rk_param_get_int("video.source:enable_rtsp", 0);
	enable_rtmp = rk_param_get_int("video.source:enable_rtmp", 0);
	LOG_INFO("enable_jpeg is %d, enable_venc_0 is %d, enable_venc_1 is %d, enable_rtsp is %d, "
	         "enable_rtmp is %d\n",
	         enable_jpeg, enable_venc_0, enable_venc_1, enable_rtsp, enable_rtmp);

	g_vi_chn_id = rk_param_get_int("video.source:vi_chn_id", 0);
	g_enable_vo = rk_param_get_int("video.source:enable_vo", 0);
	g_vo_dev_id = rk_param_get_int("video.source:vo_dev_id", 3);
	enable_npu = rk_param_get_int("video.source:enable_npu", 0);
	enable_osd = rk_param_get_int("osd.common:enable_osd", 0);
	LOG_DEBUG("g_vi_chn_id is %d, g_enable_vo is %d, g_vo_dev_id is %d, enable_npu is %d, "
	          "enable_osd is %d\n",
	          g_vi_chn_id, g_enable_vo, g_vo_dev_id, enable_npu, enable_osd);
	g_video_run_ = 1;
	#if 0
	ret |= rkipc_vi_dev_init();
	if (enable_rtsp)
		ret |= rkipc_rtsp_init(RTSP_URL_0, RTSP_URL_1, NULL);
	if (enable_rtmp)
		ret |= rkipc_rtmp_init();
	if (enable_venc_0)
		ret |= rkipc_pipe_0_init();
	if (enable_venc_1)
		ret |= rkipc_pipe_1_init();
	if (enable_jpeg)
		ret |= rkipc_pipe_jpeg_init();
	// if (g_enable_vo)
	// 	ret |= rkipc_pipe_vpss_vo_init();
//	rk_roi_set_callback_register(rk_roi_set);
//	ret |= rk_roi_set_all();
	// rk_region_clip_set_callback_register(rk_region_clip_set);
	// rk_region_clip_set_all();
	if (enable_npu || enable_ivs) {
		ret |= rkipc_pipe_2_init();
	}
	// The osd dma buffer must be placed in the last application,
	// otherwise, when the font size is switched, holes may be caused
	if (enable_osd)
		ret |= rkipc_osd_init();
	LOG_DEBUG("over\n");

	return ret;
	#else
	ret = rkipc_vi_dev_init();
	if (ret)
	{
		LOG_ERROR("rkipc_vi_dev_init failed. ret: %d\n", ret);
		return -1;
	}
	for (int i = 0; i < sizeof(s_vi_chan_param)/sizeof(s_vi_chan_param[0]); i++)
	{
		ret = rkipc_vi_chan_init(i, &s_vi_chan_param[i]);
		if (ret)
		{
			LOG_ERROR("rkipc_vi_chan_init chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
	}
	for (int i = 0; i < sizeof(s_stream_venc_chan_param)/sizeof(s_stream_venc_chan_param[0]); i++)
	{
		ret = rkipc_venc_chan_init(i, &s_stream_venc_chan_param[i]);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_init chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
		ret = rkipc_venc_chan_start(i);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_start chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
	}

	for (int i = 0; i < sizeof(s_jpeg_venc_chan_param)/sizeof(s_jpeg_venc_chan_param[0]); i++)
	{
		ret = rkipc_venc_chan_jpeg_init(&s_jpeg_venc_chan_param[i]);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_jpeg_init chan[%d] failed. ret: %d\n", s_jpeg_venc_chan_param[i].channel, ret);
			return -1;
		}
	}

	//osd
	gb_rkipc_osd_init();

	
	return 0;
	#endif
}

int rk_video_deinit() {
	LOG_DEBUG("%s\n", __func__);
	g_video_run_ = 0;
	int ret = 0;
	#if 0
	if (enable_npu || enable_ivs)
		ret |= rkipc_pipe_2_deinit();
	// rk_region_clip_set_callback_register(NULL);
	rk_roi_set_callback_register(NULL);
	if (enable_osd)
		ret |= rkipc_osd_deinit();
	// if (g_enable_vo)
	// 	ret |= rkipc_pipe_vi_vo_deinit();
	if (get_venc_0_running)
	{
		get_venc_0_running = 0;
		pthread_join(venc_thread_0, NULL);
	}
	if (enable_venc_0) {
		ret |= rkipc_pipe_0_deinit();
	}
	if (get_venc_1_running)
	{
		get_venc_1_running = 0;
		pthread_join(venc_thread_1, NULL);
	}
	if (enable_venc_1) {
		ret |= rkipc_pipe_1_deinit();
	}
	if (get_venc_2_running)
	{
		get_venc_2_running = 0;
		pthread_join(jpeg_venc_thread_id, NULL);
	}
	if (enable_jpeg) {
		ret |= rkipc_pipe_jpeg_deinit();
	}
	ret |= rkipc_vi_dev_deinit();
	if (enable_rtmp)
		ret |= rkipc_rtmp_deinit();
	if (enable_rtsp)
		ret |= rkipc_rtsp_deinit();

	return ret;
	#else

	//tmp
	ret |= rkipc_osd_deinit();

	for (int i = 0; i < sizeof(s_jpeg_venc_chan_param)/sizeof(s_jpeg_venc_chan_param[0]); i++)
	{
		ret = rkipc_venc_chan_jpeg_deinit(&s_jpeg_venc_chan_param[i]);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_jpeg_deinit chan[%d] failed. ret: %d\n", s_jpeg_venc_chan_param[i].channel, ret);
			return -1;
		}
	}
	
	for (int i = 0; i < sizeof(s_stream_venc_chan_param)/sizeof(s_stream_venc_chan_param[0]); i++)
	{
		ret = rkipc_venc_chan_stop(i);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_stop chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
		ret = rkipc_venc_chan_deinit(i);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_deinit chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
	}
	for (int i = 0; i < sizeof(s_vi_chan_param)/sizeof(s_vi_chan_param[0]); i++)
	{
		ret = rkipc_vi_chan_deinit(i);
		if (ret)
		{
			LOG_ERROR("rkipc_vi_chan_deinit chan[%d] failed. ret: %d\n", i, ret);
			return -1;
		}
	}
	ret = rkipc_vi_dev_deinit();
	if (ret)
	{
		LOG_ERROR("rkipc_vi_dev_deinit failed. ret: %d\n", ret);
		return -1;
	}
	return 0;
	#endif
}

extern char *rkipc_iq_file_path_;
int rk_video_restart() {
	int ret;
	ret = rk_storage_deinit();
	ret |= rk_video_deinit();
	if (rk_param_get_int("video.source:enable_aiq", 1))
		ret |= rk_isp_deinit(0);
	if (rk_param_get_int("video.source:enable_aiq", 1)) {
		ret |= rk_isp_init(0, rkipc_iq_file_path_);
		if (rk_param_get_int("isp:init_form_ini", 1))
			ret |= rk_isp_set_from_ini(0);
	}
	ret |= rk_video_init();
	ret |= rk_storage_init();

	return ret;
}

// -----------------------------------------------------------------------
// 新增代码

int my_video_0_restart()
{
	rkipc_osd_draw_nn_deinit();
	if (enable_osd)
		rkipc_osd_deinit();
	
	if (get_venc_0_running)
	{
		get_venc_0_running = 0;
		pthread_join(venc_thread_0, NULL);
	}
	if (enable_venc_0) {
		rkipc_pipe_0_deinit();
		rkipc_pipe_0_init();
	}
	
	if (enable_osd)
		rkipc_osd_init();
	rkipc_osd_draw_nn_init();
	return 0;
}
int my_video_1_restart()
{
	int ret;

	rkipc_osd_draw_nn_deinit();
	if (enable_osd)
		rkipc_osd_deinit();
	
	// unbind jpg
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = JPEG_VENC_CHN;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Unbind VI and VENC success\n");
	
	if (get_venc_1_running)
	{
		get_venc_1_running = 0;
		pthread_join(venc_thread_1, NULL);
	}
	if (enable_venc_1) {
		rkipc_pipe_1_deinit();
		rkipc_pipe_1_init();
	}

	// bind jpg
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = VIDEO_PIPE_1;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = JPEG_VENC_CHN;
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("Bind VI and VENC success\n");

	if (enable_osd)
		rkipc_osd_init();
	rkipc_osd_draw_nn_init();
	
	return 0;
}
int my_video_set_param(int stream_id) {

	int ret;
	char entry[128] = {'\0'};
	int sensor_fps;
	VI_CHN_ATTR_S vi_chn_attr;
	VENC_CHN_ATTR_S venc_chn_attr;
		
	sensor_fps = rk_param_get_int("isp.0.adjustment:fps", 30);

	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");

	int min_rate;
	int mid_rate;
	int max_rate;
	int frame_rate;
	int gop;

	if (0 == stream_id)
	{
		tmp_output_data_type = (0 == g_video_chn_0_enc_type) ? "H.264" : "H.265";
		if (!strcmp(tmp_rc_mode, "CBR")) {
			max_rate = g_video_chn_0_bit_rate;
		} else {
			min_rate = g_video_chn_0_bit_rate / 3;
			mid_rate = g_video_chn_0_bit_rate / 3 * 2;
			max_rate = g_video_chn_0_bit_rate;
		}
		frame_rate = g_video_chn_0_frmae_rate;
		gop = g_video_chn_0_gop;
	}
	else if (1 == stream_id)
	{
		tmp_output_data_type = (0 == g_video_chn_1_enc_type) ? "H.264" : "H.265";
		if (!strcmp(tmp_rc_mode, "CBR")) {
			max_rate = g_video_chn_1_bit_rate;
		} else {
			min_rate = g_video_chn_1_bit_rate / 3;
			mid_rate = g_video_chn_1_bit_rate / 3 * 2;
			max_rate = g_video_chn_1_bit_rate;
		}
		frame_rate = g_video_chn_1_frmae_rate;
		gop = g_video_chn_1_gop;
	}
	else
	{
		LOG_ERROR("stream_id error. stream_id:%d\n", stream_id);
		return -1;
	}

	ret = RK_MPI_VI_GetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VI_GetChnAttr failed. ret:%x %d\n", ret, ret);
		return -1;
	}

	ret = RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VENC_GetChnAttr failed. ret:%x %d\n", ret, ret);
		return -1;
	}
	
	vi_chn_attr.stFrameRate.s32SrcFrameRate = sensor_fps;
	vi_chn_attr.stFrameRate.s32DstFrameRate = frame_rate; // den == 1

	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = gop;
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = max_rate;
		} else {
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = gop;
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = mid_rate;
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = max_rate;
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = min_rate;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = gop;
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = max_rate;
		} else {
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = gop;
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = frame_rate;
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = mid_rate;
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = max_rate;
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = min_rate;
		}
	}
		
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VI_SetChnAttr failed. ret:%x %d\n", ret, ret);
		return -1;
	}
	ret = RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VENC_SetChnAttr failed. ret:%x %d\n", ret, ret);
		return -1;
	}
	
	return 0;
}


int my_video_init_param_2(int stream_id, int enc_type, int bit_rate, int frmae_rate, int gop) {
	
	if (stream_id < 0 || stream_id > 1)
	{
		LOG_ERROR("stream_id error. stream_id:%d\n", stream_id);
		return -1;
	}

	s_vi_chan_param[stream_id].frmae_rate = frmae_rate;
	
	const char *new_enc_type = (0 == enc_type) ? "H.264" : "H.265";
	s_stream_venc_chan_param[stream_id].enc_type = new_enc_type;
	s_stream_venc_chan_param[stream_id].bit_rate = bit_rate;
	s_stream_venc_chan_param[stream_id].frame_rate_num = frmae_rate;
	s_stream_venc_chan_param[stream_id].gop = gop;
	return 0;
}
int my_video_set_param_2(int stream_id, int enc_type, int bit_rate, int frmae_rate, int gop) {
	
	if (stream_id < 0 || stream_id > 1)
	{
		LOG_ERROR("stream_id error. stream_id:%d\n", stream_id);
		return -1;
	}
	
	int ret;
	const char *old_enc_type = s_stream_venc_chan_param[stream_id].enc_type;
	const char *new_enc_type = (0 == enc_type) ? "H.264" : "H.265";

	s_vi_chan_param[stream_id].frmae_rate = frmae_rate;

	//先修改 VI 帧率
	VI_CHN_ATTR_S vi_chn_attr;
	ret = RK_MPI_VI_GetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VI_GetChnAttr chan[%d] failed. ret:%x %d\n", stream_id, ret, ret);
		return -1;
	}
	vi_chn_attr.stFrameRate.s32SrcFrameRate = g_sensor_fps;
	vi_chn_attr.stFrameRate.s32DstFrameRate = s_vi_chan_param[stream_id].frmae_rate; // den == 1
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, stream_id, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("RK_MPI_VI_SetChnAttr chan[%d] failed. ret:%x %d\n", stream_id, ret, ret);
		return -1;
	}

	//编码类型改变则需重新初始化编码器
	if (strcmp(new_enc_type, old_enc_type))
	{
		printf("=============venc chan[%d] restart...\n\n", stream_id);

		//osd
		gb_rkipc_osd_detach(stream_id);
		
		ret = rkipc_venc_chan_stop(stream_id);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_stop chan[%d] failed. ret: %d\n", stream_id, ret);
			return -1;
		}
		ret = rkipc_venc_chan_deinit(stream_id);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_deinit chan[%d] failed. ret: %d\n", stream_id, ret);
			return -1;
		}

		s_stream_venc_chan_param[stream_id].enc_type = new_enc_type;
		s_stream_venc_chan_param[stream_id].bit_rate = bit_rate;
		s_stream_venc_chan_param[stream_id].frame_rate_num = frmae_rate;
		s_stream_venc_chan_param[stream_id].gop = gop;

		ret = rkipc_venc_chan_init(stream_id, &s_stream_venc_chan_param[stream_id]);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_init chan[%d] failed. ret: %d\n", stream_id, ret);
			return -1;
		}
		ret = rkipc_venc_chan_start(stream_id);
		if (ret)
		{
			LOG_ERROR("rkipc_venc_chan_start chan[%d] failed. ret: %d\n", stream_id, ret);
			return -1;
		}
		
		//osd
		gb_rkipc_osd_attach(stream_id);
	}
	else
	{
		printf("=============venc chan[%d] set param...\n\n", stream_id);

		s_stream_venc_chan_param[stream_id].enc_type = new_enc_type;
		s_stream_venc_chan_param[stream_id].bit_rate = bit_rate;
		s_stream_venc_chan_param[stream_id].frame_rate_num = frmae_rate;
		s_stream_venc_chan_param[stream_id].gop = gop;
		
		VENC_CHN_ATTR_S venc_chn_attr;
		
		ret = RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
		if (ret) {
			LOG_ERROR("RK_MPI_VENC_GetChnAttr chan[%d] failed. ret:%x %d\n", stream_id, ret, ret);
			return -1;
		}

		// print
		#if 0
		printf("venc_chn_attr.stVencAttr.enType: %d\n", venc_chn_attr.stVencAttr.enType);
		if (RK_VIDEO_ID_AVC == venc_chn_attr.stVencAttr.enType)
		{
			printf("enType is H264\n");
			printf("venc_chn_attr.stRcAttr.enRcMode: %d\n", venc_chn_attr.stRcAttr.enRcMode);
			if (VENC_RC_MODE_H264CBR == venc_chn_attr.stRcAttr.enRcMode)
			{
				printf("venc_chn_attr.stRcAttr.stH264Cbr.u32Gop: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.u32Gop);
				printf("venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate: %d\n", venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate);
			}
			if (VENC_RC_MODE_H264VBR == venc_chn_attr.stRcAttr.enRcMode)
			{
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32Gop: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32Gop);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate);
				printf("venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate: %d\n", venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate);
			}
		}
		else if (RK_VIDEO_ID_HEVC == venc_chn_attr.stVencAttr.enType)
		{
			printf("enType is H265\n");
			printf("venc_chn_attr.stRcAttr.enRcMode: %d\n", venc_chn_attr.stRcAttr.enRcMode);
			if (VENC_RC_MODE_H265CBR == venc_chn_attr.stRcAttr.enRcMode)
			{
				printf("venc_chn_attr.stRcAttr.stH265Cbr.u32Gop: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.u32Gop);
				printf("venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate: %d\n", venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate);
			}
			if (VENC_RC_MODE_H265VBR == venc_chn_attr.stRcAttr.enRcMode)
			{
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32Gop: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32Gop);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate);
				printf("venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate: %d\n", venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate);
			}
		}
		#endif
		
		if (RK_VIDEO_ID_AVC == venc_chn_attr.stVencAttr.enType) {
			if (VENC_RC_MODE_H264CBR == venc_chn_attr.stRcAttr.enRcMode) {
				venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = s_stream_venc_chan_param[stream_id].gop;
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = s_stream_venc_chan_param[stream_id].bit_rate;			
			} else if ((VENC_RC_MODE_H264VBR == venc_chn_attr.stRcAttr.enRcMode)) {
				venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = s_stream_venc_chan_param[stream_id].gop;
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = s_stream_venc_chan_param[stream_id].bit_rate / 3 * 2;
				venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = s_stream_venc_chan_param[stream_id].bit_rate;
				venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = s_stream_venc_chan_param[stream_id].bit_rate / 3;
			}
		} else if (RK_VIDEO_ID_HEVC == venc_chn_attr.stVencAttr.enType) {
			if (VENC_RC_MODE_H265CBR == venc_chn_attr.stRcAttr.enRcMode) {			
				venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = s_stream_venc_chan_param[stream_id].gop;
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = s_stream_venc_chan_param[stream_id].bit_rate;			
			} else if (VENC_RC_MODE_H265VBR == venc_chn_attr.stRcAttr.enRcMode) {			
				venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = s_stream_venc_chan_param[stream_id].gop;
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = s_stream_venc_chan_param[stream_id].frame_rate_den;
				venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = s_stream_venc_chan_param[stream_id].frame_rate_num;
				venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = s_stream_venc_chan_param[stream_id].bit_rate / 3 * 2;
				venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = s_stream_venc_chan_param[stream_id].bit_rate;
				venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = s_stream_venc_chan_param[stream_id].bit_rate / 3;
			}
		}
			
		ret = RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
		if (ret) {
			LOG_ERROR("RK_MPI_VENC_SetChnAttr chan[%d] failed. ret:%x %d\n", stream_id, ret, ret);
			return -1;
		}
	}
	
	printf("=============my_video_set_param_2 completely...\n\n");
	return 0;
}


//sd
int rk_video_set_stitch_distance(float sd)
{
	LOG_INFO("stitch_distance %f\n",sd);
	stitch_distance = sd;
}

//qrcode
static void *rkipc_get_qrcode(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	printf("rkipc_get_qrcode\n");
	VIDEO_FRAME_INFO_S frame;
	// int32_t loopCount = 0;
	int ret = 0;
	int npu_cycle_time_ms = 1000 / 5;//rk_param_get_int("video.source:npu_fps", 10);
	long long before_time, cost_time;
	while (g_vi_qrcode_run_) {
		before_time = rkipc_get_curren_time_ms();
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_1, &frame, 1000);
		if (ret == RK_SUCCESS) {
			if (QrCodeDecodeCb_g) {
				PIC_BUF_ATTR_S pstBufAttr;
				MB_PIC_CAL_S pstPicCal;

				pstBufAttr.u32Width = frame.stVFrame.u32Width;
				pstBufAttr.u32Height = frame.stVFrame.u32Height;
				pstBufAttr.enCompMode = COMPRESS_MODE_NONE;
				pstBufAttr.enPixelFormat = RK_FMT_YUV420SP;
				memset(&pstPicCal, 0, sizeof(MB_PIC_CAL_S));

				ret = RK_MPI_CAL_TDE_GetPicBufferSize(&pstBufAttr, &pstPicCal);
				if (ret != RK_SUCCESS) {
					LOG_ERROR("RK_MPI_CAL_TDE_GetPicBufferSize failure ret:%#X", ret);
				} else {
					if(QrCodeDecodeCb_g) {
						void *data = RK_MPI_MB_Handle2VirAddr(frame.stVFrame.pMbBlk);
						QrCodeDecodeCb_g(data,pstPicCal.u32MBSize, frame.stVFrame.u32Width, frame.stVFrame.u32Height);
					}
				}
			}
			ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &frame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VPSS_ReleaseChnFrame fail %x\n", ret);
			// loopCount++;
		} else {
			 LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
		cost_time = rkipc_get_curren_time_ms() - before_time;
		if ((cost_time > 0) && (cost_time < npu_cycle_time_ms))
			usleep((npu_cycle_time_ms - cost_time) * 1000);
	}

	return 0;
}

int rk_video_qrcode_init(QrCodeDecodeCallback cb)
{
	LOG_INFO("qrcode begin\n");
	QrCodeDecodeCb_g = cb;
	if (!g_vi_qrcode_run_) {
		g_vi_qrcode_run_ = 1;
		pthread_create(&get_vi_qrcode_thread, NULL, rkipc_get_qrcode, NULL);
	}
	return 0;
}
int rk_video_qrcode_deinit() 
{
	LOG_INFO("%s\n", __func__);
	QrCodeDecodeCb_g = NULL;
	g_vi_qrcode_run_ = 0;
	pthread_join(get_vi_qrcode_thread, NULL);
	LOG_INFO("qrcode end\n");
	return 0;
}



static void *rkipc_get_avs_vpss_bgr(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	printf("rkipc_get_avs_vpss_bgr\n");


	return 0;
}

unsigned int rkipc_get_venc_count(int vench)
{
	if (0==vench)
	{
		return uiMainStreamEncodeFrameCount_g;
	}
	else if (1==vench)
	{
		return uiSubStreamEncodeFrameCount_g;
	}

	return 0;
}

int set_eptz_sw(int value)
{
	return 0;
}
int rk_video_set_eptz(int scale)
{
	return 0;
}


int rkipc_ivs_set_sen(int level,motion_detect_callback cb)
{
	int ret = -1;
	IVS_CHN_ATTR_S attr;
	IVS_CHN IvsChn = 0;
	
	memset(&attr,0,sizeof(attr));
	ivs_cb = cb;

	ret = RK_MPI_IVS_GetChnAttr(IvsChn, &attr);
	if (RK_SUCCESS != ret) 
	{
		LOG_ERROR("RK_MPI_IVS_GetChnAttr failed with %#x!\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_IVS_GetChnAttr success\n");

	attr.u32MDSensibility = level;
	ivs_sensibility = level;
	LOG_INFO("u32MDSensibility=%d\n",attr.u32MDSensibility);

	ret = RK_MPI_IVS_SetChnAttr(IvsChn, &attr);
	if (RK_SUCCESS != ret) 
	{
		LOG_ERROR("RK_MPI_IVS_SetChnAttr failed with %#x!\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_IVS_SetChnAttr success\n");
}


//iva
static void person_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("person_cb ...\n");
	if (person_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_PERSON;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		person_det_cb(status,result);
		// LOG_INFO("person_cb det status=%d\n",status);
	}
}
static void vehicle_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("vehicle_cb ...\n");
	if (vehicle_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_VEHICLE;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		vehicle_det_cb(status,result);
		// LOG_INFO("vehicle_cb det status=%d\n",status);
	}
}
static void non_vehicle_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("non_vehicle_cb ...\n");
	if (non_vehicle_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		non_vehicle_det_cb(status,result);
		// LOG_INFO("non_vehicle_cb det status=%d\n",status);
	}
}

int rkipc_video_det_obj_start(int level,int type,CaptureDetectCallback cb)
{
	LOG_INFO("rkipc_video_det_obj_start\n");
	int ret = -1;

	if (enable_npu)
	{
		if (!iva_start)
			return -1;
		
		if (!cb)
			return -1;

		switch (type)
		{
			case DETECT_OBJECT_OBJECT_TYPE_PERSON:
			{
				LOG_INFO("rkipc_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_PERSON\n");
				person_det_cb = cb;
				ret = rkipc_rockiva_set_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_PERSON,person_cb);
			} 
			break;

			case DETECT_OBJECT_OBJECT_TYPE_VEHICLE:
			{
				LOG_INFO("rkipc_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_VEHICLE\n");
				vehicle_det_cb = cb;
				ret = rkipc_rockiva_set_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_VEHICLE,vehicle_cb);
			}
			break;

			case DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE:
			{
				LOG_INFO("rkipc_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE\n");
				non_vehicle_det_cb = cb;
				ret = rkipc_rockiva_set_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_NON_VEHICLE,non_vehicle_cb);
			}
			break;
		
			default:
				break;
		}
	}
	else
	{
		LOG_ERROR("rkipc_video_det_start iva not support\n");
	}
	
	return ret;
}

int rkipc_video_det_obj_stop(int type)
{
	LOG_INFO("rkipc_video_det_obj_stop\n");
	int ret = -1;

	if (enable_npu)
	{
		if (!iva_start)
			return -1;

		switch (type)
		{
			case DETECT_OBJECT_OBJECT_TYPE_PERSON:
			{
				LOG_INFO("rkipc_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_PERSON\n");
				person_det_cb = NULL;
				ret = rkipc_rockiva_clean_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_PERSON);
			} 
			break;

			case DETECT_OBJECT_OBJECT_TYPE_VEHICLE:
			{
				LOG_INFO("rkipc_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_VEHICLE\n");
				vehicle_det_cb = NULL;
				ret = rkipc_rockiva_clean_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_VEHICLE);
			}
			break;

			case DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE:
			{
				LOG_INFO("rkipc_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE\n");
				non_vehicle_det_cb = NULL;
				ret = rkipc_rockiva_clean_hvn_detect_callback(ROCKIVA_OBJECT_TYPE_NON_VEHICLE);
			}
			break;
		
			default:
				break;
		}
	}
	else
	{
		LOG_ERROR("rkipc_video_det_stop iva not support\n");
	}
	
	return ret;
}

int rkipc_video_det_init(DETECT_INIT *pAttr)
{
	LOG_INFO("rkipc_video_det_init\n");
	int ret = -1;

	if (enable_npu)
	{
		if (iva_start)
		{
			return 0;
		}
		rkipc_rockiva_init();

		if (pAttr)
			rkipc_video_det_set(pAttr);

		iva_start = 1;
	}
	else
	{
		LOG_ERROR("rkipc_video_det_init iva not support\n");
	}
	
	return ret;
}

int rkipc_video_det_deinit()
{
	LOG_INFO("rkipc_video_det_deinit\n");
	if (enable_npu)
	{
		if (!iva_start)
			return 0;

		rkipc_rockiva_stop();
		rkipc_rockiva_deinit();
		iva_start = 0;
	}
	else
	{
		LOG_ERROR("rkipc_video_det_deinit iva not support\n");
		return -1;
	}
	
	return 0;
}

int rkipc_video_det_set(DETECT_INIT *pAttr)
{
	LOG_INFO("rkipc_video_det_set\n");
	if (!pAttr)
	{
		return -1;
	}

	RockIvaInfo info;

	int vlaue = 0;
	int region_x, region_xlen, region_y, region_ylen;

	if (RA_270 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	else if (RA_NONE == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_NONE;
	else if (RA_180 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	else if (RA_90 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	else
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_NONE;
	
	if (0 == pAttr->Level) //低灵敏度
		info.sense = 30;
	else if (1 == pAttr->Level)
		info.sense = 50;
	else if (2 == pAttr->Level)
		info.sense = 90;
	
	info.areas.areaNum = pAttr->Region.Num;

	//使能区域检测根据设置的参数设置，最多4个区域，禁止区域检测就默认一个且整个图像画面
	LOG_INFO("level=%d,RegionEnable=%d Region.Num=%d\n",pAttr->Level,pAttr->RegionEnable,pAttr->Region.Num);
	if (pAttr->RegionEnable)
	{
		for (int i = 0; i < pAttr->Region.Num; i++)
		{
			//百分比
			region_x    = (pAttr->Region.RegionAttr[i] >> 24) & 0xFF;
			region_xlen = (pAttr->Region.RegionAttr[i] >> 16) & 0xFF;
			region_y    = (pAttr->Region.RegionAttr[i] >>  8) & 0xFF;
			region_ylen = (pAttr->Region.RegionAttr[i] >>  0) & 0xFF;

			LOG_INFO("[%d: %d,%d,%d,%d]\n",i,region_x,region_y,region_xlen,region_ylen);

			info.areas.areas[i].pointNum = 4;//4个点定义一个区域
			//万分比
			info.areas.areas[i].points[0].x = region_x*100;
			info.areas.areas[i].points[0].y = region_y*100;
			info.areas.areas[i].points[1].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[1].y = region_y*100;
			info.areas.areas[i].points[2].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[2].y = region_y*100 + region_ylen*100;
			info.areas.areas[i].points[3].x = region_x*100;
			info.areas.areas[i].points[3].y = region_y*100 + region_ylen*100;

			LOG_INFO("[%d: (%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n",i, info.areas.areas[i].points[0].x,info.areas.areas[i].points[0].y,
																info.areas.areas[i].points[1].x,info.areas.areas[i].points[1].y,
																info.areas.areas[i].points[2].x,info.areas.areas[i].points[2].y,
																info.areas.areas[i].points[3].x,info.areas.areas[i].points[3].y
																);
		}
	}
	else
	{
			//百分比
			region_x    = 0;
			region_xlen = 100;
			region_y    = 0;
			region_ylen = 100;
			int i = 0;
			LOG_INFO("[%d: %d,%d,%d,%d]\n",i,region_x,region_y,region_xlen,region_ylen);
			info.areas.areaNum = 1;
			info.areas.areas[i].pointNum = 4;//4个点定义一个区域
			//万分比
			info.areas.areas[i].points[0].x = region_x*100;
			info.areas.areas[i].points[0].y = region_y*100;
			info.areas.areas[i].points[1].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[1].y = region_y*100;
			info.areas.areas[i].points[2].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[2].y = region_y*100 + region_ylen*100;
			info.areas.areas[i].points[3].x = region_x*100;
			info.areas.areas[i].points[3].y = region_y*100 + region_ylen*100;

			LOG_INFO("[%d: (%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n",i, info.areas.areas[i].points[0].x,info.areas.areas[i].points[0].y,
																info.areas.areas[i].points[1].x,info.areas.areas[i].points[1].y,
																info.areas.areas[i].points[2].x,info.areas.areas[i].points[2].y,
																info.areas.areas[i].points[3].x,info.areas.areas[i].points[3].y
																);
	}
	return  rkipc_rockiva_set(&info);
}

int rkipc_video_det_get(DETECT_INIT *pAttr)
{
	LOG_INFO("rkipc_video_det_get \n");
	int ret = 0;
	RockIvaInfo info;
	ret = rkipc_rockiva_get(&info);
	if (ret) 
	{
		LOG_ERROR("rkipc_video_det_get error %d\n",ret);
		return ret;
	}
	return ret;
}
int rkipc_video_det_start()
{
	LOG_INFO("rkipc_video_det_start \n");
	return rkipc_rockiva_start();
}

int rkipc_video_det_stop()
{
	LOG_INFO("rkipc_video_det_stop \n");
	return rkipc_rockiva_stop();
}

//motion tracker
static void tracker_cb(RockIvaExecuteStatus status,RockIvaDetectResult *result)
{
	// LOG_INFO("tracker_cb ...\n");
	int i = 0;
	int objNum = 0;

	if (motion_tracker_cb)
	{
		MOTION_TRACKER_RESULT tracker_result;

		if (status == ROCKIVA_SUCCESS)
		{
			tracker_result.Status = 0;
			if (result->objNum == 0)
			{
				tracker_result.ObjNum = 0;
			}
			else
			{
				objNum = result->objNum > 128 ? 128 : result->objNum;
				tracker_result.ObjNum = objNum;
				for (i = 0; i< objNum; i++)
				{
					// LOG_INFO("result->objInfo[%d].type=%d [%u,%u,%u,%u]\n",i,result->objInfo[i].type,
					// 	result->objInfo[i].rect.topLeft.x,
					// 	result->objInfo[i].rect.topLeft.y,
					// 	result->objInfo[i].rect.bottomRight.x,
					// 	result->objInfo[i].rect.bottomRight.y);

					tracker_result.ObjInfo[i].ObjectType = result->objInfo[i].type;
					tracker_result.ObjInfo[i].Rect.topLeft.x = result->objInfo[i].rect.topLeft.x;//ROCKIVA_RATIO_PIXEL_CONVERT(G_VIDEO[2].width,result->objInfo[i].rect.topLeft.x);
					tracker_result.ObjInfo[i].Rect.topLeft.y = result->objInfo[i].rect.topLeft.y;//ROCKIVA_RATIO_PIXEL_CONVERT(G_VIDEO[2].height,result->objInfo[i].rect.topLeft.y);
					tracker_result.ObjInfo[i].Rect.bottomRight.x = result->objInfo[i].rect.bottomRight.x;//ROCKIVA_RATIO_PIXEL_CONVERT(G_VIDEO[2].width,result->objInfo[i].rect.bottomRight.x);
					tracker_result.ObjInfo[i].Rect.bottomRight.y = result->objInfo[i].rect.bottomRight.y;//ROCKIVA_RATIO_PIXEL_CONVERT(G_VIDEO[2].height,result->objInfo[i].rect.bottomRight.y);

					// LOG_INFO("tracker_result.ObjInfo[%d].ObjectType=%d [%u,%u,%u,%u]\n",i,tracker_result.ObjInfo[i].ObjectType,
					// 	tracker_result.ObjInfo[i].Rect.topLeft.x,
					// 	tracker_result.ObjInfo[i].Rect.topLeft.y,
					// 	tracker_result.ObjInfo[i].Rect.bottomRight.x,
					// 	tracker_result.ObjInfo[i].Rect.bottomRight.y);
				}
			}
		}
		else
		{
			tracker_result.Status = -1;
		}

		motion_tracker_cb(tracker_result);
		// LOG_INFO("tracker_cb status=%d\n",tracker_result.Status);
	}
}
int rkipc_video_motion_tracker_start(CaptureDetectCallback cb)
{
	if (enable_npu)
	{
		motion_tracker_cb = cb;
		rkipc_rockiva_set_hvn_motion_tracker_callback(tracker_cb);
	}
	else
	{
		LOG_ERROR("rkipc_video_motion_tracker_start iva not support\n");
	}
}

int rkipc_video_motion_tracker_stop()
{
	if (enable_npu)
	{
		motion_tracker_cb = NULL;
	}
	else
	{
		LOG_ERROR("rkipc_video_motion_tracker_stop iva not support\n");
	}
}
int rkipc_video_get_meanluma(float *value)
{
	if (value != NULL)
	{
		*value = 0;
		return 0;
	}
	else
	{
		return -1;
	}
}

//***********************ir**************************//
#include <fcntl.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "rk_smart_ir_api.h"
#define RK_SMART_IR_AUTO_IRLED false

typedef struct sample_smartIr_s {
    pthread_t tid;
    bool tquit;
    bool started;
    const rk_aiq_sys_ctx_t* aiq_ctx;
    rk_smart_ir_ctx_t* ir_ctx;
    rk_smart_ir_params_t ir_configs;
    bool camGroup;
	smart_ir_callback call;
} sample_smartIr_t;

static sample_smartIr_t g_sample_smartIr_ctx = {0,true,false,NULL,NULL,{0},true,NULL};

static void sample_smartIr_calib()
{
    const rk_aiq_sys_ctx_t* ctx = (rk_aiq_sys_ctx_t*)(rkipc_aiq_get_ctx(0));

    // 1) make sure no visible light
    // 2) ir-cutter off, ir-led on
    ////ir_cutter_ctrl(false);
    // 3. query wb info
    float RGgain = 0.0f, BGgain = 0.0f;
    int counts = 0;
    rk_aiq_isp_stats_t *stats_ref = NULL;
    rk_aiq_awb_stat_blk_res_v201_t* blockResult;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    printf("smartIr calib start ...... \n");
    while (counts++ < 100) {
        if (g_sample_smartIr_ctx.camGroup) {
            printf("get3AStatsBlk only support single ctx!\n");
            break;
        }
        ret = rk_aiq_uapi2_sysctl_get3AStatsBlk(ctx, &stats_ref, -1);
        if (ret != XCAM_RETURN_NO_ERROR || stats_ref == NULL) {
            printf("ret=%d, get3AStatsBlk fail!\n", ret);
            break;
        }

        printf("stats frame id %d, awb_hw_ver %d\n", stats_ref->frame_id, stats_ref->awb_hw_ver);
        if (stats_ref->awb_hw_ver == 4)
            blockResult = stats_ref->awb_stats_v32.blockResult;
        else if (stats_ref->awb_hw_ver == 3)
            blockResult = stats_ref->awb_stats_v3x.blockResult;
        else if (stats_ref->awb_hw_ver == 1)
            blockResult = stats_ref->awb_stats_v21.blockResult;
        else {
            printf("smartIr is not supported on this platform\n");
            rk_aiq_uapi2_sysctl_release3AStatsRef(ctx, stats_ref);
            break;
        }

        float Rvalue = 0, Gvalue = 0, Bvalue = 0, RGgain = 0, BGgain = 0;
        for (int i = 0; i < RK_AIQ_AWB_GRID_NUM_TOTAL; i++) {
            Rvalue = (float)blockResult[i].Rvalue;
            Gvalue = (float)blockResult[i].Gvalue;
            Bvalue = (float)blockResult[i].Bvalue;
            RGgain = RGgain + Rvalue / Gvalue;
            BGgain = BGgain + Bvalue / Gvalue;
        }
        RGgain /= RK_AIQ_AWB_GRID_NUM_TOTAL;
        BGgain /= RK_AIQ_AWB_GRID_NUM_TOTAL;
        printf("origin rggain_base:%0.3f, bggain_base:%0.3f\n", RGgain, BGgain);
        rk_aiq_uapi2_sysctl_release3AStatsRef(ctx, stats_ref);

    }
    printf("smartIr calib done ...... \n");
}

// SMARTIR_VERSION 2.0.0
static void* sample_smartIr_switch_thread(void* args)
{
    sample_smartIr_t* smartIr_ctx = &g_sample_smartIr_ctx;
    rk_smart_ir_result_t result;
    rk_aiq_isp_stats_t *stats_ref = NULL;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    // cam group
    rk_aiq_camgroup_ctx_t* camgroup_ctx = NULL;
    rk_aiq_camgroup_camInfos_t camInfos;
    rk_aiq_sys_ctx_t* group_ctxs[RK_AIQ_CAM_GROUP_MAX_CAMS];
    rk_aiq_isp_stats_t* group_stats[RK_AIQ_CAM_GROUP_MAX_CAMS];

    while (!smartIr_ctx->tquit) {

        if (smartIr_ctx->camGroup) {
            camgroup_ctx = (rk_aiq_camgroup_ctx_t *)smartIr_ctx->aiq_ctx;
            ret = rk_aiq_uapi2_camgroup_getCamInfos(camgroup_ctx, &camInfos);
            if (ret != XCAM_RETURN_NO_ERROR) {
                printf("ret=%d, getCamInfos fail!\n", ret);
                break;
            }
            for (int i = 0; i < camInfos.valid_sns_num; i++) {
                group_ctxs[i] = rk_aiq_uapi2_camgroup_getAiqCtxBySnsNm(camgroup_ctx, camInfos.sns_ent_nm[i]);
                if (group_ctxs[i] == NULL) {
                    printf("getAiqCtxBySnsNm fail!\n");
                    break;
                }
                ret = rk_aiq_uapi2_sysctl_get3AStatsBlk(group_ctxs[i], &group_stats[i], -1);
                if (ret != XCAM_RETURN_NO_ERROR || group_stats[i] == NULL) {
                    printf("ret=%d, get3AStatsBlk fail!\n", ret);
                    break;
                }
            }
            rk_smart_ir_groupRunOnce(smartIr_ctx->ir_ctx, group_stats, camInfos.valid_sns_num, &result);
            for (int i = 0; i < camInfos.valid_sns_num; i++) {
                rk_aiq_uapi2_sysctl_release3AStatsRef(group_ctxs[i], group_stats[i]);
            }

        } else {
            ret = rk_aiq_uapi2_sysctl_get3AStatsBlk(smartIr_ctx->aiq_ctx, &stats_ref, -1);
            if (ret != XCAM_RETURN_NO_ERROR || stats_ref == NULL) {
                printf("ret=%d, get3AStatsBlk fail!\n", ret);
                break;
            }
            rk_smart_ir_runOnce(smartIr_ctx->ir_ctx, stats_ref, &result);
            rk_aiq_uapi2_sysctl_release3AStatsRef(smartIr_ctx->aiq_ctx, stats_ref);
        }

        if (result.gray_on) {
            // 1) switch to isp night params
            ////rk_aiq_uapi2_sysctl_switch_scene(smartIr_ctx->aiq_ctx, "normal", "night");
            // 2) ir-cutter off
            ////ir_cutter_ctrl(false);
            // 3) auto ir-led, set result.fill_value
			if (smartIr_ctx->call) 
			{
				smartIr_ctx->call(SMART_IR_NIGHT);
			}

        } else {
            if (result.status == RK_SMART_IR_STATUS_DAY) {
                // 1) ir-cutter on
                ////ir_cutter_ctrl(true);
                // 2) ir-led off
                // 3) switch to isp day params
                ////rk_aiq_uapi2_sysctl_switch_scene(smartIr_ctx->aiq_ctx, "normal", "day");
				if (smartIr_ctx->call) 
				{
					smartIr_ctx->call(SMART_IR_DAY);
				}

            } else if (result.status == RK_SMART_IR_STATUS_NIGHT) {
                // 1) ir-cutter on
                ////ir_cutter_ctrl(true);
                // 2) auto vis-led, set result.fill_value
                // 3) switch to isp day params
                ////rk_aiq_uapi2_sysctl_switch_scene(smartIr_ctx->aiq_ctx, "normal", "day");
				if (smartIr_ctx->call) 
				{
					smartIr_ctx->call(SMART_IR_NIGHT);
				}
            }
        }

        // printf("SAMPLE_SMART_IR: switch to %s\n", result.status == RK_SMART_IR_STATUS_DAY ? "DAY" : "Night");

    }

    return NULL;
}
int rk_smart_ir_start(smart_ir_callback call, int sensitivity)
{	
	sample_smartIr_t* smartIr_ctx = &g_sample_smartIr_ctx;
    // 1) init
    smartIr_ctx->ir_ctx = rk_smart_ir_init((rk_aiq_sys_ctx_t*)rkipc_aiq_get_ctx(0));
	smartIr_ctx->aiq_ctx = rkipc_aiq_get_ctx(0);
    // 2) load configs: auto switch, manual ir led
    rk_smart_ir_attr_t attr;
    //memset(&attr, 0, sizeof(attr));
    rk_smart_ir_getAttr(smartIr_ctx->ir_ctx, &attr);
    attr.init_status = RK_SMART_IR_STATUS_DAY;
    attr.switch_mode = RK_SMART_IR_SWITCH_MODE_AUTO;
    attr.light_mode = RK_SMART_IR_LIGHT_MODE_MANUAL;
    attr.light_type = RK_SMART_IR_LIGHT_TYPE_IR;
    attr.light_value = 100;
    attr.params.d2n_envL_th = 0.04f;
    attr.params.n2d_envL_th = 0.20f;
    attr.params.rggain_base = 1.00f;
    attr.params.bggain_base = 1.00f;
    attr.params.awbgain_rad = 0.10f;
    attr.params.awbgain_dis = 0.20f;
    attr.params.switch_cnts_th = sensitivity;//50;
    rk_smart_ir_setAttr(smartIr_ctx->ir_ctx, &attr);
	smartIr_ctx->call = call;
    // 3) create thread
    smartIr_ctx->tquit = false;
    pthread_create(&smartIr_ctx->tid, NULL, sample_smartIr_switch_thread, NULL);
    smartIr_ctx->started = true;

	return 0;
}
int rk_smart_ir_stop()
{
	sample_smartIr_t* smartIr_ctx = &g_sample_smartIr_ctx;

    if (smartIr_ctx->started) {
        smartIr_ctx->tquit = true;
        pthread_join(smartIr_ctx->tid, NULL);
    }

    smartIr_ctx->started = false;

    if (smartIr_ctx->ir_ctx) {
        rk_smart_ir_deInit(smartIr_ctx->ir_ctx);
        smartIr_ctx->ir_ctx = NULL;
    }

	return 0;
}

int rk_video_set_venc_force_idr(int stream_id)
{
	// bInstant暂时只支持RK_FLASE，设置RK_TRUE会返回不支持VENC错误码
	// RK_ERR_VENC_NOT_SUPPORT
	return RK_MPI_VENC_RequestIDR(stream_id,RK_FALSE);
}

static unsigned char s_mirror = 0;
static unsigned char s_flip = 0;
int rk_video_set_vi_mirror_flip(unsigned char mirror, unsigned char flip)
{
	RK_S32 ret = 0;
	VI_ISP_MIRROR_FLIP_S stMirrFlip;
	stMirrFlip.mirror = mirror;
	stMirrFlip.flip = flip;

	s_mirror = mirror;
	s_flip = flip;

	/*
	*Mirror镜像功能是全局的属性配置，即某个通道设置后，其他通道均会有效。
	*Flip翻转功能是独立的属性配置，即只对某个通道设置后有效。其他通道无效
	*/
	
	ret = RK_MPI_VI_SetChnMirrorFlip(pipe_id_, VIDEO_PIPE_0, stMirrFlip);
	if (ret) 
	{
		LOG_ERROR("RK_MPI_VI_SetChnMirrorFlip VIDEO_PIPE_0 error %d\n",ret);
		return ret;
	}
	ret = RK_MPI_VI_SetChnMirrorFlip(pipe_id_, VIDEO_PIPE_1, stMirrFlip);
	if (ret) 
	{
		LOG_ERROR("RK_MPI_VI_SetChnMirrorFlip VIDEO_PIPE_1 error %d\n",ret);
		return ret;
	}
	ret = RK_MPI_VI_SetChnMirrorFlip(pipe_id_, VIDEO_PIPE_2, stMirrFlip);
	if (ret) 
	{
		LOG_ERROR("RK_MPI_VI_SetChnMirrorFlip VIDEO_PIPE_2 error %d\n",ret);
		return ret;
	}
	
	return 0;
}

int rk_video_get_vi_mirror_flip(unsigned char *mirror, unsigned char *flip)
{
	if (mirror)
		*mirror = s_mirror;
	if (flip)
		*flip = s_flip;
	return 0;
}


// -------------------------------------------------------
static int s_gb_det_start = 0;
static int s_gb_det_thread_run = 0;
static pthread_t gb_det_thread;
#define MAX_MAIN_JPG_SIZE (200*1024)
static char s_main_jpg_data[MAX_MAIN_JPG_SIZE];
static DET_PIC_S s_main_jpg_info;
#define MAX_THUM_NUM (10)
#define MAX_THUM_SIZE (10*1024)
static int s_thum_count = 0;
static char s_thum_data[MAX_THUM_NUM][MAX_THUM_SIZE];
static DET_THUM_S s_thum_inof[MAX_THUM_NUM];
static CaptureDetectSnapCallback s_det_snap_cb = NULL;
#if 01
//iva
static void gb_person_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("person_cb ...\n");
	if (person_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_PERSON;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		person_det_cb(status,result);
		// LOG_INFO("person_cb det status=%d\n",status);
	}
}
static void gb_vehicle_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("vehicle_cb ...\n");
	if (vehicle_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_VEHICLE;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		vehicle_det_cb(status,result);
		// LOG_INFO("vehicle_cb det status=%d\n",status);
	}
}
static void gb_non_vehicle_cb(int status,RockIvaRectangle rect)
{
	// LOG_INFO("non_vehicle_cb ...\n");
	if (non_vehicle_det_cb)
	{
		DETECT_RESULT result;
		result.Status = status;
		result.ObjectType = DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE;
		result.Rect.topLeft.x = rect.topLeft.x;
		result.Rect.topLeft.y = rect.topLeft.y;
		result.Rect.bottomRight.x = rect.bottomRight.x;
		result.Rect.bottomRight.y = rect.bottomRight.y;
		non_vehicle_det_cb(status,result);
		// LOG_INFO("non_vehicle_cb det status=%d\n",status);
	}
}

int gb_rkipc_video_det_obj_start(int level,int type,CaptureDetectCallback cb)
{
	LOG_INFO("gb_rkipc_video_det_obj_start\n");
	int ret = -1;

	if (!cb)
		return -1;
	
	switch (type)
	{
		case DETECT_OBJECT_OBJECT_TYPE_PERSON:
		{
			LOG_INFO("gb_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_PERSON\n");
			person_det_cb = cb;
		} 
		break;
	
		case DETECT_OBJECT_OBJECT_TYPE_VEHICLE:
		{
			LOG_INFO("gb_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_VEHICLE\n");
			vehicle_det_cb = cb;
		}
		break;
	
		case DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE:
		{
			LOG_INFO("gb_video_det_obj_start: DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE\n");
			non_vehicle_det_cb = cb;
		}
		break;
	
		default:
			break;
	}
	
	return ret;
}

int gb_rkipc_video_det_obj_stop(int type)
{
	LOG_INFO("gb_rkipc_video_det_obj_stop\n");
	int ret = -1;

	switch (type)
	{
		case DETECT_OBJECT_OBJECT_TYPE_PERSON:
		{
			LOG_INFO("gb_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_PERSON\n");
			person_det_cb = NULL;
		} 
		break;
	
		case DETECT_OBJECT_OBJECT_TYPE_VEHICLE:
		{
			LOG_INFO("gb_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_VEHICLE\n");
			vehicle_det_cb = NULL;
		}
		break;
	
		case DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE:
		{
			LOG_INFO("gb_video_det_obj_stop: DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE\n");
			non_vehicle_det_cb = NULL;
		}
		break;
	
		default:
			break;
	}
	
	return ret;
}

int gb_rkipc_video_det_set_snap_cb(CaptureDetectSnapCallback cb)
{
	LOG_INFO("gb_rkipc_video_det_set_snap_cb\n");
	s_det_snap_cb = cb;
	return 0;
}

#endif
static int det_result_proc(uint32_t *p_objNum, RockIvaBaObjectInfo *p_triggerObjects, VIDEO_FRAME_INFO_S *pstJpgViFrame)
{
	if (!p_objNum || !p_triggerObjects || !pstJpgViFrame)
		return -1;
	return 0;
}
static int cal_tde_src_rect(int width, int height, RockIvaRectangle *obj_rect, TDE_RECT_S *pstOutputTdeRect)
{
	int src_x, src_y, src_w, src_h;
	int src_center_x, src_center_y;
	int src_btm_x, src_btm_y;
	int min_src_width = (((100+15) / 16) + 1) & 0xfffffffe;
	int min_src_height = min_src_width; //1:1的比例
//	printf("min_src_width: %d, min_src_height: %d\n", min_src_width, min_src_height);
	//经测试，左上角坐标和宽高必须是偶数，否则 RK_TDE_EndJob 失败
	//只支持最大16倍的缩放，所以，宽和高最小分别是240和68，否则出现段错误

	
	if (obj_rect->topLeft.x < 0) obj_rect->topLeft.x = 0;
	if (obj_rect->topLeft.x > 9999) obj_rect->topLeft.x = 9999;
	if (obj_rect->topLeft.y < 0) obj_rect->topLeft.y = 0;
	if (obj_rect->topLeft.y > 9999) obj_rect->topLeft.y = 9999;
	if (obj_rect->bottomRight.x < 0) obj_rect->bottomRight.x = 0;
	if (obj_rect->bottomRight.x > 9999) obj_rect->bottomRight.x = 9999;
	if (obj_rect->bottomRight.y < 0) obj_rect->bottomRight.y = 0;
	if (obj_rect->bottomRight.y > 9999) obj_rect->bottomRight.y = 9999;

	src_x = width * obj_rect->topLeft.x / 10000;
	src_y = height * obj_rect->topLeft.y / 10000;
	src_w = width *
		(obj_rect->bottomRight.x - obj_rect->topLeft.x + 1) / 10000;
	src_h = height *
		(obj_rect->bottomRight.y - obj_rect->topLeft.y + 1) / 10000;

	src_btm_x = src_x + src_w - 1;
	src_btm_y = src_y + src_h - 1;
//	printf("--- src x,y(%d, %d) w,h(%d, %d)\n", src_x, src_y, src_w, src_h);

	src_center_x = src_x + src_w / 2;
	src_center_y = src_y + src_h / 2;

	//按1:1的比例取宽高
	int new_x, new_y, new_w, new_h;
	new_w = MAX(src_w, src_h);
	new_h = new_w;

	//按比例多截取20%的区域
	new_w = new_w * 1.2;
	new_w &= 0xfffffffe;
	new_h = new_w;

	if (new_w > width)
		new_w = width;
	if (new_h > height)
		new_h = height;

	if (new_w < min_src_width)
		new_w = min_src_width;
	if (new_h < min_src_height)
		new_h = min_src_height;

	new_w = MIN(new_w, new_h);
	new_w &= 0xfffffffe;
	new_h = new_w;

	int new_w_half = new_w / 2;
	int new_h_half = new_h / 2;

	if ((src_center_x + new_w_half) > width)
		new_x = width - new_w;
	else
		new_x = src_center_x - new_w_half;
	if (new_x < 0) new_x = 0;

	if ((src_center_y+new_h_half) > height)
		new_y = height - new_h;
	else
		new_y = src_center_y - new_h_half;
	if (new_y < 0) new_y = 0;

	new_x = new_x / 2 * 2;
	new_y = new_y / 2 * 2;

	pstOutputTdeRect->s32Xpos = new_x;
	pstOutputTdeRect->s32Ypos = new_y;
	pstOutputTdeRect->u32Width = new_w;
	pstOutputTdeRect->u32Height = new_h;
	return 0;
}

static void *thread_gb_det_proc(void *arg) {
	printf("-----thread_gb_det_proc start...\n");
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "gb_det_proc", 0, 0, 0);
	int ret;
	int32_t loopCount = 0;
	VIDEO_FRAME_INFO_S stJpgViFrame;
	VIDEO_FRAME_INFO_S stDetViFrame;
	int npu_cycle_time_ms = 1000 / 5;//rk_param_get_int("video.source:npu_fps", 10);
	long long before_time, cost_time;

	int jpg_vi_chan = 1;
	int det_vi_chan = 2;

	int get_jpg_frame_res;
	int get_det_frame_res;

	int32_t fd;
	uint32_t *p_objNum = NULL;
	RockIvaBaObjectInfo *p_triggerObjects = NULL;

	VENC_STREAM_S stVencFrame;
	void *data = NULL;

	//TDE
	TDE_HANDLE hTdeHandle;
	TDE_SURFACE_S stTdeSrc, stTdeDst;
	TDE_RECT_S stTdeSrcRect, stTdeDstRect;
	PIC_BUF_ATTR_S stTdeOutPicBufAttr;
	MB_PIC_CAL_S stMbPicCalResult;
	VIDEO_FRAME_INFO_S stThumFrame;

	RockIvaRectangle stEmptyRect = {{0, 0}, {0, 0}};
	int person_triggered;
	int vehicle_triggered;
	int non_vehicle_triggered;

	int snap_interval = 30*1000; //ms
	int enable_snap;
	int snap_succ;
	long long last_snap_time = -1;

	stVencFrame.pstPack = malloc(sizeof(VENC_PACK_S));
	if (NULL ==  stVencFrame.pstPack) {
		LOG_ERROR("malloc stVencFrame.pstPack failure\n");
		goto END;
	}

	/* alloc tde output blk*/
	memset(&stTdeOutPicBufAttr, 0, sizeof(PIC_BUF_ATTR_S));
	memset(&stMbPicCalResult, 0, sizeof(MB_PIC_CAL_S));
	
	memset(&stThumFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
	stThumFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
	stThumFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
	
	stTdeOutPicBufAttr.u32Width = s_jpeg_venc_chan_param[1].width;
	stTdeOutPicBufAttr.u32Height = s_jpeg_venc_chan_param[1].height;
	stTdeOutPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
	stTdeOutPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	ret = RK_MPI_CAL_TDE_GetPicBufferSize(&stTdeOutPicBufAttr, &stMbPicCalResult);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_CAL_TDE_GetPicBufferSize failure:%X\n", ret);
		goto END;
	}
	printf("stMbPicCalResult.u32MBSize: %u\n", stMbPicCalResult.u32MBSize);
	ret = RK_MPI_SYS_MmzAlloc(&stThumFrame.stVFrame.pMbBlk, RK_NULL, RK_NULL, stMbPicCalResult.u32MBSize);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_MmzAlloc failure:%X\n", ret);
		goto END;
	}

	/* tde open */
	ret = RK_TDE_Open();
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_TDE_Open failure:%X\n", ret);
		goto __TDE_INIT_FAILED;
	}

	before_time = 0;

	while (s_gb_det_thread_run) {
//		printf("-----thread_gb_det_proc running...\n");
//		sleep(1); //for debug

		cost_time = rkipc_get_curren_time_ms() - before_time;
		if ((cost_time >= 0) && (cost_time < npu_cycle_time_ms))
			usleep((npu_cycle_time_ms - cost_time) * 1000);
		before_time = rkipc_get_curren_time_ms();

		get_det_frame_res = -1;
		get_jpg_frame_res = -1;

		person_triggered = 0;
		vehicle_triggered = 0;
		non_vehicle_triggered = 0;

		#if 0
		if (last_snap_time < 0 || before_time > (last_snap_time + snap_interval))
			enable_snap = 1;
		else
			enable_snap = 0;
		#else
		//暂时不在这里做间隔判断
		enable_snap = 1;
		#endif
		snap_succ = 0;

		if (0 == s_gb_det_start)
		{
//			LOG_INFO("s_gb_det_start: %d\n", s_gb_det_start);
			goto NEXT;
		}
		
		get_det_frame_res = RK_MPI_VI_GetChnFrame(pipe_id_, det_vi_chan, &stDetViFrame, 1000);
		if (get_det_frame_res)
		{
			LOG_ERROR("RK_MPI_VI_GetChnFrame chan[%d] failed. ret: %x\n", det_vi_chan, get_det_frame_res);
			goto NEXT;
		}
		get_jpg_frame_res = RK_MPI_VI_GetChnFrame(pipe_id_, jpg_vi_chan, &stJpgViFrame, 1000);
		if (get_jpg_frame_res)
		{
			LOG_ERROR("RK_MPI_VI_GetChnFrame chan[%d] failed. ret: %x\n", jpg_vi_chan, get_jpg_frame_res);
			goto NEXT;
		}

//		printf("send frame to iva...\n");
		fd = RK_MPI_MB_Handle2Fd(stDetViFrame.stVFrame.pMbBlk);
		ret = rkipc_rockiva_write_nv12_frame_by_fd_for_gb(stDetViFrame.stVFrame.u32Width, stDetViFrame.stVFrame.u32Height, loopCount, 
															fd, &p_objNum, &p_triggerObjects);
		if (ret || !p_objNum || !p_triggerObjects)
		{
			LOG_ERROR("rkipc_rockiva_write_nv12_frame_by_fd_for_gb failed. ret: %x\n", ret);
			goto NEXT;
		}
		loopCount++;
		
//		det_result_proc(p_objNum, p_triggerObjects, &stJpgViFrame);

		if (0 == *p_objNum)
		{
//			printf("objNum : 0\n");
			goto NEXT;
		}

		for (int i = 0; i < *p_objNum; i++)
		{
			if (ROCKIVA_OBJECT_TYPE_PERSON == p_triggerObjects[i].objInfo.type)
				person_triggered = 1;
			else if (ROCKIVA_OBJECT_TYPE_VEHICLE == p_triggerObjects[i].objInfo.type)
				vehicle_triggered = 1;
			else if (ROCKIVA_OBJECT_TYPE_NON_VEHICLE == p_triggerObjects[i].objInfo.type)
				non_vehicle_triggered = 1;
		}

		if (0 ==enable_snap)
		{
//			LOG_INFO("enable_snap: %d\n", enable_snap);
			goto NEXT;
		}
		
		LOG_INFO("send frame to venc[2]...\n");		
		//编码大图
		ret = RK_MPI_VENC_SendFrame(s_jpeg_venc_chan_param[0].channel, &stJpgViFrame, 1000);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VENC_SendFrame failure:%X pipe:%d chnid:%d", ret, 0, s_jpeg_venc_chan_param[0].channel);
			goto NEXT;
		}
		LOG_INFO("get frame from venc[2]...\n");
		ret = RK_MPI_VENC_GetStream(s_jpeg_venc_chan_param[0].channel, &stVencFrame, 1000);
		if (ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VENC_GetStream failure:%X pipe:%d chnid:%d", ret, 0, s_jpeg_venc_chan_param[0].channel);
			goto NEXT;
		}
		LOG_INFO("get frame from venc[2] succ...\n");
		data = RK_MPI_MB_Handle2VirAddr(stVencFrame.pstPack->pMbBlk);
		if (stVencFrame.pstPack->u32Len <= MAX_MAIN_JPG_SIZE)
		{
			s_main_jpg_info.width = s_jpeg_venc_chan_param[0].width;
			s_main_jpg_info.height = s_jpeg_venc_chan_param[0].height;
			s_main_jpg_info.size = stVencFrame.pstPack->u32Len;
			memcpy(s_main_jpg_data, data, s_main_jpg_info.size);
			s_main_jpg_info.data = s_main_jpg_data;
			snap_succ = 1;
		}
		else
		{
			LOG_ERROR("main jpeg size: %d is exceeded.\n", stVencFrame.pstPack->u32Len);
		}
		
		{// debug
//			char path[128];
//			snprintf(path, sizeof(path), "/mnt/sdcard/alarm-%d.jpg", loopCount);
//			FILE *fp = fopen(path, "w");
//			if (fp)
//			{
//				fwrite(data, 1, stVencFrame.pstPack->u32Len, fp);
//				fclose(fp);
//			}
		}
		ret = RK_MPI_VENC_ReleaseStream(s_jpeg_venc_chan_param[0].channel, &stVencFrame);
		if (ret != RK_SUCCESS) 
			LOG_ERROR("RK_MPI_VENC_ReleaseStream chan[%d] fail %x\n", s_jpeg_venc_chan_param[0].channel, ret);

		if (0 == snap_succ)
		{
			goto NEXT;
		}

		last_snap_time = before_time;

		s_thum_count = 0;

		for (int i = 0; i < *p_objNum && s_thum_count < MAX_THUM_NUM; i++)
		{			
			LOG_INFO("tde begin...\n");
			hTdeHandle = RK_TDE_BeginJob();
			if (RK_ERR_TDE_INVALID_HANDLE == hTdeHandle) {
				LOG_ERROR("RK_TDE_BeginJob Failure\n");
				continue;
			}
			
			//截取检测区域图片并转成指定分辨率
			/* set tde src/dst rect*/
			stTdeSrc.pMbBlk = stJpgViFrame.stVFrame.pMbBlk;
			stTdeSrc.u32Width = stJpgViFrame.stVFrame.u32Width;
			stTdeSrc.u32Height = stJpgViFrame.stVFrame.u32Height;
			stTdeSrc.enColorFmt = stJpgViFrame.stVFrame.enPixelFormat;
			stTdeSrc.enComprocessMode = stJpgViFrame.stVFrame.enCompressMode;
			cal_tde_src_rect(stTdeSrc.u32Width, stTdeSrc.u32Height, &p_triggerObjects[i].objInfo.rect, &stTdeSrcRect);
			LOG_INFO("stTdeSrcRect [%d %d %u %u]\n", stTdeSrcRect.s32Xpos, stTdeSrcRect.s32Ypos, stTdeSrcRect.u32Width, stTdeSrcRect.u32Height);
			
			stTdeDst.pMbBlk = stThumFrame.stVFrame.pMbBlk;
			stTdeDst.u32Width = s_jpeg_venc_chan_param[1].width;
			stTdeDst.u32Height = s_jpeg_venc_chan_param[1].height;
			stTdeDst.enColorFmt = RK_FMT_YUV420SP;
			stTdeDst.enComprocessMode = COMPRESS_MODE_NONE;
			stTdeDstRect.s32Xpos = 0;
			stTdeDstRect.s32Ypos = 0;
			stTdeDstRect.u32Width = s_jpeg_venc_chan_param[1].width;
			stTdeDstRect.u32Height = s_jpeg_venc_chan_param[1].height;

			LOG_INFO("tde resize...\n");
			ret = RK_TDE_QuickResize(hTdeHandle, &stTdeSrc, &stTdeSrcRect, &stTdeDst, &stTdeDstRect);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_TDE_QuickCopy Failure %#X \n", ret);
				RK_TDE_CancelJob(hTdeHandle);
				continue;
			}
			
			LOG_INFO("tde end job...\n");
			ret = RK_TDE_EndJob(hTdeHandle, RK_FALSE, RK_TRUE, -1);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_TDE_EndJob Failure %#X \n", ret);
				RK_TDE_CancelJob(hTdeHandle);
				continue;
			}
			
			LOG_INFO("tde wait for done...\n");
			ret = RK_TDE_WaitForDone(hTdeHandle);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_TDE_WaitForDone Failure s32Ret: %#X\n", ret);
				continue;
			}

			//编小图
			stThumFrame.stVFrame.u32TimeRef = stJpgViFrame.stVFrame.u32TimeRef;
			stThumFrame.stVFrame.u64PTS = stJpgViFrame.stVFrame.u64PTS;
			stThumFrame.stVFrame.u32Width = s_jpeg_venc_chan_param[1].width;
			stThumFrame.stVFrame.u32Height = s_jpeg_venc_chan_param[1].height;
			stThumFrame.stVFrame.u32VirWidth = s_jpeg_venc_chan_param[1].width;
			stThumFrame.stVFrame.u32VirHeight = s_jpeg_venc_chan_param[1].height;
			stThumFrame.stVFrame.enPixelFormat = stJpgViFrame.stVFrame.enPixelFormat;
			stThumFrame.stVFrame.u32FrameFlag = stJpgViFrame.stVFrame.u32FrameFlag;
			stThumFrame.stVFrame.enCompressMode = stJpgViFrame.stVFrame.enCompressMode;
			LOG_INFO("send frame to venc[3]...\n");
			ret = RK_MPI_VENC_SendFrame(s_jpeg_venc_chan_param[1].channel, &stThumFrame, 1000);
			if (ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_SendFrame failure:%X pipe:%d chnid:%d", ret, 0, s_jpeg_venc_chan_param[1].channel);
				continue;
			}
			LOG_INFO("get frame from venc[3]...\n");
			ret = RK_MPI_VENC_GetStream(s_jpeg_venc_chan_param[1].channel, &stVencFrame, 1000);
			if (ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_GetStream failure:%X pipe:%d chnid:%d", ret, 0, s_jpeg_venc_chan_param[1].channel);
				continue;
			}

			LOG_INFO("get frame from venc[3] succ...\n");
			data = RK_MPI_MB_Handle2VirAddr(stVencFrame.pstPack->pMbBlk);
			if (stVencFrame.pstPack->u32Len <= MAX_THUM_SIZE)
			{
				if (ROCKIVA_OBJECT_TYPE_PERSON == p_triggerObjects[i].objInfo.type)
					s_thum_inof[s_thum_count].type = DETECT_OBJECT_OBJECT_TYPE_PERSON;
				else if (ROCKIVA_OBJECT_TYPE_VEHICLE == p_triggerObjects[i].objInfo.type)
					s_thum_inof[s_thum_count].type = DETECT_OBJECT_OBJECT_TYPE_VEHICLE;
				else if (ROCKIVA_OBJECT_TYPE_NON_VEHICLE == p_triggerObjects[i].objInfo.type)
					s_thum_inof[s_thum_count].type = DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE;
				else if (ROCKIVA_OBJECT_TYPE_FACE == p_triggerObjects[i].objInfo.type)
					s_thum_inof[s_thum_count].type = DETECT_OBJECT_OBJECT_TYPE_FACE;
				else
					s_thum_inof[s_thum_count].type = DETECT_OBJECT_TYPE_NONE;

				s_thum_inof[s_thum_count].rect.s32Xpos = stTdeSrcRect.s32Xpos;
				s_thum_inof[s_thum_count].rect.s32Ypos = stTdeSrcRect.s32Ypos;
				s_thum_inof[s_thum_count].rect.u32Width = stTdeSrcRect.u32Width;
				s_thum_inof[s_thum_count].rect.u32Height = stTdeSrcRect.u32Height;

				s_thum_inof[s_thum_count].pic.width = s_jpeg_venc_chan_param[1].width;
				s_thum_inof[s_thum_count].pic.height = s_jpeg_venc_chan_param[1].height;
				s_thum_inof[s_thum_count].pic.size = stVencFrame.pstPack->u32Len;
				memcpy(s_thum_data[s_thum_count], data, s_thum_inof[s_thum_count].pic.size);
				s_thum_inof[s_thum_count].pic.data = s_thum_data[s_thum_count];
				s_thum_count++;
			}

			{//debug
//				char path[128];
//				const char *obj_type = "unknown";
//				if (ROCKIVA_OBJECT_TYPE_PERSON == p_triggerObjects[i].objInfo.type)
//					obj_type = "person";
//				else if (ROCKIVA_OBJECT_TYPE_VEHICLE == p_triggerObjects[i].objInfo.type)
//					obj_type = "vehicle";
//				else if (ROCKIVA_OBJECT_TYPE_NON_VEHICLE == p_triggerObjects[i].objInfo.type)
//					obj_type = "non_vehicle";
//				else if (ROCKIVA_OBJECT_TYPE_FACE == p_triggerObjects[i].objInfo.type)
//					obj_type = "face";
//				snprintf(path, sizeof(path), "/mnt/sdcard/alarm-%d-%s.jpg", loopCount, obj_type);
//				FILE *fp = fopen(path, "w");
//				if (fp)
//				{
//					fwrite(data, 1, stVencFrame.pstPack->u32Len, fp);
//					fclose(fp);
//				}
			}
			ret = RK_MPI_VENC_ReleaseStream(s_jpeg_venc_chan_param[1].channel, &stVencFrame);
			if (ret != RK_SUCCESS) 
				LOG_ERROR("RK_MPI_VENC_ReleaseStream chan[%d] fail %x\n", s_jpeg_venc_chan_param[1].channel, ret);
		}

NEXT:
		if (RK_SUCCESS == get_det_frame_res) {
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, det_vi_chan, &stDetViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame chan[%d] fail %x", det_vi_chan, ret);
		}
		if (RK_SUCCESS == get_jpg_frame_res) {
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, jpg_vi_chan, &stJpgViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame chan[%d] fail %x", jpg_vi_chan, ret);
		}

		//先不提供区域坐标
		gb_person_cb(person_triggered, stEmptyRect);
		gb_vehicle_cb(vehicle_triggered, stEmptyRect);
		gb_non_vehicle_cb(non_vehicle_triggered, stEmptyRect);

		if (snap_succ && s_det_snap_cb)
		{
			//回调图片
			s_det_snap_cb(&s_main_jpg_info, s_thum_inof, s_thum_count)
;
		}
	}

__TDE_HANDLE_FAILED:
	RK_TDE_Close();

__TDE_INIT_FAILED:
	if (stThumFrame.stVFrame.pMbBlk) {
		RK_MPI_SYS_Free(stThumFrame.stVFrame.pMbBlk);
		stThumFrame.stVFrame.pMbBlk = RK_NULL;
	}

END:
	if (stVencFrame.pstPack)
		free(stVencFrame.pstPack);

	return NULL;
}

int gb_rkipc_video_det_init(DETECT_INIT *pAttr)
{
	LOG_INFO("gb_rkipc_video_det_init\n");
	int ret = -1;

	if (iva_start)
	{
		return 0;
	}
	rkipc_rockiva_init();
	
	if (pAttr)
		gb_rkipc_video_det_set(pAttr);

	s_gb_det_thread_run = 1;
	ret = pthread_create(&gb_det_thread, NULL, thread_gb_det_proc, NULL);
	if (ret)
	{
		s_gb_det_thread_run = 0;
		LOG_ERROR("create thread_gb_det_proc failed! ret=%d\n", ret);
		return -1;
	}
//	rkipc_osd_draw_nn_init();
	
	printf("\n\n-------------gb_rkipc_video_det_init start.n\n");
	iva_start = 1;
	
	return 0;
}

#if 01
int gb_rkipc_video_det_deinit()
{
	LOG_INFO("gb_rkipc_video_det_deinit\n");
	if (!iva_start)
		return 0;

	if (s_gb_det_thread_run)
	{
		s_gb_det_thread_run = 0;
		pthread_join(gb_det_thread, NULL);
	}
	
	rkipc_rockiva_stop();
	rkipc_rockiva_deinit();
	iva_start = 0;
	
	return 0;
}

int gb_rkipc_video_det_set(DETECT_INIT *pAttr)
{
	LOG_INFO("gb_rkipc_video_det_set\n");
	if (!pAttr)
	{
		return -1;
	}

	RockIvaInfo info;

	int vlaue = 0;
	int region_x, region_xlen, region_y, region_ylen;

	if (RA_270 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	else if (RA_NONE == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_NONE;
	else if (RA_180 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	else if (RA_90 == pAttr->Rotate)
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	else
		info.rotation = ROCKIVA_IMAGE_TRANSFORM_NONE;
	
	if (0 == pAttr->Level) //低灵敏度
		info.sense = 30;
	else if (1 == pAttr->Level)
		info.sense = 50;
	else if (2 == pAttr->Level)
		info.sense = 90;
	
	info.areas.areaNum = pAttr->Region.Num;

	//使能区域检测根据设置的参数设置，最多4个区域，禁止区域检测就默认一个且整个图像画面
	LOG_INFO("level=%d,RegionEnable=%d Region.Num=%d\n",pAttr->Level,pAttr->RegionEnable,pAttr->Region.Num);
	if (pAttr->RegionEnable)
	{
		for (int i = 0; i < pAttr->Region.Num; i++)
		{
			//百分比
			region_x    = (pAttr->Region.RegionAttr[i] >> 24) & 0xFF;
			region_xlen = (pAttr->Region.RegionAttr[i] >> 16) & 0xFF;
			region_y    = (pAttr->Region.RegionAttr[i] >>  8) & 0xFF;
			region_ylen = (pAttr->Region.RegionAttr[i] >>  0) & 0xFF;

			LOG_INFO("[%d: %d,%d,%d,%d]\n",i,region_x,region_y,region_xlen,region_ylen);

			info.areas.areas[i].pointNum = 4;//4个点定义一个区域
			//万分比
			info.areas.areas[i].points[0].x = region_x*100;
			info.areas.areas[i].points[0].y = region_y*100;
			info.areas.areas[i].points[1].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[1].y = region_y*100;
			info.areas.areas[i].points[2].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[2].y = region_y*100 + region_ylen*100;
			info.areas.areas[i].points[3].x = region_x*100;
			info.areas.areas[i].points[3].y = region_y*100 + region_ylen*100;

			LOG_INFO("[%d: (%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n",i, info.areas.areas[i].points[0].x,info.areas.areas[i].points[0].y,
																info.areas.areas[i].points[1].x,info.areas.areas[i].points[1].y,
																info.areas.areas[i].points[2].x,info.areas.areas[i].points[2].y,
																info.areas.areas[i].points[3].x,info.areas.areas[i].points[3].y
																);
		}
	}
	else
	{
			//百分比
			region_x    = 0;
			region_xlen = 100;
			region_y    = 0;
			region_ylen = 100;
			int i = 0;
			LOG_INFO("[%d: %d,%d,%d,%d]\n",i,region_x,region_y,region_xlen,region_ylen);
			info.areas.areaNum = 1;
			info.areas.areas[i].pointNum = 4;//4个点定义一个区域
			//万分比
			info.areas.areas[i].points[0].x = region_x*100;
			info.areas.areas[i].points[0].y = region_y*100;
			info.areas.areas[i].points[1].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[1].y = region_y*100;
			info.areas.areas[i].points[2].x = region_x*100 + region_xlen*100;
			info.areas.areas[i].points[2].y = region_y*100 + region_ylen*100;
			info.areas.areas[i].points[3].x = region_x*100;
			info.areas.areas[i].points[3].y = region_y*100 + region_ylen*100;

			LOG_INFO("[%d: (%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n",i, info.areas.areas[i].points[0].x,info.areas.areas[i].points[0].y,
																info.areas.areas[i].points[1].x,info.areas.areas[i].points[1].y,
																info.areas.areas[i].points[2].x,info.areas.areas[i].points[2].y,
																info.areas.areas[i].points[3].x,info.areas.areas[i].points[3].y
																);
	}
	return  rkipc_rockiva_set(&info);
}

int gb_rkipc_video_det_start()
{
	LOG_INFO("gb_rkipc_video_det_start \n");
	s_gb_det_start = 1;
	return 0;
}

int gb_rkipc_video_det_stop()
{
	LOG_INFO("gb_rkipc_video_det_stop \n");
	s_gb_det_start = 0;
	return 0;
}
#endif

extern int iconv_utf8_to_wchar(const char *in, wchar_t *out);
extern int iconv_gbk_to_wchar(const char *in, wchar_t *out);
extern int generate_date_time(const char *fmt, wchar_t *result);
extern int generate_date_time_2(const char *fmt, wchar_t *result);

#define RKIPC_OSD_DEFAULT_FONT_SIZE 64
#define RKIPC_OSD_MAX_FONT_SIZE 64
#define RKIPC_OSD_AUTO_DOWNSAMPLE 8
#define RKIPC_OSD_AUTO_THRESHOLD 128
#define RKIPC_OSD_AUTO_VI_TIMEOUT_MS 10
#define RKIPC_OSD_COLOR_BLACK 0x000000
#define RKIPC_OSD_COLOR_WHITE 0xFFFFFF

static uint32_t s_osd_font_size = RKIPC_OSD_DEFAULT_FONT_SIZE;
static int osd_update_running;
static pthread_t osd_update_thread;
static void *s_osd_update_signal;
static pthread_mutex_t s_osd_auto_color_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_osd_color_auto_enabled;
static unsigned char *s_osd_auto_color_map;
static int s_osd_auto_color_map_w;
static int s_osd_auto_color_map_h;
static int s_osd_auto_color_frame_w;
static int s_osd_auto_color_frame_h;
static int s_osd_auto_color_downsample = RKIPC_OSD_AUTO_DOWNSAMPLE;
static int s_osd_auto_color_map_valid;

typedef struct {
	int region_x;
	int region_y;
	int main_width;
	int main_height;
} RKIPC_OSD_AUTO_COLOR_CTX_T;

static int rkipc_osd_normalize_font_size(int font_size)
{
	if (font_size == 16 || font_size == 32 || font_size == 64)
		return font_size;
	return RKIPC_OSD_DEFAULT_FONT_SIZE;
}

static int rkipc_osd_hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static uint32_t rkipc_osd_parse_rgb_hex(const char *color, uint32_t default_color)
{
	const char *p = color;
	uint32_t value = 0;

	if (!p)
		return default_color;
	if (p[0] == '#')
		p++;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;

	for (int i = 0; i < 6; i++) {
		int digit = rkipc_osd_hex_value(p[i]);
		if (digit < 0)
			return default_color;
		value = (value << 4) | (uint32_t)digit;
	}
	return value;
}

static int rkipc_osd_clamp_int(int value, int min_value, int max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static int rkipc_osd_aligned_x(const RGN_OSD_PARAM_T *param, int bitmap_width, int stream_width)
{
	int x;

	if (!param)
		return 0;
	x = param->x;
	if (param->alignment == 1)
		x -= bitmap_width;
	if (stream_width > bitmap_width)
		return rkipc_osd_clamp_int(x, 0, stream_width - bitmap_width);
	return 0;
}

static void rkipc_osd_auto_color_free_locked(void)
{
	if (s_osd_auto_color_map) {
		free(s_osd_auto_color_map);
		s_osd_auto_color_map = NULL;
	}
	s_osd_auto_color_map_w = 0;
	s_osd_auto_color_map_h = 0;
	s_osd_auto_color_frame_w = 0;
	s_osd_auto_color_frame_h = 0;
	s_osd_auto_color_map_valid = 0;
}

static void rkipc_osd_auto_color_set_enabled(int enabled)
{
	pthread_mutex_lock(&s_osd_auto_color_mutex);
	s_osd_color_auto_enabled = enabled ? 1 : 0;
	if (!s_osd_color_auto_enabled)
		rkipc_osd_auto_color_free_locked();
	else
		s_osd_auto_color_map_valid = 0;
	pthread_mutex_unlock(&s_osd_auto_color_mutex);
}

static int rkipc_osd_auto_color_is_enabled(void)
{
	int enabled;

	pthread_mutex_lock(&s_osd_auto_color_mutex);
	enabled = s_osd_color_auto_enabled;
	pthread_mutex_unlock(&s_osd_auto_color_mutex);

	return enabled;
}

static int rkipc_osd_auto_color_resize_locked(int frame_w, int frame_h, int downsample)
{
	int map_w;
	int map_h;
	unsigned char *map;

	if (frame_w <= 0 || frame_h <= 0 || downsample <= 0)
		return -1;

	map_w = (frame_w + downsample - 1) / downsample;
	map_h = (frame_h + downsample - 1) / downsample;
	if (map_w <= 0 || map_h <= 0)
		return -1;
	if (s_osd_auto_color_map && s_osd_auto_color_frame_w == frame_w &&
	    s_osd_auto_color_frame_h == frame_h && s_osd_auto_color_map_w == map_w &&
	    s_osd_auto_color_map_h == map_h && s_osd_auto_color_downsample == downsample)
		return 0;

	map = (unsigned char *)malloc(map_w * map_h);
	if (!map) {
		rkipc_osd_auto_color_free_locked();
		return -1;
	}

	rkipc_osd_auto_color_free_locked();
	s_osd_auto_color_map = map;
	s_osd_auto_color_map_w = map_w;
	s_osd_auto_color_map_h = map_h;
	s_osd_auto_color_frame_w = frame_w;
	s_osd_auto_color_frame_h = frame_h;
	s_osd_auto_color_downsample = downsample;

	return 0;
}

static void rkipc_osd_auto_color_fill_map_locked(const unsigned char *y_addr, int frame_w,
                                                 int frame_h, int stride, int downsample)
{
	for (int my = 0; my < s_osd_auto_color_map_h; my++) {
		int y0 = my * downsample;
		int y1 = rkipc_osd_clamp_int(y0 + downsample, 0, frame_h);
		for (int mx = 0; mx < s_osd_auto_color_map_w; mx++) {
			int x0 = mx * downsample;
			int x1 = rkipc_osd_clamp_int(x0 + downsample, 0, frame_w);
			unsigned int sum = 0;
			unsigned int count = 0;

			for (int y = y0; y < y1; y++) {
				const unsigned char *line = y_addr + y * stride;
				for (int x = x0; x < x1; x++) {
					sum += line[x];
					count++;
				}
			}
			s_osd_auto_color_map[my * s_osd_auto_color_map_w + mx] =
				count && (sum / count >= RKIPC_OSD_AUTO_THRESHOLD) ? 1 : 0;
		}
	}
	s_osd_auto_color_map_valid = 1;
}

static int rkipc_osd_auto_color_update(void)
{
	VIDEO_FRAME_INFO_S frame;
	unsigned char *y_addr;
	int frame_w;
	int frame_h;
	int stride;
	int ret;
	int release_ret;

	if (!rkipc_osd_auto_color_is_enabled())
		return 0;

	memset(&frame, 0, sizeof(frame));
	ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_1, &frame, RKIPC_OSD_AUTO_VI_TIMEOUT_MS);
	if (ret != RK_SUCCESS)
		return ret;

	y_addr = (unsigned char *)RK_MPI_MB_Handle2VirAddr(frame.stVFrame.pMbBlk);
	frame_w = frame.stVFrame.u32Width;
	frame_h = frame.stVFrame.u32Height;
	stride = frame.stVFrame.u32VirWidth ? frame.stVFrame.u32VirWidth : frame_w;
	if (y_addr && frame.stVFrame.enPixelFormat == RK_FMT_YUV420SP &&
	    frame.stVFrame.enCompressMode == COMPRESS_MODE_NONE && frame_w > 0 && frame_h > 0 &&
	    stride >= frame_w) {
		pthread_mutex_lock(&s_osd_auto_color_mutex);
		if (s_osd_color_auto_enabled &&
		    !rkipc_osd_auto_color_resize_locked(frame_w, frame_h, RKIPC_OSD_AUTO_DOWNSAMPLE))
			rkipc_osd_auto_color_fill_map_locked(y_addr, frame_w, frame_h, stride,
			                                     RKIPC_OSD_AUTO_DOWNSAMPLE);
		pthread_mutex_unlock(&s_osd_auto_color_mutex);
	}

	release_ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_1, &frame);
	if (release_ret != RK_SUCCESS)
		LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", release_ret);

	return ret;
}

static int rkipc_osd_auto_color_map_is_bright_locked(int frame_x, int frame_y)
{
	int map_x;
	int map_y;

	if (!s_osd_auto_color_map_valid || !s_osd_auto_color_map ||
	    s_osd_auto_color_map_w <= 0 || s_osd_auto_color_map_h <= 0 ||
	    s_osd_auto_color_downsample <= 0)
		return 0;

	frame_x = rkipc_osd_clamp_int(frame_x, 0, s_osd_auto_color_frame_w - 1);
	frame_y = rkipc_osd_clamp_int(frame_y, 0, s_osd_auto_color_frame_h - 1);
	map_x = rkipc_osd_clamp_int(frame_x / s_osd_auto_color_downsample, 0,
	                            s_osd_auto_color_map_w - 1);
	map_y = rkipc_osd_clamp_int(frame_y / s_osd_auto_color_downsample, 0,
	                            s_osd_auto_color_map_h - 1);

	return s_osd_auto_color_map[map_y * s_osd_auto_color_map_w + map_x] ? 1 : 0;
}

static unsigned int rkipc_osd_auto_color_judge(void *userdata, int x, int y, int w, int h,
                                               unsigned int default_color)
{
	RKIPC_OSD_AUTO_COLOR_CTX_T *ctx = (RKIPC_OSD_AUTO_COLOR_CTX_T *)userdata;
	int sample_points[5][2];
	unsigned int color = RKIPC_OSD_COLOR_WHITE;

	if (!ctx)
		return default_color;

	if (w <= 0)
		w = 1;
	if (h <= 0)
		h = 1;

	sample_points[0][0] = x + w / 2;
	sample_points[0][1] = y + h / 2;
	sample_points[1][0] = x;
	sample_points[1][1] = y;
	sample_points[2][0] = x + w - 1;
	sample_points[2][1] = y;
	sample_points[3][0] = x;
	sample_points[3][1] = y + h - 1;
	sample_points[4][0] = x + w - 1;
	sample_points[4][1] = y + h - 1;

	pthread_mutex_lock(&s_osd_auto_color_mutex);
	if (!s_osd_color_auto_enabled) {
		pthread_mutex_unlock(&s_osd_auto_color_mutex);
		return default_color;
	}
	if (!s_osd_auto_color_map_valid || !s_osd_auto_color_map ||
	    ctx->main_width <= 0 || ctx->main_height <= 0 ||
	    s_osd_auto_color_frame_w <= 0 || s_osd_auto_color_frame_h <= 0) {
		pthread_mutex_unlock(&s_osd_auto_color_mutex);
		return color;
	}

	for (int i = 0; i < 5; i++) {
		int main_x = ctx->region_x + sample_points[i][0];
		int main_y = ctx->region_y + sample_points[i][1];
		int frame_x;
		int frame_y;

		main_x = rkipc_osd_clamp_int(main_x, 0, ctx->main_width - 1);
		main_y = rkipc_osd_clamp_int(main_y, 0, ctx->main_height - 1);
		frame_x = main_x * s_osd_auto_color_frame_w / ctx->main_width;
		frame_y = main_y * s_osd_auto_color_frame_h / ctx->main_height;
		if (rkipc_osd_auto_color_map_is_bright_locked(frame_x, frame_y)) {
			color = RKIPC_OSD_COLOR_BLACK;
			break;
		}
	}
	pthread_mutex_unlock(&s_osd_auto_color_mutex);

	return color;
}

static void rkipc_osd_draw_argb8888_text(unsigned char *bmp_buffer, int bmp_width, int bmp_height,
                                         const wchar_t *wch_text, const RGN_OSD_PARAM_T *param,
                                         int main_display_x)
{
	RKIPC_OSD_AUTO_COLOR_CTX_T auto_ctx;

	set_font_color(param->font_color);
	if (!rkipc_osd_auto_color_is_enabled()) {
		draw_argb8888_text(bmp_buffer, bmp_width, bmp_height, wch_text);
		return;
	}

	auto_ctx.region_x = UPALIGNTO16(main_display_x);
	auto_ctx.region_y = UPALIGNTO16(param->y);
	auto_ctx.main_width = s_stream_venc_chan_param[0].width;
	auto_ctx.main_height = s_stream_venc_chan_param[0].height;
	draw_argb8888_text_with_color_callback(bmp_buffer, bmp_width, bmp_height, wch_text,
	                                       &auto_ctx, rkipc_osd_auto_color_judge);
}


static void *thread_osd_update(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "thread_osd_update", 0, 0, 0);

	RGN_OSD_PARAM_T *p_osd_time_param = (RGN_OSD_PARAM_T *)arg;

	int ret;
	int last_time_sec;	
	wchar_t wch_text[MAX_WCH_BYTE];
	int bmp_width;
	int bmp_height;
	int bmp_size;
	unsigned char *bmp_buffer = NULL;
	time_t rawtime;
	struct tm *cur_time_info;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;

	last_time_sec = 0;
	while (osd_update_running) {
		// only update time bmp
		rk_signal_wait(s_osd_update_signal, 100); // TODO: in xx s 00 ms update
		time(&rawtime);
		cur_time_info = localtime(&rawtime);
		if (cur_time_info->tm_sec == last_time_sec)
			continue;
		else
			last_time_sec = cur_time_info->tm_sec;
		rkipc_osd_auto_color_update();

		for (int i = 0; i < 8; i++)
		{
			pthread_mutex_lock(&p_osd_time_param[i].mutex);
			while (p_osd_time_param[i].changed)
			{
				printf("rgn[%d] change. text: %s, x: %d, y: %d, show: %d\n", i, p_osd_time_param[i].text, 
					p_osd_time_param[i].x, p_osd_time_param[i].y, p_osd_time_param[i].show);
				if (i == 0) {
					generate_date_time_2(p_osd_time_param[i].text, wch_text);
					bmp_width = UPALIGNTO16(wstr_get_actual_advance_x(wch_text));
					bmp_height = UPALIGNTO16(s_osd_font_size) + 16;
					p_osd_time_param[i].width = bmp_width;
					p_osd_time_param[i].height = bmp_height;
				} else if (p_osd_time_param[i].show && strlen(p_osd_time_param[i].text) > 0) {
					iconv_gbk_to_wchar(p_osd_time_param[i].text, wch_text);
					bmp_width = UPALIGNTO16(wstr_get_actual_advance_x(wch_text));
					bmp_height = UPALIGNTO16(s_osd_font_size) + 16;
					p_osd_time_param[i].width = bmp_width;
					p_osd_time_param[i].height = bmp_height;
				}
				int main_display_x = rkipc_osd_aligned_x(&p_osd_time_param[i],
					p_osd_time_param[i].width, s_stream_venc_chan_param[0].width);
				stMppChn.enModId = RK_ID_VENC;
				stMppChn.s32DevId = 0;
				//主码流
				stMppChn.s32ChnId = 0;
				ret = RK_MPI_RGN_GetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_GetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);
					break;
				}
				stRgnChnAttr.bShow = p_osd_time_param[i].show ? RK_TRUE : RK_FALSE;
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = UPALIGNTO16(main_display_x);
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = UPALIGNTO16(p_osd_time_param[i].y);
				ret = RK_MPI_RGN_SetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_SetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);		
					break;
				}
				//子码流
				stMppChn.s32ChnId = 1;
				ret = RK_MPI_RGN_GetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_GetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);
					break;
				}
				stRgnChnAttr.bShow = p_osd_time_param[i].show ? RK_TRUE : RK_FALSE;
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
					UPALIGNTO16(main_display_x * s_stream_venc_chan_param[1].width /
								s_stream_venc_chan_param[0].width);
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
					UPALIGNTO16(p_osd_time_param[i].y * s_stream_venc_chan_param[1].height /
								s_stream_venc_chan_param[0].height);
				ret = RK_MPI_RGN_SetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_SetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);		
					break;
				}
				//JPG抓图
				stMppChn.s32ChnId = s_jpeg_venc_chan_param[0].channel;
				ret = RK_MPI_RGN_GetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_GetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);
					break;
				}
				stRgnChnAttr.bShow = p_osd_time_param[i].show ? RK_TRUE : RK_FALSE;
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
					UPALIGNTO16(main_display_x * s_jpeg_venc_chan_param[0].width /
								s_stream_venc_chan_param[0].width);
				stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
					UPALIGNTO16(p_osd_time_param[i].y * s_jpeg_venc_chan_param[0].height /
								s_stream_venc_chan_param[0].height);
				ret = RK_MPI_RGN_SetDisplayAttr(p_osd_time_param[i].handle, &stMppChn, &stRgnChnAttr);
				if (RK_SUCCESS != ret)
				{
					LOG_ERROR("RK_MPI_RGN_SetDisplayAttr (%d) to venc-%d failed with %#x\n", p_osd_time_param[i].handle, stMppChn.s32ChnId, ret);		
					break;
				}

				//更新自定义文字显示
				if (i > 0 && p_osd_time_param[i].show && strlen(p_osd_time_param[i].text) > 0)
				{
//					iconv_utf8_to_wchar(p_osd_time_param[i].text, wch_text);
					iconv_gbk_to_wchar(p_osd_time_param[i].text, wch_text);
					bmp_width = UPALIGNTO16(wstr_get_actual_advance_x(wch_text));
					bmp_height = UPALIGNTO16(s_osd_font_size) + 16;
					p_osd_time_param[i].width = bmp_width;
					p_osd_time_param[i].height = bmp_height;
					bmp_size = bmp_width * bmp_height * 4; // BGRA8888 4byte
					bmp_buffer = malloc(bmp_size);
					if (bmp_buffer)
					{
						memset(bmp_buffer, 0, bmp_size);
						rkipc_osd_draw_argb8888_text(bmp_buffer, bmp_width, bmp_height,
						                             wch_text, &p_osd_time_param[i],
						                             main_display_x);
						
						// set bitmap
						stBitmap.enPixelFormat = RK_FMT_ARGB8888;
						stBitmap.u32Width = bmp_width;
						stBitmap.u32Height = bmp_height;
						stBitmap.pData = (RK_VOID *)bmp_buffer;
						ret = RK_MPI_RGN_SetBitMap(p_osd_time_param[i].handle, &stBitmap);
						if (ret != RK_SUCCESS) {
							LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x, handle: %d\n", ret, p_osd_time_param[i].handle);
						}		
						free(bmp_buffer);
					}
				}
				
				p_osd_time_param[i].changed = 0;
			}
			pthread_mutex_unlock(&p_osd_time_param[i].mutex);
		}

		pthread_mutex_lock(&p_osd_time_param[0].mutex);
		if (p_osd_time_param[0].show)
		{
			generate_date_time_2(p_osd_time_param[0].text, wch_text);
			bmp_width = UPALIGNTO16(wstr_get_actual_advance_x(wch_text));
			bmp_height = UPALIGNTO16(s_osd_font_size) + 16;
			p_osd_time_param[0].width = bmp_width;
			p_osd_time_param[0].height = bmp_height;
			bmp_size = bmp_width * bmp_height * 4; // BGRA8888 4byte
			bmp_buffer = malloc(bmp_size);
			if (bmp_buffer)
			{
				int main_display_x = rkipc_osd_aligned_x(&p_osd_time_param[0],
					bmp_width, s_stream_venc_chan_param[0].width);
				memset(bmp_buffer, 0, bmp_size);
				rkipc_osd_draw_argb8888_text(bmp_buffer, bmp_width, bmp_height,
				                             wch_text, &p_osd_time_param[0],
				                             main_display_x);

				// set bitmap
				stBitmap.enPixelFormat = RK_FMT_ARGB8888;
				stBitmap.u32Width = bmp_width;
				stBitmap.u32Height = bmp_height;
				stBitmap.pData = (RK_VOID *)bmp_buffer;
				ret = RK_MPI_RGN_SetBitMap(p_osd_time_param[0].handle, &stBitmap);
				if (ret != RK_SUCCESS) {
					LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x, handle: %d\n", ret, p_osd_time_param[0].handle);
				}		
				free(bmp_buffer);
			}
		}
		pthread_mutex_unlock(&p_osd_time_param[0].mutex);
	}
	LOG_INFO("exit\n");

	return 0;
}
int gb_rkipc_osd_time_create(RGN_OSD_PARAM_T *p_st_rgn_osd_time_param) {

	pthread_mutex_lock(&p_st_rgn_osd_time_param->mutex);

	int ret = 0;
	RGN_HANDLE RgnHandle = p_st_rgn_osd_time_param->handle;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;

	if (p_st_rgn_osd_time_param->inited)
	{
		pthread_mutex_unlock(&p_st_rgn_osd_time_param->mutex);
		return 0;
	}

	p_st_rgn_osd_time_param->max_width = 800;
	p_st_rgn_osd_time_param->max_height = UPALIGNTO16(RKIPC_OSD_MAX_FONT_SIZE) + 16;
	p_st_rgn_osd_time_param->width = p_st_rgn_osd_time_param->max_width;
	p_st_rgn_osd_time_param->height = p_st_rgn_osd_time_param->max_height;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.u32CanvasNum = 2;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = p_st_rgn_osd_time_param->max_width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = p_st_rgn_osd_time_param->max_height;
	printf("stRgnAttr.unAttr.stOverlay.stSize. u32Width: %u u32Height: %d\n", 
			stRgnAttr.unAttr.stOverlay.stSize.u32Width,
			stRgnAttr.unAttr.stOverlay.stSize.u32Height);
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		goto ERR_CREAT;
	}
	LOG_DEBUG("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = p_st_rgn_osd_time_param->show ? RK_TRUE : RK_FALSE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	int main_display_x = rkipc_osd_aligned_x(p_st_rgn_osd_time_param,
		p_st_rgn_osd_time_param->width, s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = UPALIGNTO16(main_display_x);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = UPALIGNTO16(p_st_rgn_osd_time_param->y);
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = RgnHandle;

	stMppChn.s32ChnId = 0;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");

	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		UPALIGNTO16(main_display_x * s_stream_venc_chan_param[1].width /
					s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		UPALIGNTO16(p_st_rgn_osd_time_param->y * s_stream_venc_chan_param[1].height /
					s_stream_venc_chan_param[0].height);
	stMppChn.s32ChnId = 1;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA_1;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc1 success\n");
	
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		UPALIGNTO16(main_display_x * s_jpeg_venc_chan_param[0].width /
					s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		UPALIGNTO16(p_st_rgn_osd_time_param->y * s_jpeg_venc_chan_param[0].height /
					s_stream_venc_chan_param[0].height);
	stMppChn.s32ChnId = s_jpeg_venc_chan_param[0].channel;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA_2;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to jpeg success\n");


	p_st_rgn_osd_time_param->inited = 1;
	pthread_mutex_unlock(&p_st_rgn_osd_time_param->mutex);

	return RK_SUCCESS;
	
//ERR_SIGL:
//	stMppChn.s32ChnId = s_jpeg_venc_chan_param[0].channel;
//	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA_2:
	stMppChn.s32ChnId = 1;
	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA_1:
	stMppChn.s32ChnId = 0;
	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA:
	RK_MPI_RGN_Destroy(RgnHandle);
ERR_CREAT:
	pthread_mutex_unlock(&p_st_rgn_osd_time_param->mutex);

	return RK_FAILURE;
}

int gb_rkipc_osd_text_create(RGN_OSD_PARAM_T *p_st_rgn_osd_text_param) {

	pthread_mutex_lock(&p_st_rgn_osd_text_param->mutex);
	int ret = 0;
	RGN_HANDLE RgnHandle = p_st_rgn_osd_text_param->handle;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;

	if (p_st_rgn_osd_text_param->inited /*|| strlen(p_st_rgn_osd_text_param->text) == 0*/)
	{
		pthread_mutex_unlock(&p_st_rgn_osd_text_param->mutex);
		return 0;
	}

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.u32CanvasNum = 2;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = p_st_rgn_osd_text_param->max_width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = p_st_rgn_osd_text_param->max_height;
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		goto ERR_CREAT;
	}
	LOG_DEBUG("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = p_st_rgn_osd_text_param->show ? RK_TRUE : RK_FALSE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	int main_display_x = rkipc_osd_aligned_x(p_st_rgn_osd_text_param,
		p_st_rgn_osd_text_param->width, s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = UPALIGNTO16(main_display_x);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = UPALIGNTO16(p_st_rgn_osd_text_param->y);
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = RgnHandle;

	stMppChn.s32ChnId = 0;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");

	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		UPALIGNTO16(main_display_x * s_stream_venc_chan_param[1].width /
					s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		UPALIGNTO16(p_st_rgn_osd_text_param->y * s_stream_venc_chan_param[1].height /
					s_stream_venc_chan_param[0].height);
	stMppChn.s32ChnId = 1;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA_1;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc1 success\n");
	
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		UPALIGNTO16(main_display_x * s_jpeg_venc_chan_param[0].width /
					s_stream_venc_chan_param[0].width);
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		UPALIGNTO16(p_st_rgn_osd_text_param->y * s_jpeg_venc_chan_param[0].height /
					s_stream_venc_chan_param[0].height);
	stMppChn.s32ChnId = s_jpeg_venc_chan_param[0].channel;
	ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
		goto ERR_ATTA_2;
	}
	LOG_DEBUG("RK_MPI_RGN_AttachToChn to jpeg success\n");

	p_st_rgn_osd_text_param->inited = 1;
	pthread_mutex_unlock(&p_st_rgn_osd_text_param->mutex);

	return RK_SUCCESS;
	
//ERR_SIGL:
//	stMppChn.s32ChnId = s_jpeg_venc_chan_param[0].channel;
//	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA_2:
	stMppChn.s32ChnId = 1;
	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA_1:
	stMppChn.s32ChnId = 0;
	RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
ERR_ATTA:
	RK_MPI_RGN_Destroy(RgnHandle);
ERR_CREAT:
	pthread_mutex_unlock(&p_st_rgn_osd_text_param->mutex);

	return RK_FAILURE;
}

int gb_rkipc_osd_init() {

	int ret;
	
//	create_font("/mnt/sdcard/simsun_cn_3000.ttf", s_osd_font_size);
	create_font("/oem/usr/share/simsun_cn_3000.ttf", s_osd_font_size);

	gb_rkipc_osd_time_create(&s_rgn_osd_param[0]);

	for (int i = 1; i < 8; i++)
		gb_rkipc_osd_text_create(&s_rgn_osd_param[i]);

	if (s_osd_update_signal)
		rk_signal_destroy(s_osd_update_signal);
	s_osd_update_signal = rk_signal_create(0, 1);
	if (!s_osd_update_signal) {
		LOG_ERROR("create signal fail\n");
		goto ERR_SIGL;
	}

	osd_update_running = 1;
	ret = pthread_create(&osd_update_thread, NULL, thread_osd_update, (void *)s_rgn_osd_param);
	if (ret)
	{
		osd_update_running = 0;
		LOG_ERROR("create thread_osd_update failed! ret=%d\n", ret);
		goto ERR_THREAD;
	}

	return 0;
	
ERR_THREAD:
	if (s_osd_update_signal)
	{
		rk_signal_destroy(s_osd_update_signal);
		s_osd_update_signal = NULL;
	}
ERR_SIGL:
	return -1;
}

int gb_rkipc_osd_common_set(int font_size, const char *font_color_mode, const char *font_color) {

	uint32_t color = 0xfff799;
	int auto_color = font_color_mode &&
		(!strcmp(font_color_mode, "auto") || !strcmp(font_color_mode, "1"));

	s_osd_font_size = rkipc_osd_normalize_font_size(font_size);
	if (auto_color)
		color = RKIPC_OSD_COLOR_WHITE;
	else
		color = rkipc_osd_parse_rgb_hex(font_color, color);

	set_font_size(s_osd_font_size);
	rkipc_osd_auto_color_set_enabled(auto_color);
	for (int i = 0; i < 8; i++) {
		pthread_mutex_lock(&s_rgn_osd_param[i].mutex);
		s_rgn_osd_param[i].font_color = color;
		s_rgn_osd_param[i].changed = 1;
		pthread_mutex_unlock(&s_rgn_osd_param[i].mutex);
	}

	return 0;
}

int gb_rkipc_osd_time_set(int date_type, int time_type, int display_week_enabled,
						  int x, int y, int show, int alignment) {

	char str_date_type[32] = {0};
	char str_time_type[32] = {0};
	char str_week_type[32] = {0};

	if (0 == date_type) {
		sprintf(str_date_type, "CHR-%s", OSD_FMT_YMD0);
	} else if (1 == date_type) {
		sprintf(str_date_type, "CHR-%s", OSD_FMT_YMD_DOT);
	} else if (2 == date_type) {
		sprintf(str_date_type, "CHR-%s", OSD_FMT_YMD3);
	} else {
		sprintf(str_date_type, "%s", OSD_FMT_YMD0);
	}

	if (0 == time_type)
		snprintf(str_time_type, sizeof(str_time_type), "24hour");
	else
		snprintf(str_time_type, sizeof(str_time_type), "12hour");
	if (display_week_enabled)
		snprintf(str_week_type, sizeof(str_week_type), " %s", OSD_FMT_WEEK0);
	
	pthread_mutex_lock(&s_rgn_osd_param[0].mutex);
	snprintf(s_rgn_osd_param[0].text, sizeof(s_rgn_osd_param[0].text),
			 "%s %s%s", str_time_type, str_date_type, str_week_type);
	s_rgn_osd_param[0].x = rkipc_osd_clamp_int(x, 0, s_stream_venc_chan_param[0].width);
	s_rgn_osd_param[0].y = rkipc_osd_clamp_int(y, 0, s_stream_venc_chan_param[0].height);
	s_rgn_osd_param[0].alignment = alignment == 1 ? 1 : 0;
	s_rgn_osd_param[0].show = show;

	s_rgn_osd_param[0].changed = 1;
	printf("gb_rkipc_osd_time_set -> %s %d %d %d %d\n", s_rgn_osd_param[0].text,
		s_rgn_osd_param[0].x, s_rgn_osd_param[0].y, s_rgn_osd_param[0].show,
		s_rgn_osd_param[0].alignment);
	pthread_mutex_unlock(&s_rgn_osd_param[0].mutex);

	return 0;
}

int gb_rkipc_osd_text_set(int index, const char *text, int x, int y, int show, int alignment) {

	if (index < 0 || index > 6 || !text)
		return -1;
	index += 1;
	
	pthread_mutex_lock(&s_rgn_osd_param[index].mutex);
	snprintf(s_rgn_osd_param[index].text, sizeof(s_rgn_osd_param[index].text), "%s", text);
	s_rgn_osd_param[index].x = rkipc_osd_clamp_int(x, 0, s_stream_venc_chan_param[0].width);
	s_rgn_osd_param[index].y = rkipc_osd_clamp_int(y, 0, s_stream_venc_chan_param[0].height);
	s_rgn_osd_param[index].alignment = alignment == 1 ? 1 : 0;
	s_rgn_osd_param[index].show = show;

	s_rgn_osd_param[index].changed = 1;
	printf("gb_rkipc_osd_text_set -> [%d] %s %d %d %d %d\n", index, s_rgn_osd_param[index].text,
		s_rgn_osd_param[index].x, s_rgn_osd_param[index].y, s_rgn_osd_param[index].show,
		s_rgn_osd_param[index].alignment);
	pthread_mutex_unlock(&s_rgn_osd_param[index].mutex);

	return 0;
}

int gb_rkipc_osd_attach(int des_chan) {

	int ret;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = des_chan;

	for (int i = 0; i < 8; i++)
	{
		pthread_mutex_lock(&s_rgn_osd_param[i].mutex);

		memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
		stRgnChnAttr.bShow = s_rgn_osd_param[i].show ? RK_TRUE : RK_FALSE;
		stRgnChnAttr.enType = OVERLAY_RGN;
		int main_display_x = rkipc_osd_aligned_x(&s_rgn_osd_param[i],
			s_rgn_osd_param[i].width, s_stream_venc_chan_param[0].width);
		if (0 == des_chan)
		{
			stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = UPALIGNTO16(main_display_x);
			stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = UPALIGNTO16(s_rgn_osd_param[i].y);
		}
		else
		{
			stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
				UPALIGNTO16(main_display_x * s_stream_venc_chan_param[1].width /
							s_stream_venc_chan_param[0].width);
			stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
				UPALIGNTO16(s_rgn_osd_param[i].y * s_stream_venc_chan_param[1].height /
							s_stream_venc_chan_param[0].height);
		}
		stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
		stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
		stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = s_rgn_osd_param[i].handle;

		ret = RK_MPI_RGN_AttachToChn(s_rgn_osd_param[i].handle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc-%d failed with %#x\n", s_rgn_osd_param[i].handle, des_chan, ret);
		}

		s_rgn_osd_param[i].changed = 1;

		pthread_mutex_unlock(&s_rgn_osd_param[i].mutex);
	}
	return 0;
}

int gb_rkipc_osd_detach(int des_chan) {

	int ret;
	MPP_CHN_S stMppChn;

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = des_chan;

	for (int i = 0; i < 8; i++)
	{
		pthread_mutex_lock(&s_rgn_osd_param[i].mutex);

		ret = RK_MPI_RGN_DetachFromChn(s_rgn_osd_param[i].handle, &stMppChn);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_DetachFromChn (%d) to venc-%d failed with %#x\n", s_rgn_osd_param[i].handle, des_chan, ret);
		}

		pthread_mutex_unlock(&s_rgn_osd_param[i].mutex);
	}
	return 0;
}
