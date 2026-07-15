#ifndef __AUDIO_PROMPT_H__
#define __AUDIO_PROMPT_H__

#include "Common.h"

#include <deque>


//系统提示音
#define AUDIO_FILE_IAT 					"/oem/usr/audio/iat.g711a" 				//启动生产测试模式
#define AUDIO_FILE_NOTHING				"/oem/usr/audio/nothing.g711a"
#define AUDIO_FILE_DINGDINGDING 		"/oem/usr/audio/DingDingDing.g711a" 	//叮叮叮
#define AUDIO_FILE_DIDI 				"/oem/usr/audio/a_o.g711a" 				//啊哦
#define AUDIO_FILE_SIREN1               "/oem/usr/audio/siren1.g711a"
#define AUDIO_FILE_SIREN2               "/oem/usr/audio/siren2.g711a"
#define AUDIO_FILE_SIREN3               "/oem/usr/audio/siren3.g711a"
#define AUDIO_FILE_SIREN4               "/oem/usr/audio/siren4.g711a"
#define AUDIO_FILE_SIREN5               "/oem/usr/audio/siren5.g711a"
#define AUDIO_FILE_SIREN6               "/oem/usr/audio/siren6.g711a"

//自定义提示音
#define AUDIO_FILE_AP_ESTABLISHED 		"/userdata/audio/ap-jianliwancheng.g711a" 					//AP建立完成
#define AUDIO_FILE_AP_ESTABLISHING 		"/userdata/audio/ap-jianlizhong.g711a" 						//AP建立中
#define AUDIO_FILE_DEV_RESET 			"/oem/usr/audio/chongzhishexiangjichenggong.g711a" 		//重置摄像机成功
#define AUDIO_FILE_QRCODE_GET_COMPLETE 	"/userdata/audio/erweimasaomiaochenggong.g711a" 			//二维码扫描成功
#define AUDIO_FILE_QRCODE_INVALID 		"/userdata/audio/erweimwuxiao-qingchongzhi.g711a" 			//二维码无效，请重试
#define AUDIO_FILE_SD_FORMAT_FAILED 	"/userdata/audio/geshihuashibai.g711a" 						//格式化失败
#define AUDIO_FILE_SD_FORMAT_COMPLETE 	"/userdata/audio/geshihuawancheng.g711a" 					//格式化完成
#define AUDIO_FILE_DEV_UPDATING 		"/oem/usr/audio/gujianshengjizhong-qingshaodeng.g711a" 				//固件升级中，请稍等
#define AUDIO_FILE_WIFI_CONNECTING 		"/userdata/audio/lianjiezhong.g711a" 						//连接中，请稍等
#define AUDIO_FILE_PLEASE_SET_WIFI 		"/userdata/audio/qingpeizhiwifi.g711a" 						//请配置WIFI
#define AUDIO_FILE_SD_ERROR 			"/userdata/audio/sd-kayichang.g711a" 						//SD卡异常
#define AUDIO_FILE_DEV_UPDATE_COMPLETE 	"/userdata/audio/shengjiwancheng.g711a" 					//升级完成
#define AUDIO_FILE_WIFI_CONNECTED 		"/oem/usr/audio/wifi-lianjiechenggong.g711a" 				//WIFI连接成功
#define AUDIO_FILE_WIFI_INCORRECT 		"/userdata/audio/wifi-mimacuowu.g711a" 						//WIFI密码错误
#define AUDIO_FILE_WIFI_CONNECT_ERROR 	"/userdata/audio/wifi-connecterror.g711a" 					//WIFI连接失败
#define AUDIO_FILE_BIND_SUCCESS         "/oem/usr/audio/bindsuccess.g711a" 						//绑定平台成功
#define AUDIO_FILE_WIFI_MODULE_ERROR 	"/userdata/audio/WiFi-mokuaiyichang.g711a" 					//WIFI模块异常
#define AUDIO_FILE_SYSTEM_STARTING 		"/oem/usr/audio/xitongqidongzhong.g711a" 					//系统启动中
#define AUDIO_FILE_SD_FORMATING 		"/userdata/audio/zhengzaigeshihua-qingshaodeng.g711a" 		//正在格式化，请稍等
#define AUDIO_FILE_SIREN 				"/userdata/audio/jingdi.g711a" 								//警笛声
#define AUDIO_FILE_DINGDONG 			"/oem/usr/audio/dingdong.g711a"							//叮叮叮
#define AUDIO_FILE_DEV_OTA_SUCCESS 		"/oem/usr/audio/shengjichenggong.g711a" 					//固件升级中，请稍等
#define AUDIO_FILE_UNBIND_SUCCESS       "/oem/usr/audio/jiebangchenggong.g711a" 					//解绑定平台成功

class CAudioPrompt : public CThread
{
public:
	typedef struct _AudioFileParm
	{
//		FILE * pFile;
		std::string strFileName;
		int type;//0,响一次 1，常响 2，间隔响
		bool clean;//清除循环报警
	}AudioFileParm;


public:
	CAudioPrompt();
	~CAudioPrompt();

	static CAudioPrompt* instance();

	/// 开始语音对讲
	bool start();
	
	/// 停止语音对讲
	bool stop();
	

	void ThreadProc();

	int aoPlay(AudioFileParm file);
	int aoPlayForReboot(AudioFileParm file);
	int playSiren();	
	void stopPlay();
	int getPlayStatus();
	std::string get_play_siren_list(int list);
private:
	int getAudioFile(AudioFileParm &file);
	int m_play;
	bool m_bRebootStart;
	bool m_bStopPlay;
	std::deque<AudioFileParm>	m_audioFileDeque;
	CMutex m_Mutex;
	static CAudioPrompt *m_instance;
};

#define g_AudioPrompt (*CAudioPrompt::instance())

#endif 		//__AUDIO_PROMPT_H__
