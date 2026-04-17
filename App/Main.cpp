#include "Main.h"
#include "Inifile.h"

#include "Protocol/ProtocolManager.h"
#include "Protocol/config/LocalConfigProvider.h"
#include "ProduceNew/Produce.h"
#include "ProduceNew/NetWifi.h"
#include "config/ProtocolExternalConfig.h"

#include "web_server.h"


//--------------------
unsigned char bStartPrivateMode; 		//是否开启隐私模式
PtzPosition_s pstPtzPosition_sleep;
int s_scale = 1;
static bool s_bUpgrading = false;
//--------------------
static int onCapture_test(int media_chn,
        int media_type,
        int media_subtype,
        unsigned long long frame_pts,
        unsigned char *frame_data,
        int frame_len,
        int frame_end_flag);
int get_local_ip_info(char *interface, char *ip);


extern int TestModuleV2_start();
extern int g_iKeyboardReport;//add on 2025.01.02 按键上报标志位

#ifndef PROTOCOL_CONFIG_ENDPOINT
#define PROTOCOL_CONFIG_ENDPOINT "local-provider"
#endif



static int my_config_handler(const device_state_t *state) {
    // 在这里实现您的设备控制逻辑，例如写入配置文件、调用硬件接口等
    printf("My custom handler: received new config\n");
    printf("Version: %s\n", state->version);
    printf("IP Mode: %s\n", state->ip_mode == 0 ? "DHCP" : "Static");
    printf("ip_addr: %s\n", state->ip_addr);
    printf("gateway: %s\n", state->gateway);
    printf("netmask: %s\n", state->netmask);
    printf("dns    : %s\n", state->dns);
    // WiFi 配置
    printf("wifi_need_config: %d\n", state->wifi_need_config);
    printf("wifi_ssid       : %s\n", state->wifi_ssid);
    printf("wifi_password   : %s\n", state->wifi_password);
    printf("gb-----\n");
    printf("gb_code      : %s\n", state->gb_code);
    printf("gb_ip        : %s\n", state->gb_ip);
    printf("gb_port      : %s\n", state->gb_port);
    printf("gb_device_id : %s\n", state->gb_device_id);
    printf("gb_password  : %s\n", state->gb_password);
    printf("gb_domain    : %s\n", state->gb_domain);
    printf("gb_user_id   : %s\n", state->gb_user_id);
    printf("gb_channel_code: %s\n", state->gb_channel_code);
    // 视频参数
    printf("video-----\n");
    printf("main_stream.video_codec  : %d\n", state->main_stream.video_codec);
    printf("main_stream.bitrate      : %d\n", state->main_stream.bitrate);
    printf("main_stream.framerate    : %d\n", state->main_stream.framerate);
    printf("main_stream.gop          : %d\n", state->main_stream.gop);
    printf("sub_stream.video_codec  : %d\n", state->sub_stream.video_codec);
    printf("sub_stream.bitrate      : %d\n", state->sub_stream.bitrate);
    printf("sub_stream.framerate    : %d\n", state->sub_stream.framerate);
    printf("sub_stream.gop          : %d\n", state->sub_stream.gop);
    // 音频参数
    printf("audio-----\n");
    printf("audio_codec  : %d\n", state->audio_codec);
    // 夜视功能参数
    printf("night_mode-----\n");
    printf("night_mode   : %d\n", state->night_mode);
    // ... 其他字段处理
    printf("GA/T 1400-----\n");
    printf("gat1400_ip        : %s\n", state->gat1400_ip);
    printf("gat1400_port      : %s\n", state->gat1400_port);
    printf("gat1400_user      : %s\n", state->gat1400_user);
    printf("gat1400_device_id : %s\n", state->gat1400_device_id);
    printf("gat1400_password  : %s\n", state->gat1400_password);

	int ret;
	uint32_t nIp;
	uint32_t nGateway;
	uint32_t nNetmask;
	uint32_t nDns;

	if (state->ip_mode)
	{
		ret = inet_pton(AF_INET, state->ip_addr, &nIp);
		if (1 != ret)
		{
			AppErr("invalid ip_addr\n");
			return -1;
		}
		ret = inet_pton(AF_INET, state->gateway, &nGateway);
		if (1 != ret)
		{
			AppErr("invalid gateway\n");
			return -2;
		}
		ret = inet_pton(AF_INET, state->netmask, &nNetmask);
		if (1 != ret)
		{
			AppErr("invalid netmask\n");
			return -3;
		}
		ret = inet_pton(AF_INET, state->dns, &nDns);
		if (1 != ret)
		{
			AppErr("invalid dns\n");
			return -4;
		}
	}

	Json::Value table;
	NetWifiConfig LocalWifiConfig;
	NetWifiConfig NewWifiConfig;
	g_configManager.getConfig(getConfigName(CFG_WIFI), table);
	TExchangeAL<NetWifiConfig>::getConfig(table, LocalWifiConfig);
	TExchangeAL<NetWifiConfig>::getConfig(table, NewWifiConfig);
	
	NewWifiConfig.bEnable = true;
	NewWifiConfig.bStaticIpEnable = state->ip_mode == 0 ? false : true;
	if (NewWifiConfig.bStaticIpEnable)
	{
		NewWifiConfig.HostIP.l = nIp;
		NewWifiConfig.Submask.l = nNetmask;
		NewWifiConfig.Gateway.l = nGateway;
		NewWifiConfig.Dns.l = nDns;
	}
	if (state->wifi_need_config)
	{
		NewWifiConfig.strSSID = state->wifi_ssid;
		NewWifiConfig.strKeys = state->wifi_password;
	}
	if (NewWifiConfig.bEnable 			!= LocalWifiConfig.bEnable || 
		NewWifiConfig.bStaticIpEnable 	!= LocalWifiConfig.bStaticIpEnable || 
		NewWifiConfig.HostIP.l 			!= LocalWifiConfig.HostIP.l || 
		NewWifiConfig.Submask.l 		!= LocalWifiConfig.Submask.l || 
		NewWifiConfig.Gateway.l 		!= LocalWifiConfig.Gateway.l || 
		NewWifiConfig.Dns.l 			!= LocalWifiConfig.Dns.l || 
		NewWifiConfig.strSSID 			!= LocalWifiConfig.strSSID || 
		NewWifiConfig.strKeys 			!= LocalWifiConfig.strKeys)
	{
		TExchangeAL<NetWifiConfig>::setConfig(NewWifiConfig, table);
		g_configManager.setConfig(getConfigName(CFG_WIFI), table, 0, IConfigManager::applyOK);
		
		g_NetConfigHook.ReConn();
	}

	table.clear();
	bool bVideoConfigChange = false;
	VideoConf_S LocalVideoConfig;
	VideoConf_S NewVideoConfig;
	g_configManager.getConfig(getConfigName(CFG_VIDEO), table);
	TExchangeAL<VideoConf_S>::getConfig(table, LocalVideoConfig);
	NewVideoConfig.chan[0].enc_type = VIDEO_CODEC_H265 == state->main_stream.video_codec ? 1 : 0;
	NewVideoConfig.chan[0].bit_rate = state->main_stream.bitrate;
	NewVideoConfig.chan[0].frmae_rate = state->main_stream.framerate;
	NewVideoConfig.chan[0].gop = state->main_stream.gop;
	NewVideoConfig.chan[1].enc_type = VIDEO_CODEC_H265 == state->sub_stream.video_codec ? 1 : 0;
	NewVideoConfig.chan[1].bit_rate = state->sub_stream.bitrate;
	NewVideoConfig.chan[1].frmae_rate = state->sub_stream.framerate;
	NewVideoConfig.chan[1].gop = state->sub_stream.gop;
	if (memcmp(&LocalVideoConfig, &NewVideoConfig, sizeof(VideoConf_S)) != 0)
	{
		AppErr("video param change.\n");
		bVideoConfigChange = true;
		TExchangeAL<VideoConf_S>::setConfig(NewVideoConfig, table);
		g_configManager.setConfig(getConfigName(CFG_VIDEO), table, 0, IConfigManager::applyOK);
	}

	table.clear();
	AudioConf_S AudioConfig;
	g_configManager.getConfig(getConfigName(CFG_AUDIO), table);
	TExchangeAL<AudioConf_S>::getConfig(table, AudioConfig);
	if (AUDIO_CODEC_G711A == state->audio_codec)
		AudioConfig.enc_type = 0;
	else if (AUDIO_CODEC_G711U == state->audio_codec)
		AudioConfig.enc_type = 1;
	else if (AUDIO_CODEC_AAC == state->audio_codec)
		AudioConfig.enc_type = 2;
	TExchangeAL<AudioConf_S>::setConfig(AudioConfig, table);
	g_configManager.setConfig(getConfigName(CFG_AUDIO), table, 0, IConfigManager::applyOK);

	//夜视模式
	int new_night_mode = 0;//默认——自动
	if (NIGHT_MODE_WHITE_LIGHT == state->night_mode)
		new_night_mode = 1;
	else if (NIGHT_MODE_INFRARED == state->night_mode)
		new_night_mode = 2;
	table.clear();
	CameraParamAll cpa;
	memset(&cpa, 0, sizeof(cpa));
	g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
	TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
	if (new_night_mode != cpa.vCameraParamAll[0].nightVisionMode)
	{
		cpa.vCameraParamAll[0].nightVisionMode = new_night_mode;
		TExchangeAL<CameraParamAll>::setConfigV2(cpa, table, 1);
		g_configManager.setConfig(getConfigName(CFG_CAMERA_PARAM), table,0, IConfigManager::applyOK);
	}

	protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();
	protocol::GbRegisterParam gbParam = pm.GetGbRegisterConfig();
	protocol::GatRegisterParam gatParam = pm.GetGatRegisterConfig();

    gbParam.enabled = 1;
	if (state->gb_enable)
		gbParam.register_mode = "standard";
	else
		gbParam.register_mode = "zero_config";
    gbParam.server_ip = state->gb_ip;
    gbParam.server_port = atoi(state->gb_port);
    gbParam.device_id = state->gb_device_id;
    gbParam.username = state->gb_code;
    gbParam.password = state->gb_password;
	pm.SetGbRegisterConfig(gbParam);
	pm.RestartGbRegisterService();

    gatParam.server_ip = state->gat1400_ip;
    gatParam.server_port = atoi(state->gat1400_port);
    gatParam.device_id = state->gat1400_device_id;
    gatParam.username = state->gat1400_user;
    gatParam.password = state->gat1400_password;
	pm.SetGatRegisterConfig(gatParam);
	pm.RestartGatRegisterService();

//	AppErr("bVideoConfigChange: %d.\n", bVideoConfigChange);
//	if (bVideoConfigChange)//不需要重启
//	{
//		NormalRestart();
//	}

//	stop_web_server();
	return 0;
}

static void *thread_sd_format(void *arg)
{
	bool *pbFormatRunning = (bool *)arg;
	int iDiskStatus = DISK_STATUS_UNKNOWN;

	g_StorageManager->DiskFmt(false);
	
	int count = 50;
	while(count-- > 0)
	{
		iDiskStatus = g_StorageManager->GetDiskState();
		if( iDiskStatus == DISK_STATUS_FORMATING )
		{
			printf("start format. count=%d\n", count);
			break;
		}
		usleep(100000); 	// 100ms
	}
	
	if(count > 0)
	{
		while(1)
		{
			iDiskStatus = g_StorageManager->GetDiskState();
			if ( iDiskStatus != DISK_STATUS_FORMATING )
			{
				printf("finish format.\n");
				break;
			}			
			sleep(1);
		}
	}
	if( iDiskStatus == DISK_STATUS_NORMAL )
	{
		update_sd_format_status(SD_FORMAT_SUCCESS);  // 成功
	}
	else
	{
		update_sd_format_status(SD_FORMAT_FAILED);	   // 失败
	}

	*pbFormatRunning = false;
	return NULL;
}
// SD卡格式化回调函数
// 当用户在Web界面点击"确认格式化"后，此函数会被调用
int my_sd_format_handler(void) {
    printf("[Device] Need to format SD card\n\n");
    
    // TODO: 在这里实现实际的SD卡格式化逻辑
    // 例如：
    // 1. 检查SD卡是否存在
    // 2. 检查SD卡是否被占用
    // 3. 执行格式化操作（如调用系统命令或硬件接口）
    // 4. 格式化完成后，调用 update_sd_format_status() 更新状态
    
    // 示例：创建一个文件来模拟格式化成功
    // 在实际应用中，应该在这里执行真正的格式化操作
    // 格式化完成后，根据结果调用：
    // update_sd_format_status(SD_FORMAT_SUCCESS);  // 成功
    // update_sd_format_status(SD_FORMAT_FAILED);     // 失败
    
    // 返回0表示成功启动格式化操作
    // 返回非0表示启动失败（如SD卡不存在、被占用等）

	static bool s_bFormatRunning = false;
	if (s_bFormatRunning)
	{
		AppErr("Format SD card already start\n");
		return 0;
	}
	s_bFormatRunning = true;
	CreateDetachedThread((char*)"thread_sd_format",thread_sd_format, (void *)&s_bFormatRunning, true);
    return 0;
}

extern void update_gb_status(gb_status_t status);
static void *thread_update_gb_status(void *args)
{
	protocol::ProtocolManager& protocolManager = protocol::ProtocolManager::Instance();
	bool sonline = false;
	while (1)
	{
		sleep(2);
		bool online = protocolManager.GetGbOnlineStatus();
		//printf("gb online===========>%d\n",online);
		if (sonline != online)
		{
			if (online)
				update_gb_status(GB_STATUS_ONLINE);
			else 
				update_gb_status(GB_STATUS_OFFLINE);
			
			sonline = online;
			
		}
	}

}

static void *thread_web_server(void *args)
{
	while (1)
	{
		Json::Value table;
		NetWifiConfig WifiConfig;
		g_configManager.getConfig(getConfigName(CFG_WIFI), table);
		TExchangeAL<NetWifiConfig>::getConfig(table, WifiConfig);
		
		//start web server
		device_state_t verify_status = {0};
		memset(&verify_status, 0, sizeof(device_state_t));
		
		//设置参数 注：目前不支持实时更新本地的配置信息，因此需要在web_server启动之前，把相应的信息配置好
		verify_status.ip_mode = WifiConfig.bStaticIpEnable;
		strcpy(verify_status.version, "1.0.2"); 	//设备版本号
		inet_ntop(AF_INET, &WifiConfig.HostIP.l, verify_status.ip_addr, sizeof(verify_status.ip_addr));	//IP地址
		inet_ntop(AF_INET, &WifiConfig.Gateway.l, verify_status.gateway, sizeof(verify_status.gateway));	//网关地址
		inet_ntop(AF_INET, &WifiConfig.Submask.l, verify_status.netmask, sizeof(verify_status.netmask));	//子网掩码
		inet_ntop(AF_INET, &WifiConfig.Dns.l, verify_status.dns, sizeof(verify_status.dns));		//DNS服务器地址
		snprintf(verify_status.wifi_ssid, sizeof(verify_status.wifi_ssid), WifiConfig.strSSID.c_str());
		snprintf(verify_status.wifi_password, sizeof(verify_status.wifi_password), WifiConfig.strKeys.c_str());
		
		table.clear();
		VideoConf_S VideoConfig;
		g_configManager.getConfig(getConfigName(CFG_VIDEO), table);
		TExchangeAL<VideoConf_S>::getConfig(table, VideoConfig);
		verify_status.main_stream.video_codec = 0 == VideoConfig.chan[0].enc_type ? VIDEO_CODEC_H264 : VIDEO_CODEC_H265;
		verify_status.main_stream.bitrate = VideoConfig.chan[0].bit_rate;
		verify_status.main_stream.framerate = VideoConfig.chan[0].frmae_rate;
		verify_status.main_stream.gop = VideoConfig.chan[0].gop;
		
		verify_status.sub_stream.video_codec = 0 == VideoConfig.chan[1].enc_type ? VIDEO_CODEC_H264 : VIDEO_CODEC_H265;
		verify_status.sub_stream.bitrate = VideoConfig.chan[1].bit_rate;
		verify_status.sub_stream.framerate = VideoConfig.chan[1].frmae_rate;
		verify_status.sub_stream.gop = VideoConfig.chan[1].gop;
		
		table.clear();
		AudioConf_S AudioConfig;
		g_configManager.getConfig(getConfigName(CFG_AUDIO), table);
		TExchangeAL<AudioConf_S>::getConfig(table, AudioConfig);
		if (0 == AudioConfig.enc_type)
			verify_status.audio_codec = AUDIO_CODEC_G711A;
		else if (1 == AudioConfig.enc_type)
			verify_status.audio_codec = AUDIO_CODEC_G711U;
		else if (2 == AudioConfig.enc_type)
			verify_status.audio_codec = AUDIO_CODEC_AAC;
		
		//夜视模式
		verify_status.night_mode = NIGHT_MODE_DEFAULT;//默认——自动
		table.clear();
		CameraParamAll cpa;
		memset(&cpa, 0, sizeof(cpa));
		g_configManager.getConfig(getConfigName(CFG_CAMERA_PARAM), table);
		TExchangeAL<CameraParamAll>::getConfigV2(table, cpa, 1);
		if (1 == cpa.vCameraParamAll[0].nightVisionMode)
			verify_status.night_mode = NIGHT_MODE_WHITE_LIGHT;
		else if (2 == cpa.vCameraParamAll[0].nightVisionMode)
			verify_status.night_mode = NIGHT_MODE_INFRARED;
		
		protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();
		protocol::GbRegisterParam gbParam = pm.GetGbRegisterConfig();
		protocol::GatRegisterParam gatParam = pm.GetGatRegisterConfig();

		if (gbParam.register_mode == "standard")
			verify_status.gb_enable = 1;
		else
			verify_status.gb_enable = 0;
		
		snprintf(verify_status.gb_code, sizeof(verify_status.gb_code), gbParam.username.c_str());		//接入编码
		snprintf(verify_status.gb_ip, sizeof(verify_status.gb_ip), gbParam.server_ip.c_str());			//接入IP地址
		snprintf(verify_status.gb_port, sizeof(verify_status.gb_port), "%d", gbParam.server_port);					//接入端口号
		snprintf(verify_status.gb_device_id, sizeof(verify_status.gb_device_id), gbParam.device_id.c_str()); //设备编码
		snprintf(verify_status.gb_password, sizeof(verify_status.gb_password), gbParam.password.c_str());				 //设备密码
		
		snprintf(verify_status.gat1400_ip, sizeof(verify_status.gat1400_ip), gatParam.server_ip.c_str());		//接入IP
		snprintf(verify_status.gat1400_port, sizeof(verify_status.gat1400_port), "%d", gatParam.server_port);					//接入端口号
		snprintf(verify_status.gat1400_user, sizeof(verify_status.gat1400_user), gatParam.username.c_str());		//设备用户
		snprintf(verify_status.gat1400_device_id, sizeof(verify_status.gat1400_device_id), gatParam.device_id.c_str()); //设备编码
		snprintf(verify_status.gat1400_password, sizeof(verify_status.gat1400_password), gatParam.password.c_str());				 //设备密码
		
		printf("report gb param...\n");
		printf("gb_code 	 : %s\n", verify_status.gb_code);
		printf("gb_ip		 : %s\n", verify_status.gb_ip);
		printf("gb_port 	 : %s\n", verify_status.gb_port);
		printf("gb_device_id : %s\n", verify_status.gb_device_id);
		printf("gb_password  : %s\n", verify_status.gb_password);
		printf("gat1400_ip		  : %s\n", verify_status.gat1400_ip);
		printf("gat1400_port	  : %s\n", verify_status.gat1400_port);
		printf("gat1400_user	  : %s\n", verify_status.gat1400_user);
		printf("gat1400_device_id : %s\n", verify_status.gat1400_device_id);
		printf("gat1400_password  : %s\n", verify_status.gat1400_password);
		
		
		set_device_state(&verify_status);//设置设备状态信息
		
		// 注册回调（保存按钮后会调用此回调my_config_handler）
		web_server_register_config_callback(my_config_handler);
		// 注册SD卡格式化回调函数（用户点击确认格式化后会调用此回调）
		web_server_register_sd_format_callback(my_sd_format_handler);
		start_web_server();  // 阻塞运行，直到 stop_web_server 被调用
		//end
		
		sleep(2);
	}
	return NULL;
}

static int init_gb_zero_config()
{
	int ret = -1;
	CInifile ini;
	char valstr [64] = {'\0'};
	const char *path = "/userdata/zero_config.ini";
	protocol::GbZeroConfigParam local_config;

	ret = ini.read_profile_string("zero_config", "code",valstr, sizeof(valstr), path);
	if (ret)
	{
		AppErr("read [zero_config] code=? failed.\n");
		return -1;
	}
	local_config.string_code = valstr;
	ret = ini.read_profile_string("zero_config", "mac",valstr, sizeof(valstr), path);
	if (ret)
	{
		AppErr("read [zero_config] mac=? failed.\n");
		return -1;
	}
	local_config.mac_address = valstr;

	protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();
    protocol::GbZeroConfigParam curr_config = pm.GetGbZeroConfig();

	if (local_config.string_code != curr_config.string_code || 
		local_config.mac_address != curr_config.mac_address)
	{
		pm.SetGbZeroConfig(local_config);
	}
	return 0;
}

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


static void SignalFuncExit(int argc)
{
    char cThreadName[32] = {0};
    prctl(PR_GET_NAME, (unsigned long)cThreadName);
    printf("get signal(%d) thread(%ld) name(%s)\n", argc, syscall(__NR_gettid), cThreadName);
	
	if( SIGPIPE == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGPIPE exception!\n");
	}
	else if( SIGFPE == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGFPE exception!\n");
	}
	else if( SIGINT == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGINT exception!\n");
	}
	else if( SIGTERM == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGTERM exception!\n");
	}
	else if( SIGABRT == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGABRT exception!\n");
	}
	else if( SIGSEGV == argc )
	{
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ catch SIGSEGV exception!\n");
	}

	AbnormalRestart(); // 异常重启

	_exit(-1);
}

int gb_alarm_notify()
{
	static int last_notify_time = 0;

	if (0 != last_notify_time && GetSystemUptime_s() < (last_notify_time+60))
		return -1;

	last_notify_time = GetSystemUptime_s();
	
	AlarmNotifyInfo alarmInfo;
	memset(&alarmInfo, 0, sizeof(alarmInfo));
	printf("[ProtocolManager] module=gb28181 event=alarm_notify %s,%s,%d \n", __FILE__,__FUNCTION__,__LINE__);
	snprintf(alarmInfo.AlarmID, sizeof(alarmInfo.AlarmID) - 1, "%ld", time(NULL));
	alarmInfo.AlarmPriority = 3;
	alarmInfo.AlarmMethod = 5;
	alarmInfo.AlarmType = kGB_ALARM_TYPE_VIDEO_PEA_PERIMETER;
	alarmInfo.AlarmState = 1;
	time_t now = time(NULL);
	struct tm* tmInfo = localtime(&now);
	printf("[ProtocolManager] module=gb28181 event=alarm_notify %s,%s,%d \n", __FILE__,__FUNCTION__,__LINE__);
	strftime(alarmInfo.AlarmTime, sizeof(alarmInfo.AlarmTime), "%Y-%m-%d %H:%M:%S", tmInfo);
	printf("[ProtocolManager] module=gb28181 event=alarm_notify %s,%s,%d \n", __FILE__,__FUNCTION__,__LINE__);
	snprintf(alarmInfo.AlarmDescription, sizeof(alarmInfo.AlarmDescription) - 1, "Human detection alarm");
	protocol::ProtocolManager::Instance().NotifyGbAlarm(&alarmInfo);
	return 0;
}

static void *thread_gb_monitor_network_status(void *args)
{
	int ret;
	NET_WORK_LIND_MODE last_link_mode = NET_WORK_MODE_NONE;
	char last_ip[16] = {0};

	while (1)
	{
		NET_WORK_LIND_MODE curr_link_mode = g_NetConfigHook.GetNetWorkLindMode();
		if (NET_WORK_MODE_ETH0 == curr_link_mode || NET_WORK_MODE_STA == curr_link_mode)
		{
			if (curr_link_mode != last_link_mode)
			{
				AppErr("link inf change to %d\n", curr_link_mode);
				protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();
				pm.RestartGbRegisterService();
				pm.RestartGatRegisterService();

				g_NetConfigHook.GetNetWorkIp(last_ip, sizeof(last_ip));
				last_link_mode = curr_link_mode;
			}
			else
			{
				char curr_ip[16] = {0};
				ret = g_NetConfigHook.GetNetWorkIp(curr_ip, sizeof(curr_ip));
				if (0 == ret && strncmp(last_ip, curr_ip, 16))
				{
					AppErr("link inf: %d, ip: %s change to %s\n", curr_link_mode, last_ip, curr_ip);
					protocol::ProtocolManager& pm = protocol::ProtocolManager::Instance();
					pm.RestartGbRegisterService();
					pm.RestartGatRegisterService();
					strncpy(last_ip, curr_ip, 16);
				}
			}
		}

		sleep(3);
	}
	return NULL;
}

int gb_upgrade_firmware()
{
	CFeedDog::instance()->stop();
	CFeedDog::instance()->destory();
	
	//add on 2025.03.13<避免逻辑冲突>
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	//add on 2025.03.13<避免逻辑冲突>
	
	START_PROCESS("sh", "sh", "-c", "touch /tmp/ota_upgrade_flag", NULL);
	return 0;
}

static void *thread_monitor_dev_status(void *args)
{
	int ret;

	while (1)
	{
		if( true == s_bUpgrading )
			continue;
		
		//监测SD卡状态, 如果卡异常一分钟, 则重启设备
		{
			static bool bSdcardNormal = false;
			static int sdcard_error_count = 0;
			//获取SD卡状态
			static int iLastDiskStatus = DISK_STATUS_UNKNOWN;
			int iDiskStatus = DISK_STATUS_UNKNOWN;
			iDiskStatus = g_StorageManager->GetDiskState();

			if( DISK_STATUS_UNKNOWN == iLastDiskStatus )
			{
				iLastDiskStatus = iDiskStatus;
			}
			if( iLastDiskStatus != iDiskStatus )
			{
				iLastDiskStatus = iDiskStatus;
			}

			if( DISK_STATUS_NORMAL == iDiskStatus )
				bSdcardNormal = true;

			if( bSdcardNormal ) 	//只有卡从正产变为异常的时候才会重启
			{
				if( DISK_STATUS_ERROR == iDiskStatus )
				{
					sdcard_error_count++;
				}
				else
				{
					sdcard_error_count = 0;
				}

				if( sdcard_error_count >= 60 )
				{
					sdcard_error_count = 0;
					AppErr("sd card error more than 1 min. reboot\n");
					AbnormalRestart();
				}
			}
		}

		//监测主码流编码
//		if (1)
		{
			static unsigned long long LastFrameCount = 0;
			static int SameCount = 0;

			if( 1 )
			{
				unsigned int FrameCount = CaptureGetEncodeFrameCount(0);
//				AppInfo("[main encode] ----------frame count : %u\n", FrameCount);
				if (LastFrameCount == FrameCount) 
				{
					SameCount ++;
				}
				else
				{
					SameCount = 0;
				}
					
				if (SameCount > 60) 
				{
					AppErr("CheckMainStreamEncodeFrameCount count=[%d] system reboot\n",SameCount);
					AbnormalRestart();
				}
				LastFrameCount = FrameCount;
			}
			else
			{
				SameCount = 0;
			}
		}
		
		//监测子码流编码
//		if(1)
		{
			static unsigned long long LastFrameCount = 0;
			static int SameCount = 0;

			if( 1 )
			{
				unsigned int FrameCount = CaptureGetEncodeFrameCount(1);
//				AppInfo("[sub encode] ----------frame count : %u\n", FrameCount);
				if (LastFrameCount == FrameCount) 
				{
					SameCount ++;
				}
				else
				{
					SameCount = 0;
				}
					
				if (SameCount > 60) 
				{
					AppErr("CheckSubStreamEncodeFrameCount count=[%d] system reboot\n",SameCount);
					AbnormalRestart();
				}
				LastFrameCount = FrameCount;
			}
			else
			{
				SameCount = 0;
			}
		}

		sleep(1);
	}
	return NULL;
}

static void *thread_test_rtsp(void *args)
{
	StartRtspPthread();
	while (1)
	{
		if (g_test_enc_type_change) 
		{
			StopRtspPthread();
			StartRtspPthread();
			g_test_enc_type_change = 0;
		}
		sleep(2);
	}
	return NULL;
}

//------------------CSofia---------------
PATTERN_SINGLETON_IMPLEMENT(CSofia)

CSofia::CSofia()
{
}

CSofia::~CSofia()
{
	protocol::ProtocolManager* protocolManager = protocol::ProtocolManager::InstanceIfCreated();
	if (protocolManager != NULL)
	{
		protocolManager->Stop();
		protocolManager->UnInit();
	}
}

void CSofia::StartLocalRuntimeServices()
{
	AppInfo("start local runtime services without tuya thread\n");

//	g_Camera.start(); //没有效果文件，rkipc_aiq_get_ctx(cam_id) 返回空，调aiq相关接口导致段错误
//	g_Alarm.SetAllowMotionDetTime(time(NULL) + 10);
//	g_Alarm.Start();
//	g_Siren.Start();
//	g_RecordManager.Start();
}

bool CSofia::preStart()
{

	//捕获到异常信号，做静默处理 add on 2025.03.12<添加异常处理逻辑 start>
	signal(SIGFPE, SignalFuncExit);
	signal(SIGINT, SignalFuncExit);
	signal(SIGTERM, SignalFuncExit);
	signal(SIGABRT, SignalFuncExit);
	signal(SIGSEGV, SignalFuncExit);
	//捕获到异常信号，做静默处理 add on 2025.03.12<添加异常处理逻辑 end>




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
	// g_StorageManager->Init();
	
	return true;
}

//int g_test_enc_type_change; //for debug
//int g_test_enc_type[2]; //for debug
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

		#if defined(RC0240) || defined(RC0240V20) || defined(RC0240V30) || defined(RC0240V40)
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

		#if defined(RC0240) || defined(RC0240V20) || defined(RC0240V30) || defined(RC0240V40)
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
		g_AVManager.AudioInit();
		//视频参数
		g_AVManager.VideoParamInit();//掉了这个接口，帧率码率等参数使用CFG_VIDEO的配置，不掉的话使用/oem/usr/bin/rkipc.ini的配置
		g_AVManager.VideoInit();
		
		// 初始化白光灯灭
		g_Camera;
		
		// 启动提示语音播报模块
		g_AudioPrompt.start();

		if(access(NO_VOICE_PROMPT_FLAG, F_OK) != 0)
		{
			//语音提示
			CAudioPrompt::AudioFileParm audioFile;
			audioFile.strFileName = AUDIO_FILE_SYSTEM_STARTING;
			audioFile.type = 0;
			g_AudioPrompt.aoPlay(audioFile);
		}
		
		// 指示灯模块启动
		g_IndicatorLight.start();
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_ON);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
//		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);

		#if 0	
		START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/motor_gpio.ko", NULL);
		g_PtzHandle.start();
		#endif

		// 如果未烧录则启动PCBA测试模式
//		if (0 == g_EncryptionHandle.GetLicenseSavaType())
//		{
//			g_PcbaHandle.start();
//			return true;
//		}

        DeviceMode_g = 0; // 正常模式

		init_gb_zero_config();

		// 定时检查复位按钮
		m_timerCheckButton.Start(CTimer::Proc(&CSofia::OnCheckButton, this), 0, 50);

		// 启动本地web
		CreateDetachedThread((char*)"thread_web_server",thread_web_server, (void *)NULL, true);

		//等待配置网络信息
		Json::Value table;
		NetWifiConfig WifiConfig;
		g_configManager.getConfig(getConfigName(CFG_WIFI), table);
		TExchangeAL<NetWifiConfig>::getConfig(table, WifiConfig);
#if 01
		if (false == WifiConfig.bEnable)
		{
			g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_SLOW_FLICKER);
			g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_SLOW_FLICKER);
			
			//等待网线接入
			while (g_NetConfigHook.Check_netlink_status("eth0") != 1)
			{
				sleep(1);
			}
			START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);
#if 01
			//直连
			START_PROCESS("sh", "sh", "-c", "ifconfig eth0 192.168.1.101 netmask 255.255.255.0", NULL);
#else
			//调试
			START_PROCESS("sh", "sh", "-c", "udhcpc -i eth0", NULL);
#endif
			while (1)
			{
				sleep(2);
				printf("wait web server config...\n");
				g_configManager.getConfig(getConfigName(CFG_WIFI), table);
				TExchangeAL<NetWifiConfig>::getConfig(table, WifiConfig);
				if (true == WifiConfig.bEnable)
				{
					printf("web server config sompletely\n");
					break;
				}
			}

			START_PROCESS("sh", "sh", "-c", "ifconfig eth0 0.0.0.0", NULL);
		}
#endif

		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_ON);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
		
		// 启动网络管理模块
		g_NetConfigHook.Init();
		g_NetConfigHook.SetWifiSwitch(true);
		// 初始化onvif server
//		g_OnvifHandle.Init();
		// 启动tuya
		//g_TuyaHandle.start();		

		//无需二维码配网后初始化视频和录像
		
		while (g_NetConfigHook.GetQrcodeEnable())
		{
			sleep(1);
			AppErr("scan qrcoed...\n");
		}

		g_Siren.Start();
		g_Siren.SetMDEnable(true);
		g_Camera.start();
		g_Alarm.SetAllowMotionDetTime(time(NULL) + 10);
		g_Alarm.Start();
		g_RecordManager.Start();

		// for debug
		#if 0
		while (1)
		{
			sleep(1);
			char ip[16] = {0};
			g_NetConfigHook.GetNetWorkIp(ip, sizeof(ip));
			if (ip[0])
			{
				AppErr("ip: %s\n", ip);
				break;
			}
		}
		g_AVManager.RealTimeStreamStart(DMC_MEDIA_TYPE_H264 | DMC_MEDIA_TYPE_H265 | DMC_MEDIA_TYPE_AUDIO, onCapture_test);
		CreateDetachedThread((char*)"test_rtsp",thread_test_rtsp, (void *)NULL, true);
		int ret;
		char buf[32];
		int mirror;
		int flip;
		while (1)
		{
			if(access("/tmp/flip", F_OK) == 0)
			{
				FILE *fp = fopen("/tmp/flip", "r");
				if (fp)
				{
					ret = fread(buf, 1 ,31, fp);
					if (ret > 0)
					{
						buf[ret] = '\0';
						if (sscanf(buf, "%d,%d", &mirror, &flip) > 0)
						{
							CaptureSetMirrorAndFlip(mirror, flip);
						}
					}
					fclose(fp);
				}
				
				remove("/tmp/flip");
			}			
			usleep(200*1000);
		}
		#endif

#if 01
		// Start protocol manager (GB28181/GAT1400/broadcast/listen)
		protocol::ProtocolManager& protocolManager = protocol::ProtocolManager::Instance();
		if (0 != protocolManager.Init(PROTOCOL_CONFIG_ENDPOINT))
		{
			AppErr("ProtocolManager Init failed\n");
		}
		else
		{
			if (0 != protocolManager.Start())
			{
				AppErr("ProtocolManager Start failed\n");
			}
		}
		CreateDetachedThread((char*)"thread_update_gb_status",thread_update_gb_status, (void *)NULL, true);
		CreateDetachedThread((char*)"gb_monitor_network",thread_gb_monitor_network_status, (void *)NULL, true);
#endif
//		StartLocalRuntimeServices();

		CreateDetachedThread((char*)"monitor_dev_status",thread_monitor_dev_status, (void *)NULL, true);
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

	if (numTime >= 60) // 改为3s 60 * 50ms = 3s
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
		s_bUpgrading = true;
		
		AppErr("ProtocolManager stop for upgrade\n");
		protocol::ProtocolManager* protocolManager = protocol::ProtocolManager::InstanceIfCreated();
		if (protocolManager != NULL)
		{
			protocolManager->Stop();
			protocolManager->UnInit();
		}
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


int get_local_ip_info(char *interface, char *ip)
{
	int result = -1;
    char tmp[256];

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp,sizeof(tmp),"ifconfig %s",interface);
    FILE *pp = popen(tmp, "r");
    if(pp == NULL)
    {
		AppErr("popen ifconfig %s failed. errno : %d\n", interface, errno);
        return -1;
    }

    memset(tmp, 0, sizeof(tmp));
    while (fgets(tmp, sizeof(tmp), pp) != NULL)
    {
        char *pIPStart = strstr(tmp, "inet addr:");
        if(pIPStart != NULL)
        {/* It's all a line containing IP GW mask that jumps out directly  */
            break;
        }
    }
    
    {
        /* finding ip  */
        char *pIPStart = strstr(tmp, "inet addr:");
        if(pIPStart != NULL)
        {
            sscanf(pIPStart + strlen("inet addr:"), "%15s ", ip);
			result = 0;
        }
    }
        
    pclose(pp);

    return result;
}


wifi_status_enum s_link_res = WIFI_DISCONNECTED;

void wifi_result_cb(wifi_status_enum stat)
{
	s_link_res = stat;
}

static int onCapture_test(int media_chn,
        int media_type,
        int media_subtype,
        unsigned long long frame_pts,
        unsigned char *frame_data,
        int frame_len,
        int frame_end_flag)
{
	int ret = 0;
	unsigned char *pFrmBuff = frame_data;
	unsigned int uiFrmSize = (unsigned int)frame_len;
	unsigned long long ullTimeStamp = frame_pts;

	if (DMC_MEDIA_VIDEO_MAIN_STREAM == media_chn)
	{		
		RtspPutFrame_Main(media_subtype, &ullTimeStamp, (char *)pFrmBuff, uiFrmSize);
		// printf("----------------------------->onCapture, stream %d, size = %d, ret = %d\n", media_chn, frame_len, ret);	
	}
	else if  (DMC_MEDIA_VIDEO_SUB_STREAM == media_chn)
	{
		RtspPutFrame_Sub(media_subtype, &ullTimeStamp, (char *)pFrmBuff, uiFrmSize);
		// printf("----------------------------->onCapture, stream %d, size = %d, ret = %d\n", media_chn, frame_len, ret);	
	}
	else if (DMC_MEDIA_AUDIO_FRIST_STREAM == media_chn)
	{
		RtspPutFrame_Audio(media_subtype, &ullTimeStamp, (char *)pFrmBuff, uiFrmSize);
		RtspPutFrame_Audio1(media_subtype, &ullTimeStamp, (char *)pFrmBuff, uiFrmSize);
		// printf("----------------------------->onCapture, stream %d, size = %d, ret = %d\n", media_chn, frame_len, ret);	
	}

	return 0;
}

int set_ir_mode(int mode)
{
	if (0 == mode) //day
	{
		//红外灯
		SystemSetInfraredLamp(0);
		//ircut
		SystemSetIrcut(1);
		//isp彩色
		CaptureSetISPMode(0);
		SystemSetColor(0);
	}
	else //night
	{
		//红外灯
		SystemSetInfraredLamp(1);
		//ircut
		SystemSetIrcut(0);
		//isp黑白
		CaptureSetISPMode(1);
		SystemSetColor(1);
	}
	return 0;
}

int main(int argc, char **argv)
{
	//iq 调试
#if 0
	{
//		START_PROCESS("sh", "sh", "-c", "mount /dev/mmcblk1p1 /mnt/sdcard", NULL);
//		sleep(1);
		
//		WifiInit(); //初始化WiFi add on 2025.05.26
//		sleep(2);
//
//		WIFI_ST_t Stm;
//		snprintf(Stm.ssid, sizeof(Stm.ssid), "TP-LINK_5G_18A5");
//		snprintf(Stm.psk, sizeof(Stm.psk), "dgiot0202");
//		WifiStationModeCreate(&Stm, wifi_result_cb);
//
//		while (WIFI_DISCONNECTED == s_link_res || WIFI_CONNECTING == s_link_res)
//			sleep(1);

		SystemWifiPwrCtl(1); //WiFi模块下电 
//		START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);
//		START_PROCESS("sh", "sh", "-c", "udhcpc -ieth0", NULL);

		int ret;
		char ip[16];
		while (1)
		{
			sleep(2);
			printf("wait ip...\n");
			ret = get_local_ip_info("eth0", ip);
			if (0 == ret)
			{
				printf("get ip: %s\n", ip);
				break;
			}
		}

		AvInit(0.0, 1);
		g_AVManager.VideoInit();
//		g_AVManager.AudioInit();
		g_AVManager.RealTimeStreamStart(DMC_MEDIA_TYPE_H264 | DMC_MEDIA_TYPE_H265 | DMC_MEDIA_TYPE_AUDIO, onCapture_test);

		StartRtspPthread();

		system("ifconfig lo up");
		system("rkaiq_tool_server &");


		int cur_ir_mode = 0;
		int cur_w_led = 0;

		set_ir_mode(0);
		SystemSetIncandescentLamp(0);
		
		char buf[32];
		int ir_mode;
		int w_led;
		while (1)
		{
			if(access("/tmp/ir_mode", F_OK) == 0)
			{
				FILE *fp = fopen("/tmp/ir_mode", "r");
				if (fp)
				{
					ret = fread(buf, 1 ,31, fp);
					if (ret > 0)
					{
						buf[ret] = '\0';
						if (sscanf(buf, "%d", &ir_mode) > 0)
						{
							ir_mode = ir_mode ? 1 : 0;
							if (ir_mode != cur_ir_mode)
							{
								cur_ir_mode = ir_mode;
								set_ir_mode(cur_ir_mode);
							}
						}
					}
					fclose(fp);
				}
				
				remove("/tmp/ir_mode");
			}
			
			if(access("/tmp/w_led", F_OK) == 0)
			{
				FILE *fp = fopen("/tmp/w_led", "r");
				if (fp)
				{
					ret = fread(buf, 1 ,31, fp);
					if (ret > 0)
					{
						buf[ret] = '\0';
						if (sscanf(buf, "%d", &w_led) > 0)
						{
							w_led = w_led ? 1 : 0;
							if (w_led != cur_w_led)
							{
								cur_w_led = w_led;
								SystemSetIncandescentLamp(cur_w_led);
							}
						}
					}
					fclose(fp);
				}
				
				remove("/tmp/w_led");
			}
			
			usleep(200*1000);
		}

		return 0;
	}
#endif

	// tuya SDK报警推送时触发SIGPIPE导致应用退出，然后涂鸦让加的，并且表示这么加就行，不会有其他影响，推送失败会有重连
	signal(SIGPIPE, test_HandleSig);
	AVSetLogLevel(0);
	DEBUG_SETLEVEL(LEVEL_ERROR);
	DEBUG_SETMODELNAME((char*)"DGIOT");

	//----------------------------------------------------
	// 初始化存储管理模块(为了能够将打印重定向到SD卡)
	g_StorageManager->Init();
	char strPath[128] = {0};
	snprintf(strPath, sizeof(strPath), "%s/debug.log", __STORAGE_SD_MOUNT_PATH__);
	if (access(strPath, F_OK) == 0)
	{
		//重定向log到SD卡文件 /mnt/sdcard/debug.log
		printf("output to %s.\n", strPath);
		FILE *std_fp = NULL;
		if ((std_fp = fopen(strPath, "a")) == NULL) {
			printf("fopen %s fails.\n", strPath);
		} else {
			if (dup2(fileno(std_fp),STDOUT_FILENO) == -1) {
				printf("dup2 STDOUT_FILENO fails.\n");
			}
			if (dup2(fileno(std_fp),STDERR_FILENO) == -1){
				printf("dup2 STDERR_FILENO fails.\n");
			}
			fclose(std_fp);
		}
	}
	//----------------------------------------------------
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

	WifiInit(); //初始化WiFi add on 2025.05.26
	
	/// 启动前工作初始化
	CSofia::instance()->preStart();


	//天线匹配
	#if 0
	{
		AvInit(ProductCof_g.stitch_distance,ProductCof_g.ispmode);
		// 音频模块初始化
//		g_AVManager.VideoInit();
		g_AVManager.AudioInit();
		// 启动提示语音播报模块
		g_AudioPrompt.start();
		
		CAudioPrompt::AudioFileParm audioFile;
		audioFile.strFileName = AUDIO_FILE_IAT;
		audioFile.type = 0;
		g_AudioPrompt.aoPlay(audioFile);
		
		// 指示灯模块启动
		g_IndicatorLight.start();
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_ALWAYS_ON);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
//		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);

		if (PRODUCT_PRO_TEST != g_ProductCofHandle.GetProductMode())
		{
			g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_SLOW_FLICKER);
			audioFile.strFileName = AUDIO_FILE_PLEASE_SET_WIFI;
			while (1)
			{
				g_AudioPrompt.aoPlay(audioFile);
				sleep(15);
			}
		}

		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_FAST_FLICKER);

		WIFI_ST_t Stm;
		memset(&Stm, 0, sizeof(Stm));
		snprintf(Stm.ssid, sizeof(Stm.ssid), ProductCof_g.ssid);
		snprintf(Stm.psk, sizeof(Stm.psk), ProductCof_g.pwd);
		WifiStationModeCreate(&Stm, wifi_result_cb);

		while (WIFI_DISCONNECTED == s_link_res || WIFI_CONNECTING == s_link_res)
			sleep(1);

		int ret;
		char ip[16];
		while (1)
		{
			sleep(2);
			printf("wait ip...\n");
			ret = get_local_ip_info("wlan0", ip);
			if (0 == ret)
			{
				printf("get ip: %s\n", ip);
				break;
			}
		}

		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_ON);

		while (1)
			sleep(1);
	}
	#endif
	
	/// 启动Sofia主程序
	CSofia::instance()->start();
	/// 完成启动后相关工作
	CSofia::instance()->postStart();

	return 0;
}
