#include "SipEventManager.h"
#include "eXosip2/eXosip.h"
#include "eXosip2/eX_call.h"
#include "eXosip2/eX_setup.h"
#include "SipUtil.h"
#include "WaiterManager.h"
#include "ShareSDK.h"
#include "common/ClientInfoManager.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>


#define WAITTIME		50				     //SIP事件监听时间间隔
#define LIVETIME		(90 * 1000)		//保活时间
#define MAX_LENGTH_TR    1024
static const int kRegisterAutomaticActionSettleUs = 200 * 1000;

static void custom_osip_trace(const char *fi, int li, osip_trace_level_t level, const char *chfr, va_list ap)
{
	if (!fi || !chfr) {
		return;
	}

	char buffer[MAX_LENGTH_TR];
	int in = 0;
	memset(buffer, 0, sizeof(buffer));
	if (level == OSIP_FATAL)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_FATAL | <%s: %i> ", fi, li);
	else if (level == OSIP_BUG)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_BUG | <%s: %i> ", fi, li);
	else if (level == OSIP_ERROR)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_ERROR | <%s: %i> ", fi, li);
	else if (level == OSIP_WARNING)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_WARNING| <%s: %i> ", fi, li);
	else if (level == OSIP_INFO1)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_INFO1 | <%s: %i> ", fi, li);
	else if (level == OSIP_INFO2)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_INFO2 | <%s: %i> ", fi, li);
	else if (level == OSIP_INFO3)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_INFO3 | <%s: %i> ", fi, li);
	else if (level == OSIP_INFO4)
		in = snprintf(buffer, MAX_LENGTH_TR - 1, "| OSIP_INFO4 | <%s: %i> ", fi, li);

	vsnprintf(buffer + in, MAX_LENGTH_TR - 1 - in, chfr, ap);

	TVT_LOG_INFO(buffer);
}

static bool NeedResolveLocalSipIp(const char* ip)
{
    return ip == NULL || ip[0] == '\0' || strcmp(ip, "0.0.0.0") == 0;
}

static bool ResolveOutboundLocalSipIp(const char* remote_ip, int remote_port, std::string& local_ip)
{
    if (NeedResolveLocalSipIp(remote_ip) || remote_port <= 0 || remote_port > 65535) {
        return false;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return false;
    }

    sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons((uint16_t)remote_port);
    if (inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) != 1) {
        close(sockfd);
        return false;
    }

    if (connect(sockfd, (sockaddr*)&remote_addr, sizeof(remote_addr)) != 0) {
        close(sockfd);
        return false;
    }

    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(sockfd, (sockaddr*)&local_addr, &local_addr_len) != 0) {
        close(sockfd);
        return false;
    }

    char ipbuf[INET_ADDRSTRLEN] = {0};
    const char* ipstr = inet_ntop(AF_INET, &local_addr.sin_addr, ipbuf, sizeof(ipbuf));
    close(sockfd);
    if (ipstr == NULL || NeedResolveLocalSipIp(ipstr)) {
        return false;
    }

    local_ip = ipstr;
    return true;
}

void FreeSipData(SipData* data)
{
    if(!data) {
        return;
    }

    if(data->messgae.content){
        free(data->messgae.content);
    }

    if(data->messgae.Date){
        free(data->messgae.Date);
    }

    if(data->messgae.Event){
        free(data->messgae.Event);
    }

    if(data->messgae.From){
        free(data->messgae.From);
    }

    if(data->messgae.reason_phrase){
        free(data->messgae.reason_phrase);
    }

    if(data->messgae.Subject){
        free(data->messgae.Subject);
    }

    if(data->messgae.To){
        free(data->messgae.To);
    }

    free(data);
    data = NULL;
}

void FreeSipBuf(char** buf)
{
	if (buf)
	{
		free(buf);
		buf = NULL;
	}
}

struct ResponseCallbackInfo
{
	CSipEventManager* manager;
	SipSessionHandle handle;
	std::string token;
	SipData* sip_data;
};

struct RequestCallbackInfo
{
	CSipEventManager* manager;
	SipSessionHandle handle;
	SipData* sip_data;
};

void ResponseProcess(LPVOID msg)
{
	ResponseCallbackInfo* info = (ResponseCallbackInfo*)(msg);
	if(info){
		info->manager->HandleResponseCallback(info->token, info->handle, info->sip_data);
		delete info;
		info = NULL;
	}
}


void RequestProcess(LPVOID msg)
{
	RequestCallbackInfo* info = (RequestCallbackInfo*)(msg);
	if(info){
		info->manager->HandleResquestCallback(info->handle, info->sip_data);
		delete info;
		info = NULL;
	}
}

std::string GetContentType(osip_message* msg)
{
      std::string res = "";
      char *ContentType = NULL;
      if( 0 == osip_content_type_to_str(msg->content_type, &ContentType) ) {
               res = ContentType;
               osip_free(ContentType);
      }
      return res;
}


CSipEventManager::CSipEventManager()
{
       m_stop_flag.SetValue(1);
       m_listen_flag.SetValue(0);
       m_register_guard_count.SetValue(0);
       m_sip_context  = NULL;
       m_listen_thread_id = NULL;
       m_sip_log_handler = NULL;
       CSipUtil::Init();
	   m_callback_threadpool.Create();
}

CSipEventManager::~CSipEventManager()
{
       m_stop_flag.SetValue(0);
	   Stop();
	   m_callback_threadpool.Destroy();
}

void CSipEventManager::Stop()
{
	m_stop_flag.SetValue(0);
    m_register_guard_count.SetValue(0);
    if (m_listen_flag && m_sip_context ) {
        eXosip_quit(  m_sip_context  );
		m_listen_flag.SetValue(0);
    }

    if(m_listen_thread_id) {
        DESTROY_THREAD(m_listen_thread_id);
        m_listen_thread_id = NULL;
    }

	if(  m_waiter_manager   ){
		delete m_waiter_manager;
		m_waiter_manager = NULL;
	}

	if(  m_client_manager   ){
		delete m_client_manager;
		m_client_manager = NULL;
	}

}

int CSipEventManager::StartSipListen(  SipTransportType type , const NetAddress* local, const char* local_name )
{
	
    if( m_listen_flag  ==  1){
        return kSipSuccess;
    }

    m_sip_context = eXosip_malloc();
	if(!m_sip_context){
          return kSipInitFail;
    }

    TRACE_INITIALIZE(OSIP_INFO1, stdout);

    int result = eXosip_init(m_sip_context);
    osip_trace_initialize(OSIP_FATAL,NULL);
    if ( result != 0){
        return kSipInitFail;
    }
    int proco =  ( type == kSipOverTCP ) ? IPPROTO_TCP : IPPROTO_UDP ;
    eXosip_set_user_agent(m_sip_context,"DGIOT");
     result = eXosip_listen_addr(m_sip_context, proco, NULL , local->port , AF_INET, 0);
    if ( result != 0) {
        eXosip_quit(m_sip_context);
        return kSipListenFail;
    }

	m_stop_flag.SetValue(1);
	m_waiter_manager = new CWaiterManager;
	m_client_manager = new CClientInfoManager;

    m_listen_thread_id = CREATE_THREAD(StartSipEventProcess, this, 0, "##SipListen");
    if (! m_listen_thread_id){
		if (m_sip_context ) {
			eXosip_quit(  m_sip_context  );
			m_stop_flag.SetValue(0);
			m_listen_flag.SetValue(0);
		}
        return kSipListenThreadFail;
    }
   
	osip_trace_initialize_func(TRACE_LEVEL3, custom_osip_trace);
        if (NeedResolveLocalSipIp(local->ip)) {
            char guessed_ip[64] = {0};
            if (eXosip_guess_localip(m_sip_context, AF_INET, guessed_ip, sizeof(guessed_ip)) == 0 &&
                guessed_ip[0] != '\0') {
                m_local_ip = guessed_ip;
            }
            else {
                m_local_ip = "0.0.0.0";
            }
        }
        else {
	    m_local_ip = local->ip;
        }
	    m_local_port = local->port;
	    m_local_name = local_name;
	    m_listen_flag.SetValue(1);
    return kSipSuccess;

}


int CSipEventManager::SetSipHandler(const SipServerHandler* handler)
{

    if(handler == NULL){
        return kSipBadParam;
    }
     m_sip_handler = (SipServerHandler*)handler;
    return kSipSuccess;

}


void CSipEventManager::StartSipEventProcess(void* user)
{
    CSipEventManager* manager = (CSipEventManager*)user;
    if(manager){
          manager->SipEventProcess();
    }
}

int  CSipEventManager::ResponseForSubAndNotify(int tid, const SipMessage* message)
{
    osip_message_t *answer = NULL;
    eXosip_lock (m_sip_context);
    int res = eXosip_insubscription_build_answer(
                                                     m_sip_context,
                                                     tid,
                                                     message->code,
                                                     &answer
                                                     );
    if( res != 0 ){

		eXosip_unlock(m_sip_context);
        return kSipBuildAnswerFailed;
    }

    SetOSipMessageContent(message, answer);
    res = eXosip_insubscription_send_answer(
                                                  m_sip_context,
                                                  tid,
                                                  message->code,
                                                  answer
                                                 );
    eXosip_unlock(m_sip_context);

    if( res != 0 ) {
        return kSipMessageSendFailed;
    }
        return kSipSuccess;
}
int  CSipEventManager::ResponseForCallAnswer(int tid, const SipMessage* message)
{
    osip_message_t *answer = NULL;
    eXosip_lock (m_sip_context);
    int res = eXosip_call_build_answer(
                                                     m_sip_context,
                                                     tid,
                                                     message->code,
                                                     &answer
                                                     );
    if( res != 0 ){
		eXosip_unlock(m_sip_context);
        return kSipBuildAnswerFailed;
    }

    SetOSipMessageContent(message, answer);
    res = eXosip_call_send_answer(
                                                  m_sip_context,
                                                  tid,
                                                  message->code,
                                                  answer
                                                 );
    eXosip_unlock(m_sip_context);

    if( res != 0 ) {
        return kSipMessageSendFailed;
    }
        return kSipSuccess;
}

int   CSipEventManager::ResponseForMessage(int tid, const SipMessage* message)
{
    osip_message_t *answer = NULL;
    eXosip_lock (m_sip_context);
    int res = eXosip_message_build_answer(
                                                        m_sip_context,
                                                        tid,
                                                        message->code,
                                                        &answer
                                                         );
    if( res != 0 ){

		eXosip_unlock(m_sip_context);
        return kSipBuildAnswerFailed;
    }

    SetOSipMessageContent(message, answer);
    res = eXosip_message_send_answer(
                                                  m_sip_context,
                                                  tid,
                                                  message->code,
                                                  answer
                                                 );
    eXosip_unlock(m_sip_context);

    if( res != 0 ) {
        return kSipMessageSendFailed;
    }
        return kSipSuccess;
}

int CSipEventManager::SendReponse(int tid, const SipMessage* sendData )
{

    if( sendData->Method == kSipInviteMethod || sendData->Method == kSipInfoMethod
        || sendData->Method == kSipAckMethod || sendData->Method== kSipByeMethod )
    {
           return ResponseForCallAnswer(tid,sendData);
    }
    else if( sendData->Method == kSipNotifyMethod || sendData->Method == kSipSubscribeMethod ){

           return ResponseForSubAndNotify(tid,sendData);
    }

           return ResponseForMessage(tid,sendData);

}


int CSipEventManager::ResponseWithUnauthorized(eXosip_event_t* pEvent)
{

    return CSipUtil::Unauthorized( pEvent ,
                                               m_sip_context,
                                               3600,
                                               "Digest realm=\"34020000\",nonce=\"b1f444e6805f2768\""
                                              );

}

// for client
void CSipEventManager::HandleRegistResponse(eXosip_event_t* pEvent)
{
    osip_message_t *pResponse = pEvent->response;
    if( pResponse == NULL ) {
              return;
    }
    std::string content = GetContentType(pResponse);
    SipContentType  ContentType = CSipUtil::ContentStringToEnum( content );

    SipData* data =  GetSipData(false, pEvent);
    if(!data) {
        return ;
    }
    SipMethod method = kSipRegisterMethod;
    data->messgae.content_type =  ContentType;
    if(m_waiter_manager->WakeUpWaitResponse(pResponse, *data)) {
        return;
    }

    if(m_sip_handler) {
         m_sip_handler->OnResponse(NULL,method,data );
    }
         FreeSipData(data);
}


void CSipEventManager::HandleRegistResquest(eXosip_event_t* pEvent)
{
	/*
	char* buffer = NULL;
	size_t len;
	osip_message_to_str(pEvent->request, &buffer, &len);
	if (buffer) {
		TVT_LOG_ERROR("sip message not found : " << buffer);
		osip_free(buffer);
	}
	*/

    osip_authorization_t* pAuth = NULL;
    osip_message_get_authorization(pEvent->request, 0, &pAuth);
      if( pAuth == NULL) {
            ResponseWithUnauthorized(pEvent);
            return;
      }

      std::string username;
      if(pAuth->username) {
          username = pAuth->username;
      }

      osip_header_t* expirse_header = NULL;
      int expirse = -1;
      if(osip_message_get_expires(pEvent->request,0,&expirse_header) >= 0) {
              expirse = atoi(expirse_header->hvalue);
      }

      SipMessage message;
      memset(&message,0,sizeof(message));
      message.Method = kSipMessageMethod;

      SipConnectParam connect_param;
      memset(&connect_param,0,sizeof(connect_param));

      SipRegistParam regist_param;
      memset(&regist_param,0,sizeof(regist_param));

      PeerInfo* peer_info = m_client_manager->GetPeerInfo(pEvent->request, true);
      ClientInfo* client_info = m_client_manager->FindClient(peer_info);

      if( expirse <= 0  && client_info == NULL ) {
          message.code = kSuccessRequest;
          goto finally;
      }

      if(! client_info) {
           client_info  = new ClientInfo;
      }

      client_info->RemotePort = peer_info->port;
      client_info->RemoteIp = peer_info->ip;
      client_info->RemoteSipSrvName  = peer_info->name;
      client_info->Username = username;
      client_info->Password = username;
      client_info->local_name =m_local_name;
      client_info->LocalIp =  m_local_ip;
      client_info->LocalPort = m_local_port;
      client_info->Expire = expirse;
      client_info->SetFromeAndTo();

      connect_param.ip = (char*)client_info->RemoteIp.c_str();
      connect_param.port = client_info->RemotePort;
      connect_param.sip_code = (char*)client_info->RemoteSipSrvName.c_str();


      regist_param.expires = expirse;
      regist_param.user_name = (char*)client_info->Username.c_str();
      regist_param.password = (char*)client_info->Password.c_str();

      //unregister
      if( expirse == 0  ) {
          m_sip_handler->OnClientRegist((SipSessionHandle)client_info, &regist_param, &connect_param  );
          message.code = kSuccessRequest;
          client_info->Reg_flag = false;
          goto finally;
      }

      //register
      m_client_manager->InsertClientSession(client_info);
      if( m_sip_handler && m_sip_handler->OnClientRegist((SipSessionHandle)client_info, &regist_param, &connect_param  )  != kSuccessRequest) {
          m_client_manager->DeletClientSession(client_info);
          delete client_info;
          message.code = kForbidden;
          goto finally;
      }

         client_info->Reg_flag = true;
         message.code = kSuccessRequest;

finally:
         if(peer_info) {
             delete peer_info;
             peer_info = NULL;
         }
         SendReponse(pEvent->tid, &message);
         return;

}

ClientInfo* CSipEventManager::GetClientInfo(eXosip_event_t* event)
{
       ClientInfo*  client  = NULL;
        PeerInfo*  peer =  m_client_manager->GetPeerInfo( event->request ,true);
        if(!peer) {
            goto reponse;
        }
       client =  m_client_manager->FindClient(peer);
       delete peer;
       if(!client) {
            goto reponse;
       }
       return client;
reponse:
       peer =  m_client_manager->GetPeerInfo( event->response ,false);
       if(!peer) {
           return  NULL;
       }
       client =  m_client_manager->FindClient(peer);
       delete peer;
       if(!client) {
           return  NULL;
        }
          return client;
}


// reponse
void CSipEventManager::HandleResponse(eXosip_event_t* event)
{
        ClientInfo*  client =  GetClientInfo(event);
        if(!client) {

			char* buffer = NULL;
			size_t len;
			osip_message_to_str(event->response, &buffer, &len);
			if (buffer) {
				TVT_LOG_ERROR("sip message not found : " << buffer);
				osip_free(buffer);
			}
			
              return ;
        }

        SipData* data =  GetSipData(false, event);
        if(!data) {
            return;
        }

        std::string content = GetContentType(event->request);
        SipContentType  ContentType = CSipUtil::ContentStringToEnum( content );
        data->messgae.content_type =  ContentType;
        SipMethod method = CSipUtil::MethodStringToEnum( event->response->cseq->method);
		data->messgae.Method = method;

		ResponseCallbackInfo* info = new ResponseCallbackInfo;
		info->manager = this;
		info->handle = (SipSessionHandle)client;
		info->sip_data = data;
		info->token = m_waiter_manager->MakeIdentifier(event->response);

		m_callback_threadpool.Run(ResponseProcess,info);

}

// reponse
void CSipEventManager::HandleResponseCallback(  std::string& token, SipSessionHandle handle, SipData* data)
{
	if( m_waiter_manager->WakeUpWaitResponse(token, *data) ) {
		return;
	}

	if(  m_sip_handler ) {
		m_sip_handler->OnResponse(handle, data->messgae.Method , data );
	}
	FreeSipData(data);
}

void CSipEventManager::HandleResquestCallback(SipSessionHandle handle,  SipData* data)
{
	if(m_sip_handler) {
		m_sip_handler->OnRequest(handle, data->messgae.Method,  data );
	}
	FreeSipData(data);
}

 void CSipEventManager::HandleResquest(eXosip_event_t* event,  SipMethod  method)
 {

        PeerInfo*  peer =  m_client_manager->GetPeerInfo( event->request , true);
         if(!peer) {
             TVT_LOG_ERROR("sip request peer parse failed"
                           << " method=" << event->request->sip_method
                           << " tid=" << event->tid);
             return ;
         }
         ClientInfo*  client =  m_client_manager->FindClient(peer);
         const std::string peer_name = peer->name;
         const std::string peer_ip = peer->ip;
         const int peer_port = peer->port;
         delete peer;
         if(!client) {
             TVT_LOG_ERROR("sip request dropped because client was not matched"
                           << " method=" << event->request->sip_method
                           << " peer_name=" << peer_name
                           << " peer=" << peer_ip << ":" << peer_port
                           << " tid=" << event->tid);
             return ;
         }

         SipData* data =  GetSipData(true, event);
         if(!data){
             return;
         }

         std::string content = GetContentType(event->request);
         SipContentType  ContentType = CSipUtil::ContentStringToEnum( content );
         data->messgae.content_type =  ContentType;
		 data->messgae.Method = method;

		 RequestCallbackInfo* info = new RequestCallbackInfo;
		 info->manager = this;
		 info->handle = (SipSessionHandle)client;
		 info->sip_data = data;
		
		 m_callback_threadpool.Run(RequestProcess,info);

 }


void CSipEventManager::SipEventProcess()
{
    
    eXosip_event_t* pEvent = NULL;		

	TVT_LOG_INFO("SipEventProcess start");

    while(  m_stop_flag  )  {
        
        pEvent = eXosip_event_wait( m_sip_context, 0, 50 );

        if (pEvent == NULL) {
			continue;
		}

        switch(pEvent->type)
        {

             case EXOSIP_MESSAGE_NEW:
              {
                        // UAC  register request
                     if (strcmp(pEvent->request->sip_method, "REGISTER") == 0)  {
                             HandleRegistResquest(pEvent);
                     }
                     else{
                         SipMethod method = CSipUtil::MethodStringToEnum(  pEvent->request->sip_method   );
                         HandleResquest( pEvent,   method );
                     }

              }
                break;

             case EXOSIP_CALL_INVITE:
             case EXOSIP_CALL_REINVITE:
                    {
                        HandleResquest( pEvent, kSipInviteMethod);
                    }
                    break;
             case EXOSIP_CALL_MESSAGE_NEW:
                    {
                        SipMethod method = CSipUtil::MethodStringToEnum(  pEvent->request->sip_method   );
                        HandleResquest( pEvent,   method );
                    }
                    break;
             case EXOSIP_IN_SUBSCRIPTION_NEW:
                    {
                         HandleResquest( pEvent,   kSipSubscribeMethod );
                    }
                    break;
             case EXOSIP_SUBSCRIPTION_NOTIFY:
                    {
                           HandleResquest(pEvent, kSipNotifyMethod);
                    }
                    break;

             case EXOSIP_CALL_ACK:
                    {
                           HandleResquest(pEvent, kSipAckMethod);
                    }
                    break;

                case EXOSIP_CALL_REDIRECTED:
                case EXOSIP_CALL_RELEASED:
                case EXOSIP_CALL_CLOSED:
                case EXOSIP_CALL_CANCELLED:
                    {



                    }
                    break;
                case EXOSIP_CALL_PROCEEDING:
                    {

                    }
                    break;
                case EXOSIP_CALL_RINGING:
                    {

                    }
                    break;

                case EXOSIP_CALL_NOANSWER:
                case EXOSIP_CALL_REQUESTFAILURE:
                case EXOSIP_CALL_SERVERFAILURE:
                case EXOSIP_CALL_GLOBALFAILURE:
                    {
                       HandleResponse(pEvent);
                    }
                    break;
                case EXOSIP_CALL_ANSWERED:
                    {
                       HandleResponse(pEvent);
                    }
                    break;
                case EXOSIP_CALL_MESSAGE_REQUESTFAILURE:
                case EXOSIP_CALL_MESSAGE_ANSWERED:
                    {
                       HandleResponse(pEvent);
                    }
                    break;

                case EXOSIP_SUBSCRIPTION_PROCEEDING:
                    {

                    }
                    break;

                case EXOSIP_SUBSCRIPTION_ANSWERED:
                    {
                      HandleResponse(pEvent);
                    }
                    break;

                case EXOSIP_REGISTRATION_FAILURE:
                    {
                       HandleRegistResponse(pEvent);
                    }
                    break;

                case EXOSIP_REGISTRATION_SUCCESS:
                    {
                        HandleRegistResponse(pEvent);
                    }
                    break;

                case EXOSIP_MESSAGE_ANSWERED:
                    {
                       HandleResponse(pEvent);
                    }
                    break;

                case EXOSIP_MESSAGE_REDIRECTED:
                case EXOSIP_MESSAGE_REQUESTFAILURE:
                case EXOSIP_MESSAGE_SERVERFAILURE:
                case EXOSIP_MESSAGE_GLOBALFAILURE:
                    {
                       // HandleResponse(pEvent);
                    }
                    break;
                default :
                    {

                    }

     }
	   eXosip_event_free(pEvent);
    }

	TVT_LOG_INFO("SipEventProcess end");
}

int CSipEventManager::Register(ClientInfo* client_info , int timeout , SipData** result )
{
       osip_message_t *pMsg = NULL;
       const bool guardAutomaticAction = (result != NULL);
       if (guardAutomaticAction) {
           m_register_guard_count.Increment();
       }

       if (NeedResolveLocalSipIp(client_info->LocalIp.c_str())) {
           std::string resolved_local_ip;
           if (ResolveOutboundLocalSipIp(client_info->RemoteIp.c_str(), client_info->RemotePort, resolved_local_ip)) {
               client_info->LocalIp = resolved_local_ip;
               client_info->SetFromeAndTo();
               if (NeedResolveLocalSipIp(m_local_ip.c_str())) {
                   m_local_ip = resolved_local_ip;
               }
               TVT_LOG_INFO("sip register resolved local ip by remote"
                            << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort
                            << " local=" << client_info->LocalIp);
           }
           else {
               TVT_LOG_ERROR("sip register resolve local ip failed"
                             << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort
                             << " local=" << client_info->LocalIp);
           }
       }

       TVT_LOG_INFO("sip register enter"
                    << " new_reg=" << (client_info->new_reg ? 1 : 0)
                    << " auth=" << (client_info->auth_flag ? 1 : 0)
                    << " regid=" << client_info->RegId
                    << " user=" << client_info->Username
                    << " from=" << client_info->from
                    << " route=" << client_info->route
                    << " contact=" << client_info->contact
                    << " expire=" << client_info->Expire);

       if( client_info->auth_flag) {
           if (client_info->Username.empty() || client_info->Password.empty()) {
               TVT_LOG_ERROR("sip register auth info invalid"
                             << " user_empty=" << (client_info->Username.empty() ? 1 : 0)
                             << " pwd_empty=" << (client_info->Password.empty() ? 1 : 0));
               if (guardAutomaticAction) {
                   m_register_guard_count.Decrement();
               }
               return kSipBuildRegisterFailed;
           }

           eXosip_lock(m_sip_context);
           const int authRet = eXosip_add_authentication_info( m_sip_context,
                                                             client_info->Username.c_str(),
                                                             client_info->Username.c_str(),
                                                             client_info->Password.c_str(),
                                                             NULL,
                                                             NULL  );
          eXosip_unlock(m_sip_context);
          TVT_LOG_INFO("sip register add auth"
                       << " ret=" << authRet
                       << " user=" << client_info->Username);
          if (authRet != OSIP_SUCCESS) {
              TVT_LOG_ERROR("sip register add auth failed"
                            << " ret=" << authRet
                            << " user=" << client_info->Username);
              if (guardAutomaticAction) {
                  m_register_guard_count.Decrement();
              }
              return kSipBuildRegisterFailed;
          }
       }

       if(  client_info->new_reg  ) {

           eXosip_lock(m_sip_context);

           if (client_info->RegId > 0 ) {
                   eXosip_register_remove(m_sip_context,client_info->RegId);
            }

            int regid = eXosip_register_build_initial_register ( m_sip_context, client_info->from.c_str(),
                  client_info->route.c_str(), client_info->contact.c_str(),client_info->Expire, &pMsg);

            eXosip_unlock(m_sip_context);

            if( regid < 1 ) {
                TVT_LOG_ERROR("sip build initial register failed"
                              << " regid=" << regid
                              << " from=" << client_info->from
                              << " route=" << client_info->route);
                if (guardAutomaticAction) {
                    m_register_guard_count.Decrement();
                }
                return kSipBuildRegisterFailed;
            }
                client_info->RegId = regid;
                TVT_LOG_INFO("sip build initial register ok"
                             << " regid=" << client_info->RegId);
       }
       else{

           int res = OSIP_WRONG_STATE;
           int attempt = 0;
           for (; attempt < 10; ++attempt) {
               eXosip_lock(m_sip_context);
               res = eXosip_register_build_register(m_sip_context, client_info->RegId, client_info->Expire, &pMsg);
               eXosip_unlock(m_sip_context);
               if (res != OSIP_WRONG_STATE) {
                   break;
               }
               TVT_LOG_INFO("sip build register waiting previous transaction"
                            << " regid=" << client_info->RegId
                            << " attempt=" << (attempt + 1));
               usleep(50 * 1000);
           }
           if( res !=  OSIP_SUCCESS ) {
               TVT_LOG_ERROR("sip build register failed"
                             << " res=" << res
                             << " regid=" << client_info->RegId
                             << " expire=" << client_info->Expire
                             << " attempts=" << attempt);
               if (guardAutomaticAction) {
                   m_register_guard_count.Decrement();
               }
               return kSipBuildRegisterFailed;
           }
           TVT_LOG_INFO("sip build register ok"
                        << " regid=" << client_info->RegId
                        << " expire=" << client_info->Expire
                        << " attempts=" << attempt);
       }

        CResponseWaiter *pCond = NULL;
        if( result ) {

            pCond = m_waiter_manager->CreateResponseWaiter(pMsg, kSipRegisterMethod );
            if( pCond==NULL ) {
                if (guardAutomaticAction) {
                    m_register_guard_count.Decrement();
                }
                return kSipBuildRegisterFailed;
            }

        }

        eXosip_lock(m_sip_context);
        int res = eXosip_register_send_register(m_sip_context , client_info->RegId, pMsg );
        eXosip_unlock(m_sip_context);
        TVT_LOG_INFO("sip send register"
                     << " regid=" << client_info->RegId
                     << " res=" << res
                     << " wait_response=" << (pCond != NULL ? 1 : 0));

        m_client_manager->InsertClientSession(   client_info  );

        if ( res ) {
            m_waiter_manager->CancleWaitResponse(pCond);
            if (guardAutomaticAction) {
                m_register_guard_count.Decrement();
            }
            TVT_LOG_ERROR("sip send register failed"
                          << " regid=" << client_info->RegId
                          << " res=" << res);
            return kSipMessageSendFailed;
        }

        int code = kSipSuccess;
        SipData* output = NULL;
        if( pCond ) {
             output = (SipData*)malloc(sizeof(SipData));
			 memset(output,0,sizeof(SipData));
             code = m_waiter_manager->WaitResponse(pCond, timeout, output );
             TVT_LOG_INFO("sip wait register response"
                          << " code=" << code
                          << " sip_code=" << (output ? output->messgae.code : -1)
                          << " regid=" << client_info->RegId);
         }

         if (guardAutomaticAction) {
             usleep(kRegisterAutomaticActionSettleUs);
             m_register_guard_count.Decrement();
         }

         if(code == kSipSuccess && result != NULL){
             *result = output;
         }else{
              if(output)
                  free(output);
         }
        if (code != kSipSuccess) {
            TVT_LOG_ERROR("sip register wait failed"
                          << " code=" << code
                          << " regid=" << client_info->RegId);
        }
        return code;
}



int CSipEventManager::SendSipResquest( SipMethod method,
                                                           int timeout,
                                                           ClientInfo* client_info,
                                                           const SipDialogKey* key,
                                                           const SipMessage* message,
                                                           SipData** result  )
{

    if(  !m_client_manager->FindClientSession(client_info)  ) {
          return kSipSessionNoExist;
    }

     int code;
     osip_message_t *pRequest = NULL;
     eXosip_lock (m_sip_context);
     switch( method )
     {
     case kSipCancleMethod :
     case kSipByeMethod:
         {
                    code = eXosip_call_terminate(  m_sip_context, key->CallId, key->DialogId);
                    eXosip_unlock (m_sip_context);
                    if( code ) {
                         return kSipBuildByeFailed;
                    }
                        return kSipSuccess;
         } break;
     case kSipInviteMethod :
        {
             code = BuildInvite(client_info, message, &pRequest);
        } break;
     case kSipAckMethod :
        {
          code = BuildAck(key, message, &pRequest);
        } break;
     case kSipSubscribeMethod :
        {
          code = BuildSubscribe(client_info, message, &pRequest);
        } break;
     case kSipInfoMethod :
         {
             code = BuildInfo(key, message, &pRequest);
         } break;
     case kSipNotifyMethod :
         {
             code = BuildNotify(client_info,key, message, &pRequest);
         } break;
	 case kSipMessageWithCallMethod:
	     {
		     code = BuildRequestWithCall(key, message, &pRequest);
	     } break;
	 case kSipRefreshSubscribeMethod:
		 {
			 code = BuildRefreshSubscribe(client_info, message, &pRequest,key);
		 }break;
	 case kSipCancelSubscribeMethod:
		 { 
			 code = eXosip_subscription_remove(m_sip_context,key->DialogId); 
			 eXosip_unlock (m_sip_context);
			 return kSipSuccess;
		 }
		 break;
      default :
         {
			 code = BuildNormal(client_info, method, message, &pRequest);
         } break;
      }

      if(code!= kSipSuccess) {
             if (method == kSipMessageMethod) {
                 TVT_LOG_ERROR("sip request build failed"
                               << " method=" << (int)method
                               << " code=" << code
                               << " remote_name=" << client_info->RemoteSipSrvName
                               << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort);
             }
             eXosip_unlock (m_sip_context);
             return code;
      }

      if (method == kSipMessageMethod) {
          TVT_LOG_INFO("sip request built"
                       << " method=" << (int)method
                       << " timeout=" << timeout
                       << " has_result=" << (result != NULL ? 1 : 0)
                       << " remote_name=" << client_info->RemoteSipSrvName
                       << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort
                       << " body_len=" << ((message && message->content) ? (int)strlen(message->content) : 0));
      }

	  return  SendResquest(method, timeout, pRequest, client_info, key, message, result);
}

int CSipEventManager::MessageToStr(SipMethod method,ClientInfo* client_info,const SipMessage* message, char** buf, size_t* buf_len)
{
	if(  !m_client_manager->FindClientSession(client_info)  ) {
		return kSipSessionNoExist;
	}
	osip_message_t *pRequest = NULL;
	BuildNormal(client_info, method, message, &pRequest);
//	char* temp;
	osip_message_to_str(pRequest,buf, buf_len);
	osip_free(pRequest);
	return kSipSuccess;
}

int  CSipEventManager::SendResquest( SipMethod method,
                                                       int timeout,
                                                       osip_message_t *pRequest,
                                                       ClientInfo* client_info,
                                                       const SipDialogKey* key,
                                                       const SipMessage* message,
                                                       SipData** result  )
{

      CResponseWaiter *pCond = NULL;
      if(  timeout > 0 &&  result ) {
            pCond = m_waiter_manager->CreateResponseWaiter(pRequest,  method);
            if( pCond== NULL ) {
                eXosip_unlock (m_sip_context);
                return kOperationFail;
            }
      }

	
     int  code;
     switch( method )
     {
        case kSipInviteMethod :
            {
                 int  c_id;
                 code  = 0 ;
                 c_id =  eXosip_call_send_initial_invite(m_sip_context,pRequest);
                 if(c_id <0){
                    code = -1;
                 }
            }
              break;
        case kSipAckMethod :
            {
                code = eXosip_call_send_ack( m_sip_context,key->DialogId, pRequest);
            }
              break;
        case kSipMessageMethod :
            {
                code = eXosip_message_send_request(m_sip_context,pRequest);
            }
               break;
        case kSipNotifyMethod:
            {
                code = eXosip_insubscription_send_request(m_sip_context,key->DialogId, pRequest);
            }
               break;
        case kSipSubscribeMethod :
            {
				int  c_id;
                c_id = eXosip_subscription_send_initial_request(m_sip_context, pRequest);
				code  = 0 ;
				if(c_id <0){
					code = -1;
				}
            }
              break;
		case kSipRefreshSubscribeMethod :
			{
				int  c_id;
				c_id = eXosip_subscription_send_refresh_request(m_sip_context,key->DialogId, pRequest);
				code  = 0 ;
				if(c_id <0){
					code = -1;
				}
			}
			break;
        case kSipInfoMethod :
		case kSipMessageWithCallMethod :
        default :
            {
                code = eXosip_call_send_request( m_sip_context,key->DialogId, pRequest);
            }
              break;
     }

     eXosip_unlock (m_sip_context);

     if (method == kSipMessageMethod) {
         TVT_LOG_INFO("sip request sent"
                      << " method=" << (int)method
                      << " code=" << code
                      << " remote_name=" << client_info->RemoteSipSrvName
                      << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort
                      << " wait_response=" << (pCond != NULL ? 1 : 0));
     }


     if( code != 0 ) {
            if( pCond ) {
                m_waiter_manager->CancleWaitResponse(pCond);
            }
            if (method == kSipMessageMethod) {
                TVT_LOG_ERROR("sip request send failed"
                              << " method=" << (int)method
                              << " code=" << code
                              << " remote_name=" << client_info->RemoteSipSrvName
                              << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort);
            }
            return kSipMessageSendFailed;
     }

    SipData* output = NULL;
    if( pCond ) {
         output = (SipData*)malloc(sizeof(SipData));
         code = m_waiter_manager->WaitResponse(pCond, timeout, output );
         if (method == kSipMessageMethod) {
             TVT_LOG_INFO("sip request wait response"
                          << " method=" << (int)method
                          << " wait_code=" << code
                          << " sip_code=" << ((code == kSipSuccess && output) ? output->messgae.code : -1)
                          << " remote_name=" << client_info->RemoteSipSrvName
                          << " remote=" << client_info->RemoteIp << ":" << client_info->RemotePort);
         }
    }

     if(result && code == kSipSuccess ) {
         *result = output;
     }else{
          if(output)
              free(output);
     }
           return code;

}


int CSipEventManager::BuildAck( const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message)
{
       int res = 0;
        osip_message_t *pMsg = NULL;

        res = eXosip_call_build_ack (m_sip_context,  key->DialogId,
                        &pMsg);
        if (res) {
            return kSipBuildAckFailed;
        }
        SetOSipMessageContent( sendData, pMsg);
        *osip_message = pMsg;
           return kSipSuccess;
}

int CSipEventManager::BuildInfo(const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message)
{
    int res = 0;
     osip_message_t *pMsg = NULL;

     res = eXosip_call_build_info (m_sip_context,key->DialogId,
                     &pMsg);
     if (res) {
         return kSipBuildInfoFailed;
     }
     SetOSipMessageContent( sendData, pMsg);
     *osip_message = pMsg;
        return kSipSuccess;
}

int CSipEventManager::BuildRequestWithCall(const SipDialogKey* key, const SipMessage* sendData, osip_message_t **osip_message)
{
	int res = 0;
	osip_message_t *pMsg = NULL;
	std::string  m = CSipUtil::MethodEnumToString( sendData->Method );
	res = eXosip_call_build_request(m_sip_context, key->DialogId, m.c_str(),&pMsg);
	if (res) {
		return kSipBuildInfoFailed;
	}
	SetOSipMessageContent(sendData, pMsg);
	*osip_message = pMsg;
	return kSipSuccess;
}

int CSipEventManager::BuildInvite(ClientInfo *client,const  SipMessage* sendData, osip_message_t **osip_message)
{

       int res = 0 ;
       osip_message_t *invite= NULL;

       if( sendData->To && sendData->From  ){

           res = eXosip_call_build_initial_invite(   m_sip_context ,
                                                                    &invite,
                                                                    sendData->To,
                                                                    sendData->From,
                                                                    sendData->To,
                                                                    NULL
                                                                     );
       }else{
           res = eXosip_call_build_initial_invite(   m_sip_context ,
                                                                    &invite,
                                                                    client->to.c_str(),
                                                                    client->from.c_str(),
                                                                    client->route.c_str(),
                                                                    NULL
                                                                     );
       }

        if (res) {
            return kSipBuildInviteFailed;
        }
        SetOSipMessageContent(sendData, invite);
        *osip_message = invite;
        return  kSipSuccess;
}

int  CSipEventManager::BuildSubscribe(ClientInfo *client, const SipMessage* sendData, osip_message_t **osip_message)
{
     int res = 0;
     osip_message_t *pMsg = NULL;
     res = eXosip_subscription_build_initial_subscribe(
                                                                      m_sip_context,
                                                                      &pMsg,
                                                                      client->to.c_str(),
                                                                      client->from.c_str(),
                                                                      client->route.c_str(),
                                                                      sendData->Event,
                                                                      sendData->Expirse
                                                                       );

     if (res) {
            return kSipBuildSubscriFailed;
      }
       SetOSipMessageContent(sendData, pMsg);
     *osip_message = pMsg;
      return  kSipSuccess;
}

int CSipEventManager::BuildRefreshSubscribe(ClientInfo *client,const SipMessage* sendData, osip_message_t **osip_message,const SipDialogKey* key)
{
	int res = 0;
	osip_message_t *pMsg = NULL;
	res = eXosip_subscription_build_refresh_request(m_sip_context,key->DialogId, &pMsg);
	if (res) {
		return kSipBuildSubscriFailed;
	}
	char buffer[64];
	sprintf(buffer,"%d",sendData->Expirse);

	osip_message_set_header(pMsg, "Expires",buffer);

	SetOSipMessageContent(sendData, pMsg);
	*osip_message = pMsg;
	return  kSipSuccess;
}


int  CSipEventManager::BuildNotify(ClientInfo *client, const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message)
{
    int res = 0;

     res = eXosip_insubscription_build_notify(
                                                               m_sip_context,
                                                               key->DialogId,
                                                               EXOSIP_SUBCRSTATE_ACTIVE,
                                                               0,
                                                               osip_message
                                                               );

     if (res != 0) {
            TVT_LOG_INFO(" failed to notify : " << res);
            return kSipBuildNotifyFailed;
     }

     osip_uri_t* uri = NULL;
     osip_uri_clone(   (*osip_message)->req_uri, &uri);


     if( uri->host ) {
         osip_free(uri->host);
         osip_free(uri->port);
         uri->host = osip_strdup(client->RemoteIp.c_str());
         char buf[10];
         sprintf(buf,"%d",client->RemotePort);
         uri->port = osip_strdup(buf);
         osip_uri_free( (*osip_message)->req_uri);
         (*osip_message)->req_uri = uri;
     }

    if(sendData->Event) {
        osip_message_set_header(*osip_message, "Event", sendData->Event);
    }

     SetOSipMessageContent(sendData, *osip_message);
     return  kSipSuccess;
}


int  CSipEventManager::BuildNormal(ClientInfo *client, SipMethod method,const SipMessage* sendData, osip_message_t **osip_message)
{
    osip_message_t* pMsg = NULL;

    int res;
    std::string  m = CSipUtil::MethodEnumToString(method);
    res = eXosip_message_build_request(   m_sip_context,
                                                           &pMsg,
                                                           m.c_str() ,
                                                           client->to.c_str(),
                                                           client->from.c_str(),
                                                           client->route.c_str() );

    if (res != 0){
           return kSipBuildRequestFailed;
    }
    SetOSipMessageContent(sendData, pMsg);
    *osip_message = pMsg;
    return  kSipSuccess;
}



void CSipEventManager::SetOSipMessageContent(const SipMessage* send_data,  osip_message* msg)
{
    if(  send_data->content && send_data->content_type != kSipContentEmpty  ) {

           osip_message_set_body (msg, send_data->content,  strlen(send_data->content));

           std::string content = CSipUtil::ContentEnumToString(send_data->content_type);

           osip_message_set_content_type(msg, content.c_str()) ;
      }

      if(  send_data->Subject ) {
            osip_message_set_header(msg, "Subject",   send_data->Subject);
      }

      if(  send_data->Date ) {
            osip_message_set_header(msg, "Date",   send_data->Date);
      }

	  if(  send_data->Event ) {
		  osip_message_set_header(msg, "Event",   send_data->Event);
	  }
}

int CSipEventManager::SendSipResponse( ClientInfo* info,const SipDialogKey* key,const SipMessage* message)
{
    if(  !m_client_manager->FindClientSession(info)  ) {
          return kSipSessionNoExist;
    }

    return SendReponse(key->TransId, message);
}

SipData* CSipEventManager::GetSipData(bool isRequest, const eXosip_event_t *event )
{

    osip_message_t *pMessage = NULL;
    if(  isRequest  )  {
         pMessage = event->request;
    }else{
         pMessage = event->response;
    }

    if(!pMessage) {
        return NULL;
    }

    SipData* data = (SipData*)malloc(sizeof(SipData));
    memset(data,0, sizeof(*data));
    data->Dialog.DialogId= event->did;
    data->Dialog.TransId = event->tid;
    data->Dialog.CallId = event->cid;

    if(  !isRequest  )  {
         data->messgae.code = (SipReponseCode)pMessage->status_code;
         if( pMessage->reason_phrase ) {
             CSipUtil::memcpy_safe(&(data->messgae.reason_phrase) , pMessage->reason_phrase);
         }
    }

    char *temp;
    temp = osip_cseq_get_number(pMessage->cseq);
    if( temp ) {
        data->messgae.CSeq =  atoi( temp );
    }

    if( pMessage->from ) {

        temp = NULL;
        osip_from_to_str( pMessage->from, &temp);
        if( temp ) {
            CSipUtil::memcpy_safe(&(data->messgae.From) , temp);
            osip_free(temp);
        }

    }

    if( pMessage->to ) {
        temp = NULL;
        osip_to_to_str( pMessage->to, &temp);
        if( temp )
        {
            CSipUtil::memcpy_safe( &(data->messgae.To) ,temp);
            osip_free(temp);
        }
    }

    osip_body_t *pBody = NULL;
    if( osip_message_get_body (pMessage, 0, &pBody) == 0 ) {
        char *temp =NULL;
        size_t Len = 0;
        if(  0== osip_body_to_str(pBody, &temp, &Len) ) {
            std::string content= std::string(temp, Len);
            CSipUtil::memcpy_safe( &(data->messgae.content) ,content);
            osip_free(temp);
        }
    }

    osip_generic_param_t *pMsg = NULL;
    osip_generic_param_get_byname((osip_list_t *) &pMessage->headers,"Subject", &pMsg );

    if( pMsg ) {
        temp = osip_generic_param_get_value(pMsg);
        if ( temp ) {
            CSipUtil::memcpy_safe( &(data->messgae.Subject) ,temp);
        }
    }

    osip_header_t* expirse = NULL;

	if( osip_message_get_expires(pMessage,0,&expirse) >= 0){
		data->messgae.Expirse = atoi(expirse->hvalue);
	}
	osip_header_t* date = NULL;
	if (osip_message_get_date(pMessage,0,&date) >=0)
	{
		CSipUtil::memcpy_safe(&(data->messgae.Date),date->hvalue);
	}
	osip_header_t* event_header = NULL;
	if (osip_message_header_get_byname(pMessage, "Event", 0, &event_header) >= 0 && event_header != NULL) {
		CSipUtil::memcpy_safe(&(data->messgae.Event), event_header->hvalue);
	}
       return data;
}
