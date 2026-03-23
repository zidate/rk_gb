#ifndef __MAIN_H__
#define __MAIN_H__

#include <string>

#include "Common.h"
#include "Protocol/ProtocolManager.h"

class GB28181ClientSDK;

class CSofia
{
	PATTERN_SINGLETON_DECLARE(CSofia);
public:

	/// 启动前准备工作
	bool preStart();
	/// 启动
	bool start();
	/// 启动后收尾工作
	bool postStart();
	protocol::ProtocolManager* GetProtocolManager();
	const protocol::ProtocolManager* GetProtocolManager() const;
	
private:
	/// 启动与 Tuya 线程解耦的本地业务
	void StartLocalRuntimeServices();
	/// 系统启动期间向前面板发送保活命令，防止启动期间系统被复位
	void OnKeepAlive(Param wParam);
	/// 系统升级监控
	void OnTimeReboot(Param wParam);
	void OnCheckButton(Param wParam);
	void OnCheckStreamInfo(Param wParam);
	void OnCheckButton_Produce(Param wParam);//add on 2025.01.02 产测模式，按键检测
	
	void onAppEvent(std::string code, int index, appEventAction action, const EventHandler *param, Json::Value* data);

private:

	CTimer m_timerKeepAlive;
	CTimer m_timerCheckButton;
	CTimer m_timerReboot;
	CTimer m_timerCheckSdCard;
	CTimer m_timerCheckIspStream;
	protocol::ProtocolManager m_protocolManager;
	GB28181ClientSDK* m_gbClientSdk;
};
#endif
