#include "GBClientImpl.h"


#include "SipStackSDK.h"


#include <stdio.h>


#include <stdlib.h>


#include <memory.h>


#include "StreamRequestManager.h"


#include "SdpUtil.h"


#include "Rtsp/RtspParser.h"


#include "waiter/WaiterManager.h"


#include "ShareSDK.h"

#include "gbutil.h"


#include <sstream>


#include "PFHelper.h"








struct CatalogTimerStruct


{


    CGBClientImpl*     client;


    CGbResponseWaiter* waiter;


};











CGBClientImpl::CGBClientImpl()


{


      m_sip_client = new SipUserAgentClient();


      m_sip_regist_param = new SipRegistParam;


      m_sip_connect_param = new SipConnectParam;


      m_xml_parser = new CGB28181XmlParser;


      m_stream_manager = new CStreamRequestManager;


      m_gb_regist_param  = new GBRegistParam;


      m_gb_connect = new   ConnectParam;


      m_waiter_manager = new CGbWaiterManager;


      m_gb_receiver = NULL;


	  m_subscribe_manager = new CSubscribeManager;


      m_catalog_sub.sn = 0;





}








CGBClientImpl::~CGBClientImpl()


{


     if(m_sip_client){


         delete m_sip_client;


     }


     if(m_sip_regist_param) {


         delete m_sip_regist_param;


     }


     if(m_sip_connect_param){


         delete m_sip_connect_param;


     }





     if(m_xml_parser){


         delete m_xml_parser;


     }





     if(m_stream_manager){


         delete m_stream_manager;


     }





	 if (m_subscribe_manager){


		 delete m_subscribe_manager;


	 }





     delete m_gb_regist_param;


     delete m_gb_connect;


}





void CGBClientImpl::SetGBReceiver(const GBClientReceiver* receiver)


{


    m_gb_receiver  = (GBClientReceiver*)receiver;


}





int CGBClientImpl::Start(GB28181Version version, TransPorco proco , const char* localGbCode, const char* ip , uint16_t port)


{


    m_xml_parser->m_version = version;


    m_xml_parser->m_local_code = localGbCode;


    m_xml_parser->m_sn.SetValue(1);


    m_xml_parser->m_local_ip = ip;


    m_xml_parser->m_port = port;





    SipTransportType type = kSipOverUDP;


    if( proco == kSipProcoOverTCP) {


         type = kSipOverTCP;


    }





    NetAddress addr;


    addr.ip = (char*)ip;


    addr.port = port;


    m_sip_client->SetHandler(this);


    int code = m_sip_client->Start(type ,&addr, localGbCode );


    if( code != kSipSuccess) {


         return kGbFail;


    }


        return kGb28181Success;


}





void CGBClientImpl::Stop()


{


     m_sip_client->Stop();


}





int CGBClientImpl::Register(const GBRegistParam* gb_param, const ConnectParam* gb_connect)


{


    memset(m_sip_regist_param,0, sizeof(*m_sip_regist_param));


    memset(m_sip_connect_param,0, sizeof(*m_sip_connect_param));


    memcpy(m_gb_regist_param,gb_param,  sizeof(GBRegistParam));


    memcpy(m_gb_connect, gb_connect,  sizeof(ConnectParam));


    SipData*     result = NULL;


    int timeout = 3000;





    m_sip_regist_param->new_reg = true;


    // Keep all long-lived SIP parameter pointers backed by the SDK-owned copies.
    m_sip_regist_param->user_name = m_gb_regist_param->username;


    m_sip_regist_param->password = m_gb_regist_param->password;


    m_sip_regist_param->expires = m_gb_regist_param->expires;


    m_sip_regist_param->auth_flag = false;








    m_sip_connect_param->ip = m_gb_connect->ip;


    m_sip_connect_param->port = m_gb_connect->port;


    m_sip_connect_param->sip_code = m_gb_connect->GBCode;


	m_strIP = m_gb_connect->ip;





   if( kSipSuccess != m_sip_client->Register(m_sip_regist_param, m_sip_connect_param, timeout , &result)) {


       return kGbRegistFail;


   }





   if (result && result->messgae.code == kSuccessRequest) {


	   m_sip_client->FreeSipResult(result);


	   return kGb28181Success;


   }





   if( !result || result->messgae.code  != kUnauthorized   ) {


        goto finally;


   }





   m_sip_client->FreeSipResult(result);





    m_sip_regist_param->new_reg = false;


    m_sip_regist_param->auth_flag = true;





    if( kSipSuccess != m_sip_client->Register(m_sip_regist_param, m_sip_connect_param, timeout , &result)) {


           return kGbRegistFail;


    }





   if(  !result || result->messgae.code  != kSuccessRequest   ) {


             goto finally;


    }


   if (result->messgae.Date)


   {


	   m_dateStr = result->messgae.Date;


   }


   m_sip_client->FreeSipResult(result);


   return kGb28181Success;





finally:


    if(!result) {


          return kGbFail;


    }


    m_sip_client->FreeSipResult(result);


          return kGbResponseCodeError;


}





int CGBClientImpl::Logout()


{


   m_gb_regist_param->expires = 0;


   return Register(m_gb_regist_param, m_gb_connect);


}





//Heartbeat


int CGBClientImpl::SendHeartbeat(  const KeepaliveInfo* list)


{





    std::string content;


    int code = kGb28181Success;


    m_xml_parser->PackKeepalive( list, content);


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


    message.content = (char*)content.c_str();





    int timeout = 3000;


    SipData* result = NULL;

    TVT_LOG_INFO("gb heartbeat send"
                 << " device=" << m_xml_parser->m_local_code
                 << " remote_name=" << m_gb_connect->GBCode
                 << " remote=" << m_strIP << ":" << m_sip_connect_param->port
                 << " xml_len=" << (int)content.size()
                 << " timeout=" << timeout);





    const int sipRet = m_sip_client->Message(&message, timeout,  &result);

    if( kSipSuccess != sipRet) {

         TVT_LOG_ERROR("gb heartbeat sip message failed"
                       << " device=" << m_xml_parser->m_local_code
                       << " remote_name=" << m_gb_connect->GBCode
                       << " remote=" << m_strIP << ":" << m_sip_connect_param->port
                       << " sip_ret=" << sipRet);


         return kGbMessageFail;


    }





    if( !result || result->messgae.code != kSuccessRequest) {

        TVT_LOG_ERROR("gb heartbeat response invalid"
                      << " device=" << m_xml_parser->m_local_code
                      << " remote_name=" << m_gb_connect->GBCode
                      << " remote=" << m_strIP << ":" << m_sip_connect_param->port
                      << " has_result=" << (result ? 1 : 0)
                      << " sip_code=" << (result ? result->messgae.code : -1));

        code = kGbResponseCodeError;


    }
    else {
        TVT_LOG_INFO("gb heartbeat response ok"
                     << " device=" << m_xml_parser->m_local_code
                     << " remote_name=" << m_gb_connect->GBCode
                     << " remote=" << m_strIP << ":" << m_sip_connect_param->port
                     << " sip_code=" << result->messgae.code);
    }


        m_sip_client->FreeSipResult(result);


        return code;


}





// device information reponse interface


int CGBClientImpl::ResponseDeviceCatalog( ResponseHandle handle,  const DeviceCatalogList*  catalog, int trans_mode)


{


    SipMessage message;


    memset(&message, 0,sizeof(message));


	message.content_type = kSipContentMANSCDP_XML;


    int sn = (int)handle;


    std::string content;


    int i = 0;





	if (catalog->sum == 0) {


		m_xml_parser->PackCatalogResponse(sn, 0,0, catalog, content);


		message.content = (char*)content.c_str();


		m_sip_client->Message(&message, 0, NULL);


		return kGb28181Success;


	}





	int start = 0;


	int end = 0;


	while (i < (int)catalog->sum ) {





		start = i;





		end = i + 1;





		if (end >= (int)catalog->sum) {


			end = catalog->sum - 1;


		}





		content = "";


		m_xml_parser->PackCatalogResponse(sn, start,end, catalog, content);


		if (content.empty()) {


			continue;


		}


		message.content = (char*)content.c_str();


		if (trans_mode == 0)


		{


			int code = m_sip_client->Message(&message, 0, NULL);


			if (code != kSipSuccess) {


				return kGbFail;


			}


		}


		else if (trans_mode == 1)


		{


			long hNetCommunication;


			StartConnect(hNetCommunication, (char*)m_strIP.c_str(), m_sip_connect_param->port);


			char* pBuf = NULL;


			size_t len = 0;


			m_sip_client->MessageToStr(&message, &pBuf, &len);


			tint32 nSendLen = Send(hNetCommunication, pBuf, len);


            if (nSendLen == (tint32)len)


			{


				m_sip_client->FreeSipBuffer(&pBuf);


				NET_SOCKET_Stop(hNetCommunication);


				NET_SOCKET_UnRegisterNode(hNetCommunication);


				NET_SOCKET_DestroyHNetCommunication(hNetCommunication);


			}


		}


		i += 2;


	}





	return kGb28181Success;


}





int CGBClientImpl::ResponseDeviceInfo(ResponseHandle handle,const DeviceInfo* device_info)


{


    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;





    std::string content;


    int sn = (int)handle;


    m_xml_parser->PackDeviceInfoResponse( sn, device_info, content );


    if(content.empty()) {


         return kGbXmlEncodeFail;


    }





    message.content = (char*)content.c_str();


    int code = m_sip_client->Message(&message,0,NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


        return kGb28181Success;





}





int CGBClientImpl::ResponseDeviceStatus(ResponseHandle handle, const DeviceStatus* device_status)


{


    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;





    std::string content;


    int sn = (int)handle;





    m_xml_parser->PackDeviceStatusResponse( sn, device_status, content );


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    message.content = (char*)content.c_str();


    int code =  m_sip_client->Message(&message,0,NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


        return kGb28181Success;


}





int CGBClientImpl::ResponseDeviceConfigQuery(ResponseHandle handle,  const DeviceConfigDownload* device_config)


{


	SipMessage message;


	memset(&message,0,sizeof(SipMessage));


	message.content_type = kSipContentMANSCDP_XML;


	std::string content;


	int sn = (int)handle;


	m_xml_parser->PackConfigDownloadResponse( sn,device_config,content);


	if (content.empty())


	{


		return kGbXmlEncodeFail;


	}


	message.content = (char*)content.c_str();


	int code = m_sip_client->Message(&message,0,NULL);


	if (code != kSipSuccess)


	{


		return kGbFail;


	}


    return kGb28181Success;


}





int CGBClientImpl::ResponseDevicePresetInfo(ResponseHandle handle, const PresetInfo*  preset_info)


{


    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;





    std::string content;


    int sn = (int)handle;





    m_xml_parser->PackDevicePresetResponse( sn, preset_info, content );


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    message.content = (char*)content.c_str();


    int code =  m_sip_client->Message(&message,0,NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


        return kGb28181Success;





}





// stream media interface


int CGBClientImpl::ResponseMediaPlayInfo(StreamHandle stream_handle,  const MediaInfo* info)


{


	if( !m_stream_manager->FindStreamHandle(stream_handle)) {

		TVT_LOG_ERROR("gb media response skipped because stream handle does not exist"
			<< " handle=" << stream_handle);


		return kGbStreamHandleNotExist   ;


    }





    std::string content;


    CSdpUtil::ToString(info, content);


    SipMessage message;


    memset(&message,0, sizeof(message) );


    message.code = kSuccessRequest;


    message.content = (char*)content.c_str();


    message.content_type = kSipContentSDP;


    message.Method = kSipInviteMethod;


	SipDialogKey* key = (SipDialogKey*)stream_handle;


	bool result = kGb28181Success;

	TVT_LOG_INFO("gb media response try send 200ok"
		<< " handle=" << stream_handle
		<< " answer_transport=" << (info ? (int)info->RtpType : -1)
		<< " answer_ip=" << (info ? info->IP : "")
		<< " answer_port=" << (info ? info->Port : 0)
		<< " ssrc=" << (info ? info->Ssrc : "")
		<< " content_len=" << content.size()
		<< " call_id=" << (key ? key->CallId : -1)
		<< " dialog_id=" << (key ? key->DialogId : -1));





	if(kSipSuccess != m_sip_client->Response(key, &message)) {


		result = kGbFail;

		TVT_LOG_ERROR("gb media response send 200ok failed"
			<< " handle=" << stream_handle
			<< " call_id=" << (key ? key->CallId : -1)
			<< " dialog_id=" << (key ? key->DialogId : -1));


	}
	else {

		TVT_LOG_INFO("gb media response send 200ok success"
			<< " handle=" << stream_handle
			<< " call_id=" << (key ? key->CallId : -1)
			<< " dialog_id=" << (key ? key->DialogId : -1));

	}


       return   result;


}





int CGBClientImpl::ResponseRecordIndex(ResponseHandle handle, const RecordIndex* index, int trans_mode)


{





    if (index == NULL) {


        return kGbNullPointer;


    }





    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


    int sn = (int)handle;


    const size_t kMaxRecordInfoBodyLen = 24000;


	long hNetCommunication = 0;


    bool tcp_connected = false;


	if (trans_mode == 1)


	{


		tcp_connected = StartConnect(hNetCommunication,(char*)m_strIP.c_str(),m_sip_connect_param->port);


        if (!tcp_connected) {


            return kGbFail;


        }


	}


    else if (trans_mode != 0)


    {


        return kGbFail;


    }


    int result = kGb28181Success;


    unsigned int total_num = index->Num;


    unsigned int start = 0;


    unsigned int chunk_index = 0;


    do {


        std::string content;


        unsigned int send_count = 0;


        if (total_num == 0) {


            m_xml_parser->PackDeviceRecordIndexResponseEx(sn,
                                                          index->GBCode,
                                                          0,
                                                          NULL,
                                                          0,
                                                          content);


        } else {


            std::string best_content;


            unsigned int best_count = 0;


            unsigned int remain = total_num - start;


            for (unsigned int count = 1; count <= remain; ++count) {


                std::string candidate_content;


                m_xml_parser->PackDeviceRecordIndexResponseEx(sn,
                                                              index->GBCode,
                                                              total_num,
                                                              index->record_list + start,
                                                              count,
                                                              candidate_content);


                if (candidate_content.empty()) {


                    result = kGbXmlEncodeFail;


                    break;


                }


                best_content.swap(candidate_content);


                best_count = count;


                if (best_content.size() > kMaxRecordInfoBodyLen) {


                    if (best_count > 1) {


                        best_count -= 1;


                        m_xml_parser->PackDeviceRecordIndexResponseEx(sn,
                                                                      index->GBCode,
                                                                      total_num,
                                                                      index->record_list + start,
                                                                      best_count,
                                                                      best_content);


                    }


                    break;


                }


            }


            if (result != kGb28181Success) {


                break;


            }


            content.swap(best_content);


            send_count = best_count;


        }


        if(content.empty()) {


            result = kGbXmlEncodeFail;


            break;


        }


        ++chunk_index;


        TVT_LOG_INFO("gb recordinfo response chunk"
                     << " sn=" << sn
                     << " chunk=" << chunk_index
                     << " start=" << start
                     << " count=" << send_count
                     << " total=" << total_num
                     << " body_len=" << content.size()
                     << " trans_mode=" << trans_mode);


		message.content = (char*)content.c_str();


		if (trans_mode == 0)


		{


			int code =  m_sip_client->Message(&message,0,NULL);


			if( code != kSipSuccess) {


                result = kGbFail;


                break;


			}


		}


		else if (trans_mode == 1)


		{


			char* pBuf = NULL;


            size_t len = 0;


			m_sip_client->MessageToStr(&message, &pBuf, &len);


			tint32 nSendLen = Send(hNetCommunication,pBuf,len);


            m_sip_client->FreeSipBuffer(&pBuf);


            if (nSendLen != (tint32)len)


			{


                result = kGbFail;


                break;


			}


		}


        if (total_num == 0) {


            break;


        }


        if (send_count == 0) {


            result = kGbFail;


            break;


        }


        start += send_count;


    } while (start < total_num);


    if (tcp_connected) {


        NET_SOCKET_Stop(hNetCommunication);


        NET_SOCKET_UnRegisterNode(hNetCommunication);


        NET_SOCKET_DestroyHNetCommunication(hNetCommunication);


    }


	return result;


}





// Notify


int CGBClientImpl::NotifyDeviceAlarm(const AlarmNotifyInfo* alarm_info)


{





    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;





    std::string content;





    m_xml_parser->PackAlarmNotify(m_alarm_sub.sn, alarm_info, content);


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    message.content = (char*)content.c_str();


    int code = m_sip_client->Message(&message,0,NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


        return kGb28181Success;





}





int CGBClientImpl::NotifyDeviceUpgradeResult(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description)

{

    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


    std::string content;


    m_xml_parser->PackDeviceUpgradeResultNotify(device_id, session_id, firmware, result, description, content);


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }


    message.content = (char*)content.c_str();


    int code = m_sip_client->Message(&message,0,NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


    return kGb28181Success;


}



int CGBClientImpl::NotifyMediaStatus(StreamHandle stream_handle, const char* device_id,  int NotifyType )


{


	if (!m_stream_manager->FindStreamHandle(stream_handle)) {


		return kGbStreamHandleNotExist;


	}





	SipDialogKey* key = (SipDialogKey*)stream_handle;


	//bool result = kGb28181Success;





    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


	message.Method = kSipMessageWithCallMethod;





    std::string content;


    m_xml_parser->PackMediaStatusNotify(device_id, NotifyType, content );


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    message.content = (char*)content.c_str();


	int code = m_sip_client->RequestWithCall(key, &message, 0, NULL);


    if( code != kSipSuccess) {


         return kGbFail;


    }


         return kGb28181Success;





}





int CGBClientImpl::NotifyMobilePositionForSub( const MobilePositionInfo* info )


{


 


    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


    std::string content;


    m_xml_parser->PackMobilePositionNotify(m_mobile_position_sub.sn, info, content );


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }


    message.content = (char*)content.c_str();


	message.Event = (char*)m_mobile_position_sub.event.c_str();


	int code = m_sip_client->Notify(&m_mobile_position_sub.key, &message, 0, NULL);


	if (code != kSipSuccess) {


		return kGbFail;


	}


	return kGb28181Success;


}





// reponse subscribe


int CGBClientImpl::NotifyDeviceAlarmForSub(const AlarmNotifyInfo* alarm_info)


{


    SipMessage message;


    memset(&message, 0,sizeof(message));


    message.content_type = kSipContentMANSCDP_XML;


    std::string content;


    m_xml_parser->PackAlarmNotify(m_alarm_sub.sn, alarm_info, content );


    if(content.empty()) {


        return false;


    }


    message.content = (char*)content.c_str();


	message.Event = (char*)m_alarm_sub.event.c_str();


    int code =  m_sip_client->Notify(&m_alarm_sub.key, &message, 0, NULL);


	if (code != kSipSuccess) {


		return kGbFail;


	}


	return kGb28181Success;


}





int CGBClientImpl::NotifyDeviceCatalogForSub(const DeviceCatalogList* catalog)


{


	SipMessage message;


	memset(&message, 0,sizeof(message));


	message.content_type = kSipContentMANSCDP_XML;


	std::string content;


	m_xml_parser->PackCatalogNotify(m_catalog_sub.sn, catalog->sum, catalog, content);


	if (content.empty()) {


		return kGbXmlEncodeFail;


	}


	message.content = (char*)content.c_str();


	message.Event = (char*)m_catalog_sub.event.c_str();


	int code = m_sip_client->Notify(&m_catalog_sub.key, &message, 0, NULL);


	if (code != kSipSuccess) {


		return kGbFail;


	}


	   return kGb28181Success;





}





int    CGBClientImpl::SendSipMessageWithOk(const SipData* data)


{


    SipMessage message;


    memset(&message, 0 , sizeof(message));


    message.code = kSuccessRequest;


	message.Method = kSipMessageMethod;


    return m_sip_client->Response(&(data->Dialog),   &message );


}





int    CGBClientImpl::SendSipMessageWithBadReq(const SipData* data)


{


    SipMessage message;


    memset(&message, 0 , sizeof(message));


    message.code = kBadRequest;


	message.Method = kSipMessageMethod;


    return m_sip_client->Response(&(data->Dialog),   &message );


}





int CGBClientImpl::SendSipMessageForDeviceControlResponse(std::string cmdStr,const SipData* data,int sn,bool result,char* GBCode)


{


	SipMessage message;


	memset(&message,0,sizeof(message));


	message.code = kSuccessRequest;


	message.Method = kSipMessageMethod;


	std::string content;


	m_xml_parser->PackResponse(cmdStr.c_str(), sn, result, GBCode, content );


	message.content = (char*)content.c_str();


	message.content_type = kSipContentMANSCDP_XML;


	return m_sip_client->Message(&message, 0,  NULL);





}





void CGBClientImpl::OnRequest(SipMethod  method,const SipData* data )


{





    switch((int)method)


    {


         case kSipInviteMethod :       OnInvite(data);           break;


         case kSipByeMethod :         OnBye(data);              break;


         case kSipAckMethod :         OnACK(data);             break;


         case kSipInfoMethod :         OnInfo(data);             break;


         case kSipNotifyMethod:


         case kSipMessageMethod :


         case kSipSubscribeMethod:  OnMessage(data);     break;


    }





}





 void  CGBClientImpl::OnInfo(const SipData* data)


 {


     bool res;


     std::string content;


     SipMessage message;


     StreamHandle Handle = NULL;


     PlayCtrlCmd cmd;


     std::string cseq;


     memset(&message,0, sizeof(message));


     message.code = kSuccessRequest;


	





     if( data->messgae.content_type != kSipContentMANSRTSP && data->messgae.content_type != kSipContentRTSP) {


          message.code = kBadRequest;


          goto finally;


     }





     res = RtspParser::StringToCmd(data->messgae.content, cseq, &cmd  );


     content =  RtspParser::GetResponseMessage(400, "Bad Request",  cseq );


     if( !res ) {


         message.code = kBadRequest;


         goto finally;


     }


     Handle = m_stream_manager->FindStreamHandle(data->Dialog.CallId,data->Dialog.DialogId );


     if(  Handle == NULL) {


          message.code = kForbidden;


          goto finally;


     }





     if(  !m_gb_receiver || !m_gb_receiver->OnPlayControl(Handle,   &cmd ) ) {


          message.code = kForbidden;


          goto finally;


     }


	 


         content =  RtspParser::GetResponseMessage(200, "OK",  cseq );


finally:


         message.content = (char*)content.c_str();


		 message.content_type = kSipContentMANSRTSP;


         message.Method = kSipInfoMethod;


         m_sip_client->Response(&(data->Dialog), &message );








 }





void  CGBClientImpl::OnMessage(const SipData* data)


{


    SipMessage message;


    memset(&message,0, sizeof(message));


	message.Method = kSipMessageMethod;


    message.code = kBadRequest;


    if( data->messgae.content_type != kSipContentMANSCDP_XML) {


         m_sip_client->Response(&(data->Dialog), &message );


         return ;


    }





    ProtocolType  proco =  (ProtocolType)m_xml_parser->GetProtocolType( data->messgae.content  );


    if( proco == kInvailProco  ) {


          m_sip_client->Response(&(data->Dialog), &message );


          return;


     }





      if( proco >= kControlProco  && proco < kQueryProco ) {


            OnDeviceControl( proco, data);


            return;


      }





	  if( proco > kQueryProco  && proco < kNotifyProco ) {





		  if( data->messgae.Method ==  kSipSubscribeMethod ) 


		  {


			  if (proco == kCatalogQueryProco)


			  {


				  if (data->messgae.Expirse == 0)


				  {


					  SubscribeHandle handle = (SubscribeHandle)(&m_catalog_sub.key);


					  OnCancelSubScribe(handle,m_subscribe_manager);


					  return;


				  }


				  OnCatalogSub(kCatalogSubscribeProco,data);


			  }


			  else if (proco == kAlarmQueryProco)


			  {


				  if (data->messgae.Expirse == 0)


				  {


					  SubscribeHandle handle = (SubscribeHandle)(&m_alarm_sub.key);


					  OnCancelSubScribe(handle,m_subscribe_manager);


					  return;


				  }


				  OnAlarmSub(kAlarmSubscribeProco,data);


			  }


			  else if (proco == kMobilePositionQueryProco)


			  {


				  if (data->messgae.Expirse == 0)


				  {


					  SubscribeHandle handle = (SubscribeHandle)(&m_mobile_position_sub.key);


					  OnCancelSubScribe(handle,m_subscribe_manager);


					  return;


				  }


				  OnMobilePositionSub(kMobilePositionSubscribeProco,data);


			  }


		  }


		  else{


			  OnQuery( proco, data);


		  }


		  return;


	  }





	  if( proco > kNotifyProco  && proco < kResponseProco ) {


		  OnNotify( proco, data);


		  return;


	  }





	  if( proco > kResponseProco ) {


		  OnGBProcoResponse( proco, data);


		  return;


	  }


}





void  CGBClientImpl::OnQuery(ProtocolType  type, const SipData* data)


{





    QueryParam param;


	memset(&param,0, sizeof(param));


    param.type = GetQueryType(type);


    if(  param.type == kUnknowQuery  ) {


       SendSipMessageWithBadReq(data);


       return;


    }





    int sn;


    SendSipMessageWithOk(data);


    m_xml_parser->UnPackDeviceQuery (data->messgae.content, sn,   param );





    if( m_gb_receiver) {


        m_gb_receiver->OnQuery( (ResponseHandle)sn,  &param );


    }





}





void  CGBClientImpl::OnNotify(ProtocolType  type, const SipData* data)


{


	if (GetNotifyType(type) == kBroadcastNotify) {


		SipMessage message;


		memset(&message, 0, sizeof(message));


		message.code = kSuccessRequest;


		message.Method = kSipMessageMethod;


		int sn = 0;


		NotifyInfo info;


		memset(&info, 0, sizeof(info));


		info.type = GetNotifyType(type);


		if (!m_xml_parser->UnPackNotifyInfo(data->messgae.content, sn, info)) {


			message.code = kBadRequest;


			TVT_LOG_ERROR("gb notify unpack failed"
			              << " notify_type=" << (int)info.type
			              << " xml_len=" << (data->messgae.content ? (int)strlen(data->messgae.content) : 0));


			if (kSipSuccess != m_sip_client->Response(&(data->Dialog), &message)) {


				TVT_LOG_ERROR("gb notify sip response send failed"
				              << " notify_type=" << (int)info.type
				              << " sn=" << sn
				              << " gb_code=" << info.GBCode
				              << " sip_code=" << message.code);


			}


			return;


		}


		bool accepted = true;


		if (m_gb_receiver && !m_gb_receiver->OnNotify(info.type, info.GBCode, &info)) {


			accepted = false;


			message.code = kForbidden;


		}


		TVT_LOG_INFO("gb notify sip response"
		             << " notify_type=" << (int)info.type
		             << " sn=" << sn
		             << " gb_code=" << info.GBCode
		             << " sip_code=" << message.code);


		if (kSipSuccess != m_sip_client->Response(&(data->Dialog), &message)) {


			TVT_LOG_ERROR("gb notify sip response send failed"
			              << " notify_type=" << (int)info.type
			              << " sn=" << sn
			              << " gb_code=" << info.GBCode
			              << " sip_code=" << message.code);


			return;


		}


		bool responseOk = false;


		if (accepted) {


			responseOk = (SendBroadcastResponseMessage(sn, true, info.GBCode) == kGb28181Success);


		}


		if (m_gb_receiver) {


			m_gb_receiver->OnBroadcastResponse(info.GBCode, &(info.notify_message.broadcast_info), responseOk);


		}


		return;


	}


	SipMessage message;


	memset(&message, 0, sizeof(message));


	message.code = kSuccessRequest;


	message.Method = kSipMessageMethod;


	message.content_type = kSipContentMANSCDP_XML;


	bool result = true;


	int sn = 0;


	NotifyInfo info;


	memset(&info, 0, sizeof(info));


	info.type = GetNotifyType(type);


	std::string content;


	if (!m_xml_parser->UnPackNotifyInfo(data->messgae.content, sn, info)) {


		message.code = kBadRequest;


		TVT_LOG_ERROR("gb notify unpack failed"
		              << " notify_type=" << (int)info.type
		              << " xml_len=" << (data->messgae.content ? (int)strlen(data->messgae.content) : 0));


		goto finally;


	}


	if (m_gb_receiver && !m_gb_receiver->OnNotify(info.type, info.GBCode, &info)) {


		message.code = kForbidden;


	}


finally:


	if (message.code != kSuccessRequest) {


		result = false;


	}


	m_xml_parser->PackResponse("Broadcast", sn, result, info.GBCode, content);


	message.content = (char*)content.c_str();


	TVT_LOG_INFO("gb notify response"
	             << " notify_type=" << (int)info.type
	             << " sn=" << sn
	             << " gb_code=" << info.GBCode
	             << " sip_code=" << message.code
	             << " result=" << (result ? 1 : 0)
	             << " xml_len=" << (int)content.size());


	if (kSipSuccess != m_sip_client->Response(&(data->Dialog), &message)) {


		TVT_LOG_ERROR("gb notify response send failed"
		              << " notify_type=" << (int)info.type
		              << " sn=" << sn
		              << " gb_code=" << info.GBCode
		              << " sip_code=" << message.code);


	}


}

int CGBClientImpl::SendBroadcastResponseMessage(int sn, bool result, const char* GBCode)


{


	SipMessage message;


	memset(&message,0,sizeof(message));


	message.Method = kSipMessageMethod;


	message.content_type = kSipContentMANSCDP_XML;


	std::string content;


	m_xml_parser->PackResponse("Broadcast", sn, result, GBCode, content );


	if(content.empty()) {


		TVT_LOG_ERROR("gb broadcast response message encode failed"
		              << " sn=" << sn
		              << " gb_code=" << (GBCode != NULL ? GBCode : "")
		              << " result=" << (result ? 1 : 0));


		return kGbXmlEncodeFail;


	}


	message.content = (char*)content.c_str();


	const int timeout = 3000;


	SipData* response = NULL;


	TVT_LOG_INFO("gb broadcast response message send"
	             << " sn=" << sn
	             << " gb_code=" << (GBCode != NULL ? GBCode : "")
	             << " result=" << (result ? 1 : 0)
	             << " xml_len=" << (int)content.size()
	             << " timeout=" << timeout);


	const int sipRet = m_sip_client->Message(&message, timeout, &response);


	if(kSipSuccess != sipRet) {


		TVT_LOG_ERROR("gb broadcast response message sip failed"
		              << " sn=" << sn
		              << " gb_code=" << (GBCode != NULL ? GBCode : "")
		              << " sip_ret=" << sipRet);


		return kGbMessageFail;


	}


	int code = kGb28181Success;


	if(!response || response->messgae.code != kSuccessRequest) {


		TVT_LOG_ERROR("gb broadcast response message invalid"
		              << " sn=" << sn
		              << " gb_code=" << (GBCode != NULL ? GBCode : "")
		              << " has_result=" << (response ? 1 : 0)
		              << " sip_code=" << (response ? response->messgae.code : -1));


		code = kGbResponseCodeError;


	}


	else {


		TVT_LOG_INFO("gb broadcast response message ok"
		             << " sn=" << sn
		             << " gb_code=" << (GBCode != NULL ? GBCode : "")
		             << " sip_code=" << response->messgae.code);


	}


	m_sip_client->FreeSipResult(response);


	return code;




}





void  CGBClientImpl::OnGBProcoResponse(ProtocolType  type, const SipData* data)


{


    SendSipMessageWithOk(data);


    switch( (int)type) {


       case kCatalogResponseProco:             OnCatalogResponse(data) ;break;


    }


}





void CGBClientImpl::OnCatalogSub(ProtocolType  type, const SipData* data)


{





    int sn;


    std::string code;


    std::string content;


    CatalogSubcribeInfo  info;


    memset(&info, 0 , sizeof(info));


    m_xml_parser->UnPackCatalogSubcribe(data->messgae.content,sn,code,&info);


    m_xml_parser->PackResponse("Catalog", sn, true, code.c_str(), content );





    if(data->messgae.Event) {


        m_catalog_sub.event = data->messgae.Event;


    }


    m_catalog_sub.sn = sn;


    m_catalog_sub.key = data->Dialog;





	SipMessage message;


	memset(&message, 0 , sizeof(message));


	message.code = kSuccessRequest;


    message.Method = kSipSubscribeMethod;


    message.content_type = kSipContentMANSCDP_XML;


    message.content = (char*)content.c_str();


    m_sip_client->Response(&(data->Dialog),   &message );





	if (m_subscribe_manager->FindSubscribeHandle(m_catalog_sub.key.DialogId)) {


		m_subscribe_manager->DeleteSubscribeHandle((SubscribeHandle)(&m_catalog_sub.key));


	}





	SubscribeHandle handle = (SubscribeHandle)(&(m_catalog_sub.key));





	m_subscribe_manager->InsertSubscribeHandle(handle,data->messgae.Expirse);





	m_subscribe_manager->SetMessage(ExpireMessageSender,this);//??????





	if( m_gb_receiver) {


		m_gb_receiver->OnSubscribe(handle,kCatalogSubscribe,info.DeivceID,data->messgae.content );


	}


}





void CGBClientImpl::OnAlarmSub(ProtocolType  type, const SipData* data)


{


	int sn;


	//int sum;


	std::string code;


	AlarmSubcribeInfo info;


	memset(&info,0,sizeof(info));


	m_xml_parser->UnPackAlarmSubcribe(data->messgae.content,sn,code,&info);





    if(data->messgae.Event){


        m_alarm_sub.event = data->messgae.Event;


    }


	m_alarm_sub.sn = sn;


	m_alarm_sub.key = data->Dialog;








	SipMessage message;


	memset(&message, 0 , sizeof(message));


	message.code = kSuccessRequest;


	message.Method = kSipSubscribeMethod;


	message.content_type =  kSipContentMANSCDP_XML;


	std::string content;


	m_xml_parser->PackResponse("Alarm",sn, true,code.c_str(),content);


	message.content = (char*)content.c_str();


	m_sip_client->Response(&(data->Dialog),   &message );





	if (m_subscribe_manager->FindSubscribeHandle(m_alarm_sub.key.DialogId))


	{


		m_subscribe_manager->DeleteSubscribeHandle((SubscribeHandle)(&m_alarm_sub));


	}





	SubscribeHandle handle = (SubscribeHandle)(&(m_alarm_sub.key));


	m_subscribe_manager->InsertSubscribeHandle(handle,data->messgae.Expirse);


	m_subscribe_manager->SetMessage(ExpireMessageSender,this);


	if (m_gb_receiver)


	{


		m_gb_receiver->OnSubscribe(handle,kAlarmSubscribe,info.DeivceID,data->messgae.content);


	}


}





void CGBClientImpl::OnMobilePositionSub(ProtocolType  type, const SipData* data)


{


	int sn;


	//int sum;


	std::string code;


	MobilePositionSubInfo info;


	memset(&info,0,sizeof(info));


	m_xml_parser->UnPackMobilePositionSubcribe(data->messgae.content,sn,code,&info);





    if(data->messgae.Event) {


		m_mobile_position_sub.event = data->messgae.Event;


    }


	m_mobile_position_sub.sn = sn;


	m_mobile_position_sub.key = data->Dialog;





	SipMessage message;


	memset(&message, 0 , sizeof(message));


	message.code = kSuccessRequest;


	message.Method = kSipSubscribeMethod;


	message.content_type =  kSipContentMANSCDP_XML;


	std::string content;


	m_xml_parser->PackResponse("MobilePosition",sn, true,code.c_str(),content);


	message.content = (char*)content.c_str();


	m_sip_client->Response(&(data->Dialog),   &message );





	if (m_subscribe_manager->FindSubscribeHandle(m_mobile_position_sub.key.DialogId))


	{


		m_subscribe_manager->DeleteSubscribeHandle((SubscribeHandle)(&m_mobile_position_sub));


	}





	SubscribeHandle handle = (SubscribeHandle)(&(m_mobile_position_sub.key));


	m_subscribe_manager->InsertSubscribeHandle(handle,data->messgae.Expirse);


	m_subscribe_manager->SetMessage(ExpireMessageSender,this);


	if (m_gb_receiver)


	{


		m_gb_receiver->OnSubscribe(handle,kMobilePositionSubscribe,info.DeivceID,data->messgae.content);


	}


}





void CGBClientImpl::OnSubScribe(ProtocolType  type, const SipData* data)


{


	if( type == kCatalogSubscribeProco )


	{


       OnCatalogSub(type,data);


	}





}





void  CGBClientImpl::OnCancelSubScribe(SubscribeHandle handle, CSubscribeManager* sub_manager)


{


 	if(!sub_manager->FindSubscribeHandle(handle)){


 		return;


 	}


 	bool flag = sub_manager->DeleteSubscribeHandle(handle);


 	if (flag){


 		TVT_LOG_INFO(" upper cancel subscribe success");


 	}


}





void CGBClientImpl::ExpireMessageSender (SubscribeHandle handle,void* user)


{


	if(!user){


		return;


	}


	CGBClientImpl* subscribe_manager = (CGBClientImpl*)user;


	//????????


	SubscribeExpireInfo* info = new SubscribeExpireInfo;


	memset(info,0,sizeof(SubscribeExpireInfo));


	info->handle = handle;


	info->client = subscribe_manager;


	SHARESDK_RunOneThread(SubscribeExpireDelThread,info, "", "", 0, "SubscribeExpireDelThread");


}





void CGBClientImpl::SubscribeExpireDelThread(LPVOID p)


{


	SubscribeExpireInfo* info = reinterpret_cast<SubscribeExpireInfo*>(p);


	if(info){


		


        SubscribeType type = kCatalogSubscribe;


		SubscribeHandle catalog_handle = (SubscribeHandle)(&info->client->m_catalog_sub.key);


		SubscribeHandle alarm_handle = (SubscribeHandle)(&info->client->m_alarm_sub.key);





		if( catalog_handle == info->handle){


			type = kCatalogSubscribeExpire;


		}


		else if (alarm_handle == info->handle){


			type = kAlarmSubscribeExpire;


		}


		info->client->m_subscribe_manager->DeleteSubscribeHandle(info->handle);


		info->client->m_gb_receiver->OnSubscribe(info->handle,type,NULL,NULL);


		delete info;


		info = NULL;


	}


}





void  CGBClientImpl::OnCatalogResponse(const SipData* data)


{


    int sn ;


    int sum;


    DeviceCatalogList  list;


    memset(&list, 0 , sizeof(list));


    m_xml_parser->UnPackCatalogResponse(data->messgae.content, sn, sum, &list );


    m_waiter_manager->WakeUpWaitResponse(sn ,"Catalog", list.GBCode ,sum, &list );


    if(list.catalog) {


        free(list.catalog);


    }


}





void  CGBClientImpl::OnDeviceControl(ProtocolType  type, const SipData* data)


{


    DevControlCmd cmd;


	memset(&cmd,0, sizeof(cmd));


    cmd.type = GetControlType(type);


    if(   cmd.type == kUnknowControl  ) {


         SendSipMessageWithBadReq(data);


         return;


    }





    int sn;


    //int result = true;


	m_xml_parser->UnPackDeviceControl( data->messgae.content, sn,   cmd );


    bool callback_result = false;


    if( m_gb_receiver ) {


          callback_result = m_gb_receiver->OnDeviceControl( (ResponseHandle)sn,  &cmd );


    }


	if (cmd.type == kPtzControl || cmd.type == kTeleBootControl || cmd.type == kIFameControl)


	{


		SendSipMessageWithOk(data);


	}


	if (cmd.type == kRecordControl || cmd.type == kHomePositionControl || cmd.type == kAlarmResetControl ||


		cmd.type == kGurdControl)


	{


		SendSipMessageWithOk(data);


		SendSipMessageForDeviceControlResponse("DeviceControl",data,sn,"OK",cmd.GBCode);


	}


	if (cmd.type == kDevConfigControl)


	{


		SendSipMessageWithOk(data);


		SendSipMessageForDeviceControlResponse("DeviceConfig",data,sn,"OK",cmd.GBCode);


		SendSipMessageForDeviceControlResponse("DeviceControl",data,sn,"OK",cmd.GBCode);


	}


	if (cmd.type == kDeviceUpgradeControl)


	{


		SendSipMessageWithOk(data);


		SendSipMessageForDeviceControlResponse("DeviceControl",data,sn,callback_result,cmd.GBCode);


	}


}





void  CGBClientImpl::OnInvite(const SipData* data)


{


    std::string subject;


    MediaInfo media_info;


    SipDialogKey   *dialog = NULL;


    memset(&media_info,0, sizeof(media_info));


    media_info.RtpDescri.mapDescri  = NULL;


    SipMessage message;


   memset(&message,0, sizeof(message));


   StreamHandle handle = NULL;


	if( data->messgae.content_type != kSipContentSDP){

		TVT_LOG_ERROR("gb invite rejected because content type is not sdp"
			<< " content_type=" << (int)data->messgae.content_type
			<< " call_id=" << data->Dialog.CallId
			<< " dialog_id=" << data->Dialog.DialogId);


		message.code = kBadRequest;


        goto finally;


    }





	if( !CSdpUtil::String2MediaInfo( data->messgae.content,  &media_info)){

		  TVT_LOG_ERROR("gb invite rejected because sdp parse failed"
			<< " call_id=" << data->Dialog.CallId
			<< " dialog_id=" << data->Dialog.DialogId
			<< " content_len=" << (data->messgae.content ? strlen(data->messgae.content) : 0)
			<< " sdp=" << (data->messgae.content ? data->messgae.content : ""));


		  message.code = kBadRequest;


          goto finally;


    }





	if (data->messgae.Subject) {


		subject = data->messgae.Subject;


		size_t end = subject.find_first_of(":,");
		if (end == std::string::npos) {
			end = subject.size();
		}

		std::string subject_device = subject.substr(0, end);
		while (!subject_device.empty() &&
		       (subject_device[subject_device.size() - 1] == ' ' ||
		        subject_device[subject_device.size() - 1] == '\t')) {
			subject_device.erase(subject_device.size() - 1);
		}

		if (!subject_device.empty()) {
			memset(media_info.DeviceID, 0, sizeof(media_info.DeviceID));
			memcpy(media_info.DeviceID,
			       subject_device.c_str(),
			       (subject_device.size() < (sizeof(media_info.DeviceID) - 1))
			           ? subject_device.size()
			           : (sizeof(media_info.DeviceID) - 1));
		}


	}

	TVT_LOG_INFO("gb invite parsed"
		<< " device=" << media_info.DeviceID
		<< " request_type=" << (int)media_info.RequestType
		<< " transport=" << (int)media_info.RtpType
		<< " remote_ip=" << media_info.IP
		<< " remote_port=" << media_info.Port
		<< " call_id=" << data->Dialog.CallId
		<< " dialog_id=" << data->Dialog.DialogId);


 


    dialog = (SipDialogKey*)malloc( sizeof(SipDialogKey) );


    memcpy(dialog,   &(data->Dialog),  sizeof(*dialog));


    handle = (StreamHandle)dialog;


    m_stream_manager->InsertStreamHandle(   (StreamHandle)dialog  );


    


	if(  !m_gb_receiver || !m_gb_receiver->OnStreamRequest(  handle,  media_info.DeviceID,  media_info.RequestType,  &media_info   )) {

		  TVT_LOG_ERROR("gb invite stream request callback rejected"
			<< " device=" << media_info.DeviceID
			<< " request_type=" << (int)media_info.RequestType
			<< " handle=" << handle
			<< " has_receiver=" << (m_gb_receiver != NULL ? 1 : 0));


		  m_stream_manager->DeleteStreamHandle(   (StreamHandle)dialog     );


          free(dialog);


          message.code = kForbidden;


          goto finally;


	}


		  message.code = kSuccessRequest;

		  TVT_LOG_INFO("gb invite stream request accepted and waiting media response"
			<< " device=" << media_info.DeviceID
			<< " request_type=" << (int)media_info.RequestType
			<< " handle=" << handle
			<< " call_id=" << data->Dialog.CallId
			<< " dialog_id=" << data->Dialog.DialogId);





finally:


       if( media_info.RtpDescri.mapDescri ) {


           free(media_info.RtpDescri.mapDescri);


       }





       if( message.code != kSuccessRequest) {


           message.Method = kSipInviteMethod;


           m_sip_client->Response(&(data->Dialog), &message );


       }





}





void  CGBClientImpl::OnACK(const SipData* data)


{

	StreamHandle  handle =  m_stream_manager->FindStreamHandle( data->Dialog.CallId , data->Dialog.DialogId    );
	TVT_LOG_INFO("gb invite ack received"
		<< " handle=" << handle
		<< " call_id=" << data->Dialog.CallId
		<< " dialog_id=" << data->Dialog.DialogId);

    if(!handle){
		TVT_LOG_ERROR("gb invite ack ignored because stream handle not found"
			<< " call_id=" << data->Dialog.CallId
			<< " dialog_id=" << data->Dialog.DialogId);
        return ;
    }

    PlayCtrlCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
    cmd.Type = kPlayStart;

    if(  !m_gb_receiver || !m_gb_receiver->OnPlayControl(handle,&cmd)) {
		TVT_LOG_ERROR("gb invite ack failed to trigger play start"
			<< " handle=" << handle
			<< " call_id=" << data->Dialog.CallId
			<< " dialog_id=" << data->Dialog.DialogId);
		return;
    }

	TVT_LOG_INFO("gb invite ack triggered play start"
		<< " handle=" << handle
		<< " call_id=" << data->Dialog.CallId
		<< " dialog_id=" << data->Dialog.DialogId);


}





void  CGBClientImpl::OnBye(const SipData* data)


{


    SipMessage message;


    memset( &message, 0 , sizeof(message) );


    message.Method = kSipByeMethod;


    StreamHandle  handle =  m_stream_manager->DeleteStreamHandle( data->Dialog.CallId , data->Dialog.DialogId    );





    if( handle == NULL ) {


        message.code = kBadRequest;


        goto finally;


    }





     if( m_gb_receiver) {


         m_gb_receiver->OnStreamRequest(handle, NULL, kStopStream, NULL    );


     }


       message.code = kSuccessRequest;


finally:


       m_sip_client->Response(&(data->Dialog), &message );





}





void CGBClientImpl::OnResponse(SipMethod  method,const SipData* data )


{





}





// device information query interface


int CGBClientImpl::QueryCatalog(const CatalogQueryParam* catalog)


{


    if(  !catalog ) {


        return kGbNullPointer;


    }


    int sn;


    std::string content;


    sn = m_xml_parser->PackCatalogQuery(catalog, content);


    if(content.empty()) {


        return kGbXmlEncodeFail;


    }





    SipMessage message;


    memset(&message,0,  sizeof(message));


    message.content = (char*)content.c_str();


    message.content_type = kSipContentMANSCDP_XML;





    CGbResponseWaiter* waiter = m_waiter_manager->CreateResponseWaiter(sn ,"Catalog", catalog->DeviceID  );


    CatalogTimerStruct*   catalog_timer = new CatalogTimerStruct;


    catalog_timer->client = this;


    catalog_timer->waiter = waiter;


    SHARESDK_RunOneThread(CatalogQueryProcess,


                                              catalog_timer,


                                              __FILE__,


                                              "",


                                              0,


                                              "query thread" );





   if( kSipSuccess !=m_sip_client->Message(&message, 0,NULL )) {


         return kGbMessageFail;


   }


         return kGb28181Success;


}





void CGBClientImpl::CatalogQueryProcess(LPVOID lpThreadParameter)


{


       CatalogTimerStruct* handle = (CatalogTimerStruct*)lpThreadParameter;


       if(handle){


          handle->client->CatalogQueryTimer( handle->waiter);


          delete handle;


       }


}





void CGBClientImpl::CatalogQueryTimer(  CGbResponseWaiter* waiter)


{


    int timeout = 1000000;


    DeviceCatalogList catalog_list;


    if( !m_waiter_manager->WaitResponse(waiter, timeout, &catalog_list )) {


          return ;


    }





    if(  m_gb_receiver ) {


        m_gb_receiver->OnQueryResponse( kDeviceCatalogQuery,   catalog_list.GBCode,  &catalog_list );


    }


    if(catalog_list.catalog){


        free(catalog_list.catalog);


    }


}





int   CGBClientImpl::SendSipMessageWithOk(SipSessionHandle handle, const SipData* data)


{


    SipMessage message;


    memset(&message, 0 , sizeof(message));


    message.code = kSuccessRequest;


    return m_sip_client->Response(&(data->Dialog),   &message );


}





// stream media interface


int CGBClientImpl::StartLiveStreamRequest(const MediaInfo* input,  MediaInfo* result, StreamHandle* stream_handle)


{


   return StreamRequest(input,   result, stream_handle);


}

int CGBClientImpl::StartBroadcastStreamRequest(const char* target_id,
                                               const MediaInfo* input,
                                               MediaInfo* result,
                                               StreamHandle* stream_handle)
{
    if (!target_id || !input || !result || !stream_handle) {
        return kGbNullPointer;
    }

    MediaInfo request = *input;
    const char* subject_sender_id = request.DeviceID[0] != '\0' ? request.DeviceID : target_id;
    return StreamRequestEx(&request, target_id, subject_sender_id, m_xml_parser->m_local_code.c_str(), result, stream_handle);
}





int CGBClientImpl::StopStreamRequest(StreamHandle stream_handle)


{


    if( !m_stream_manager->DeleteStreamHandle(stream_handle)){


        return kGbStreamHandleNotExist   ;


    }





    SipDialogKey* key = (SipDialogKey*)stream_handle;


    int timeout = 3000;


    SipData*  result = NULL;





    int error_code;


    if(kSipSuccess != m_sip_client->Bye( key, NULL, timeout, &result)) {


        error_code = kGbByeFail;


        goto finally;


    }





    if(  !result || result->messgae.code != kSuccessRequest) {


         error_code = kGbResponseCodeError;


         goto finally;


    }


         error_code = kGb28181Success;


finally:


        if(key)  {


            free(key);


        }


        m_sip_client->FreeSipResult(result);


        return  error_code;


}








std::string  CGBClientImpl::GenerateSubject( const std::string& devid, int StreamNum )


{


    std::ostringstream os;


    os << devid << ":" << StreamNum << ","<< m_xml_parser->m_local_code <<":0";


    std::string Subject = os.str();


    return Subject;


}

std::string CGBClientImpl::GenerateSubjectPair(const std::string& sender_id,
                                               int sender_stream_num,
                                               const std::string& receiver_id)
{
    std::ostringstream os;
    os << sender_id << ":" << sender_stream_num << "," << receiver_id << ":0";
    return os.str();
}








int   CGBClientImpl::StreamRequest(const MediaInfo* input,  MediaInfo* output, StreamHandle* stream_handle)


{


    return StreamRequestEx(input, NULL, NULL, NULL, output, stream_handle);


}




int   CGBClientImpl::StreamRequestEx(const MediaInfo* input,
                                     const char* request_id,
                                     const char* subject_sender_id,
                                     const char* subject_receiver_id,
                                     MediaInfo* output,
                                     StreamHandle* stream_handle)


{


    std::string res;


    CSdpUtil::ToString(input,res);





    SipMessage message;


    memset(&message, 0, sizeof(message));


    


  /*  std::string from;


    std::string to;


    std::string route;


    





    if(m_client_manager->GenerateSipAddress(handle,input->DeviceID, from,to,route   )) {


          message.From = (char*)from.c_str();


          message.To = (char*)to.c_str();


    }*/


   


    message.content = (char*)res.c_str();


    message.content_type = kSipContentSDP;


    std::string subject;
    if (subject_sender_id && subject_receiver_id) {
        subject = GenerateSubjectPair(subject_sender_id, input->StreamNum, subject_receiver_id);
    }
    else {
        subject = GenerateSubject(input->DeviceID, input->StreamNum);
    }


    message.Subject = (char*)subject.c_str();


    std::string from;


    std::string to;


    if (request_id && request_id[0] != '\0') {


        std::ostringstream from_builder;
        from_builder << "sip:" << m_xml_parser->m_local_code << "@"
                     << m_xml_parser->m_local_ip << ":" << m_xml_parser->m_port;
        from = from_builder.str();


        std::ostringstream to_builder;
        to_builder << "sip:" << request_id << "@"
                   << m_sip_connect_param->ip << ":" << m_sip_connect_param->port;
        to = to_builder.str();


        message.From = (char*)from.c_str();
        message.To = (char*)to.c_str();


    }





    int timeout = 30000;


    int code;


    SipData* result = NULL;


    SipDialogKey   *dialog  = NULL;


    std::string media_info;





    if(kSipSuccess != m_sip_client->Invite(&message, timeout,  &result)){


        return kGbInviteFail;


    }





    if( !result || result->messgae.code != kSuccessRequest) {


        code = kGbResponseCodeError;


        goto finally;


    }





    media_info = result->messgae.content;


    if( !CSdpUtil::String2MediaInfo(media_info.c_str(), output)) {


        code = kGbSdpParseFail;


        goto finally;


    }





    if(kSipSuccess !=m_sip_client->Ack(&(result->Dialog), &message, 0,  NULL)) {


        code = kGbAckFail;


        goto finally;


    }





    dialog = (SipDialogKey*)malloc( sizeof(SipDialogKey) );


    memcpy(dialog,   &(result->Dialog),  sizeof(*dialog));


    *stream_handle = (StreamHandle)dialog;


    m_stream_manager->InsertStreamHandle(   *stream_handle     );


    code = kGb28181Success;


finally:


        m_sip_client->FreeSipResult(result);


        return code;


}





 bool CGBClientImpl::StartConnect(long &hNetCommunication,char *ip,tint32 port)


 {


 	hNetCommunication = NET_SOCKET_AddConnect(0,ip,port);


 	tint32 ret=-1;


 	tint32 times=10;//???10???


 	while(times > 0)


 	{


 		NET_SOCKET_PopConnectResult(hNetCommunication,ret);//????????


 		if(ret >0)


 		{


 			//?????????????


 			break;


 		}


 		CPFHelper::Sleep(64);


 		times--;


 	}


 	if (ret > 0)


 	{


        NORMAL_OUTPUT("????????connect Upper server success ServerIp=%s?port=%d?m_hNetCommunication=%ld",ip,port,hNetCommunication);


 		NET_SOCKET_RegisterNode(hNetCommunication, this,(LPVOID)hNetCommunication /*this*/, 8, 8);


 		NET_SOCKET_Start(hNetCommunication);


 		return true;


 	}


 	else if(ret <= 0)


 	{


 		//??0??????????????????10??????????


        NORMAL_OUTPUT("????????connect Upper server failed ServerIp=%s?port=%d?m_hNetCommunication=%ld",ip,port,hNetCommunication);


 		return false;


 	}


	return false;


 }


 


 int CGBClientImpl::Send(tint32 hNetCommunication, const char *pBuf, tint32 nLen)


 {


 	return NET_SOCKET_Send_Immediate(hNetCommunication,pBuf,nLen);


 }





tuint32 CGBClientImpl::OnSocketRecvData(tint32 hNetCommunication, const char* pBuffer, tuint32 len, tuint32 *pNeedLen,LPVOID lpParameter)//??????


{


	return 0;


}





