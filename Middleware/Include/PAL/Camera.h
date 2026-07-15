#ifndef _INCLUDE_PAL_CAMERA_H
#define _INCLUDE_PAL_CAMERA_H


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//网络摄像头参数
struct CameraParam				// HI3518平台
{
	int				rotateAttr;			// 图像旋转属性(关闭/水平/垂直/180度旋转)
	//add by shang 2020.06.01
	int				wdrSwitch;		// 宽动态开关
	int				osdSwitch;		// 时间水印开关
	int				nightVisionSwitch; 	//夜视开关
	int				nightVisionMode; 	// 夜视模式  单光 0-自动     1-关夜视   2-开夜视
										//           双光 0-红外夜视 1-全彩夜视 2-智能夜视
	int 			iAntiFlicker;		// 防闪烁  0-关闭 1-50HZ 2-60HZ
	int mirror;
	int flip;
};
//多个摄像头的参数
struct CameraParamAll
{
	struct CameraParam vCameraParamAll[1];
};
// 获取当前亮度和统计值
int VideoGetChnLuma(int Chn, unsigned int *pU32Lum);
// 获取光敏电阻采集数据
int ExSystemGetADCResult(unsigned int *pU32Result);

#ifdef __cplusplus
}
#endif // __cplusplus


#endif //_INCLUDE_PAL_CAMERA_H
