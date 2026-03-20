#include "GB28181ClientSDK.h"
#include "GBClientImpl.h"

GB28181ClientSDK::GB28181ClientSDK()
{
     m_client_Impl = new CGBClientImpl();
}

GB28181ClientSDK::~GB28181ClientSDK()
{
     if(m_client_Impl) {
         delete m_client_Impl;
     }
}

int GB28181ClientSDK::Start(GB28181Version version, TransPorco proco , const char* localGbCode, const char* ip , uint16_t port)
{
    if( !localGbCode || !ip ) {
        return kGbNullPointer;
    }
     return m_client_Impl->Start( version,  proco ,  localGbCode, ip ,  port);
}

void GB28181ClientSDK::Stop()
{
     m_client_Impl->Stop();
}

int GB28181ClientSDK::Register(const GBRegistParam* regist_param, const ConnectParam* connect_param)
{
    if(  !regist_param || !connect_param ){
        return kGbNullPointer;
    }
     return m_client_Impl->Register(regist_param, connect_param);
}

char* GB28181ClientSDK::GetTime()
{	
	if (m_client_Impl->m_dateStr.empty()) {
		return NULL;
	}
	const size_t len = m_client_Impl->m_dateStr.length();
	char* buf = new char[len + 1];
	memset(buf,0,len + 1);
	memcpy(buf,m_client_Impl->m_dateStr.c_str(),len);
	return buf;
}


int GB28181ClientSDK::Logout()
{
     return m_client_Impl->Logout();
}

void GB28181ClientSDK::SetReceiver(const GBClientReceiver* receiver)
{
    m_client_Impl->SetGBReceiver(receiver);
}

//Heartbeat
int GB28181ClientSDK::SendHeartbeat(const KeepaliveInfo* list  )
{
    if(  !list  ){
        return kGbNullPointer;
    }
    return m_client_Impl->SendHeartbeat(list);
}

// device information reponse interface
int GB28181ClientSDK::ResponseDeviceCatalog( ResponseHandle handle,  const DeviceCatalogList*  catalog, int trans_mode)
{
    if(  !handle || !catalog ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseDeviceCatalog(handle, catalog,trans_mode);
}

int GB28181ClientSDK::ResponseDeviceInfo( ResponseHandle handle, const DeviceInfo* device_info)
{
    if(  !handle || !device_info ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseDeviceInfo(handle,  device_info);
}
int GB28181ClientSDK::ResponseDeviceStatus(ResponseHandle handle,const DeviceStatus* device_status)
{
    if(  !handle || !device_status ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseDeviceStatus(handle,device_status);
}
int GB28181ClientSDK::ResponseDeviceConfigQuery( ResponseHandle handle,const DeviceConfigDownload* device_config)
{
    if(  !handle || !device_config ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseDeviceConfigQuery(handle,device_config);
}

int GB28181ClientSDK::ResponseDevicePresetInfo(ResponseHandle handle, const PresetInfo*  preset_info)
{
    if(  !handle || !preset_info ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseDevicePresetInfo(handle, preset_info);
}

// stream media interface
int GB28181ClientSDK::ResponseMediaPlayInfo(StreamHandle handle, const MediaInfo* info)
{
    if(  !handle || !info ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseMediaPlayInfo(handle,  info);
}

int GB28181ClientSDK::ResponseRecordIndex(ResponseHandle handle, const RecordIndex* index,int trans_mode)
{
    if(  !handle || !index ){
        return kGbNullPointer;
    }
    return m_client_Impl->ResponseRecordIndex(handle,index,trans_mode);
}

// Notify
int GB28181ClientSDK::NotifyDeviceAlarm(const AlarmNotifyInfo* alarm_info)
{
    if(  !alarm_info ){
        return kGbNullPointer;
    }
    return m_client_Impl->NotifyDeviceAlarm(alarm_info);
}

int GB28181ClientSDK::NotifyMediaStatus(StreamHandle handle, const char* device_id,  int NotifyType )
{
    if(  !device_id ){
        return kGbNullPointer;
    }
    return m_client_Impl->NotifyMediaStatus(handle, device_id, NotifyType);
}

int GB28181ClientSDK::NotifyDeviceUpgradeResult(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description)
{
    if( !device_id ){
        return kGbNullPointer;
    }
    return m_client_Impl->NotifyDeviceUpgradeResult(device_id, session_id, firmware, result, description);
}

// reponse subscribe
int GB28181ClientSDK::NotifyDeviceAlarmForSub(SubscribeHandle handle, const AlarmNotifyInfo* alarm_info)
{
    if(  !handle || !alarm_info ){
        return kGbNullPointer;
    }
    return m_client_Impl->NotifyDeviceAlarmForSub(alarm_info);
}

int GB28181ClientSDK::NotifyDeviceCatalogForSub(const DeviceCatalogList* catalog)
{
    if(!catalog )
	{
        return kGbNullPointer;
    }
     return m_client_Impl->NotifyDeviceCatalogForSub(catalog);
}

int GB28181ClientSDK::NotifyMobilePositionForSub(SubscribeHandle handle, const MobilePositionInfo* info )
{
    if(  !handle || !info ){
        return kGbNullPointer;
    }
   return m_client_Impl->NotifyMobilePositionForSub(info);
}

// device information query interface
int GB28181ClientSDK::QueryCatalog(const CatalogQueryParam* catalog)
{
    if( !catalog ){
        return kGbNullPointer;
    }
    return m_client_Impl->QueryCatalog(catalog);
}

// stream media interface
int GB28181ClientSDK::StartLiveStreamRequest(const MediaInfo* input,  MediaInfo* result, StreamHandle* stream_handle)
{
    if( !input || !result || !stream_handle ){
        return kGbNullPointer;
    }
   return m_client_Impl->StartLiveStreamRequest(input,  result, stream_handle);
}

int GB28181ClientSDK::StartBroadcastStreamRequest(const char* target_id,
                                                  const MediaInfo* input,
                                                  MediaInfo* result,
                                                  StreamHandle* stream_handle)
{
    if( !target_id || !input || !result || !stream_handle ){
        return kGbNullPointer;
    }
    return m_client_Impl->StartBroadcastStreamRequest(target_id, input, result, stream_handle);
}

int GB28181ClientSDK::StopStreamRequest(StreamHandle stream_handle)
{
     return m_client_Impl->StopStreamRequest(stream_handle);
}



