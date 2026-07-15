
#ifndef __PAL_CAPTURE_H__
#define __PAL_CAPTURE_H__

#include "Types.h"
#include "PAL/MotionDetect.h"
#include "PAL/libdmc.h"

#ifdef __cplusplus
extern "C" {
#endif

//图像旋转属
typedef enum ROTATE_ATTR_e
{
	RA_NONE = 0,
	RA_90,
	RA_180,
	RA_270,
	RA_BUTT
}RotateAttr_t;

//检测目标类型
typedef enum {
    DETECT_OBJECT_TYPE_NONE = 0,
    DETECT_OBJECT_OBJECT_TYPE_PERSON = 1,
    DETECT_OBJECT_OBJECT_TYPE_VEHICLE = 2,
    DETECT_OBJECT_OBJECT_TYPE_NON_VEHICLE = 3,
    DETECT_OBJECT_OBJECT_TYPE_FACE = 4,
    DETECT_OBJECT_OBJECT_TYPE_MAX
} DETECT_OBJECT_TYPE;

//检测目标点
typedef struct tagDETECT_POINT{
    unsigned int x; /*万分之一*/
    unsigned int y; /*万分之一*/
} DETECT_POINT;

//检测目标点坐标
typedef struct {
    DETECT_POINT topLeft;     /*左上*/
    DETECT_POINT bottomRight; /*右下*/
} DETECT_RECT;

//检测结果
typedef struct tagDETECT_RESULT
{
	int ObjectType; //DETECT_OBJECT_TYPE
	int Status;
	DETECT_RECT Rect;
} DETECT_RESULT;

typedef void (* CaptureDetectCallback)(int status, DETECT_RESULT result);
typedef struct{
	int width;
	int height;
	int size;
	char *data;
} DET_PIC_S;
typedef struct {
    int s32Xpos;   /* <Horizontal coordinate */
    int s32Ypos;   /* <Vertical coordinate */
    unsigned int u32Width;  /* <Width */
    unsigned int u32Height; /* <Height */
} DET_RECT_S;
typedef struct{
	DETECT_OBJECT_TYPE type;
	DET_RECT_S rect;
	DET_PIC_S pic;
} DET_THUM_S;
typedef void (* CaptureDetectSnapCallback)(DET_PIC_S *snap, DET_THUM_S *thum, int thum_num);

typedef struct tagDETECT_ATTR
{
	int ObjectType;
	int Level;
	CaptureDetectCallback Callback;
} DETECT_ATTR;

typedef struct tagREGIONS_ATTR
{
	int Num;
	unsigned int RegionAttr[4];
} REGION_ATTR;

typedef struct tagDETECT_INIT
{
	RotateAttr_t Rotate;
	int Level;//1-100
	int RegionEnable;
	REGION_ATTR Region;
} DETECT_INIT;


//移动追踪检测信息
typedef struct 
{
    int ObjectType; //DETECT_OBJECT_TYPE
	DETECT_RECT Rect;
} MOTION_TRACKER_INFO;

//移动追踪检测结果
typedef struct 
{
	int Status;                       /* 为0数据有效 */
    int ObjNum;                       /* 目标个数 */
    MOTION_TRACKER_INFO ObjInfo[128]; /* 各目标检测信息 */
} MOTION_TRACKER_RESULT;

typedef void (* CaptureMotionTrackerCallback)(MOTION_TRACKER_RESULT result);


// 整数坐标点
typedef struct {
    int x;
    int y;
} CmiotPoint_t;

// 轴对齐矩形：min左上角，max右下角
typedef struct {
    CmiotPoint_t min;
    CmiotPoint_t max;
} CmiotRect_t;

// 任意四边形（不规则4点区域）
typedef struct {
    CmiotPoint_t p[4];
} CmiotQuad_t;
/*
 *@param type 1-人形 2-入侵 3-运动
 */
typedef void(* cmoit_intrude_event_start_callback_t)(int type, char *pic, int pic_size, uint64_t utcms);
typedef void(* cmoit_intrude_event_stop_callback_t)(int type, uint64_t utcms);

int AVSetLogLevel(int level);
int AvInit(float sd,int ispmode);
int AvRelease();
int CaptureGetChannels(void);
int CaptureSetStreamCallBack(char *module_name, int media_type, dmc_media_input_fn proc);
int CaptureInitEncParam(int channel, int enc_type, int bit_rate, int frmae_rate, int gop);
int CaptureChangeEncParam(int channel, int enc_type, int bit_rate, int frmae_rate, int gop);
int CaptureCreate(int channel);
int CaptureDestroy(int channel);
int CaptureStart(int  channel, unsigned int dwType);
int CaptureStop(int  channel, unsigned int dwType);

int CaptureForceIFrame(int  channel, unsigned int dwType);
int CaptureSetISPMode(int iMode);
int CaptureSetBitRate(int Channel, int iTargetBitRate);
int CaptureSetRotate(int enRotate);
int CaptureSetMirrorAndFlip(unsigned char mirror, unsigned char flip);
int CaptureSetfps(int fps);
int CaptureGetResolution(int stream_id, int *pWith,int *pHeight);

unsigned int CaptureGetEncodeFrameCount(int Channel);
int CaptureSnapshotGetBuffer(char *buffer, int size, int timeout_s);

int CaptureSetWdr(int onoff);
int CaptureSetOSDSwitch(int onoff);
int CaptureSetAntiFlicker(int antiflicker);

int CaptureSetSaturation();
int CaptureSetContrast();
int CaptureSetSharpness();
int CaptureSetPwrFrequency();

int CaptureSetEptz(int scale); //eptz

int CaptureDetectInit(DETECT_INIT *pAttr);
int CaptureDetectDeInit();
int CaptureDetectStart();
int CaptureDetectStop();
int CaptureDetectSet(DETECT_INIT *pAttr);
int CaptureDetectGet(DETECT_INIT *pAttr);
int CaptureDetectObjectStart(DETECT_ATTR *pAttr);
int CaptureDetectObjectStop(int ObjectType);
int CaptureDetectObjectSetSnapCb(CaptureDetectSnapCallback cb);

int CaptureGetMeanLuma(float *value);

int CaptureMotionTrackerStart(CaptureMotionTrackerCallback cb);
int CaptureMotionTrackerStop();

#ifdef __cplusplus
}
#endif

#endif

