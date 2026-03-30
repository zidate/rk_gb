#include "SipClientImpl.h"
#include "common/SipEventManager.h"
#include "common/ClientInfoManager.h"

CSipClientImpl::CSipClientImpl()
{
    m_sip_server_handler  = new SipServerHandlerImpl();
    m_event_manager = new CSipEventManager();
    m_client_info       =  new ClientInfo;
}

CSipClientImpl::~CSipClientImpl()
{
    if(m_sip_server_handler) {
         delete m_sip_server_handler;
        m_sip_server_handler = NULL;
    }

    if( m_event_manager ) {
        delete m_event_manager;
        m_event_manager = NULL;
    }

    if( m_client_info ) {
        delete m_client_info;
        m_client_info = NULL;
    }

}

int   CSipClientImpl::Start(SipTransportType type , const NetAddress* local, const char*local_name)
{
    m_client_info->LocalIp  = local->ip;
    m_client_info->LocalPort =  local->port;
    m_client_info->local_name = local_name;
    const int ret = m_event_manager->StartSipListen(type,local,local_name);
    if (ret == kSipSuccess && m_event_manager->GetLocalPort() > 0) {
        m_client_info->LocalPort = m_event_manager->GetLocalPort();
    }
    return ret;
}

void CSipClientImpl::Stop()
{
    m_event_manager->Stop();
}

void CSipClientImpl::SetHandler(const SipClientHandler* handler)
{
     m_sip_server_handler->client_handler = (SipClientHandler*)handler;
     m_event_manager->SetSipHandler(m_sip_server_handler);
}

void CSipClientImpl::FreeSipResult(SipData* data)
{
    FreeSipData(data);
}

void CSipClientImpl::FreeSipBuffer(char** buf)
{
	FreeSipBuf(buf);
}

// Sip register
int   CSipClientImpl::Register(const SipRegistParam* message, const SipConnectParam* info, int timeout , SipData** result )
{
    m_client_info->auth_flag = message->auth_flag;

    if (message->local_name != NULL && message->local_name[0] != '\0') {
        m_client_info->local_name = message->local_name;
    }
    m_client_info->Username = message->user_name;
    m_client_info->Password = message->password;
    m_client_info->Expire = message->expires;
    m_client_info->RemoteIp = info->ip;
    m_client_info->RemotePort= info->port;
    m_client_info->RemoteSipSrvName = info->sip_code;
    m_client_info->new_reg = message->new_reg;
    m_client_info->UseZeroConfigHeaders = message->use_zero_config_headers;
    m_client_info->DisplayName = (message->display_name != NULL) ? message->display_name : "";
    m_client_info->StringCode = (message->string_code != NULL) ? message->string_code : "";
    m_client_info->MacAddress = (message->mac_address != NULL) ? message->mac_address : "";
    m_client_info->LineId = (message->line_id != NULL) ? message->line_id : "";
    m_client_info->Manufacturer = (message->manufacturer != NULL) ? message->manufacturer : "";
    m_client_info->Model = (message->model != NULL) ? message->model : "";
    m_client_info->CustomProtocolVersion =
        (message->custom_protocol_version != NULL) ? message->custom_protocol_version : "";
    if (m_event_manager->GetLocalPort() > 0) {
        m_client_info->LocalPort = m_event_manager->GetLocalPort();
    }
    m_client_info->SetFromeAndTo();

    TVT_LOG_INFO("sip client register prepare"
                 << " new_reg=" << (m_client_info->new_reg ? 1 : 0)
                 << " auth=" << (m_client_info->auth_flag ? 1 : 0)
                 << " zero_cfg=" << (m_client_info->UseZeroConfigHeaders ? 1 : 0)
                 << " user=" << m_client_info->Username
                 << " local_name=" << m_client_info->local_name
                 << " local=" << m_client_info->LocalIp << ":" << m_client_info->LocalPort
                 << " remote=" << m_client_info->RemoteIp << ":" << m_client_info->RemotePort
                 << " remote_name=" << m_client_info->RemoteSipSrvName
                 << " expire=" << m_client_info->Expire
                 << " pwd_set=" << (!m_client_info->Password.empty() ? 1 : 0));

    return  m_event_manager->Register(m_client_info, timeout, result );
}

int   CSipClientImpl::UnRegister()
{
    m_client_info->Expire = 0;
    return  m_event_manager->Register(m_client_info, 0, NULL );
}

//  sip call
int   CSipClientImpl::Invite(const SipMessage* message, int timeout,  SipData** result )
{
       return  m_event_manager->SendSipResquest(
                                                                         kSipInviteMethod,
                                                                         timeout,
                                                                         m_client_info,
                                                                         NULL,
                                                                         message,
                                                                         result
                                                                       );
}

int   CSipClientImpl::Info(const  SipDialogKey* key ,const SipMessage* message,  int timeout,   SipData** result)
{
    return  m_event_manager->SendSipResquest(
                                                                      kSipInfoMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      key,
                                                                      message,
                                                                      result
                                                                    );
}

int   CSipClientImpl::Ack( const  SipDialogKey* key ,const SipMessage* message, int timeout,   SipData** result )
{
    return  m_event_manager->SendSipResquest(
                                                                      kSipAckMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      key,
                                                                      message,
                                                                      result
                                                                    );
}

int   CSipClientImpl::Bye( const  SipDialogKey* key ,const SipMessage* message, int timeout,   SipData** result )
{
    return  m_event_manager->SendSipResquest(
                                                                      kSipByeMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      key,
                                                                      message,
                                                                      result
                                                                    );
}


// sip subcribe
int   CSipClientImpl::Subcribe(const SipMessage* message, int timeout,  SipData** result )
{
    return  m_event_manager->SendSipResquest(
                                                                      kSipSubscribeMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      NULL,
                                                                      message,
                                                                      result
                                                                    );
}

int   CSipClientImpl::Notify(const  SipDialogKey* key ,const SipMessage* message, int timeout,  SipData** result )
{
    return  m_event_manager->SendSipResquest(
                                                                      kSipNotifyMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      key,
                                                                      message,
                                                                      result
                                                                    );
}


// sip message
int   CSipClientImpl::Message(const SipMessage* message, int timeout,  SipData** result)
{
    TVT_LOG_INFO("sip client message request"
                 << " timeout=" << timeout
                 << " has_result=" << (result != NULL ? 1 : 0)
                 << " content_type=" << (message ? (int)message->content_type : -1)
                 << " body_len=" << ((message && message->content) ? (int)strlen(message->content) : 0)
                 << " local_name=" << m_client_info->local_name
                 << " local=" << m_client_info->LocalIp << ":" << m_client_info->LocalPort
                 << " remote_name=" << m_client_info->RemoteSipSrvName
                 << " remote=" << m_client_info->RemoteIp << ":" << m_client_info->RemotePort);

    const int ret = m_event_manager->SendSipResquest(
                                                                      kSipMessageMethod,
                                                                      timeout,
                                                                      m_client_info,
                                                                      NULL,
                                                                      message,
                                                                      result
                                                                    );
    TVT_LOG_INFO("sip client message result"
                 << " ret=" << ret
                 << " remote_name=" << m_client_info->RemoteSipSrvName
                 << " remote=" << m_client_info->RemoteIp << ":" << m_client_info->RemotePort);
    return ret;
}

int CSipClientImpl::MessageToStr(const SipMessage* message, char** buf, size_t* buf_len)
{
	return m_event_manager->MessageToStr(kSipMessageMethod,m_client_info,message,buf,buf_len);
}

int   CSipClientImpl::Response( const  SipDialogKey* key, const SipMessage* message)
{
    return  m_event_manager->SendSipResponse(m_client_info, key, message);
}

int   CSipClientImpl::RequestWithCall(const  SipDialogKey* key, const SipMessage* message, int timeout, SipData** result)
{
	return  m_event_manager->SendSipResquest(
		kSipMessageWithCallMethod,
		timeout,
		m_client_info,
		key,
		message,
		result
	);
}
