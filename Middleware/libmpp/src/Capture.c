#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>

#include "PAL/Capture.h"
#include "PAL/MotionTrack.h"
#include "PAL/Camera.h"
#include "PAL/MW_Common.h"
#include "PAL/Misc.h"
#include "PAL/QrCode.h"
#include "PAL/Audio.h"
#include "PAL/Motor.h"

// 平台库头文件
#include "common.h"
#include "isp.h"
#include "osd.h"
#include "region_clip.h"
#include "rockiva.h"
#include "roi.h"
#include "rtmp.h"
#include "rtsp.h"
// #include "storage.h"

#include "log.h"
#include "network.h"
#include "param.h"

#include "system.h"
#include "video.h"

#include <rga/im2d.h>
#include <rga/rga.h>
#include <rk_comm_tde.h>
#include <rk_debug.h>
#include <rk_mpi_avs.h>
#include <rk_mpi_cal.h>
#include <rk_mpi_ivs.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_mmz.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_tde.h>
#include <rk_mpi_venc.h>
#include <rk_mpi_vi.h>
#include <rk_mpi_vpss.h>

int enable_minilog = 0;
int rkipc_log_level = LOG_LEVEL_ERROR;//LOG_LEVEL_INFO;LOG_LEVEL_ERROR
static float stitch_distance = 5;
char *rkipc_ini_path_ = NULL;
char *rkipc_iq_file_path_ = NULL;

pthread_mutex_t MUTEX_LOG;
void log_lock(bool lock, void* udata)
{
    pthread_mutex_t *LOCK = (pthread_mutex_t*)(udata);
    if (lock)
        pthread_mutex_lock(LOCK);
    else
        pthread_mutex_unlock(LOCK);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// 下面是 与上层的接口
///////////////////////////////////////////////////////////////////////////////////////////////////
int AVSetLogLevel(int level)
{
	log_set_quiet(false);
    log_set_level(level);
    pthread_mutex_init(&MUTEX_LOG, NULL);
    log_set_lock(log_lock, &MUTEX_LOG);
    /* Insert threaded application code here... */
    log_info("I'm threadsafe use log %s\n",LOG_VERSION);

	// rkipc_log_level = level;

	return 0;
}
int AvInit(float sd,int ispmode)
{
	MSG("Middleware AvInit stitch_distance = %f ispmode = %d\n",sd,ispmode);
	stitch_distance = sd;

	MSG("Middleware DMC_Init\n");
	dmc_init();

	rkipc_version_dump();

	rkipc_ini_path_ = "/oem/usr/bin/rkipc.ini";
	rkipc_iq_file_path_ = "/oem/usr/share/iqfiles";

	if (ispmode)
	{
		rkipc_iq_file_path_ = "/mnt/sdcard/iqfiles";
	}

	MSG("rkipc_ini_path_ is %s, rkipc_iq_file_path_ is %s, rkipc_log_level is %d\n", rkipc_ini_path_, rkipc_iq_file_path_, rkipc_log_level);
	
	// init
	rk_param_init(rkipc_ini_path_);

	// WX
	#if 0
	if (rk_param_get_int("isp:group_mode", 1)) {
		rk_isp_group_init(0, rkipc_iq_file_path_);
		rk_isp_set_frame_rate(0, rk_param_get_int("isp.0.adjustment:fps", 30));
	} else {
		rk_isp_init(0, rkipc_iq_file_path_);
		rk_isp_init(1, rkipc_iq_file_path_);
		rk_isp_set_frame_rate(0, rk_param_get_int("isp.0.adjustment:fps", 30));
		rk_isp_set_frame_rate(1, rk_param_get_int("isp.0.adjustment:fps", 30));
	}
	#else
	if (rk_param_get_int("video.source:enable_aiq", 1)) {
		rk_isp_init(0, rkipc_iq_file_path_);
		rk_isp_set_frame_rate(0, rk_param_get_int("isp.0.adjustment:fps", 30));
//		rk_isp_set_frame_rate(0, 30); //shang
//		if (rk_param_get_int("isp:init_form_ini", 1))
//			rk_isp_set_from_ini(0);
	}
	#endif

	RK_MPI_SYS_Init();

	// rk_isp_set_frame_rate_without_ini(0, rk_param_get_int("isp.0.adjustment:fps", 15));

	// system("echo \"all=0\" > /tmp/rt_log_level");//数字0~3分别对应ERROR、WARN、INFO、DEBUG四个级别
	return 0;
}

int AvRelease()
{
	if (rk_param_get_int("isp:group_mode", 1)) {
		rk_isp_group_deinit(0);
	} else {
		rk_isp_deinit(1);
		rk_isp_deinit(0);
	}
	
	RK_MPI_SYS_Exit();

	return 0;
}
int CaptureSetStreamCallBack(char *module_name, int media_type, dmc_media_input_fn proc)
{
	if (proc)
	{
		return dmc_subscribe(module_name, media_type, proc);
	}
	else
	{
		return dmc_unsubscribe(module_name, media_type);
	}
}

int CaptureGetResolution(int stream_id, int *pWith, int *pHeight)
{
	int width = 0;
	int height = 0;
	int ret = -1;

	ret = rk_video_get_resolution_v10(stream_id, &width, &height);
	if (0 == ret)
	{
		*pWith = width;
		*pHeight = height;
		return 0;
	}
	else
	{
		return -1;
	}
}

int CaptureGetChannels(void)
{
	MSG("Middleware CaptureGetChannels\n");
	return 1;
}

int dayornight = 0;
static void smart_ir_status(int status)
{
	//MSG("=================dayornight : [%d]===============\n",status);
	dayornight = status;
}

// 在 CaptureCreate 之前调用
int s_is_video_inited = 0;
int g_video_chn_0_enc_param_inited = 0;
int g_video_chn_0_enc_type;			//0-264 1-265
int g_video_chn_0_bit_rate;			//码率
int g_video_chn_0_frmae_rate; 		//帧率
int g_video_chn_0_gop;				//I帧间隔
int g_video_chn_1_enc_param_inited = 0;
int g_video_chn_1_enc_type;			//0-264 1-265
int g_video_chn_1_bit_rate;			//码率
int g_video_chn_1_frmae_rate; 		//帧率
int g_video_chn_1_gop;				//I帧间隔
int CaptureInitEncParam(int channel, int enc_type, int bit_rate, int frmae_rate, int gop)
{
	if (0 == channel)
	{
		g_video_chn_0_enc_param_inited = 1;
		g_video_chn_0_enc_type = enc_type;
		g_video_chn_0_bit_rate = bit_rate;
		g_video_chn_0_frmae_rate = frmae_rate;
		g_video_chn_0_gop = gop;
		
		my_video_init_param_2(channel, enc_type, bit_rate, frmae_rate, gop);
		return 0;
	}
	if (1 == channel)
	{
		g_video_chn_1_enc_param_inited = 1;
		g_video_chn_1_enc_type = enc_type;
		g_video_chn_1_bit_rate = bit_rate;
		g_video_chn_1_frmae_rate = frmae_rate;
		g_video_chn_1_gop = gop;

		my_video_init_param_2(channel, enc_type, bit_rate, frmae_rate, gop);
		return 0;
	}

	return -1;
}

int CaptureChangeEncParam(int channel, int enc_type, int bit_rate, int frmae_rate, int gop)
{
	if (0 == s_is_video_inited)
		return -1;

//	int old_enc_type;
//	if (0 == channel && g_video_chn_0_enc_param_inited)
//	{
//		old_enc_type = g_video_chn_0_enc_type;
//		g_video_chn_0_enc_type = enc_type;
//		g_video_chn_0_bit_rate = bit_rate;
//		g_video_chn_0_frmae_rate = frmae_rate;
//		g_video_chn_0_gop = gop;
//		if (old_enc_type != enc_type)
//			my_video_0_restart();
//		else
//			my_video_set_param(0);
//	}
//	if (1 == channel && g_video_chn_1_enc_param_inited)
//	{
//		old_enc_type = g_video_chn_1_enc_type;
//		g_video_chn_1_enc_type = enc_type;
//		g_video_chn_1_bit_rate = bit_rate;
//		g_video_chn_1_frmae_rate = frmae_rate;
//		g_video_chn_1_gop = gop;
//		if (old_enc_type != enc_type)
//			my_video_1_restart();
//		else
//			my_video_set_param(0);
//	}

	MSG("CaptureChangeEncParam ->channel[%d], enc_type[%d], bit_rate[%d], frmae_rate[%d], gop[%d]\n", 
		channel, enc_type, bit_rate, frmae_rate, gop);
	my_video_set_param_2(channel, enc_type, bit_rate, frmae_rate, gop);

	return 0;
}


int CaptureCreate(int channel)
{
	MSG("CaptureCreate CaptureCreate ch %d\n", channel);
	
//	rk_video_set_stitch_distance(stitch_distance);
//	MSG("CaptureCreate rk_video_init stitch_distance = %f\n",stitch_distance);

	rk_video_init();

//	MSG("CaptureCreate rk_smart_ir_start\n");
//	rk_smart_ir_start(smart_ir_status,50);
//	MSG("CaptureCreate rk_smart_ir_start end\n");

	// rk_video_set_stitch_distance(stitch_distance);
	// MSG("CaptureCreate rk_video_init stitch_distance = %f\n",stitch_distance);

	s_is_video_inited = 1;

	return 0;
}

int CaptureDestroy(int channel)
{
	MSG("CaptureCreate rk_smart_ir_stop\n");
	rk_smart_ir_stop();
	MSG("CaptureCreate rk_video_deinit\n");
	rk_video_deinit();

	return 0;
}

int CaptureForceIFrame(int channel, unsigned int dwType)
{
	MSG("Middleware CaptureForceIFrame,channel:%d\n", channel);

	if ((0 != channel) && (1 != channel))
		return -1;
	int ret = rk_video_set_venc_force_idr(channel);
	if (ret != 0)
	{
		MSG("Request IDR error ret =0x%x\n", ret);
		return -1;
	}
	MSG("Request IDR ok\n");

	return 0;
}

int CaptureSetfps(int fps)
{
	return  0;
}

int CaptureSetRotate(int enRotate)
{
	printf("===not use CaptureSetRotate===\n");
	return 0 ;
	MSG("Middleware CaptureSetRotate\n");
	int ret = 0;
	int vlaue = 0;

	if (RA_270 == enRotate)
		vlaue = 270;
	else if (RA_NONE == enRotate)
		vlaue = 0;
	else if (RA_180 == enRotate)
		vlaue = 180;
	else if (RA_90 == enRotate)
		vlaue = 90;
	else
		vlaue = 0;

	MSG("rk_video_set_rotation enRotate=%d ret=0x%x\n", enRotate, ret);

	ret = rk_video_set_rotation(vlaue);
	if (ret != 0)
	{
		EMSG("rk_video_set_rotation error! enRotate=%d ret=0x%x\n", enRotate, ret);
	}

	return 0;
}

int CaptureSetMirrorAndFlip(unsigned char mirror, unsigned char flip)
{
	MSG("Middleware CaptureSetMirrorAndFlip mirror: %d, flip: %d\n", mirror, flip);
	int ret = 0;

	ret = rk_video_set_vi_mirror_flip(mirror, flip);
	if (ret != 0)
	{
		EMSG("rk_video_set_vi_mirror_flip error! ret=0x%x\n", ret);
		return -1;
	}

	return 0;
}

int CaptureSet3Dnr()
{
	return 0;
}

int CaptureSetWdr(int onoff)
{
	return 0;
}

int CaptureSetOSDSwitch(int onoff)
{
	rkipc_osd_show(onoff); //250121 add
	return 0;
}

int CaptureSetSaturation()
{
	return 0;
}

int CaptureSetContrast()
{
	return 0;
}

int CaptureSetSharpness()
{
	return 0;
}

int CaptureSetPwrFrequency()
{
	return 0;
}

int CaptureStart(int channel, unsigned int dwType)
{
	return 0;
}

int CaptureStop(int channel, unsigned int dwType)
{
	return 0;
}

int CaptureSetAntiFlicker(int antiflicker)
{
	MSG("Middleware CaptureSetAntiFlicker\n");
	rk_isp_setAntiFlicker(0,antiflicker);
}

int CaptureSetBitRate(int Channel, int iTargetBitRate)
{
	return 0;
}

// Channel : 0 - main 	1 - sub
unsigned int CaptureGetEncodeFrameCount(int Channel)
{
	if (0 == Channel)
		return rkipc_get_venc_count(0);
	else
		return rkipc_get_venc_count(1);
}

int CaptureSnapshotGetBuffer(char *buffer, int size, int timeout_s)
{
	MSG("Middleware CaptureSnapshotGetBuffer size:%d,timeout:%d\n",size,timeout_s);
	return  rk_take_photo_v10(buffer,size,timeout_s);
}

int CaptureSetEptz(int scale)
{
	MSG("Middleware CaptureSetEptz scale:%d\n",scale);
	return rk_video_set_eptz(scale);
}

int CaptureDetectInit(DETECT_INIT *pAttr)
{
	MSG("Middleware CaptureDetectInit\n");
	return rkipc_video_det_init(pAttr);
}

int CaptureDetectDeInit()
{
	MSG("Middleware CaptureDetectDeInit\n");
	return  rkipc_video_det_deinit();
}

int CaptureDetectSet(DETECT_INIT *pAttr)
{
	MSG("Middleware CaptureDetectSet \n");
	return rkipc_video_det_set(pAttr);
}

int CaptureDetectGet(DETECT_INIT *pAttr)
{
	MSG("Middleware CaptureDetectGet \n");
	return rkipc_video_det_get(pAttr);
}

int CaptureDetectStart()
{
	MSG("Middleware CaptureDetecStart \n");
	return rkipc_video_det_start();
}
int CaptureDetectStop()
{
	MSG("Middleware CaptureDetecStop \n");
	return rkipc_video_det_stop();
}

int CaptureDetectObjectStart(DETECT_ATTR *pAttr)
{
	MSG("Middleware CaptureDetectObjectStart \n");
	if (!pAttr)
	{
		EMSG("Middleware CaptureDetectObjectStart error\n");
		return -1;
	}
	MSG("Middleware CaptureDetectObjectStart type:%d\n",pAttr->ObjectType);
	return rkipc_video_det_obj_start(pAttr->Level,pAttr->ObjectType,pAttr->Callback);
}

int CaptureDetectObjectStop(int ObjectType)
{
	MSG("Middleware CaptureDetectObjectStop type:%d\n",ObjectType);
	return rkipc_video_det_obj_stop(ObjectType);
}

int CaptureMotionTrackerStart(CaptureMotionTrackerCallback cb)
{
	MSG("Middleware CaptureMotionTrackerStart\n");
	if (!cb)
	{
		EMSG("Middleware CaptureMotionTrackerStart error\n");
		return -1;
	}

	return rkipc_video_motion_tracker_start(cb);
}

int CaptureMotionTrackerStop()
{
	MSG("Middleware CaptureMotionTrackerStop\n");
	return rkipc_video_motion_tracker_stop();
}

int CaptureGetMeanLuma(float *value)
{
	// MSG("Middleware CaptureGetMeanLuma\n");
	return rkipc_video_get_meanluma(value);
}
