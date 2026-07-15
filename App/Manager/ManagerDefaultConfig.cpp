
#include "MediaDefaultConfig.h"

PATTERN_SINGLETON_IMPLEMENT(CDefaultConfig);

CDefaultConfig::CDefaultConfig()
{
}

CDefaultConfig::~CDefaultConfig()
{
}

int CDefaultConfig::Start()
{
	ManagerInfo("CDefaultConfig::Start\n");
	NetWork();
	Camera();
	SdCardFromat();
	UpdateFlag();
    AiOnOff();
    PrivateOnOff();
	LightStatus();
	setFlightWarn();
	setResumePosition();
	setUsageMode();
	setOnvif();
	setRebootCount();
	return 0;
}


void CDefaultConfig::NetWork()
{
	CConfigTable NetNTP;

	NetNTP[Json::StaticString("Enable")] = true;
	NetNTP[Json::StaticString("Server")][Json::StaticString("Address")] = Json::StaticString("0x00000000");
	NetNTP[Json::StaticString("Server")][Json::StaticString("Anonymity")] = false;
	NetNTP[Json::StaticString("Server")][Json::StaticString("Name")] = Json::StaticString("ntp1.aliyun.com");
	NetNTP[Json::StaticString("Server")][Json::StaticString("Password")] = Json::StaticString("");
	NetNTP[Json::StaticString("Server")][Json::StaticString("Port")] = 123;
	NetNTP[Json::StaticString("Server")][Json::StaticString("UserName")] = Json::StaticString("");
	NetNTP[Json::StaticString("TimeZone")] = 13; //see gTimeZone
	NetNTP[Json::StaticString("UpdatePeriod")] = 1;//分钟
	g_configManager.setDefault(getConfigName(CFG_NETNTP),  NetNTP);


	//WIFI配置
	CConfigTable NetWifi;

	NetWifi[Json::StaticString("Enable")] = false;
	NetWifi[Json::StaticString("StaticIpEnable")] = false;
	NetWifi[Json::StaticString("SSID")] = Json::StaticString("");
	NetWifi[Json::StaticString("Channel")] = 0;
	NetWifi[Json::StaticString("NetType")] = Json::StaticString("Infra");
	NetWifi[Json::StaticString("EncrypType")] = Json::StaticString("NONE");
	NetWifi[Json::StaticString("Auth")] = Json::StaticString("OPEN");
	NetWifi[Json::StaticString("KeyType")] = 0;
	NetWifi[Json::StaticString("Keys")] = Json::StaticString("");
	NetWifi[Json::StaticString("GateWay")] = Json::StaticString("");
	NetWifi[Json::StaticString("HostIP")] = Json::StaticString("");
	NetWifi[Json::StaticString("Submask")] = Json::StaticString("");	
	NetWifi[Json::StaticString("DNS")] = Json::StaticString("");	
	NetWifi[Json::StaticString("Bind")] = false;
	g_configManager.setDefault(getConfigName(CFG_WIFI), NetWifi);

	//AP模式配置
	CConfigTable NetAP;

	NetAP[Json::StaticString("Enable")] = true;
	NetAP[Json::StaticString("SSID")] = Json::StaticString("dgiot");
	NetAP[Json::StaticString("Channel")] = 6;
	NetAP[Json::StaticString("EncrypType")] = Json::StaticString("CCMP");
	NetAP[Json::StaticString("Auth")] = Json::StaticString("WPA2PSK");
	NetAP[Json::StaticString("Keys")] = Json::StaticString("12345678");
	NetAP[Json::StaticString("HostIP")] = Json::StaticString("192.168.0.1");
	NetAP[Json::StaticString("GateWay")] = Json::StaticString("192.168.0.255");
	NetAP[Json::StaticString("Netmask")] = Json::StaticString("255.255.255.0");
	g_configManager.setDefault(getConfigName(CFG_WIFI_AP), NetAP);
}

/*
#include "PAL/Alarm.h"
#include "PAL/System.h"
#include "PAL/Capture.h"
#include "NetService/INetInterFace.h"
*/

//摄像头默认配置
void CDefaultConfig::Camera()		// HI3518平台摄像头参数
{
	CConfigTable table;

	for(int i = 0; i < 1; i++)
	{
		table[i]["RotateAttr"] = RA_NONE;		
		table[i]["WDRSwitch"] = 0;
        table[i]["OSDSwitch"] = 1;
        table[i]["nightVisionSwitch"] = 1;
        table[i]["NightVisionMode"] = 0;
        table[i]["AntiFlicker"] = 0;
        table[i]["mirror"] = 0;
        table[i]["flip"] = 0;
	}

	g_configManager.setDefault(getConfigName(CFG_CAMERA_PARAM), table);
}


// 	SD卡默认配置
void CDefaultConfig::SdCardFromat()
{
	ManagerInfo("CDefaultConfig SdCardFromat...\n");
	CConfigTable table;

	table["Status"] = 0;//是否要格式化

	g_configManager.setDefault(getConfigName(CFG_SDF), table);
}
// 	升级转态卡默认配置
void CDefaultConfig::UpdateFlag()
{
	ManagerInfo("CDefaultConfig UpdateFlag...\n");
	CConfigTable table;

	table["Status"] = 0;//是否升级成功
	table["GbPending"] = 0;
	table["GbSessionID"] = "";
	table["GbFirmware"] = "";
	table["GbDeviceID"] = "";
	table["GbDescription"] = "";

	g_configManager.setDefault(getConfigName(CFG_UPDATE), table);
}
// 	AI默认配置
void CDefaultConfig::AiOnOff()
{
	ManagerInfo("CDefaultConfig AiOnOff...\n");
	CConfigTable table;

	table["Status"] = 0;//是否开启ai

	g_configManager.setDefault(getConfigName(CFG_AIONOFF), table);
}
void CDefaultConfig::PrivateOnOff()
{
	ManagerInfo("CDefaultConfig PrivateOnOff...\n");
	CConfigTable table;

	table["Status"] = 0;//是否开启

	g_configManager.setDefault(getConfigName(CFG_PRIVATE), table);
}
void CDefaultConfig::LightStatus()
{
	ManagerInfo("CDefaultConfig LightStatus...\n");
	CConfigTable table;
	table["Light"] = 1;
	g_configManager.setDefault(getConfigName(CFG_LIGHT), table);
}
void CDefaultConfig::setResumePosition()
{
	//////////////////////////////////////////////////////////////////////////
	/// 云台位置配置
	CConfigTable table;
	
	table[Json::StaticString("Enable")] = true; 			//开关
    table[Json::StaticString("X")] = -1;
    table[Json::StaticString("Y")] = -1;
	table[Json::StaticString("PX")] = -1;
	table[Json::StaticString("PY")] = -1;
	g_configManager.setDefault(getConfigName(CFG_PTZ_RESUME_POS), table);
}
void CDefaultConfig::setFlightWarn()
{
	//////////////////////////////////////////////////////////////////////////
	/// 灯控默认配置
	CConfigTable table;
	
    table[Json::StaticString("ManualEnable")] = false;
    table[Json::StaticString("Enable")] = true;
	table[Json::StaticString("Duration")] = 30; 	//10-600 sec
    table[Json::StaticString("Luminance")] = 100;
	table[Json::StaticString("Luminance_yellow")] = 1000;
	g_configManager.setDefault(getConfigName(CFG_FLIGHT_WARN), table);
}


void CDefaultConfig::setUsageMode()
{
	//////////////////////////////////////////////////////////////////////////
	/// 推送配置
	CConfigTable table;
	
    table[Json::StaticString("Mode")] = 1;
	g_configManager.setDefault(getConfigName(CFG_USAGE_MODE), table);
}

void CDefaultConfig::setOnvif()
{
	//////////////////////////////////////////////////////////////////////////
	/// onvif
	CConfigTable table;
	
    table[Json::StaticString("Enable")] = false; 			//开关
    table[Json::StaticString("FirstEnable")] = true; 			//开关
	table[Json::StaticString("Password")] = Json::StaticString("admin");
	g_configManager.setDefault(getConfigName(CFG_ONVIF), table);
}

void CDefaultConfig::setRebootCount()
{	
	CConfigTable table;
	table["count"] = 0;
	g_configManager.setDefault(getConfigName(CFG_REBOOT), table);
}

