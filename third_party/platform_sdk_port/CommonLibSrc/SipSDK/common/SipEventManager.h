#ifndef CSIPEVENTMANAGER_H
#define CSIPEVENTMANAGER_H
#include "base_type_define.h"
#include "Interlocked.h"
#include "SipDefs.h"
#include "Lock.h"
#include  <set>
#include "SipUtil.h"
#include "ThreadPool.h"

class  CWaiterManager;
class  CClientInfoManager;
class  ClientInfo;
class  InternalSipData;

extern  void FreeSipData(SipData* data);
extern  void FreeSipBuf(char** buf);

class CSipEventManager
{
public:
    CSipEventManager();
    ~CSipEventManager();
    int StartSipListen(SipTransportType type , const NetAddress* local, const char* local_name);
    void Stop();
    uint16_t GetLocalPort() const { return m_local_port; }
    int SetSipHandler( const SipServerHandler* );
    int Register(ClientInfo* info , int timeout , SipData** result );
    int SendSipResquest( SipMethod method,
                                    int timeout,
                                    ClientInfo* info,
                                    const SipDialogKey* key,
                                    const SipMessage* message,
                                    SipData** result
                                   );

    int SendSipResponse( ClientInfo* info,const SipDialogKey* key,const SipMessage* message);
	void HandleResponseCallback(  std::string& token,SipSessionHandle handle, SipData* data);
    void HandleResquestCallback(SipSessionHandle handle, SipData* data);
	int MessageToStr(SipMethod method,ClientInfo* client_info,const SipMessage* message, char** buf, size_t* buf_len);

private:
    // register interface
    void    HandleRegistResquest(eXosip_event_t* pEvent);
    void    HandleRegistResponse(eXosip_event_t* pEvent);
    int      ResponseForSubAndNotify(int tid, const SipMessage* mesage);
    int      ResponseForCallAnswer(int tid, const SipMessage* mesage);
    int      ResponseForMessage(int tid, const SipMessage* mesage);
    // reponse
    void HandleResponse(eXosip_event_t*);
	// reponse
    // request
    void HandleResquest(eXosip_event_t*,  SipMethod  method);
	
    int  SendResquest( SipMethod method,
                               int timeout,
                               osip_message_t *pRequest,
                               ClientInfo* client_info,
                               const SipDialogKey* key,
                               const SipMessage* message,
                               SipData** result  );

    SipData* GetSipData(bool isRequest, const eXosip_event_t *event );

    int BuildBye( const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message);

    int BuildAck(const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message);

    int BuildInfo(const SipDialogKey* key,const SipMessage* sendData, osip_message_t **osip_message);

    int  BuildInvite(ClientInfo *pClient, const SipMessage* sendData, osip_message_t **osip_message);

    int  BuildNotify(ClientInfo *client, const SipDialogKey* key,const  SipMessage* sendData, osip_message_t **osip_message);

    int BuildSubscribe(ClientInfo *client,const SipMessage* sendData, osip_message_t **osip_message);

	int BuildRefreshSubscribe(ClientInfo *client,const SipMessage* sendData, osip_message_t **osip_message,const SipDialogKey* key);

    int  BuildNormal(ClientInfo *client, SipMethod method,const SipMessage* sendData, osip_message_t **osip_message);

    void SetOSipMessageContent(const SipMessage* send_data,  osip_message_t* msg);

	int BuildRequestWithCall(const SipDialogKey* key, const SipMessage* sendData, osip_message_t **osip_message);
private:
    int    ResponseWithUnauthorized(eXosip_event_t* pEvent);
    int    SendReponse(int tid,const SipMessage* sendData );
    ClientInfo* GetClientInfo(eXosip_event_t* event);
private:
    void              SipEventProcess();
    static void     StartSipEventProcess(void* param);
private:
    Interlocked                   m_stop_flag;
    Interlocked                   m_listen_flag;
    Interlocked                   m_register_guard_count;
    eXosip_t*                     m_sip_context;
    SipServerHandler*         m_sip_handler;
    CWaiterManager*          m_waiter_manager;
    CClientInfoManager*      m_client_manager;
    std::string                     m_local_ip;
    uint16_t                        m_local_port;
    std::string                     m_local_name;
    HANDLE                        m_listen_thread_id;
    HANDLE                        m_log_thread_id;
    SipLogHandler*             m_sip_log_handler;
	CThreadPool                m_callback_threadpool;
};

#endif // CSIPEVENTMANAGER_H
