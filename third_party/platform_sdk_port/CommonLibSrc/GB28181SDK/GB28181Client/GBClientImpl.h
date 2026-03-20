#ifndef CGBCLIENTIMPL_H

#define CGBCLIENTIMPL_H

#include "GB28181Defs.h"

#include "SipDefs.h"

#include "Interlocked.h"

#include "SubscribeManager.h"

#include "NetSocketSDK.h"

#include "GB28181XmlParser.h"



class SipUserAgentClient;

class CGB28181XmlParser;

class CStreamRequestManager;

class CGbWaiterManager;

class CGbResponseWaiter;

class CGBClientImpl;



struct SubscribeExpireInfo

{

	CGBClientImpl*  client;

	SubscribeHandle handle;



};



class CGBClientImpl : public SipClientHandler,CSocketDataObserver

{

public:

    CGBClientImpl();

    ~CGBClientImpl();

    int Start(GB28181Version version, TransPorco proco , const char* localGbCode, const char* ip , uint16_t port);//????



    void Stop();//????



    void SetGBReceiver(const GBClientReceiver* receiver);//??????????



    int Register(const GBRegistParam* messge, const ConnectParam* param);//?????????

    int Logout();//???



    //Heartbeat

    int SendHeartbeat(const KeepaliveInfo*  list);//?????



    // Notify

    int NotifyDeviceAlarm(const AlarmNotifyInfo* alarm_info);//??????
    int NotifyDeviceUpgradeResult(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description);



    // device information reponse interface

    int ResponseDeviceCatalog( ResponseHandle handle,  const DeviceCatalogList*  catalog, int trans_mode);//????????

    int ResponseDeviceInfo( ResponseHandle handle,const DeviceInfo* device_info);//????????

    int ResponseDeviceStatus(ResponseHandle handle,const DeviceStatus* device_status);//????????

    int ResponseDeviceConfigQuery( ResponseHandle handle,  const DeviceConfigDownload* device_config);//????????

    int ResponseDevicePresetInfo(  ResponseHandle handle, const PresetInfo*  perset_info);//??????????



    int ResponseRecordIndex( ResponseHandle handle,  const RecordIndex* index, int trans_mode);//????????



    // stream media interface

    int ResponseMediaPlayInfo(  StreamHandle handle,   const MediaInfo* info);//????????

    int NotifyMediaStatus(StreamHandle handle,const char* device_id,  int NotifyType );//??????

    // reponse subscribe



    int NotifyDeviceAlarmForSub(const AlarmNotifyInfo* alarm_info);

    int NotifyDeviceCatalogForSub(const DeviceCatalogList* catalog);

    int NotifyMobilePositionForSub(const MobilePositionInfo* info );



    //query

    int QueryCatalog(const CatalogQueryParam* catalog);//????

	//subscribe callback

	static void ExpireMessageSender (SubscribeHandle handle, void* user);

			

	static void SubscribeExpireDelThread(LPVOID p);	 //??????????

	void SubscribeExpireDelThread();



    // stream media interface

    int StartLiveStreamRequest(const MediaInfo* input,  MediaInfo* result, StreamHandle* stream_handle);//?????
    int StartBroadcastStreamRequest(const char* target_id, const MediaInfo* input, MediaInfo* result, StreamHandle* stream_handle);

    int StopStreamRequest(StreamHandle stream_handle);//???????



	bool StartConnect(long &hNetCommunication,char *ip,tint32 port);

	int Send(tint32 hNetCommunication, const char *pBuf, tint32 nLen);



	tuint32 OnSocketRecvData(tint32 hNetCommunication, const char* pBuffer, tuint32 len, tuint32 *pNeedLen,LPVOID lpParameter);//??????



public:

    virtual void OnRequest(SipMethod  method,const SipData* data );

    virtual void OnResponse(SipMethod  method,const SipData* data );



    void  OnMessage(const SipData* data);

    void  OnInvite(const SipData* data);

    void  OnACK(const SipData* data);

    void  OnBye(const SipData* data);

    void  OnInfo(const SipData* data);



    void  OnQuery(ProtocolType  type, const SipData* data);

    void  OnDeviceControl(ProtocolType  type, const SipData* data);

    void  OnNotify(ProtocolType  type, const SipData* data);

    void  OnGBProcoResponse(ProtocolType  type, const SipData* data);

	void  OnSubScribe(ProtocolType  type, const SipData* data);

	void  OnCancelSubScribe(SubscribeHandle handle, CSubscribeManager* sub_manager);

	void  OnCatalogSub(ProtocolType  type, const SipData* data);

	void OnAlarmSub(ProtocolType  type, const SipData* data);

	void OnMobilePositionSub(ProtocolType  type, const SipData* data);



    int	SendSipMessageWithOk(const SipData* data);

    int	SendSipMessageWithBadReq(const SipData* data);

	int	SendSipMessageForDeviceControlResponse(std::string cmdStr,const SipData* data,int sn,bool result,char* GBCode);
    int SendBroadcastResponseMessage(int sn, bool result, const char* GBCode);



private:

    static void CatalogQueryProcess(LPVOID lpThreadParameter);

    void CatalogQueryTimer(  CGbResponseWaiter* waiter);

    int   SendSipMessageWithOk(SipSessionHandle handle, const SipData* data);

    void  OnCatalogResponse(const SipData* data);

    int   StreamRequest(const MediaInfo* input,  MediaInfo* output, StreamHandle* stream_handle);
    int   StreamRequestEx(const MediaInfo* input,
                          const char* request_id,
                          const char* subject_sender_id,
                          const char* subject_receiver_id,
                          MediaInfo* output,
                          StreamHandle* stream_handle);

    std::string  GenerateSubject( const std::string& devid, int StreamNum );
    std::string  GenerateSubjectPair(const std::string& sender_id, int sender_stream_num, const std::string& receiver_id);

private:

    SipUserAgentClient*            m_sip_client;

    SipRegistParam*                 m_sip_regist_param;

    SipConnectParam*               m_sip_connect_param;

    GBRegistParam*                  m_gb_regist_param;

    ConnectParam*                    m_gb_connect;

    CGB28181XmlParser*          m_xml_parser;

    CStreamRequestManager*    m_stream_manager;

    std::string                            m_sip_from;

    std::string                            m_sip_to;

    GBClientReceiver*                m_gb_receiver;

    CGbWaiterManager*                 m_waiter_manager;

public:

	std::string m_dateStr;

	std::string m_strIP;

	CSubscribeInfo                  m_catalog_sub;

	CSubscribeInfo                  m_alarm_sub;

	CSubscribeInfo                  m_mobile_position_sub;

	CSubscribeManager*              m_subscribe_manager;

};



#endif // CGBCLIENTIMPL_H


