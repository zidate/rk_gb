

#ifndef _NETTHOOK_H
#define _NETTHOOK_H

#include "Common.h"

#define MAX_NETCARD 3

typedef enum {
	WLAN_IDLE = 0,
	WLAN_CONNECTING,
	WLAN_CONN_FAIL,
	WLAN_CONNECTED,
	WLAN_GOT_IP,
}WLAN_STATE;

//1-eth0 2-AP 3-STA
typedef enum {
	NET_WORK_MODE_NONE = 0,
	NET_WORK_MODE_ETH0,
	NET_WORK_MODE_AP,
	NET_WORK_MODE_STA
}NET_WORK_LIND_MODE;

typedef enum {
	WIFI_RTL8188 = 0,
	WIFI_ATBM6062,
	WIFI_ATBM6012B,
	WIFI_ATBM6132,
	WIFI_AIC8800DL,
	WIFI_NONE
}WLAN_MODEL;

typedef enum {
	WIFI_SINGLE_FREQ , //单频
	WIFI_DUAL_FREQ     //双频
}WLAN_FREQ;


//add on 2025.01.11
typedef enum {
	WIFI_BLE_SUPPORT = 0, 		//支持蓝牙
	WIFI_BLE_NOT_SUPPORT    //不支持蓝牙
}WLAN_BLE;
//add on 2025.01.11

class CNetConfigHook : public CThread
{
	PATTERN_SINGLETON_DECLARE(CNetConfigHook);

public:
	int Init();
	int UnInit();
	/// 设置wifi ap 模式
	int StartWifiApMode(std::string &strSSID, std::string &strPasswd);
	/// 停止wifi ap 模式
	int StopWifiApMode(void);
	/// 设置wifi station模式
	int StartWifiStationMode(void);
	/// 停止wifi station 模式
	int StopWifiStationMode(void);
    
	/// 网络重连
    void ReConn();
	void onAppEvent(std::string code, int index, appEventAction action, const EventHandler *param, Json::Value* data);

	WLAN_STATE GETNETSTATUS(void );

	NET_WORK_LIND_MODE GetNetWorkLindMode(void );

	WLAN_MODEL GetCurlWifiModel();
	WLAN_FREQ GetCurlWifiFreq();
	WLAN_BLE GetCurlWifiBleSupport();//add on 2025.01.11

	bool GetQrcodeEnable();
	void SetQrcodeEnable(bool en);
	bool SaveWifi();

	typedef	enum {
		NET_CHECK_ETH0_VALID = 0,
		NET_CHECK_ETH0_INLINE,
		NET_CHECK_WLAN0_VALID,
		NET_CHECK_WLAN0_INLINE,
		NET_CHECK_WAITE_FOR,
	}NET_STATE;
		
private:
	int GetWifiModel();

	void OnCfgNetComm(const CConfigTable& cfgNetComm, int &ret);
	void OnCfgWifiComm(const CConfigTable& cfgNetComm, int &ret);
	
	void OnCfgAPComm(const CConfigTable& table, int &ret);
	
	//void OnCfgNetDNS(const CConfigTable& CfgNetDNS, int &ret);

	/// 截取通用网络配置修改
	void onHookNetCommon(CConfigTable &cfgNetCommon, int &ret);

	/// 截取网络DDNS配置的修改
	void onHookNetDDNS(CConfigTable &cfgNetDDNS, int &ret);

	/// 截取PPPoE配置的修改
	void onHookNetPPPoE(CConfigTable &cfgNetDDNS, int &ret);

	/// 截取DHCP配置修改
	void onHookNetDHCP(CConfigTable &cfgNetDHCP, int &ret);

	/// 截取DNS配置修改
	void onHookNetDNS(CConfigTable &cfgNetDNS, int &ret);

	/// 截取3G配置的修改
	void onHook3GNet(CConfigTable &cfgNet3G, int &ret);

	/// 获取WIFI配置修改
	void onHookWifi(CConfigTable &cfgWifi, int &ret);

	/// 截取网络优先级修改
	void onHookNetOrder(CConfigTable &cfgNetOrder, int &ret);

	/// 网络状态监视线程
	void ThreadProc(void);
	/// 检查网卡是否有效
	bool Check(char * name);
	// /// 检查网口连接状态
	// int Check_netlink_status(const char * if_name);
	/// 调试接口
	int Dump(int argc, char **argv);
public:
	/// 
	void SetWifi(const char *ssid, const char *pwd);
	void SetWifiSwitch(bool enable);
	int GetNetWorkIp(char *pIp, int len);
	/// 检查网口连接状态
	int Check_netlink_status(const char * if_name);
private:
	NetCommonConfig	m_ConfigNet;
	NetWifiConfig m_ConfigWifi;
	NetAPConfig m_ConfigAP;
	int m_nActiveNet; //0: 有线网卡, 1: WIFI
	NetDevice m_NetEthCfg[MAX_NETCARD];
	char m_ip[20];
	int m_eth0_status;
	int m_wifi_status;
	WLAN_STATE m_eWlanStat;
	NET_WORK_LIND_MODE m_nwLinkMode;
	WLAN_MODEL m_wlanModel;
	WLAN_FREQ m_wlanFreq;
	WLAN_BLE m_wlanBleSupport;//add on 2025.01.11 WIFI_BLE_SUPPORT :支持蓝牙  WIFI_BLE_NOT_SUPPORT :不支持蓝牙

	bool m_bWifiEnable;

	bool m_bNeedSaveWifiInfo;//用于WiFi连接成功后保存WiFi信息---WiFi切换功能
	
	bool m_bCanScanQrCode; //插过网线或者超时后就不允许再启动
};

#define g_NetConfigHook (*CNetConfigHook::instance())
#endif

