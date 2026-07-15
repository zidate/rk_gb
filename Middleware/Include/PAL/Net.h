#ifndef __PAL_NETWORK_H__
#define __PAL_NETWORK_H__

#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

//------------------------add on 2025.05.26
typedef enum
{
	WIFI_MODEL_UNKNOWN 		= 0, 	//未知
	WIFI_MODEL_RTL8188FTV,
	WIFI_MODEL_SV6256P,
	WIFI_MODEL_ATBM6012B,
	WIFI_MODEL_ATBM6062,
	WIFI_MODEL_ATBM6132,
	WIFI_MODEL_AIC8800DL,
}WIFI_MODEL_E;

typedef struct{
	WIFI_MODEL_E model;
	int is5G;
	int isBle;
} Wifi_Model_info_S;
//------------------------add on 2025.05.26

enum network_media {
 NM_WIRED = 0,  //有线网卡
 NM_WLAN,       //Wifi
 NM_3G,         //3G网卡
 NM_NR,    //种类数
};
#define NM_WIRELESS NM_WLAN

typedef struct tagNetDevice
{
	unsigned char valid;		//当前网卡是否连接
	unsigned char state;		//是否连接上网
	unsigned char trans_media;	//传输媒介
	unsigned char reserved[6];	//保留
	char name[32];		//网卡名称
	
}NetDevice;


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
int NetGetEthDevice(NetDevice *pNetDev, int iMax);

///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
enum
{
	WIFI_MODE_AP = 0,
	WIFI_MODE_ST,
	WIFI_MODE_END = -1
};

typedef struct WIFI_ST_s
{
	char ssid[64];
	char psk[64];//mod by ale 16->64
	unsigned int bStaticIp;
	unsigned int ip;
	unsigned int netmask;
	unsigned int gateway;
	unsigned int dns;
}WIFI_ST_t;


typedef enum
{
	WIFI_DISCONNECTED 		= 0, 	//WIFI未连接
	WIFI_CONNECTING 		= 1, 	//WIFI正在连接
	WIFI_CONNECTED 			= 2,	//WIFI已连接
	WIFI_PSW_ERROR 			= 3,	//WIFI密码错误
	WIFI_CONN_FAILED		= 4,	//WIFI连接失败
}wifi_status_enum;

typedef void(* connect_wifi_result_cb)(wifi_status_enum); 	//wifi连接结果回调




#define WIFI_ESSID_NAME_LEN 64
#define WIFI_ROUTER_NUM_OF_ONE_TIME	    2
#define WIFI_ROUTER_LIST_NUM 			50

//***********************WIFI***********************************//

typedef struct
{
	int moulde; ///< @see WIFI_TYPE_E
}MsgWifiMoudle_S;

typedef enum
{
	MODE_AUTO			= 0,		//NULL
	MODE_ADHOC			= 1,		//Adhoc
	MODE_MANAGED		= 2,		//Managed
	MODE_MASTER		    = 3,		
	MODE_REPEATER		= 4,		
	MODE_SECONDARY		= 5,		
	MODE_MONITOR		= 6,		
	MODE_EX_UNKNOWN		= 7,		
}mode_type_enum;

typedef enum
{
	ENCRYPT_NULL			= 0,		//NULL
	ENCRYPT_NONE			= 1,		//NONE
	ENCRYPT_WEP				= 2,		//WEP
	ENCRYPT_WPA1_TKIP		= 3,		//WPA1_TKIP
	ENCRYPT_WPA1_AES		= 4,		//WPA1_AES
	ENCRYPT_WPA2_TKIP		= 5,		//WPA2_TKIP
	ENCRYPT_WPA2_AES		= 6,		//WPA2_AES
	ENCRYPT_WPA1_WPA2_TKIP  = 7,        //WPA1_WPA2_TKIP
	ENCRYPT_WPA1_WPA2_AES   = 8,        //WPA1_WPA2__AES
	ENCRYPT_WPA1_TKIP_WPA2_AES = 9,
	ENCRYPT_WPA1_AES_WPA2_TKIP = 10,
}encrypt_type_enum;


typedef struct
{
	char essid[WIFI_ESSID_NAME_LEN + 1];	//essid
	char mode;						//ModeType_E
	char encryptType;				//EncryptType_E
	char quality;                   //max is 100  
	char rsv[28];
}router_list_s;


typedef struct
{
	char essid[WIFI_ESSID_NAME_LEN + 1];	//essid
	char mode;						//mode_type_enum
	char encryptType;				//encrypt_type_enum 0-open 1-wep 2-wap1 3-wap2 4-wap3
	char quality;                   //max is 100  
	char connented;                   // 
}router_list_2_s;

typedef struct
{
	char essid[WIFI_ESSID_NAME_LEN + 1];	//essid
	char mode;						//mode_type_enum
	char encryptType;				//encrypt_type_enum 0-open 1-wep 2-wap1 3-wap2 4-wap3
	char quality;                   //max is 100  
	char connented;                   // 
	char bssid[128 + 1];	//bssid
}router_list_3_s;

typedef struct
{
	char essid[WIFI_ESSID_NAME_LEN + 1];	//essid
	unsigned char uchSignal;
	unsigned char uchLindSpeed; //Mbps
	unsigned char uchFreq; //1-2.4G 2-5G
	char dBm;
}router_signal_s;


typedef struct 
{
	int            beEnd;                        ///< 1:表示结束 0:未结束
	int            allListCount;              
	int            curListCount; 	        
	char           data[512*2];   //router_list_s
}RouterListHead_S;

/// @brief 关闭无线网卡
/// @return 
int WifiIfconfigDown();

/// @brief 初始化wifi模块
/// @return 
void WifiInit();

/// @brief 获取wifi模式
/// @return 
int WifiGetMode();

/// @brief 创建ap模式
/// @param ssid 
/// @param passwd 
/// @return 
int WifiApModeCreate(const char *ssid, const char *passwd);

/// @brief 销毁ap模式
/// @return 
int WifiApModeDestroy();

/// @brief 获取WiFi的连接状态
/// @return 
int NetGetWifiConnectStatus(WIFI_ST_t *pStm,int timeout);


/// @brief 创建station模式
/// @param pStm 
/// @param cb 
/// @return 
int WifiStationModeCreate(WIFI_ST_t *pStm, connect_wifi_result_cb cb);

/// @brief 销毁station模式
/// @return 
int WifiiStationDestroy();

/// @brief 获取wifi列表
/// @param list 
/// @param MAX_Len 
/// @return 
int WifiList_2(router_list_2_s* list, int MAX_Len);
int WifiList_3(router_list_3_s* list, int MAX_Len);

/// @brief 检测是否ap模式
/// @param ssid 
/// @return 
int WifiCheckApIsExist(const char *ssid);

/// @brief wifi信号
/// @param stSignal 
/// @return 
int WifiSignal(router_signal_s* stSignal);

/// @brief 获取mac
/// @param eth_inf 
/// @param mac 
/// @return 
int NetGetMac(const char *eth_inf, char *mac);

/// @brief 获取mac
/// @param eth_inf 
/// @param mac 
/// @return 
int NetGetMac_2(const char *eth_inf, unsigned char *mac);

/// @brief 获取本地ip地址
/// @param eth_inf 
/// @param ip 
/// @return 
int NetGetLocalIp(const char *eth_inf, char *ip);

/// @brief 获取本地网关
/// @param way 
/// @return 
int NetGetGateWay(char *way);

/// @brief 设置有线mac
/// @param mac 
/// @return 
int NetSetEthMac(char* mac);

/// @brief 启动有线
/// @return 
int NetSetEth(uint bStaticIp, uint ip, uint netmask, uint gateway, uint dns);

/// @brief 有线配网
/// @return 
int NetSetEth_Peiwang();

/// @brief 检测网络
/// @param name 
/// @return 
int NetIfInLine(char *name);


int NetGetDns(char *dns);
int NetGetGateway(char *gw_addr, char *gw_mac);

#ifdef __cplusplus
}
#endif

#endif 
