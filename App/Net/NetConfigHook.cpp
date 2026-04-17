#include <net/if.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <sys/ioctl.h>

#include "NetConfigHook.h"

extern int DeviceMode_g; 		//0:正常模式， 1:测试模式

bool check_firtst_connecting = 0;
bool check_firtst_connected = 0;
bool check_firtst_connectfail = 0;

void wifi_connect_callback(wifi_status_enum status);


static int s_QrcodeExit = 0;
//提取到二维码信息时的回调
static bool QrCodeExtractedCB(const std::string &strResult)
{
	//WIFI:S:TP-LINK_18A5;T:WPA;P:dgiot0202;;
	AppErr("strResult: %s\n", strResult.c_str());

	int ret;
	char ssid[64] = {0};
	char psw[64] = {0};
	const char *str_qrcode = strResult.c_str();
	char * p = NULL;

	memset(ssid, 0, sizeof(ssid));
	memset(psw, 0, sizeof(psw));

	ret = sscanf(str_qrcode, "WIFI:S:%63[^;];T:%*[^;];P:%63[^;];;", ssid, psw);
	if (2 != ret)
	{
		AppErr("parse ssid or psw failed\n");
		return false;
	}
		
	s_QrcodeExit = 1;
	
	AppErr("ssid: %s, psw: %s\n", ssid, psw);
	
	CAudioPrompt::AudioFileParm audioFile;
	audioFile.strFileName = AUDIO_FILE_QRCODE_GET_COMPLETE;	
    audioFile.type = 0;
	g_AudioPrompt.aoPlay(audioFile);
	
	g_NetConfigHook.SetWifi(ssid, psw);

	return true;
}
static void* DoQrcode(void* args)
{
	printf("DoQrcode start\n");

	CAudioPrompt::AudioFileParm audioFile;
	audioFile.strFileName = AUDIO_FILE_PLEASE_SET_WIFI;
	audioFile.type = 0;
	g_AudioPrompt.aoPlay(audioFile);
	
	int count = 15;
	while (!s_QrcodeExit) {

		if (count -- < 0) {
			count = 15;
			
			g_AudioPrompt.aoPlay(audioFile);
		}
		sleep(1);
	}
	return NULL;
}



PATTERN_SINGLETON_IMPLEMENT(CNetConfigHook)

CNetConfigHook::CNetConfigHook():CThread("NetInterFace", TP_DEFAULT)
{
	m_bCanScanQrCode = true;
	
	m_wlanModel = WIFI_NONE;
	m_wlanFreq = WIFI_SINGLE_FREQ;
	m_wlanBleSupport = WIFI_BLE_NOT_SUPPORT;
	memset(m_ip,0,sizeof(m_ip));
	//获取配置信息
	Json::Value WifiTable;
	g_configManager.getConfig(getConfigName(CFG_WIFI), WifiTable);
	TExchangeAL<NetWifiConfig>::getConfig(WifiTable, m_ConfigWifi);
	g_configManager.attach(getConfigName(CFG_WIFI), IConfigManager::Proc(&CNetConfigHook::OnCfgWifiComm, this));

	//获取配置信息
	Json::Value APTable;
	g_configManager.getConfig(getConfigName(CFG_WIFI_AP), APTable);
	printJsonValue(APTable);
	TExchangeAL<NetAPConfig>::getConfig(APTable, m_ConfigAP);
	g_configManager.attach(getConfigName(CFG_WIFI_AP), IConfigManager::Proc(&CNetConfigHook::OnCfgAPComm, this));

	m_bWifiEnable = false;
	m_nActiveNet = 0;
	m_nwLinkMode = NET_WORK_MODE_NONE;
	const unsigned char *pMac = DevInfoFromEEPROM_g.MAC;
    IEventManager::instance()->attach(IEventManager::Proc(&CNetConfigHook::onAppEvent, this));
}

CNetConfigHook::~CNetConfigHook()
{
}

int CNetConfigHook::Init()
{
	AppErr("CNetConfigHook::Init() start\n");
	if( false == IsThreadOver() ) 	//防止多次调用
		return 0;

	GetWifiModel();
#if 1
	const unsigned char *pMac = DevInfoFromEEPROM_g.MAC;
	char tempBuf[32] = {0};
	snprintf(tempBuf, sizeof(tempBuf), "%02x:%02x:%02x:%02x:%02x:%02x", pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5]);

	//设置eth0 MAC		
	NetSetEthMac(tempBuf);
#endif

	memset(m_NetEthCfg, 0, sizeof(NetDevice) * MAX_NETCARD);

	m_nActiveNet = -1;
	int iEthNums;

	bool bRet = CreateThread();
	AppErr("CNetConfigHook::Init() end\n");

	if (true != bRet)
	{
		AppErr("Create thread error!\n");
		return -1;
	}	
	return 0;
}

int CNetConfigHook::UnInit()
{
	AppErr("CNetConfigHook::UnInit() start\n");
	DestroyThread();
	AppErr("CNetConfigHook::UnInit() end\n");
	return 0;
}

/// 设置wifi ap 模式
int CNetConfigHook::StartWifiApMode(std::string &strSSID, std::string &strPasswd)
{
	int ret;
	ret = WifiApModeCreate(strSSID.c_str(), strPasswd.c_str());
	return ret;
}
/// 停止wifi ap 模式
int CNetConfigHook::StopWifiApMode(void)
{
	return WifiApModeDestroy();
}

/// 设置wifi station模式
int CNetConfigHook::StartWifiStationMode(void)
{
	AppInfo("m_ConfigWifi.strSSID=%s\n",m_ConfigWifi.strSSID.c_str());
	AppInfo("m_ConfigWifi.strKeys=%s\n",m_ConfigWifi.strKeys.c_str());
	
	WIFI_ST_t par = {0};
	memset(&par, 0, sizeof(par));
	strcpy(par.ssid, m_ConfigWifi.strSSID.c_str());
	strcpy(par.psk, m_ConfigWifi.strKeys.c_str());

	par.bStaticIp = m_ConfigWifi.bStaticIpEnable;
	par.ip = m_ConfigWifi.HostIP.l;
	par.netmask = m_ConfigWifi.Submask.l;
	par.gateway = m_ConfigWifi.Gateway.l;
	par.dns = m_ConfigWifi.Dns.l;
	
	return WifiStationModeCreate(&par, wifi_connect_callback);
}

/// 停止wifi station 模式
int CNetConfigHook::StopWifiStationMode(void)
{
	return WifiiStationDestroy();
}

bool CNetConfigHook::Check(char * name) 
{
	int iEthNums;
	iEthNums = NetGetEthDevice(m_NetEthCfg, MAX_NETCARD);
	for (int j = 0; (j < iEthNums) && (j < MAX_NETCARD); j++)
	{
		AppInfo("m_NetEthCfg[%d].name=%s\n",j,m_NetEthCfg[j].name);
		AppInfo("m_NetEthCfg[%d].valid=%d\n",j,m_NetEthCfg[j].valid);
		AppInfo("m_NetEthCfg[%d].state=%d\n",j,m_NetEthCfg[j].state);
		AppInfo("m_NetEthCfg[%d].trans_media=%d\n",j,m_NetEthCfg[j].trans_media);
		
		if(strcmp(m_NetEthCfg[j].name,name) == 0 )
		{
			return true;
		}
	}//end for
	return false;
}

//-1:error 	0:interface link down 	1:interface link up
int CNetConfigHook::Check_netlink_status(const char * if_name) 
{
	int sockfd;
    struct ifreq ifr;
	int status = 0;
	
    // 打开网络接口设备
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to open socket");
        return -1;
    }

    // 设置要查询的网络接口名称
    strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);

    // 查询接口状态
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("Failed to get interface flags");
        close(sockfd);
        return -1;
    }

    // 检查接口的插入状态
    if (ifr.ifr_flags & IFF_RUNNING) {
        // printf("%s interface link up\n",if_name);
		//printf("\033[1;36m %s interface link up \033[0m\n",if_name);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_FAST_FLICKER);// change on 2025.01.20 取消注释
		status = 1;
    } else {
        // printf("%s interface link down\n",if_name);
		//printf("\033[1;35m %s interface link down \033[0m\n",if_name);
		g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_EPI_INDICATOR_LIGHT_ALWAYS_OFF);// change on 2025.01.20 取消注释
		status = 0;
    }

    // 关闭网络接口设备
    close(sockfd);

    return status;
}

static wifi_status_enum wifi_connect_result_g = WIFI_DISCONNECTED;
static wifi_status_enum wifi_connect_result_last_g = WIFI_DISCONNECTED;

void wifi_connect_callback(wifi_status_enum status)
{
	AppInfo("wifi connect result : %d\n", status);
	wifi_connect_result_g = status;
}

WLAN_STATE CNetConfigHook::GETNETSTATUS(void )
{
	return m_eWlanStat;
}

NET_WORK_LIND_MODE CNetConfigHook::GetNetWorkLindMode(void )
{
	return m_nwLinkMode;
}

WLAN_MODEL CNetConfigHook::GetCurlWifiModel()
{
	return m_wlanModel;
}

WLAN_FREQ CNetConfigHook::GetCurlWifiFreq()
{
	return m_wlanFreq;
}

//add on 2025.01.11
WLAN_BLE CNetConfigHook::GetCurlWifiBleSupport()
{
	return m_wlanBleSupport;
}

int CNetConfigHook::GetWifiModel()
{	
	FILE *fp;	
	char buf[512];	 
	char cmd[128];
	m_wlanModel = WIFI_NONE;
	m_wlanFreq = WIFI_SINGLE_FREQ;
	m_wlanBleSupport = WIFI_BLE_NOT_SUPPORT;//add on 2025.01.11
	strcpy(cmd, "cat /sys/bus/usb/devices/*/uevent");
	fp = popen(cmd, "r");	
	if(NULL == fp)	 
	{	
		perror("popen error");
		return -1;	 
	}	
	while(fgets(buf, sizeof(buf), fp) != NULL)	 
	{	
		if ( NULL != strstr(buf,"bda/f179") )
		{
			//rtl8188ftv
			printf("Detected wifi rtl8188ftv\n");
			m_wlanModel =  WIFI_RTL8188;
			m_wlanFreq = WIFI_SINGLE_FREQ;
			m_wlanBleSupport = WIFI_BLE_NOT_SUPPORT;//add on 2025.01.11
			goto get_exit;

		}
		else if ( NULL != strstr(buf,"7a/6052") )
		{
			//atbm6052
			printf("Detected wifi atbm6062\n");
			m_wlanModel = WIFI_ATBM6062;
			m_wlanFreq = WIFI_SINGLE_FREQ;
			m_wlanBleSupport = WIFI_BLE_SUPPORT;//add on 2025.01.11
			goto get_exit;
		}
		else if ( NULL != strstr(buf,"7a/888b") )
		{
			//atbm6012
			printf("Detected wifi atbm6012b\n");
			m_wlanModel = WIFI_ATBM6012B;
			m_wlanFreq = WIFI_SINGLE_FREQ;
			m_wlanBleSupport = WIFI_BLE_SUPPORT;//add on 2025.01.11
			goto get_exit;
		}
		else if ( NULL != strstr(buf,"7a/8890") )
		{
			//atbm6132
			printf("Detected wifi atbm6132\n");
			m_wlanModel = WIFI_ATBM6132;
			m_wlanFreq = WIFI_DUAL_FREQ;
			m_wlanBleSupport = WIFI_BLE_SUPPORT;//add on 2025.01.11
			goto get_exit;
		}
		//After the aic_load_fw.ko is installed successfully, the usb device id will change from a69c:8d80 to a69c:8d81
		else if ( NULL != strstr(buf,"a69c/8d81") )
		{
			//aic8800dl
			printf("Detected wifi aic8800dl\n");
			m_wlanModel = WIFI_AIC8800DL;
			m_wlanFreq = WIFI_DUAL_FREQ;
			m_wlanBleSupport = WIFI_BLE_SUPPORT;//add on 2025.01.11
			goto get_exit;
		}
	}	
get_exit:	
	if (m_wlanModel == WIFI_NONE)
	{
		printf("Detected wifi unkown\n");
	}
	
	pclose(fp);   

	return 0;
}  

void CNetConfigHook::ThreadProc(void)
{
	int ret;
	int count = 0;
	int wifi_connect_failed_count = 0;

	m_eth0_status = NET_CHECK_ETH0_VALID;
	m_wifi_status = NET_CHECK_WLAN0_VALID;

	unsigned int wifi_connecting_aoPlay_time = 0;

	bool bQrCodeConHandleRunning = false;
	
	CAudioPrompt::AudioFileParm audioFile;
	Json::Value networkStatus;

//	bool bCanScanQrCode = true; //插过网线或者超时后就不允许再启动
	bool bWifiConfig = false;
	bool bApQrcodeStart = false;
	int QrcodeStartTime = 0;

	bool bFirstCheckEth0 = true;

	while (m_bLoop)
	{
		if (bApQrcodeStart && GetSystemUptime_s() > (QrcodeStartTime + 300))
		{
			m_bCanScanQrCode = false;
			bApQrcodeStart = false;
			s_QrcodeExit = 1;		//停止播报
			if( true == g_QrCodeConHandle.GetCreatedStatus() )
			{
				g_QrCodeConHandle.Stop();
				g_QrCodeConHandle.Destory();		//退出二维码配网模式
				// g_Camera.SetNightModeQrcode(0);
			}
		}
	
		//测试模式时,只用wifi连接网络
		//正常模式
		if( 0 == DeviceMode_g )
		{
			ret = Check_netlink_status("eth0");
			if (bFirstCheckEth0) //因为插着网线，第一次返回也是0
			{
				AppErr("Check_netlink_status(\"eth0\") -> ret: %d\n", ret);
				bFirstCheckEth0 = false;
				sleep(3);
				continue;
			}
//			if((m_ConfigWifi.strSSID == "") )//未配网，则不跑当前线程的联网逻辑 强制进行wifi配网
//			{
//				ret = 0;//走无线去配网
//			}
		}
		else
		{
			ret = 0;
		}
//		ret = 0; //<shang> debug

		//网线已插入
		if( 1 == ret )
		{
			
			count = 0;
			wifi_connect_result_g = WIFI_DISCONNECTED;
			wifi_connect_result_last_g = WIFI_DISCONNECTED;
			m_wifi_status = NET_CHECK_WLAN0_VALID; 	//设置WiFi状态标志，为了切换到WiFi时，正常连接WiFi
			
			if( NET_CHECK_ETH0_VALID ==  m_eth0_status)
			{
				if (bApQrcodeStart)
				{
					bApQrcodeStart = false;
					s_QrcodeExit = 1;		//停止播报
					if( true == g_QrCodeConHandle.GetCreatedStatus() )
					{
						g_QrCodeConHandle.Stop();
						g_QrCodeConHandle.Destory();		//退出二维码配网模式
						// g_Camera.SetNightModeQrcode(0);
					}
				}
				
				//通知其他模块，网络未连接
				networkStatus["param"] = 0;
				IEventManager::instance()->notify("NetWorkStatus", 0, appEventPulse, NULL, NULL, &networkStatus);
				memset(m_ip,0,sizeof(m_ip));
				m_nwLinkMode = NET_WORK_MODE_NONE;
				m_eWlanStat = WLAN_IDLE;
				if( Check((char*)"eth0") )
				{
					g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_FAST_FLICKER);
					StopWifiStationMode();
//					if( true == g_TuyaHandle.GetRegisterStatus() )
					{
						WifiIfconfigDown();
						CTime::sleep(1000);
						NetSetEth(m_ConfigWifi.bStaticIpEnable, m_ConfigWifi.HostIP.l, m_ConfigWifi.Submask.l, m_ConfigWifi.Gateway.l, m_ConfigWifi.Dns.l);
					}
//					else
//					{
//						NetSetEth_Peiwang();
//					}
					
//					CTime::sleep(3 * 1000);
					m_eth0_status = NET_CHECK_ETH0_INLINE;
				}
				else
				{
					//正常情况下不应该走到这里	
					AppErr("eth0....check error!\n");
				}
			}
			else if( NET_CHECK_ETH0_INLINE == m_eth0_status )
			{
				// printf("check eth0 if inline...\n");
				static unsigned char uchEthUnexpectedCount = 0;		//eth 异常计数
				char ip[20] = {0};
				int b = NetGetLocalIp("eth0",ip);
				if ((b == 0)&& (ip[0] != '\0')&&(NetIfInLine((char*)"eth0") == 0))
				{
					m_bCanScanQrCode = false;
					
					g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_ON);
					uchEthUnexpectedCount = 0;
					m_nActiveNet = 0;//eth0
					m_eWlanStat = WLAN_IDLE;
					//通知其他模块，网络连接成功
					networkStatus["param"] = 1;
					IEventManager::instance()->notify("NetWorkStatus", 0, appEventPulse, NULL, NULL, &networkStatus);
					strncpy(m_ip,ip,20);
					m_nwLinkMode = NET_WORK_MODE_ETH0;
				}
				else
				{
					AppErr("eth0 get ip unsucc...\n");
					//多次失败的情况下，应该重新连接
					uchEthUnexpectedCount++;
					if( uchEthUnexpectedCount > 20 )
					{
						m_eth0_status = NET_CHECK_ETH0_VALID;
						uchEthUnexpectedCount = 0;
					}
				}
			}
		}
		else if( m_bWifiEnable ) 	//网线未插入
		{
			m_eth0_status = NET_CHECK_ETH0_VALID;
			
			if ((true == m_bCanScanQrCode) && (m_ConfigWifi.strSSID.length() == 0)) //插过有线 && 没配网
			{
				bWifiConfig = true;
				if (false == bApQrcodeStart)
				{
					bApQrcodeStart = true;
					QrcodeStartTime = GetSystemUptime_s();
					
					g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_SLOW_FLICKER);
					//启动二维码扫描
					if( false == g_QrCodeConHandle.GetCreatedStatus() )
					{
						g_Camera.setMode_ScanQrcode(1);
						g_QrCodeConHandle.Create(QrCodeExtractedCB);
						g_QrCodeConHandle.Start();
					}
					
					//间隔30 s 循环播报请配置WiFi语音提示
					s_QrcodeExit = 0;		//停止播报
					CreateDetachedThread((char*)"DoQrcode",DoQrcode, (void *)NULL, true);	
				}
			}
			else
			{
				if( NET_CHECK_WLAN0_VALID ==  m_wifi_status)
				{
					//通知其他模块，网络未连接
					networkStatus["param"] = 0;
					IEventManager::instance()->notify("NetWorkStatus", 0, appEventPulse, NULL, NULL, &networkStatus);
					memset(m_ip,0,sizeof(m_ip));
					m_nwLinkMode = NET_WORK_MODE_NONE;
					m_eWlanStat = WLAN_IDLE;
					AppInfo("check wlan0...\n");
					if (Check((char*)"wlan0"))
					{
						AppInfo("m_ConfigWifi.strSSID=%s\n",m_ConfigWifi.strSSID.c_str());
						AppInfo("m_ConfigWifi.strSSID.length=%d\n",m_ConfigWifi.strSSID.length());
						AppInfo("m_ConfigWifi.strKeys=%s\n",m_ConfigWifi.strKeys.c_str());
						AppInfo("m_ConfigWifi.strKeys.length=%d\n",m_ConfigWifi.strKeys.length());
	
						if ( !(m_ConfigWifi.strSSID == "") /*&& !(m_ConfigWifi.strKeys == "")*/ ) 
						{
							m_bCanScanQrCode = false;
							if (bApQrcodeStart)
							{
								bApQrcodeStart = false;
								s_QrcodeExit = 1;		//停止播报
								if( true == g_QrCodeConHandle.GetCreatedStatus() )
								{
									g_QrCodeConHandle.Stop();
									g_QrCodeConHandle.Destory();		//退出二维码配网模式
									// g_Camera.SetNightModeQrcode(0);
								}
							}
							
							g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_FAST_FLICKER);

							wifi_connect_result_g = WIFI_DISCONNECTED;
							wifi_connect_result_last_g = WIFI_DISCONNECTED;
							StartWifiStationMode(); 		//开始连接WiFi
							m_wifi_status = NET_CHECK_WLAN0_INLINE;
						}
					}
					else
					{
						//正常情况下不应该走到这里	
						AppErr("wifi....check error!\n");
					}
				}
				else if( NET_CHECK_WLAN0_INLINE ==	m_wifi_status)
				{
					AppInfo("check wlan0 if inline...\n");
					if( WIFI_DISCONNECTED == wifi_connect_result_g )
					{
						////WIFI未连接
						AppErr("wifi is disconected...\n"); 	
                        
					}
					else if( (WIFI_PSW_ERROR == wifi_connect_result_g) || (WIFI_CONN_FAILED == wifi_connect_result_g) )
					{
						//WIFI密码错误
						if( wifi_connect_result_last_g != wifi_connect_result_g )
						{
							if(check_firtst_connectfail == 0)
							{
								check_firtst_connectfail = 1;
								if(wifi_connect_result_g == WIFI_PSW_ERROR)
								{
									m_eWlanStat = WLAN_CONN_FAIL;
									AppErr("wifi psw is error!!!\n");
									audioFile.strFileName = AUDIO_FILE_WIFI_INCORRECT;
									audioFile.type = 0;
									g_AudioPrompt.aoPlay(audioFile);
								}
								else
								{
									m_eWlanStat = WLAN_CONN_FAIL;
									AppErr("wifi connect fail!!!\n");
									audioFile.strFileName = AUDIO_FILE_WIFI_CONNECT_ERROR;
									audioFile.type = 0;
									g_AudioPrompt.aoPlay(audioFile);
								}
							}
						
							wifi_connect_result_last_g = wifi_connect_result_g;
						}

						//如果在配网阶段密码错误，重启配网
						if (bWifiConfig)
						{                            
							m_ConfigWifi.strSSID = "";
							m_ConfigWifi.strKeys = "";
						}
						
						m_wifi_status = NET_CHECK_WLAN0_VALID;
						wifi_connect_result_g = WIFI_DISCONNECTED;
						wifi_connect_result_last_g = WIFI_DISCONNECTED;

						wifi_connect_failed_count++;
						
						AppErr("wifi connect failed. count=[%d]\n", wifi_connect_failed_count);
						if( wifi_connect_failed_count >= 5 )
						{
                            wifi_connect_failed_count = 0;
						}
					}
					else if( WIFI_CONNECTING == wifi_connect_result_g )
					{
						//WIFI正在连接
						// printf("wifi is conecting...\n");
						if( (time(NULL) - wifi_connecting_aoPlay_time) > 10 )
						{
							wifi_connecting_aoPlay_time = time(NULL);
							AppErr("ENUM_INDICATOR_LIGHT_FAST_FLICKER\n");
							
							if(access(NO_VOICE_PROMPT_FLAG, F_OK) != 0)
							{
								//语音提示
								if(check_firtst_connecting == 0)
								{
									check_firtst_connecting = 1;
									audioFile.strFileName = AUDIO_FILE_WIFI_CONNECTING;
									audioFile.type = 0;
									g_AudioPrompt.aoPlay(audioFile);
								}
							}
							m_eWlanStat = WLAN_CONNECTING;
						}
					}
					else if( WIFI_CONNECTED == wifi_connect_result_g )
					{
						//WIFI已连接
						// printf("wifi is conected...\n");
						m_eWlanStat = WLAN_CONNECTED;
						static unsigned char uchWifiUnexpectedCount = 0; 		//WiFi 异常计数
						char ip[20] = {0};
						int b = NetGetLocalIp((char*)"wlan0",ip);
						if ((b == 0) && (ip[0] != '\0')&&(NetIfInLine((char*)"wlan0") == 0)) 
						{
							g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_ON);
							//printf("wlan0 is inline...\n");
							uchWifiUnexpectedCount = 0;
							m_nActiveNet = 1;//wifi
							m_eWlanStat = WLAN_GOT_IP;

							if( wifi_connect_result_last_g != wifi_connect_result_g )
							{
								wifi_connect_result_last_g = wifi_connect_result_g;
								
								if(access(NO_VOICE_PROMPT_FLAG, F_OK) != 0)
								{
									//语音提示
									if(check_firtst_connected == 0)
									{
										check_firtst_connected = 1;
										audioFile.strFileName = AUDIO_FILE_WIFI_CONNECTED;
										audioFile.type = 0;
										g_AudioPrompt.aoPlay(audioFile);
									}
								}
							}
							
							networkStatus["param"] = 3;
							IEventManager::instance()->notify("NetWorkStatus", 0, appEventPulse, NULL, NULL, &networkStatus);
							strncpy(m_ip,ip,20);
							m_nwLinkMode = NET_WORK_MODE_STA;
							wifi_connect_failed_count = 0;
							
							//如果在配网阶段，保存WiFi信息
							if (bWifiConfig)
							{
								bWifiConfig = false;
								Json::Value WifiTable;
								TExchangeAL<NetWifiConfig>::setConfig(m_ConfigWifi, WifiTable);
								g_configManager.setConfig(getConfigName(CFG_WIFI), WifiTable, 0, IConfigManager::applyOK);
							}
						}
						else
						{
							//正常情况下不应该走到这里
							AppErr("wifi get ip unsucc...\n");
							
							//多次失败的情况下，应该重新连接WiFi
							uchWifiUnexpectedCount++;
							if( uchWifiUnexpectedCount > 20 )
							{
								m_eWlanStat = WLAN_IDLE;
								m_wifi_status = NET_CHECK_WLAN0_VALID;
								wifi_connect_result_g = WIFI_DISCONNECTED;
								wifi_connect_result_last_g = WIFI_DISCONNECTED;
								uchWifiUnexpectedCount = 0;
							}
						}
					}
				}
			}

		}
		
		CTime::sleep(3 * 1000);
	}
}

void CNetConfigHook::SetWifiSwitch(bool enable)
{
	m_bWifiEnable = enable;
}

void CNetConfigHook::SetWifi(const char *ssid, const char *pwd)
{
	m_ConfigWifi.strSSID = ssid;
	m_ConfigWifi.strKeys = pwd;

	wifi_connect_result_g = WIFI_DISCONNECTED;
	wifi_connect_result_last_g = WIFI_DISCONNECTED;
	m_wifi_status = NET_CHECK_WLAN0_VALID;

#if 0
	CAudioPrompt::AudioFileParm audioFile;
	audioFile.strFileName = AUDIO_FILE_IAT;
	audioFile.type = 0;
	g_AudioPrompt.aoPlay(audioFile);
#endif
	AppInfo("CNetConfigHook----------------SetWifi\n");
	AppInfo("wifi ssid : %s\n", m_ConfigWifi.strSSID.c_str());
	AppInfo("wifi pwd : %s\n", m_ConfigWifi.strKeys.c_str());
}

int CNetConfigHook::GetNetWorkIp(char *pIp, int len)
{
	if (pIp)
	{
		strncpy(pIp,m_ip,len);
		return 0;
	}
	
	return -1;
}

void CNetConfigHook::OnCfgNetComm(const CConfigTable & table, int &ret)
{
	TExchangeAL<NetCommonConfig>::getConfig(table, m_ConfigNet);
}
void CNetConfigHook::OnCfgWifiComm(const CConfigTable& table, int &ret)
{
	//WiFi配置每次有改动都会调用次函数更新m_ConfigWifi
	AppInfo("OnCfgWifiComm---------wifi config change------------------------ret=%d>\n",ret);
	TExchangeAL<NetWifiConfig>::getConfig(table, m_ConfigWifi);
}
void CNetConfigHook::OnCfgAPComm(const CConfigTable& table, int &ret)
{
	//AP配置每次有改动都会调用次函数更新m_ConfigAP
	AppInfo("OnCfgAPComm---------AP config change------------------------ret=%d>\n",ret);
	TExchangeAL<NetAPConfig>::getConfig(table, m_ConfigAP);
}

void CNetConfigHook::onHookNetCommon(CConfigTable &cfgNetCommon, int &ret)
{
}

void CNetConfigHook::onHookNetDDNS(CConfigTable &cfgNetDDNS, int &ret)
{
}

void CNetConfigHook::onHookNetPPPoE(CConfigTable &cfgNetPPPOE, int &ret)
{
}

void CNetConfigHook::onHookNetDHCP(CConfigTable &cfgNetDHCP, int &ret)
{
}

void CNetConfigHook::onHookNetDNS(CConfigTable &cfgNetDNS, int &ret)
{
}

extern ConfigPair s_3gNetType[WIRELESS_MAX];
void CNetConfigHook::onHook3GNet(CConfigTable &cfgNet3G, int &ret)
{
}

void CNetConfigHook::onHookWifi(CConfigTable &cfgNetWifi, int &ret)
{
}

void CNetConfigHook::onHookNetOrder(CConfigTable &cfgNetOrder, int &ret)
{
}

int CNetConfigHook::Dump(int argc, char **argv)
{
	return 0;
}
void CNetConfigHook::ReConn()
{
	AppErr("ReConn\n");
	m_eWlanStat = WLAN_IDLE;
	memset(m_ip,0,sizeof(m_ip));
	m_nwLinkMode = NET_WORK_MODE_NONE;

	m_eth0_status = NET_CHECK_ETH0_VALID;
	m_wifi_status = NET_CHECK_WLAN0_VALID;
	wifi_connect_result_g = WIFI_DISCONNECTED;
	wifi_connect_result_last_g = WIFI_DISCONNECTED;
}
void CNetConfigHook::onAppEvent(std::string code, int index, appEventAction action, const EventHandler *param, Json::Value* data)
{
    if ("MqttError" == code)
    {
        AppErr("MqttErrors\n");
		
		ReConn();
    }
	return;
}

bool CNetConfigHook::GetQrcodeEnable()
{
	return m_bCanScanQrCode;
}

