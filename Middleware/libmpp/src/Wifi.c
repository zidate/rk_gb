#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <net/route.h>
#include <ctype.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/if_arp.h>

#include "PAL/MW_Common.h"
#include "PAL/Net.h"

#define WPA_CONFIG "/tmp/wpa_conf"

#if 0
static char *hostapd_conf[24] = {"interface=wlan0\n",
						"ctrl_interface=/var/run/hostapd\n",
						"channel=6\n",
						"wpa=2\n",
						"eap_server=1\n",
						"wps_state=2\n",
						"uuid=12345678-9abc-def0-1234-56789abcdef0\n",
						"device_name=RTL8188F\n",
						"manufacturer=Realtek\n",
						"model_name=RTW_SOFTAP\n",
						"model_number=WLAN_CU\n",
						"serial_number=12345\n",
						"device_type=6-0050F204-1\n",
						"os_version=01020300\n",
						"config_methods=label display push_button keypad\n",
						"beacon_int=100\n",
						"hw_mode=g\n",
						"ieee80211n=1\n",
						"wme_enabled=1\n",
						"ht_capab=[SHORT-GI-20][SHORT-GI-40][HT40+]\n",
						"wpa_key_mgmt=WPA-PSK\n",
						"wpa_pairwise=CCMP\n",
						"max_num_sta=5\n",
						"wpa_group_rekey=86400\n"};
#else
static char *hostapd_conf[] = {"interface=wlan0\n",
						"ctrl_interface=/var/run/hostapd\n",
						"channel=6\n",
//						"eap_server=1\n",
//						"wps_state=2\n",
//						"uuid=12345678-9abc-def0-1234-56789abcdef0\n",
//						"device_name=RTL8188F\n",
//						"manufacturer=Realtek\n",
//						"model_name=RTW_SOFTAP\n",
//						"model_number=WLAN_CU\n",
//						"serial_number=12345\n",
//						"device_type=6-0050F204-1\n",
//						"os_version=01020300\n",
//						"config_methods=label display push_button keypad\n",
//						"beacon_int=100\n",
						"hw_mode=g\n",
//						"ieee80211n=1\n",
						"wme_enabled=1\n",
//						"ht_capab=[SHORT-GI-20][SHORT-GI-40][HT40+]\n",
//						"wpa_key_mgmt=WPA-PSK\n",
//						"wpa_pairwise=CCMP\n",
						"max_num_sta=5\n",
						"wpa_group_rekey=86400\n"};
#endif



typedef void *(*wifi_funcThread)(void*);

int CreateDetachedThread(wifi_funcThread route, void *param, int scope)
{
	int ret;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init( &attr);
	if (scope)
	{
		pthread_attr_setscope( &attr, PTHREAD_SCOPE_SYSTEM);
	}
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create( &thread, &attr, route, param);
	pthread_attr_destroy( &attr);
	return ret;
}

//创建AP配置文件
///< 0:succ 	-1:failed
static int CreateHostapdConfig(const char *ssid, const char *passwd)
{
	FILE *fp = NULL;
	char buffer[128];
	int len, ret;
	int i;
	if ((fp = fopen("/tmp/hostapd.conf", "wb")) == NULL)
	{
		EMSG("hostapd.conf fopen is NULL!");	
		return -1;
	}

	for( i = 0; i < sizeof(hostapd_conf) / sizeof(char *); i++ )
	{
		snprintf(buffer, 128, "%s", hostapd_conf[i]);
		len = strlen(buffer);
		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
			goto err;
		}
	}

	snprintf(buffer, 128, "ssid=%s\n", ssid);
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
		goto err;
	}
	
	if( strlen(passwd) > 0 )
	{
		snprintf(buffer, 128, "wpa_passphrase=%s\n", passwd);
		len = strlen(buffer);
		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
			goto err;
		}

		snprintf(buffer, 128, "wpa=2\n");
		len = strlen(buffer);
		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
			goto err;
		}
		
		snprintf(buffer, 128, "wpa_key_mgmt=WPA-PSK\n");
		len = strlen(buffer);
		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
			goto err;
		}
		
		snprintf(buffer, 128, "wpa_pairwise=CCMP\n");
		len = strlen(buffer);
		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("hostapd.conf fwrite is err, len:%d,ret:%d!", len, ret);
			goto err;
		}	
	}

	if( fp )
	{
		fflush(fp);
		fclose(fp);
	}
	return 0;

err:
	if( fp )
		fclose(fp);
	return -1;
}



void testAPConfig_create()
{
	EMSG("create ap conf : %d\n", CreateHostapdConfig("test_ap", "123456789"));
}


//创建WPA配置文件
static int CreateWpaConfig(int encrypt, const char *ssid, const char *passwd)
{
	FILE *fp = NULL;
	char buffer[128];
	int len, ret;
	const char *encryType[] = {"NONE", "WEP", "WPA", "WPA2"};

	if ((fp = fopen("/tmp/wpa_supplicant.conf", "wb")) == NULL)
	{
		EMSG("devmanage wifimanager fopen is NULL!");	
		return -1;
	}

	//ctrl_interface
	sprintf(buffer, "ctrl_interface=/var/run/wpa_supplicant\n\t\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	//config
	sprintf(buffer, "network={\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	sprintf(buffer, "\tssid=\"%s\"\n", ssid);
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	if(( encrypt == 0) || (0 == strlen(passwd)) )
	{
		sprintf(buffer, "\tkey_mgmt=%s\n}\n", encryType[0]);
		len = strlen(buffer);

		if((ret = fwrite(buffer, 1, len, fp)) != len)
		{
			EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
			fclose(fp);
			return -1;
		}	 
		fclose(fp);
		return 0;
	}

	sprintf(buffer, "\tproto=WPA RSN\n");      
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}	    

	sprintf(buffer, "\tkey_mgmt=WPA-PSK\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}


	sprintf(buffer, "\tpairwise=TKIP CCMP\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	sprintf(buffer, "\tgroup=TKIP CCMP\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	sprintf(buffer, "\tpsk=\"%s\"\n", passwd);
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	sprintf(buffer, "\tpriority=1\n}\n");
	len = strlen(buffer);
	if((ret = fwrite(buffer, 1, len, fp)) != len)
	{
		EMSG("devmanage wifimanager fwrite is err, len:%d,ret:%d!", len, ret);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

//设置Wifi连接模式
static int SetWifiLinkMode(int encrypt,const char *ssid,const char *passwd)
{
	char buffer[128];

	if(encrypt > 2)	//wpa,none
	{
		CreateWpaConfig(encrypt, ssid, passwd);
		//sprintf(buffer, "wpa_supplicant -Dwext -i%s -c /etc/conf/wpa_supplicant.conf -B > %s", "wlan0",WPA_CONFIG);
		sprintf(buffer, "wpa_supplicant -Dnl80211 -i%s -c /tmp/wpa_supplicant.conf &","wlan0");
		MW_START_PROCESS("sh", "sh", "-c", buffer, NULL);
		MSG("devmanage wifimanager %s\n", buffer);
	}

	return 0;
}

//检测wifi密码是否正确
static int CheckWifiPwd(void)
{
	int rate = 0;
	FILE *fp = NULL;
	char buf[256];
	char *p = NULL;
	fp = fopen(WPA_CONFIG, "r");
	if(fp == NULL)
	{
		return -1;
	}

	memset(buf, 0x0, sizeof(buf));
	while(fgets(buf, sizeof(buf), fp))
	{
		p = buf;
		while(*p && isspace(*p))
			p++;

		p = strstr(buf, "reason=WRONG_KEY");
		if(p!=NULL)
		{
			fclose(fp);
			return 0;
		}
		p = strstr(buf, "Failed to read or parse configuration");
		if(p!=NULL)
		{
			fclose(fp);
			return 1;
		}
		p = strstr(buf, "Invalid passphrase");
		if(p!=NULL)
		{
			fclose(fp);
			return 1;
		}
		p = strstr(buf, "failed to parse psk");
		if(p!=NULL)
		{
			fclose(fp);
			return 1;
		}
		p = strstr(buf, "failed to parse network block");
		if(p!=NULL)
		{
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//下面是 与上层的接口
///////////////////////////////////////////////////////////////////////////////////////////////////
#define MAC_SIZE    18  
#define IP_SIZE	   16 

static int WifiMode = -1;

//-------------------------------
Wifi_Model_info_S s_stWifiModelInfo = {WIFI_MODEL_UNKNOWN, 0, 0};

#define WIFI_RTL8188FTV_ID 	"0bda:f179"
#define WIFI_SV6256P_ID 	"8065:6000"
#define WIFI_ATBM6012B_ID 	"007a:888b"
#define WIFI_ATBM6062_ID 	"007a:6052"
#define WIFI_ATBM6132_ID 	"007a:8890"
#define WIFI_AIC8800DL_ID 	"a69c:8d80"
typedef struct{
	WIFI_MODEL_E eModel;
	const char *strId;
} WifiModelId_S;


//初始化WiFi 识别并加载驱动
void WifiInit()
{
	WifiModelId_S astWifiModelId[] = {{WIFI_MODEL_RTL8188FTV, WIFI_RTL8188FTV_ID},
									  {WIFI_MODEL_SV6256P, WIFI_SV6256P_ID}, 
									  {WIFI_MODEL_ATBM6012B, WIFI_ATBM6012B_ID}, 
									  {WIFI_MODEL_ATBM6062, WIFI_ATBM6062_ID},
									  {WIFI_MODEL_ATBM6132, WIFI_ATBM6132_ID},
									  {WIFI_MODEL_AIC8800DL, WIFI_AIC8800DL_ID}};
	//reset wifi
	SystemWifiPwrCtl(1); //WiFi模块下电	
	sleep(1);
	SystemWifiPwrCtl(0); //WiFi模块上电	
	sleep(1);

	const int RETRY_COUNT = 5;
	int i;
	for (i = 0; i < RETRY_COUNT; i++)
	{
		FILE *pp = popen("lsusb", "r");
		if(pp == NULL)
		{
			printf("popen lsusb fails. errno: %d\n", errno);
			usleep(500*1000);
			continue ;
		}
		
		char buffer[128];
		memset(buffer, 0, sizeof(buffer));
		
		while (fgets(buffer, sizeof(buffer), pp) != NULL)
		{
			printf("%s\n", buffer);
			
			if (strstr(buffer, WIFI_RTL8188FTV_ID)) 	//rtl8188ftv
			{
				printf("wifi is rtl8188ftv\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/libarc4.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/rtl8188ftv/cfg80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/mac80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/rtl8188ftv/8188fu.ko", NULL);
				s_stWifiModelInfo.model = WIFI_MODEL_RTL8188FTV;
				s_stWifiModelInfo.is5G = 0;
				s_stWifiModelInfo.isBle = 0;
				break;
			}
			else if (strstr(buffer, WIFI_SV6256P_ID)) 	//sv6256p
			{
				printf("wifi is sv6256p\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/cfg80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/libarc4.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/mac80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/ctr.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/ccm.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/libaes.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/aes_generic.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod  /oem/usr/ko/ssv6x5x.ko", NULL); 

				s_stWifiModelInfo.model = WIFI_MODEL_SV6256P;
				s_stWifiModelInfo.is5G = 1;
				s_stWifiModelInfo.isBle = 0;

				break;
			}
			else if (strstr(buffer, WIFI_ATBM6012B_ID)) 	//atbm6012b
			{
				printf("wifi is atbm6012b\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/libarc4.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/cfg80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/mac80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/atbm6012b/atbm603x_comb_wifi_usb.ko wifi_bt_comb=1", NULL);

				s_stWifiModelInfo.model = WIFI_MODEL_ATBM6012B;
				s_stWifiModelInfo.is5G = 0;
				s_stWifiModelInfo.isBle = 1;
				break;
			}
			else if (strstr(buffer, WIFI_ATBM6062_ID)) 	//atbm6062
			{
				printf("wifi is atbm6062\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/libarc4.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/atbm6062/cfg80211.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/atbm6062/ATBM606x_wifi_usb.ko wifi_bt_comb=1", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "iwpriv wlan0 common set_rate_txpower_mode,6", NULL);
				
				s_stWifiModelInfo.model = WIFI_MODEL_ATBM6062;
				s_stWifiModelInfo.is5G = 0;
				s_stWifiModelInfo.isBle = 1;
				break;
			}
			else if (strstr(buffer, WIFI_ATBM6132_ID)) 	//atbm6132
			{
				printf("wifi is atbm6132\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/libarc4.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/cfg80211.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/mac80211.ko", NULL); 
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/atbm6132/atbm6x3x_wifi_usb.ko wifi_bt_comb=1", NULL); 

				s_stWifiModelInfo.model = WIFI_MODEL_ATBM6132;
				s_stWifiModelInfo.is5G = 1;
				s_stWifiModelInfo.isBle = 1;
				break;
			}
			else if (strstr(buffer, WIFI_AIC8800DL_ID)) 	//atbm6132
			{
				printf("wifi is aic8800dl\n");
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/libarc4.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/cfg80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/mac80211.ko", NULL);
				MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/aic8800dl/aic_load_fw.ko aic_fw_path=/oem/usr/ko/aic8800dl/aic8800D80", NULL);
				int wait_count = 0;
				while(wait_count++ < 10)
				{
					sleep(1);
					MW_START_PROCESS("sh", "sh", "-c", "lsusb > /tmp/lsusb.output", NULL);
					FILE *fp = fopen("/tmp/lsusb.output", "r");
					if(fp == NULL)
					{
						printf("fopen /tmp/lsusb.output fails. errno: %d\n", errno);
						continue ;
					}

					int load_fw_succ = 0;
					char str_line[64];					
					while (fgets(str_line, sizeof(str_line), fp) != NULL)
					{
						if (strstr(str_line, "a69c:8d81"))
						{
							load_fw_succ = 1;
							break;
						}
					}
					fclose(fp);
					MW_START_PROCESS("sh", "sh", "-c", "rm /tmp/lsusb.output", NULL);
					if (load_fw_succ)
					{
						MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/aic8800dl/aic8800_fdrv.ko", NULL);
//						MW_START_PROCESS("sh", "sh", "-c", "insmod /oem/usr/ko/aic8800dl/aic_btusb.ko", NULL); //用于搜索热点
						
						s_stWifiModelInfo.model = WIFI_MODEL_AIC8800DL;
						s_stWifiModelInfo.is5G = 1;
						s_stWifiModelInfo.isBle = 1;
						break;
					}
				}
				break;
			}
		}
		pclose(pp);

		if (WIFI_MODEL_UNKNOWN != s_stWifiModelInfo.model)
			break;
		usleep(500*1000);
	}
}




//-------------------------------
/// 创建ap模式
int WifiGetMode()
{
	return WifiMode;
}

int WifiIfconfigDown()
{
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 0.0.0.0", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 down", NULL);
	WifiMode = WIFI_MODE_END;
}

/// 创建ap模式
/// 
/// \retval 0 创建失败
/// \retval 0 创建成功

int WifiApModeCreate(const char *ssid, const char *passwd)
{
	MSG("Middleware WifiApModeCreate\n");
	//if (WifiMode != WIFI_MODE_END)
	//	return -1;

	if( 0 != CreateHostapdConfig(ssid, passwd) )
	{
		EMSG("create ap mode err!!!---------->create hostapd.conf err!!!\n");
		return -1;
	}

	
//	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 down", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall hostapd", NULL);
//	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
//	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);
	
	usleep(1000*100);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 192.168.175.1 netmask 255.255.255.0 &", NULL);
	usleep(1000*100);
	MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", "hostapd -d /tmp/hostapd.conf &", NULL);
	usleep(1000*100);
	MW_START_PROCESS("sh", "sh", "-c", "touch /var/lib/misc/udhcpd.leases", NULL);
	usleep(1000*100);
	MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", "udhcpd  -f /usr/share/udhcpc/udhcpd.conf &", NULL);

	sleep(2);
	//MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);

	WifiMode = WIFI_MODE_AP;
	return 0;	
}
/// 销毁ap模式
/// 
/// \retval 0 销毁失败
/// \retval 0 销毁成功
int WifiApModeDestroy()
{
	MSG("Middleware WifiApModeDestroy\n");
	//if (WifiMode != WIFI_MODE_AP)
	//	return -1;
#if 0
	system("killall udhcpd");
	system("killall hostapd");
#else
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall hostapd", NULL);
#endif
	WifiMode = WIFI_MODE_END;
	return 0;	
}

static WIFI_ST_t s_stWifiConf;
static int wifi_connecting = 0;
static int wifi_connect_thread_running = 0;
void *thread_get_wifi_connect_result(void *arg)
{
	wifi_connect_thread_running = 1;

	int ret;
	FILE *fp = NULL;
	int wrong_key_count = 0;
	int else_count = 0;
	wifi_status_enum wifi_status = WIFI_DISCONNECTED;
	char *p = NULL;
	char buffer[256];
	connect_wifi_result_cb cb = (connect_wifi_result_cb)arg;

//	remove("/system/etc/wpa_output");
	sprintf(buffer, "wpa_supplicant -Dnl80211 -i%s -c /tmp/wpa_supplicant.conf -f /tmp/wpa_output &","wlan0");
	MSG("cmd : %s\n", buffer);


		//删除默认路由eth0
//		MW_START_PROCESS("sh", "sh", "-c", "route del default eth0", NULL);

	MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", buffer, NULL);


	wifi_status = WIFI_CONNECTING;
	if(cb)
		cb(wifi_status);

	while( wifi_connecting && (WIFI_CONNECTING == wifi_status) )
	{
		sleep(1);

//		MSG("wifi connecting...\n");
		else_count++;
		if( else_count >= 60)
		{
			EMSG("wifi connect falied...\n");
			wifi_status = WIFI_CONN_FAILED;
			break;
		}		

		fp = fopen("/tmp/wpa_output", "r");
		if( NULL == fp )
			continue;

		while (fgets(buffer, sizeof(buffer), fp) != NULL)
		{
//			MSG("%s\n", buffer);
			if ((p = strstr(buffer, "CTRL-EVENT-CONNECTED")) != NULL)
			{
				MSG("wifi connected...\n");
				wifi_status = WIFI_CONNECTED;
				break;
			}
			
			if ((p = strstr(buffer, "reason=WRONG_KEY")) != NULL)
			{
				wrong_key_count++;
				if( wrong_key_count >= 2 )
				{
					EMSG("wifi passwd error...\n");
					wifi_status = WIFI_PSW_ERROR;
					break;
				}
			}
		}
		
		if(fp)
			fclose(fp);
	}


	remove("/tmp/wpa_output");

	if( WIFI_CONNECTED == wifi_status )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 down", NULL);
		
		char strIp[16];
		char strNetmask[16];
		char strGateway[16];
		char strDns[16];
		if (s_stWifiConf.bStaticIp)
		{
			if (inet_ntop(AF_INET, &s_stWifiConf.ip, strIp, sizeof(strIp)) == NULL || 
				inet_ntop(AF_INET, &s_stWifiConf.netmask, strNetmask, sizeof(strNetmask)) == NULL || 
				inet_ntop(AF_INET, &s_stWifiConf.gateway, strGateway, sizeof(strGateway)) == NULL || 
				inet_ntop(AF_INET, &s_stWifiConf.dns, strDns, sizeof(strDns)) == NULL)
				s_stWifiConf.bStaticIp = 0;
		}
		
		if (0 == s_stWifiConf.bStaticIp)
		{
			MSG("udhcpc -b -i wlan0 -h ipcam -s /usr/share/udhcpc/default.script\n");
			MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", "udhcpc -b -i wlan0 -h ipcam -s /usr/share/udhcpc/default.script", NULL);
		}
		else
		{
			snprintf(buffer, sizeof(buffer), "ifconfig wlan0 %s netmask %s", strIp, strNetmask);
			printf("cmd : %s\n", buffer);
			MW_START_PROCESS("sh", "sh", "-c", buffer, NULL);
			snprintf(buffer, sizeof(buffer), "route add default gw %s", strGateway);
			printf("cmd : %s\n", buffer);
			MW_START_PROCESS("sh", "sh", "-c", buffer, NULL);
			snprintf(buffer, sizeof(buffer), "echo -e \'nameserver %s\' > /etc/resolv.conf", strDns);
			MW_START_PROCESS("sh", "sh", "-c", buffer, NULL);
		}
		
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);

		WifiMode = WIFI_MODE_ST;
	}
	
	if(cb)
		cb(wifi_status);

	wifi_connect_thread_running = 0;
	return NULL;
}

int NetGetWifiConnectStatus(WIFI_ST_t *pStm,int timeout)
{
	int ret;
	FILE *fp = NULL;
	int wrong_key_count = 0;
	int else_count = 0;

	char *p = NULL;
	char buffer[256];
	int connected = 0;
	//------------------------------为WiFi连接做准备
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall hostapd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);

	usleep(1000*100);
    MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
    usleep(1000*100);

	int encrypt = 3;
	const char *ssid = pStm->ssid;
	const char *passwd  = pStm->psk;

	if( 0 != strlen(passwd) )
		CreateWpaConfig(encrypt, ssid, passwd);
	else
		CreateWpaConfig(0, ssid, passwd);
	//-----------------------------


	sprintf(buffer, "wpa_supplicant -Dnl80211 -i%s -c /tmp/wpa_supplicant.conf -f /tmp/wpa_output &","wlan0");
	MSG("cmd : %s\n", buffer);
	MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", buffer, NULL);

	while(1)
	{
		sleep(1);

		else_count++;
		if( else_count >= timeout)
		{
			printf("wifi connect falied................................................................................\n");
			break;
		}		

		fp = fopen("/tmp/wpa_output", "r");
		if( NULL == fp )
			continue;

		while (fgets(buffer, sizeof(buffer), fp) != NULL)
		{
			if ((p = strstr(buffer, "CTRL-EVENT-CONNECTED")) != NULL)
			{
				printf("wifi connected..................................................................................\n");
				connected = 1;
				goto cleanup;
			}
			
			if ((p = strstr(buffer, "reason=WRONG_KEY")) != NULL)
			{
				wrong_key_count++;
				if( wrong_key_count >= 2 )
				{
					printf("wifi passwd error.................................................................................\n");
					goto cleanup;
				}
			}
		}
		
		if(fp)
			fclose(fp);
	}

cleanup:
    if(fp) 
        fclose(fp);
    remove("/tmp/wpa_output");

    return connected;

}




/// 创建station模式
/// 
/// \retval 0 创建失败
/// \retval 0 创建成功
int WifiStationModeCreate(WIFI_ST_t *pStm, connect_wifi_result_cb cb)
{
	char buffer[128];
	MSG("WifiStationModeCreate\n");
	// MSG("%s,%s\n",pStm->ssid,pStm->psk);
		//if (WifiMode != WIFI_MODE_END)
		//return -1;
	
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall hostapd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);

	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 0.0.0.0", NULL);
	usleep(1000*100);
    MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 0.0.0.0", NULL);
    usleep(1000*100);

	int encrypt = 3;
	const char *ssid = pStm->ssid;
	const char *passwd  = pStm->psk;

//<shang>	SetWifiLinkMode(encrypt,ssid,passwd);

//	if(encrypt > 2)	//wpa,none
//	{
		if( 0 != strlen(passwd) )
			CreateWpaConfig(encrypt, ssid, passwd);
		else
			CreateWpaConfig(0, ssid, passwd);

		wifi_connecting = 0;
		//如果线程正在运行，等待线程退出
		while( 1 == wifi_connect_thread_running )
			usleep(1000*100);

		memcpy(&s_stWifiConf, pStm, sizeof(WIFI_ST_t));
		wifi_connecting = 1;
		CreateDetachedThread(thread_get_wifi_connect_result, (void *)cb, 1);

	return 0;	
}
/// 销毁station模式
/// 
/// \retval 0 销毁失败
/// \retval 0 销毁成功
int WifiiStationDestroy()
{
	MSG("WifiiStationDestroy\n");
	//if (WifiMode != WIFI_MODE_ST)
	//	return -1;
	wifi_connecting = 0;

	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);

	WifiMode = WIFI_MODE_END;
	return 0;	
}

#if 0
int WifiList(router_list_s* list,int *num)
{
	router_list_s *router_list = NULL;
	int router_num = 0;
	int i = 0;
	
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
	//router_list = (router_list_s *)malloc(sizeof(router_list_s) * WIFI_ROUTER_LIST_NUM);
	get_wrieless_router_list(&list, &router_num,"wlan0");
	
	EMSG("router_num=%d\n",router_num);
	*num = router_num;
	
	if( WIFI_MODE_END == WifiMode )
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 down", NULL);
#if 0
	int ethnum = 0;
	char line[255];
	char ethname[20][30];
	FILE *fp = NULL;
	int n = 0;
    char buf[1024] = {0};
    fp = popen("/usr/dgiot/bin/iwlist wlan0 scanning | grep ESSID","w");
	fread(buf, 1024, 1, fp);
    EMSG("/usr/dgiot/bin/iwlist wlan0 scanning | grep ESSID:%s\n",buf);
	char *pbuf = buf;
	char *p = NULL;
	char *q = NULL;
	int i = 0;
	int j = 0;
	int k = 0;

	while(*pbuf != '\0') {
		if (*pbuf == '"') {
			p = pbuf+1;
			k = 0;
			while(*p != '"') {
				ethname[ethnum][k] = *p;
				k++;
				p++;
			}
			ethnum++;
			pbuf += k+1;
			continue;
		}
		pbuf++;
	}


	
	EMSG("ethnum=%d\n",ethnum);
	for (i =0;i<ethnum;i++) {
		EMSG("ethname[%d]=%s\n",i,ethname[i]);
	}
    pclose(fp);
#endif

}
#endif

int WifiList_2(router_list_2_s* list, int MAX_Len)
{
	int realApNum = -1;
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
	}
	sleep(1);

	FILE *pp = popen("iwlist wlan0 scan", "r");
	if(pp == NULL)
	{
		EMSG("WifiList--->popen fails\r\n");
		goto exit;
	}

	char tmp[512] = {0};
	memset(tmp, 0, sizeof(tmp));



#if 0 	/// debug
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
		EMSG("%s", tmp);
	}
	exit:
		return 0;
#else
	unsigned char bNeedFindHead = 1;
	int recordIdx = -1;
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
//		MSG("%s", tmp);
		/* 首先找BSSID	  以此为基准 */
		char *pBSSIDStart = strstr(tmp, " - Address: ");
		if(pBSSIDStart != NULL)
		{
			recordIdx++;
			if(recordIdx >= MAX_Len)
			{
				EMSG(" Reach Max Record \r\n");
				recordIdx--;
				break;
			}
			
			list[recordIdx].encryptType = 0;
			bNeedFindHead = 0;
			goto ReadNext;
		}
		else
		{
			if(recordIdx < 0)
			{/* 只有找到第一个BSSID 才能继续读下去 */
				goto ReadNext;
			}
		}

		if( 1 == bNeedFindHead )
		{
			goto ReadNext;
		}
		
		{
			/* 查找ssid  */
			char *pSSIDStart = strstr(tmp, "ESSID:\"");
			if(pSSIDStart != NULL)
			{
#if 1
                sscanf(pSSIDStart + strlen("ESSID:\""), "%s", list[recordIdx].essid);
                int s_len = strlen((char *)(list[recordIdx].essid));
                if(s_len != 0)
                {
                    list[recordIdx].essid[s_len - 1] = 0;
                    s_len--;
                }
#else
				snprintf(list[recordIdx].essid, sizeof(list[recordIdx].essid), "%s", pSSIDStart + strlen("ESSID:\""));
#endif
				if( 0 == strlen(list[recordIdx].essid) )
				{
					bNeedFindHead = 1;
					recordIdx--;
				}
				goto ReadNext;
			}
		}
#if 0
		{
			/* 查找channel	*/
			char *pCHANNELStart = strstr(tmp, "(Channel ");
			if(pCHANNELStart != NULL)
			{
				int x = 0;
				sscanf(pCHANNELStart + strlen("(Channel "), "%d)", &x);
				list[recordIdx].channel = x;
				goto ReadNext;
			}
		}
#endif
		{
			/* 查找encrypt  */
			char *p = strstr(tmp, "IE: ");
			if(p != NULL)
			{
				if (strstr(tmp, "WPA "))
					list[recordIdx].encryptType = 2;
				else if (strstr(tmp, "WPA2 "))
					list[recordIdx].encryptType = 3;
				else if (strstr(tmp, "WPA3 "))
					list[recordIdx].encryptType = 4;
				goto ReadNext;
			}
		}

		if (WIFI_MODEL_AIC8800DL == s_stWifiModelInfo.model)
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Signal level=");
			if(pSIGNALStart != NULL)
			{
				int dBm = 0;
				sscanf(pSIGNALStart + strlen("Signal level=") , "%d ", &dBm);
				if (dBm > -30) dBm = -30;
				if (dBm < -100) dBm = -100;
				list[recordIdx].quality = (dBm - (-100)) * 100 / ((-30) - (-100));
				goto ReadNext;
			}
		}
		else
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Quality=");
			if(pSIGNALStart != NULL)
			{
				int x = 0;
				int y = 0;
				sscanf(pSIGNALStart + strlen("Quality=") , "%d/%d ",&x,&y);
				list[recordIdx].quality = x*100/y;
				goto ReadNext;
			}
		}


ReadNext:
		memset(tmp, 0, sizeof(tmp));
	}

	pclose(pp);

exit:
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 down", NULL);
	}

	realApNum = recordIdx + 1;
	return realApNum;
#endif
}


//------------------------------------------------
int WifiList_3(router_list_3_s* list, int MAX_Len)
{
	int realApNum = -1;
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
	}
	sleep(1);

	FILE *pp = popen("iwlist wlan0 scan", "r");
	if(pp == NULL)
	{
		EMSG("WifiList--->popen fails\r\n");
		goto exit;
	}

	char tmp[512] = {0};
	memset(tmp, 0, sizeof(tmp));

	// sleep(6);


#if 0 	/// debug
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
		EMSG("%s", tmp);
	}
	exit:
		return 0;
#else
	unsigned char bNeedFindHead = 1;
	int recordIdx = -1;
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
//		MSG("%s", tmp);
		/* 首先找BSSID	  以此为基准 */
		char *pBSSIDStart = strstr(tmp, " - Address: ");
		if(pBSSIDStart != NULL)
		{
			recordIdx++;
			if(recordIdx >= MAX_Len)
			{
				EMSG(" Reach Max Record \r\n");
				recordIdx--;
				break;
			}

			/* 解析 BSSID */
			char *pAddr = pBSSIDStart + strlen(" - Address: ");
			while(*pAddr == ' ') pAddr++;
			strncpy(list[recordIdx].bssid, pAddr, 17);
			list[recordIdx].bssid[17] = '\0';

			
			
			list[recordIdx].encryptType = 0;
			bNeedFindHead = 0;
			goto ReadNext;
		}
		else
		{
			if(recordIdx < 0)
			{/* 只有找到第一个BSSID 才能继续读下去 */
				goto ReadNext;
			}
		}

		if( 1 == bNeedFindHead )
		{
			goto ReadNext;
		}
		
		{
			/* 查找ssid  */
			char *pSSIDStart = strstr(tmp, "ESSID:\"");
			if(pSSIDStart != NULL)
			{
#if 1
                sscanf(pSSIDStart + strlen("ESSID:\""), "%s", list[recordIdx].essid);
                int s_len = strlen((char *)(list[recordIdx].essid));
                if(s_len != 0)
                {
                    list[recordIdx].essid[s_len - 1] = 0;
                    s_len--;
                }
#else
				snprintf(list[recordIdx].essid, sizeof(list[recordIdx].essid), "%s", pSSIDStart + strlen("ESSID:\""));
#endif
				if( 0 == strlen(list[recordIdx].essid) )
				{
					bNeedFindHead = 1;
					recordIdx--;
				}
				goto ReadNext;
			}
		}
#if 0
		{
			/* 查找channel	*/
			char *pCHANNELStart = strstr(tmp, "(Channel ");
			if(pCHANNELStart != NULL)
			{
				int x = 0;
				sscanf(pCHANNELStart + strlen("(Channel "), "%d)", &x);
				list[recordIdx].channel = x;
				goto ReadNext;
			}
		}
#endif
		{
			/* 查找encrypt  */
			char *p = strstr(tmp, "IE: ");
			if(p != NULL)
			{
				if (strstr(tmp, "WPA "))
					list[recordIdx].encryptType = 2;
				else if (strstr(tmp, "WPA2 "))
					list[recordIdx].encryptType = 3;
				else if (strstr(tmp, "WPA3 "))
					list[recordIdx].encryptType = 4;
				goto ReadNext;
			}
		}

		if (WIFI_MODEL_AIC8800DL == s_stWifiModelInfo.model)
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Signal level=");
			if(pSIGNALStart != NULL)
			{
				int dBm = 0;
				sscanf(pSIGNALStart + strlen("Signal level=") , "%d ", &dBm);
				if (dBm > -30) dBm = -30;
				if (dBm < -100) dBm = -100;
				list[recordIdx].quality = (dBm - (-100)) * 100 / ((-30) - (-100));
				goto ReadNext;
			}
		}
		else
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Quality=");
			if(pSIGNALStart != NULL)
			{
				int x = 0;
				int y = 0;
				sscanf(pSIGNALStart + strlen("Quality=") , "%d/%d ",&x,&y);
				list[recordIdx].quality = x*100/y;
				goto ReadNext;
			}
		}


ReadNext:
		memset(tmp, 0, sizeof(tmp));
	}

	pclose(pp);

exit:
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 down", NULL);
	}

	realApNum = recordIdx + 1;
	return realApNum;
#endif
}





//------------------------------------------------


int WifiCheckApIsExist(const char *ssid)
{
	int findRes = 0;
	
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 up", NULL);
	}


	FILE *pp = popen("iwlist wlan0 scan", "r");
	if(pp == NULL)
	{
		EMSG("WifiList--->popen fails\r\n");
		goto exit;
	}

	char tmp[512] = {0};
	memset(tmp, 0, sizeof(tmp));


	unsigned char bNeedFindHead = 1;
	int recordIdx = -1;
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
//		MSG("%s", tmp);
		/* 首先找BSSID	  以此为基准 */
		char *pBSSIDStart = strstr(tmp, " - Address: ");
		if(pBSSIDStart != NULL)
		{
			recordIdx++;			
			bNeedFindHead = 0;
			goto ReadNext;
		}
		else
		{
			if(recordIdx < 0)
			{/* 只有找到第一个BSSID 才能继续读下去 */
				goto ReadNext;
			}
		}

		if( 1 == bNeedFindHead )
		{
			goto ReadNext;
		}
		
		{
			/* 查找ssid  */
			char *pSSIDStart = strstr(tmp, "ESSID:\"");
			if(pSSIDStart != NULL)
			{
#if 1
				char essid[WIFI_ESSID_NAME_LEN + 1] = {0};	//essid
                sscanf(pSSIDStart + strlen("ESSID:\""), "%s", essid);
                int s_len = strlen(essid);
                if(s_len != 0)
                {
                    essid[s_len - 1] = 0;
                    s_len--;
                }
#else
				snprintf(list[recordIdx].essid, sizeof(list[recordIdx].essid), "%s", pSSIDStart + strlen("ESSID:\""));
#endif
				if( 0 == strlen(essid) )
				{
					bNeedFindHead = 1;
					recordIdx--;
				}

				if( strcmp(ssid, essid) == 0 )
				{
					//找到了
					findRes = 1;
					break;
				}

				goto ReadNext;
			}
		}

ReadNext:
		memset(tmp, 0, sizeof(tmp));
	}

	pclose(pp);

exit:
	if( (WIFI_MODE_AP != WifiMode) && (WIFI_MODE_ST != WifiMode) )
	{
		MW_START_PROCESS("sh", "sh", "-c", "ifconfig wlan0 down", NULL);
	}

	return findRes;
}

int WifiSignal(router_signal_s* stSignal)
{
	if( ! stSignal )
	{
		return -1;
	}
	
	FILE *pp = popen("iwconfig wlan0", "r");
	if(pp == NULL)
	{
		EMSG("WifiSignal--->popen fails\r\n");
		return -1;
	}

	char tmp[512] = {0};
	memset(tmp, 0, sizeof(tmp));

	unsigned char bNeedFindHead = 1;
	int recordIdx = -1;
	while (fgets(tmp, sizeof(tmp), pp) != NULL)
	{
		MSG("%s", tmp);
		
		{
			/* 查找ssid  */
			char *pSSIDStart = strstr(tmp, "ESSID:\"");
			if(pSSIDStart != NULL)
			{
			#if 01
				char *pSSIDEnd = strchr(pSSIDStart+strlen("ESSID:\""), '"');
				if( pSSIDEnd )
				{
					int s_len = pSSIDEnd - pSSIDStart - strlen("ESSID:\"");
					strncpy(stSignal->essid, pSSIDStart+strlen("ESSID:\""), s_len);
					stSignal->essid[s_len] = '\0';
					
					goto ReadNext;
				}
			#else
				sscanf(pSSIDStart + strlen("ESSID:") , "\"%s\" ", stSignal->essid);
				goto ReadNext;
			#endif
			}
		}

		{
			/* 查找Frequency  */
			char *p = strstr(tmp, "Frequency:");
			if(p != NULL)
			{
				float freq = 0;
				sscanf(p + strlen("Frequency:") , "%f ", &freq);
				if (freq < 5.0)
					stSignal->uchFreq = 1;
				else
					stSignal->uchFreq = 2;
				break;
			}
		}

		{
			/* 查找Bit Rate:  */
			char *p = strstr(tmp, "Bit Rate:");
			if(p != NULL)
			{
				int speed = 0;
				sscanf(p + strlen("Bit Rate:") , "%d ", &speed);
				stSignal->uchLindSpeed = speed;
				break;
			}
		}

		if (WIFI_MODEL_AIC8800DL == s_stWifiModelInfo.model)
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Signal level=");
			if(pSIGNALStart != NULL)
			{
				int dBm = 0;
				sscanf(pSIGNALStart + strlen("Signal level=") , "%d ", &dBm);
				if (dBm > -30) dBm = -30;
				if (dBm < -100) dBm = -100;
				stSignal->uchSignal = (dBm - (-100)) * 100 / ((-30) - (-100));
				stSignal->dBm = dBm;
				break;
			}
		}
		else
		{
			/* 查找signal  */
			char *pSIGNALStart = strstr(tmp, "Link Quality=");
			if(pSIGNALStart != NULL)
			{
				int x = 0;
				int y = 0;
				sscanf(pSIGNALStart + strlen("Link Quality=") , "%d/%d ",&x,&y);
				stSignal->uchSignal = x*100/y;

				pSIGNALStart = strstr(tmp, "Signal level=");
				if(pSIGNALStart != NULL)
				{
					int dBm = 0;
					sscanf(pSIGNALStart + strlen("Signal level=") , "%d ", &dBm);
					stSignal->dBm = dBm;
				}

				break;
			}
		}

ReadNext:
		memset(tmp, 0, sizeof(tmp));
	}

	pclose(pp);

exit:

	return 0;
}

// 获取本机mac  
int NetGetMac(const char *eth_inf, char *mac)  
{  
	struct ifreq ifr;  
	int sd;  
	//char mac[MAC_SIZE]; 
	bzero(&ifr, sizeof(struct ifreq));  
	if( (sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)  
	{  
		EMSG("get %s mac address socket creat error\n", eth_inf);  
		return -1;  
	}  
	
	strncpy(ifr.ifr_name, eth_inf, sizeof(ifr.ifr_name) - 1);  

	if(ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)  
	{  
		EMSG("get %s mac address error\n", eth_inf);  
		close(sd);  
		return -1;  
	}  

	snprintf(mac, MAC_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x",  
		(unsigned char)ifr.ifr_hwaddr.sa_data[0],   
		(unsigned char)ifr.ifr_hwaddr.sa_data[1],  
		(unsigned char)ifr.ifr_hwaddr.sa_data[2],   
		(unsigned char)ifr.ifr_hwaddr.sa_data[3],  
		(unsigned char)ifr.ifr_hwaddr.sa_data[4],  
		(unsigned char)ifr.ifr_hwaddr.sa_data[5]);  

	close(sd);  
	
	return 0;  
}  

// 获取本机mac  
int NetGetMac_2(const char *eth_inf, unsigned char *mac)
{  
	struct ifreq ifr;  
	int sd;  
	//char mac[MAC_SIZE]; 
	bzero(&ifr, sizeof(struct ifreq));  
	if( (sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)  
	{  
		EMSG("get %s mac address socket creat error\n", eth_inf);  
		return -1;  
	}  

	strncpy(ifr.ifr_name, eth_inf, sizeof(ifr.ifr_name) - 1);  

	if(ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)  
	{  
		EMSG("get %s mac address error\n", eth_inf);  
		close(sd);  
		return -1;  
	}  

	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	
	close(sd);  

	return 0;  
}  

// 获取本机ip  
int NetGetLocalIp(const char *eth_inf, char *ip)
{  
	int sd;  
	//char ip[IP_SIZE];  

	struct sockaddr_in sin;  
	struct ifreq ifr;  

	sd = socket(AF_INET, SOCK_DGRAM, 0);  
	if (-1 == sd)  
	{  
		EMSG("socket error: %s\n", strerror(errno));  
		return -1;        
	}  

	strncpy(ifr.ifr_name, eth_inf, IFNAMSIZ);  
	ifr.ifr_name[IFNAMSIZ - 1] = 0;  
	
	// if error: No such device  
	if (ioctl(sd, SIOCGIFADDR, &ifr) < 0)  
	{  
		EMSG("ioctl error: %s\n", strerror(errno));  
		close(sd);  
		return -1;  
	}  

	memcpy(&sin, &ifr.ifr_addr, sizeof(sin));  
	snprintf(ip, IP_SIZE, "%s", inet_ntoa(sin.sin_addr));  

	close(sd);  
	return 0;  
}  

/********************************************************************
**
**	函数名	:	NetGetEthDevice
**	入参	:	
**				1.	pNetDev:指向网卡属性结构NetDevice的指针数组。
**				2.	iMax   :最大支持的网卡个数(可以设置为10,目前实际为3个)
**	返回	:	> 0 支持的实际网卡个数
**				= 0 支持网卡个数为0,即失败
**	功能说明:	此函数用于应用层获取网卡支持的个数,以及对应网卡的种类和当前是否可用
**
*********************************************************************/
#define DIR_DEV_NET 	"/proc/net/dev"
int NetGetEthDevice(NetDevice *pNetDev, int iMax)
{
	int ethnum = 0;
	char line[255];
	char ethname[10][30];
	int i = 0;
	FILE *fp;
	char *p = NULL;
	
	if(pNetDev == NULL)
	{
		return 0;
	}

	memset(pNetDev,0,iMax*sizeof(NetDevice));
	
	fp = fopen(DIR_DEV_NET, "r");
	if(fp == NULL)
	{
		EMSG("Open File %s Failed!\n", DIR_DEV_NET);
		return 0;
	}
	else
	{
		while (fgets(line, sizeof(line), fp))
		{
			p = NULL;
			if ((p = strstr(line, "eth")) != NULL)
			{
				i = 0; 
				while(p[i] != ':')
				{
					ethname[ethnum][i] = p[i];
					i++;
				}
				ethname[ethnum][i] = '\0';
				ethnum++;
			}
			p = NULL;
			if ((p = strstr(line, "wlan")) != NULL)
			{
				i = 0; 
				while(p[i] != ':')
				{
					ethname[ethnum][i] = p[i];
					i++;
				}
				ethname[ethnum][i] = '\0';
				ethnum++;
			}
		}
		fclose(fp);
	}
	for(i = 0; i < iMax && i < ethnum;i++)
	{
		strcpy(pNetDev[i].name,&ethname[i][0]);
		if(strcmp(pNetDev[i].name,"eth0")==0)
		{
			pNetDev[i].valid = 1;
			pNetDev[i].trans_media = NM_WIRED;
		}
		else if(strcmp(pNetDev[i].name,"eth1")==0)
		{
			pNetDev[i].valid = 0;
			pNetDev[i].trans_media = NM_WIRED;
		}
		//else if(strcmp(pNetDev[i].name,"eth2")==0)
		else if(strcmp(pNetDev[i].name,"wlan0")==0)
		{
			pNetDev[i].valid = 1;
			pNetDev[i].trans_media = NM_WIRELESS;
		}
	}
	return i;
}

void ValueToString(char* str,unsigned long dwValue)   
{
	struct sockaddr_in addr1;
	addr1.sin_addr.s_addr = (dwValue);
	inet_ntoa(addr1.sin_addr);
	strcpy(str,inet_ntoa(addr1.sin_addr));
}

int NetGetGateWay(char *way)
{
	char devname[64];
	unsigned long d, g, m;
	int flgs, ref, use, metric, mtu, win, ir;

	FILE *fp = fopen("/proc/net/route", "r");

	if (fscanf(fp, "%*[^\n]\n") < 0)
	{ 
		fclose(fp);
		return -1;
	}

	while (1)
	{
		int r;
		r = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n", devname, &d, &g, &flgs, &ref, &use, &metric, &m, &mtu, &win, &ir);
		if (r != 11)
		{
			if ((r < 0) && feof(fp))
			{ 
				/* EOF with no (nonspace) chars read. */
				break;
			}
		}

		//MSG("way %3s %lx %lx %X %d %d %d %lx %d %d %d\n", devname, d, g, flgs, ref, use, metric, m, mtu, win, ir);

		if (!(flgs & RTF_UP))
		{
			/* Skip interfaces that are down. */
			continue;
		}
		if (d == 0UL)
		{
			ValueToString(way,g);
		}
	}
	fclose(fp);

	return 0;
}

int NetSetEthMac(char* mac)
{
	char cmd[255];
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 down", NULL);
	snprintf(cmd,sizeof(cmd),"ifconfig eth0 hw ether %s",mac);
	MSG("NetSetEthMac cmd %s\n",cmd);
	MW_START_PROCESS("sh", "sh", "-c", cmd, NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);
}

int NetSetEth(uint bStaticIp, uint ip, uint netmask, uint gateway, uint dns)
{
//<shang>	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 down", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall hostapd", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 0.0.0.0", NULL);

	char strIp[16];
	char strNetmask[16];
	char strGateway[16];
	char strDns[16];
	if (bStaticIp)
	{
		printf("NetSetEth %08X---%08X---%08X\n", ip, netmask, gateway);
		if (inet_ntop(AF_INET, &ip, strIp, sizeof(strIp)) == NULL || 
			inet_ntop(AF_INET, &netmask, strNetmask, sizeof(strNetmask)) == NULL || 
			inet_ntop(AF_INET, &gateway, strGateway, sizeof(strGateway)) == NULL || 
			inet_ntop(AF_INET, &dns, strDns, sizeof(strDns)) == NULL)
			bStaticIp = 0;
		printf("NetSetEth converted %s---%s---%s\n", strIp, strNetmask, strGateway);
		printf("NetSetEth DNS: %s\n", strDns);
	}
	printf("NetSetEth bStaticIp: %d\n", bStaticIp);

	if (0 == bStaticIp)
	{
		MW_START_PROCESS_CLOSE_FD("sh", "sh", "-c", "udhcpc -b -i eth0 -s /usr/share/udhcpc/default.script", NULL);
	}
	else
	{
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "ifconfig eth0 %s netmask %s", strIp, strNetmask);
		MW_START_PROCESS("sh", "sh", "-c", cmd, NULL);
		snprintf(cmd, sizeof(cmd), "route add default gw %s", strGateway);
		MW_START_PROCESS("sh", "sh", "-c", cmd, NULL);
		snprintf(cmd, sizeof(cmd), "echo -e \'nameserver %s\' > /etc/resolv.conf", strDns);
		MW_START_PROCESS("sh", "sh", "-c", cmd, NULL);
	}
}

int NetSetEth_Peiwang()
{
	MW_START_PROCESS("sh", "sh", "-c", "killall wpa_supplicant", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "killall udhcpc", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "ifconfig eth0 up", NULL);
	MW_START_PROCESS("sh", "sh", "-c", "udhcpc -b -i eth0 -s /usr/share/udhcpc/default.script", NULL);
}

int NetIfInLine(char *name)
{
	FILE *fp = NULL;
	int ret;
    char buf[1024] = {0};
	char cmd[255];
	snprintf(cmd,sizeof(cmd),"ifconfig %s",name);
    fp = popen(cmd,"r");
	if( !fp )
	{
		return -1;
	}
	ret = fread(buf, 1, 1023, fp);
	if(ret >= 0)
		buf[ret] = '\0';
    //MSG("cmd:%s\n",cmd);
	//MSG("buf:%s\n",buf);
	pclose(fp);
	if (strstr(buf,"RUNNING") != NULL) {
		return 0;
	}
	return -1;
}

int NetGetDns(char dns[16])
{
	FILE *fp = NULL;
	int ret;
    char line[128] = {0};

	if (!dns)
		return -1;
	
    fp = fopen("/etc/resolv.conf", "r");
	if (!fp)
	{
		return -1;
	}
	
	while (fgets(line, sizeof(line), fp))
	{
		if (strstr(line, "nameserver"))
		{
			memset(dns, 0, 16);
			ret = sscanf(line, "nameserver %15[^ ] ", dns);
			if (1 == ret)
			{
				fclose(fp);
				return 0;
			}
		}
	}
	fclose(fp);
	return -1;
}

int NetGetGateway(char gw_addr[16], char gw_mac[18])
{
	FILE *fp = NULL;
	int ret;
	int found = 0;
    char line[256] = {0};
	char iFace[24] = {0};
	unsigned int ip_addr;

	if (!gw_addr || !gw_mac)
		return -1;
	
    fp = fopen("/proc/net/route", "r");
	if (!fp)
	{
		return -1;
	}

	fgets(line, sizeof(line), fp);
	//eth0    00000000        010AA8C0        0003    0       0       0       00000000        0       0       0 
	while (fgets(line, sizeof(line), fp))
	{
		char *token = NULL;
		char delim[2] = {0x09, 0};
		char *saveptr = NULL;
		token = strtok_r(line, delim, &saveptr);
		if (token)
		{
			memset(iFace, 0, sizeof(iFace));
			strncpy(iFace, token, sizeof(iFace)-1);
			token = strtok_r(NULL, delim, &saveptr);
			if (token && strcmp(token, "00000000") == 0)
			{
				token = strtok_r(NULL, delim, &saveptr);
				if (token)
				{
					ret = sscanf(token, "%x", &ip_addr);
					if (1 == ret)
					{
						printf("ip_dadr: %08X\n", ip_addr);
						struct in_addr sin_addr;
						sin_addr.s_addr = ip_addr;
						snprintf(gw_addr, 16, "%s", inet_ntoa(sin_addr));
						printf("ip_dadr: %s\n", gw_addr);
						found = 1;
						break;
					}
				}
			}
		}
	}
	fclose(fp);

	if (0 == found)
		return -1;
	
    struct arpreq arp; // Structure for ARP request.
    int sockfd; // Socket file descriptor.
    struct sockaddr_in *s_in; // Pointer to sockaddr_in structure.

    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // Create a socket for ARP requests.
    if (sockfd < 0) {
        perror("socket");
		return -1;
    }
    s_in = (struct sockaddr_in *)&arp.arp_pa; // Set the address family to AF_INET.
    s_in->sin_family = AF_INET; // Set the address family to AF_INET.
    s_in->sin_addr.s_addr = ip_addr; // Set the gateway IP address.
    strcpy(arp.arp_dev, iFace); // Set the interface name.
    ret = ioctl(sockfd, SIOCGARP, &arp);
    close(sockfd); // Close the socket file descriptor.
    if (ret != 0) // Get the ARP entry for the gateway IP.
    {
        perror("SIOCGARP"); // Print error message if ARP request fails.
		return -1;
    }
	// Copy the MAC address from arp structure to output buffer.
	snprintf(gw_mac, 18, "%02x:%02x:%02x:%02x:%02x:%02x", arp.arp_ha.sa_data[0], arp.arp_ha.sa_data[1], 
			arp.arp_ha.sa_data[2], arp.arp_ha.sa_data[3], arp.arp_ha.sa_data[4], arp.arp_ha.sa_data[5]);

	return 0;
}

