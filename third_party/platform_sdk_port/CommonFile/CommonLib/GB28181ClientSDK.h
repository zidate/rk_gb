#ifndef GB28181CLIENTSDK_H
#define GB28181CLIENTSDK_H
#include "GB28181Defs.h"

class CGBClientImpl;

class EXPORT_GB28181_SDK  GB28181ClientSDK
{
public:
    GB28181ClientSDK();
    ~GB28181ClientSDK();
    int Start(GB28181Version version, TransPorco proco , const char* localGbCode, const char* ip , uint16_t port);
    void Stop();

    void SetReceiver(const GBClientReceiver* receiver);


    int Register(const GBRegistParam* messge, const ConnectParam* param);

	char* GetTime();
    int Logout();

    //Heartbeat
    int SendHeartbeat(const KeepaliveInfo* list);

    // device information reponse interface
    int ResponseDeviceCatalog( ResponseHandle handle,  const DeviceCatalogList*  catalog, int trans_mode);
    int ResponseDeviceInfo(ResponseHandle handle,const DeviceInfo* device_info);
    int ResponseDeviceStatus(ResponseHandle handle,const DeviceStatus* device_status);
    int ResponseDeviceConfigQuery(ResponseHandle handle,const DeviceConfigDownload* device_config);
    int ResponseDevicePresetInfo(ResponseHandle handle,const PresetInfo*  perset_info);
    int ResponseRecordIndex(ResponseHandle handle,const RecordIndex* index,int trans_mode);

    // stream media interface
    int ResponseMediaPlayInfo(StreamHandle handle, const MediaInfo* info);
    int NotifyMediaStatus(StreamHandle handle, const char* device_id,  int NotifyType);

    // Notify
    int NotifyDeviceAlarm(const AlarmNotifyInfo* alarm_info);
    int NotifyDeviceUpgradeResult(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description);

    // reponse subscribe
    int NotifyDeviceAlarmForSub(SubscribeHandle handle, const AlarmNotifyInfo* alarm_info);
    int NotifyDeviceCatalogForSub(const DeviceCatalogList* catalog);
    int NotifyMobilePositionForSub(SubscribeHandle handle, const MobilePositionInfo* info );


	// device information query interface
	int QueryCatalog(const CatalogQueryParam* catalog);

	// stream media interface
	int StartLiveStreamRequest(const MediaInfo* input, MediaInfo* result, StreamHandle* stream_handle);
	int StartBroadcastStreamRequest(const char* target_id, const MediaInfo* input, MediaInfo* result, StreamHandle* stream_handle);
	int StopStreamRequest(StreamHandle stream_handle);
	int DeviceKeyFrameControl(const char* deviceID);

private:
    CGBClientImpl*  m_client_Impl;

};

#endif // GB28181CLIENTSDK_H
