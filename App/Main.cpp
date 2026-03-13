#include "Main.h"

#include "ProduceNew/Produce.h"
#include "ProduceNew/NetWifi.h"

#ifndef PROTOCOL_HAS_GB28181_CLIENT_SDK
#define PROTOCOL_HAS_GB28181_CLIENT_SDK 1
#endif

#if PROTOCOL_HAS_GB28181_CLIENT_SDK
#include "Protocol/gb28181/GB28181ClientSDK.h"
#endif


extern int TestModuleV2_start();
extern int g_iKeyboardReport;//add on 2025.01.02 按键上报标志位

#ifndef PROTOCOL_CONFIG_ENDPOINT
#define PROTOCOL_CONFIG_ENDPOINT "local-provider"
#endif


static void *do_audio_test(void *args)
{
	AppInfo("do_audio_test start\n");

	while (1)
	{
		CAudioPrompt::AudioFileParm audioFile;
		audioFile.strFileName = AUDIO_FILE_PLEASE_SET_WIFI;
		audioFile.type = 0;
		g_AudioPrompt.aoPlay(audioFile);

		sleep(30);
	}
	return NULL;
}

//------------------CSofia---------------
PATTERN_SINGLETON_IMPLEMENT(CSofia)

CSofia::CSofia()
	: m_gbClientSdk(NULL)
{
}

CSofia::~CSofia()
{
#if PROTOCOL_HAS_GB28181_CLIENT_SDK
	if (m_gbClientSdk != NULL)
	{
		m_protocolManager.UnbindGbClientSdk();
		delete m_gbClientSdk;
		m_gbClientSdk = NULL;
	}
#endif
}

bool CSofia::preStart()
{
	/// Infra 启动定时器 和 线程管理
	CInfra::instance()->start();
	/// 启动事件管理
	IEventManager::instance();

	/// 配置文件启动
	IConfigManager::config(CONFIG_DIR "/Json", CONFIG_DIR "/Json", CUSTOM_DIR "/CustomConfig2");
	IConfigManager::instance()->start();	  // 启动配置
	CMediaDefaultConfig::instance()->start(); // 设置应用程序默认配置
	CDefaultConfig::instance()->Start();	  // 设置应用程序默认配置
	IConfigManager::instance()->saveFile();

	IEventManager::instance()->attach(IEventManager::Proc(&CSofia::onAppEvent, this));
	
	// 存储模块初始化
	g_StorageManager->Init();
#if 0  
    //重定向log到SD卡文件tuya.log
    FILE *std_fp;
    if((std_fp = fopen("/mnt/sdcard/tuya.log","w+")) == NULL){
       //exit(6);
    }
    if(dup2(fileno(std_fp),STDOUT_FILENO) == -1){
		fclose(std_fp);
       //exit(5);
    }
    if(dup2(fileno(std_fp),STDERR_FILENO) == -1){
		fclose(std_fp);
       //exit(5);
    }
    fclose(std_fp);
#endif
	
	return true;
}

bool CSofia::start()
{
	if (PRODUCT_AGING_TEST == g_ProductCofHandle.GetProductMode()) // 老化测试
	{
		AvInit(ProductCof_g.stitch_distance,ProductCof_g.ispmode);
		// 音频模块初始化
		g_AVManager.VideoInit();
		g_AVManager.AudioInit();
		// 启动提示语音播报模块
		g_AudioPrompt.start();
		
		// 指示灯模块启动
		g_IndicatorLight.start();
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_OFF);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
//		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);

		#if 0
		START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/motor_gpio.ko", NULL);
		g_PtzHandle.start();
		#endif
		
		AppErr("start aging test ........\n");
		CreateDetachedThread((char*)"do_audio_test",do_audio_test, (void *)NULL, true);
		return true;
	}
	else if (PRODUCT_CAL_TEST == g_ProductCofHandle.GetProductMode()) // 拼接标定
	{
		AppErr("start cal test ........\n");

		SystemSetInfraredLamp(0);
		SystemSetIrcut(0);

		#if defined(RC0240)
		SystemSetIncandescentLampLumi(0,1000000);
		#endif

		#if defined(RC0240_LGV10)
		SystemSetIncandescentLampLumi(100,1000000);
		#endif

		g_NetWifi.Init();
		g_NetWifi.SetWifi(ProductCof_g.ssid, ProductCof_g.pwd, ProductCof_g.ip, ProductCof_g.gw, ProductCof_g.mask);
		while (0 == g_NetWifi.GetWifiConnetStatus())
		{
			AppErr("wait=========================>>>network...\n");
			sleep(1);
		}
		// g_ProducdeHandle.init();
		// g_ProducdeHandle.startcal();

		char cmd[256] = {"\0"};
		snprintf(cmd,256,"sample_demo_vi_avs_venc --vi_size 1920x1080 --avs_chn0_size 3840x1080 --avs_chn1_size 1920x544 -a /oem/usr/share/iqfiles/ -e h264cbr -b 4096 -n 2 --stitch_distance %f -F 15 --avs_mode_blend 2 --set_ldch -1 &",ProductCof_g.stitch_distance);
		printf("cal test=%s\n",cmd);
		system("ifconfig lo up");
		system(cmd);
		system("rkaiq_tool_server &");
		while(1)
		{
			sleep(1);
		}
	}
	else if (PRODUCT_CAL_AWB_TEST == g_ProductCofHandle.GetProductMode()) // 白平衡标定
	{
		AppErr("start awb test ........\n");
		SystemSetInfraredLamp(0);
		SystemSetIrcut(0);

		#if defined(RC0240)
		SystemSetIncandescentLampLumi(0,1000000);
		#endif

		#if defined(RC0240_LGV10)
		SystemSetIncandescentLampLumi(100,1000000);
		#endif

		g_NetWifi.Init();
		g_NetWifi.SetWifi(ProductCof_g.ssid, ProductCof_g.pwd, ProductCof_g.ip, ProductCof_g.gw, ProductCof_g.mask);
		while (0 == g_NetWifi.GetWifiConnetStatus())
		{
			AppInfo("wait=========================>>>network...\n");
			sleep(1);
		}
		// g_ProducdeHandle.init();
		// g_ProducdeHandle.startcal();

		system("ifconfig lo up");
		system("rkaiq_tool_server &");

		while(1)
		{
			sleep(1);
		}
	}
	else if (PRODUCT_PRO_TEST == g_ProductCofHandle.GetProductMode()) // 进入生产测试
	{
		AvInit(ProductCof_g.stitch_distance,ProductCof_g.ispmode);
		// 音频模块初始化
		g_AVManager.VideoInit();
		g_AVManager.AudioInit();
		// 启动提示语音播报模块
		g_AudioPrompt.start();
		// 指示灯模块启动
		g_IndicatorLight.start();
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_OFF);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
//		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);

		#if 0
		START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/motor_gpio.ko", NULL);
		g_PtzHandle.start();
		#endif

		AppErr("start product test ........\n");
        DeviceMode_g = 1; // 测试模式
		/// 定时检查复位按钮
		m_timerCheckButton.Start(CTimer::Proc(&CSofia::OnCheckButton_Produce, this), 0, 50);
		CAudioPrompt::AudioFileParm audioFile;
		audioFile.strFileName = AUDIO_FILE_IAT;
		audioFile.type = 0;
		g_AudioPrompt.aoPlay(audioFile);

		// 初始化白光灯灭
		g_Camera;

		g_NetWifi.Init();
		g_NetWifi.SetWifi(ProductCof_g.ssid, ProductCof_g.pwd, ProductCof_g.ip, ProductCof_g.gw, ProductCof_g.mask);
		while (0 == g_NetWifi.GetWifiConnetStatus())
		{
			AppInfo("wait=========================>>>network...\n");
			sleep(1);
		}

		// 启动生产测试
//		g_ProducdeHandle.init();
//		g_ProducdeHandle.start();
		TestModuleV2_start();
	}
	else // 正常启动
	{
		AvInit(ProductCof_g.stitch_distance,ProductCof_g.ispmode);
		// 音频模块初始化
		g_AVManager.VideoInit();
		g_AVManager.AudioInit();
		// 启动提示语音播报模块
		g_AudioPrompt.start();
		
		// 指示灯模块启动
		g_IndicatorLight.start();
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_OFF);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
//		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);

		#if 0	
		START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/motor_gpio.ko", NULL);
		g_PtzHandle.start();
		#endif

		// 如果未烧录则启动PCBA测试模式
		if (0 == g_EncryptionHandle.GetLicenseSavaType())
		{
			g_PcbaHandle.start();
			return true;
		}

        DeviceMode_g = 0; // 正常模式

		// 定时检查复位按钮
		m_timerCheckButton.Start(CTimer::Proc(&CSofia::OnCheckButton, this), 0, 50);
		// 启动网络管理模块
		g_NetConfigHook.Init();
		// Start protocol manager (GB28181/GAT1400/broadcast/listen)
		if (0 != m_protocolManager.Init(PROTOCOL_CONFIG_ENDPOINT))
		{
			AppErr("ProtocolManager Init failed\n");
		}
		else
		{
#if PROTOCOL_HAS_GB28181_CLIENT_SDK
			if (m_gbClientSdk == NULL)
			{
				m_gbClientSdk = new GB28181ClientSDK;
			}

			m_protocolManager.BindGbClientSdk(m_gbClientSdk);
#endif

			if (0 != m_protocolManager.Start())
			{
				AppErr("ProtocolManager Start failed\n");
#if PROTOCOL_HAS_GB28181_CLIENT_SDK
				m_protocolManager.UnbindGbClientSdk();
				delete m_gbClientSdk;
				m_gbClientSdk = NULL;
#endif
			}
		}
		// ???????
		g_Camera;
		// 初始化onvif server
		g_OnvifHandle.Init();
		// 启动tuya
		//g_TuyaHandle.start();

		if(access(NO_VOICE_PROMPT_FLAG, F_OK) != 0)
		{
			//语音提示
			CAudioPrompt::AudioFileParm audioFile;
			audioFile.strFileName = AUDIO_FILE_SYSTEM_STARTING;
			audioFile.type = 0;
			g_AudioPrompt.aoPlay(audioFile);
		}
	}

	return true;
}

bool CSofia::postStart()
{
	CThread::DumpThreads();
	CTimer::DumpThread();

	CSemaphore sem;
	sem.Pend();

	return true;
}

void CSofia::OnTimeReboot(Param wParam)
{
	printf("\033[1;36m   OnTimeReboot  \033[0m\n");
	AppInfo("----- OnTimeReboot timer -------\n");
	SystemReset();
}

void CSofia::OnCheckButton(Param wParam)
{
	int val = -1;

	val = SystemRest();

	static int numTime = 0;
	static bool up = false;

	if (val == 0)
	{
		// AppInfo("@@@@@@@@@@@@@@@@ key down @@@@@@@@@@@@@@@@@@@\n");
		up = false;
		numTime++;
	}
	else
	{
		// AppInfo("@@@@@@@@@@@@@@@@ key up @@@@@@@@@@@@@@@@@@@\n");

		up = true;
		if (up == true && numTime >= 3 && numTime <= 20)
		{
			// AppErr("=========== GOTO AP =============\n");
		}
		numTime = 0;
	}

	if (numTime >= 60) // 改为3s
	{
		// AppInfo("************** GOTO reset ******************\n");
		// numTime = 0;
		AppInfo("reset system\n");
		static int reb = 0;
		if (reb == 0)
		{
			reb = 1;
			m_timerReboot.Start(CTimer::Proc(&CSofia::OnTimeReboot, this), 0, 0);
		}
	}
}

void CSofia::OnCheckButton_Produce(Param wParam)
{
	int val = -1;

	val = SystemRest();

	static int numTime = 0;
	static bool up = false;

	if (val == 0)
	{
		// AppInfo("@@@@@@@@@@@@@@@@ key down @@@@@@@@@@@@@@@@@@@\n");
		up = false;
		numTime++;
	}
	else
	{
		// AppInfo("@@@@@@@@@@@@@@@@ key up @@@@@@@@@@@@@@@@@@@\n");

		up = true;
		if (up == true && numTime >= 3 && numTime <= 20)
		{
			AppErr("=========== GOTO AP =============\n");
			static int s_iLastTime = 0;
			int iNowTime = GetSystemUptime_s();
			if(iNowTime - s_iLastTime > 2)
			{
				g_iKeyboardReport ++;
				s_iLastTime = iNowTime;
			}
			
		}
		
		numTime = 0;
	}

}



void CSofia::onAppEvent(std::string code, int index, appEventAction action, const EventHandler *param, Json::Value *data)
{
	(void)index;
	(void)action;
	(void)param;
	(void)data;

	if ("UpgradeReleaseResource" == code)
	{
		AppErr("ProtocolManager stop for upgrade\n");
#if PROTOCOL_HAS_GB28181_CLIENT_SDK
		m_protocolManager.UnbindGbClientSdk();
		if (m_gbClientSdk != NULL)
		{
			delete m_gbClientSdk;
			m_gbClientSdk = NULL;
		}
#endif
		m_protocolManager.Stop();
	}
}

static void test_HandleSig(int signo)		
{
	printf("test_HandleSig!\n");
	signal(SIGPIPE, SIG_IGN);

	if (SIGPIPE == signo)
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGPIPE exception! @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	}

}

int main(int argc, char **argv)
{
	// tuya SDK报警推送时触发SIGPIPE导致应用退出，然后涂鸦让加的，并且表示这么加就行，不会有其他影响，推送失败会有重连
	signal(SIGPIPE, test_HandleSig);
	AVSetLogLevel(0);
	DEBUG_SETLEVEL(LEVEL_ERROR);
	DEBUG_SETMODELNAME((char*)"DGIOT");


	printf("\n\n==============================Ver: %s==========================\n\n", IPC_APP_VERSION);
	{
		/*
			判断之前是否为主动重启或软件异常重启，是的话，不播放提示语音
			放在前面是为了降低出现"删除REBOOT_FLAG前断电重启没有提示音"问题的概率
		*/
		#if 1
		if (access(VOICE_PROMPT_FLAG, F_OK) != 0)
		{
			if (access(REBOOT_FLAG, F_OK) == 0)
			{
				START_PROCESS("sh", "sh", "-c", "rm -rf " REBOOT_FLAG, NULL);
				START_PROCESS("sh", "sh", "-c", "touch " NO_VOICE_PROMPT_FLAG, NULL);
			}
		}
		else
		{
			START_PROCESS("sh", "sh", "-c", "rm -rf " VOICE_PROMPT_FLAG, NULL);
			START_PROCESS("sh", "sh", "-c", "rm -rf " NO_VOICE_PROMPT_FLAG, NULL);
			START_PROCESS("sh", "sh", "-c", "rm -rf " REBOOT_FLAG, NULL);
		}
		#else
		if(access(REBOOT_FLAG, F_OK) == 0)
		{
			TUYA_START_PROCESS("sh", "sh", "-c", "rm -rf "REBOOT_FLAG, NULL);
			TUYA_START_PROCESS("sh", "sh", "-c", "touch "NO_VOICE_PROMPT_FLAG, NULL);
		}
		#endif
	}

#ifndef DDBUG
	CFeedDog::instance()->create();
	CFeedDog::instance()->start();
#endif

	AppInfo("Dgiot Build in:%s, %s\n", __DATE__, __TIME__);
	// 产测配置初始化
    g_ProductCofHandle.Init();
	// 加密模块初始化
    g_EncryptionHandle.Init();

    if (1 == ProductCof_g.telnet)
	{
		AppWarning("use telnetd\n");
		START_PROCESS("sh", "sh", "-c", "telnetd &", NULL);
	}

	AppInfo("-------------------------------------------------------------------------->\n");
	AppInfo("ProductCof_g.enable            = %d\n", ProductCof_g.enable);
	AppInfo("ProductCof_g.ircut_flip        = %d\n", ProductCof_g.ircut_flip);
	AppInfo("ProductCof_g.image_flip        = %d\n", ProductCof_g.image_flip);
	AppInfo("ProductCof_g.language          = %d\n", ProductCof_g.language);
	AppInfo("ProductCof_g.ptz_opposite_run  = %d\n", ProductCof_g.ptz_opposite_run);
	AppInfo("ProductCof_g.play_vol          = %d\n", ProductCof_g.play_vol);
	AppInfo("ProductCof_g.capture_vol       = %d\n", ProductCof_g.capture_vol);
	AppInfo("ProductCof_g.lamp_board        = %d\n", ProductCof_g.lamp_board);
	AppInfo("ProductCof_g.lamp_board_value  = %d\n", ProductCof_g.lamp_board_value);
	AppInfo("ProductCof_g.ud_len            = %d\n", ProductCof_g.ud_len);
	AppInfo("ProductCof_g.lr_len            = %d\n", ProductCof_g.lr_len);
	AppInfo("ProductCof_g.ud_dir            = %d\n", ProductCof_g.ud_dir);
	AppInfo("ProductCof_g.lr_dir            = %d\n", ProductCof_g.lr_dir);
	AppInfo("ProductCof_g.ud_notuse         = %d\n", ProductCof_g.ud_notuse);
	AppInfo("ProductCof_g.check_speed       = %d\n", ProductCof_g.check_speed);
	AppInfo("ProductCof_g.run_speed         = %d\n", ProductCof_g.run_speed);
	AppInfo("ProductCof_g.run_ud_speed      = %d\n", ProductCof_g.run_ud_speed);
	AppInfo("ProductCof_g.track_speed       = %d\n", ProductCof_g.track_speed);
	AppInfo("ProductCof_g.tracker_step_multiple    = %d\n", ProductCof_g.tracker_step_multiple);
	AppInfo("ProductCof_g.tracker_stop_overtime    = %d\n", ProductCof_g.tracker_stop_overtime);
	AppInfo("ProductCof_g.up_len            = %d\n", ProductCof_g.up_len);
	AppInfo("ProductCof_g.tracker_lr_dir    = %d\n", ProductCof_g.tracker_lr_dir);
	AppInfo("ProductCof_g.tracker_ud_dir    = %d\n", ProductCof_g.tracker_ud_dir);
	AppInfo("ProductCof_g.light_ctrl        = %d\n", ProductCof_g.light_ctrl);
	AppInfo("ProductCof_g.ir_led_ctrl       = %d\n", ProductCof_g.ir_led_ctrl);
	AppInfo("ProductCof_g.smartir_en        = %d\n", ProductCof_g.smartir_en);
	AppInfo("ProductCof_g.pwmfrequency      = %d\n", ProductCof_g.pwmfrequency);
	AppInfo("ProductCof_g.hardtype          = %d\n", ProductCof_g.hardtype);
	AppInfo("ProductCof_g.speaker_reversed  = %d\n", ProductCof_g.speaker_reversed);
	AppInfo("ProductCof_g.auto_light_off    = %d\n", ProductCof_g.auto_light_off);
	AppInfo("ProductCof_g.auto_light_off_time  = %d\n", ProductCof_g.auto_light_off_time);
	AppInfo("ProductCof_g.private_motorstatus  = %d\n", ProductCof_g.private_motorstatus);
	AppInfo("ProductCof_g.stitch_distance   = %f\n", ProductCof_g.stitch_distance);
	AppInfo("ProductCof_g.pwd               = %s\n", ProductCof_g.pwd);
	AppInfo("ProductCof_g.ssid              = %s\n", ProductCof_g.ssid);
	AppInfo("-------------------------------------------------------------------------->\n\n");

	/// 启动前工作初始化
	CSofia::instance()->preStart();
	/// 启动Sofia主程序
	CSofia::instance()->start();
	/// 完成启动后相关工作
	CSofia::instance()->postStart();

	return 0;
}
