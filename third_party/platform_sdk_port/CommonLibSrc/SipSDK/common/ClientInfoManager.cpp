#include "ClientInfoManager.h"
#include "eXosip2/eXosip.h"
#include "ShareSDK.h"
#include <stdlib.h>

namespace {

static int ParseSipPort(const char* port)
{
    if (port == NULL || port[0] == '\0') {
        return 0;
    }
    return atoi(port);
}

static void FillPeerFromUri(PeerInfo* peer, osip_uri_t* uri)
{
    if (peer == NULL || uri == NULL) {
        return;
    }

    if (peer->name.empty() && uri->username != NULL) {
        peer->name = uri->username;
    }

    if (peer->ip.empty() && uri->host != NULL) {
        peer->ip = uri->host;
    }

    if (peer->port <= 0) {
        peer->port = ParseSipPort(uri->port);
    }
}

}

CClientInfoManager::CClientInfoManager()
{

}

CClientInfoManager::~CClientInfoManager()
{

}

void CClientInfoManager::InsertClientSession(ClientInfo* handle)
{
    m_lock.Lock();
    m_client_set.insert(handle);
    m_lock.Unlock();
}

void CClientInfoManager::DeletClientSession(ClientInfo* handle)
{
    m_lock.Lock();
    m_client_set.erase(handle);
    m_lock.Unlock();
}

bool CClientInfoManager::FindClientSession(ClientInfo* handle)
{
    m_lock.Lock();
    std::set<ClientInfo*>::iterator iter = m_client_set.find(handle);
    if( iter != m_client_set.end() ){
         m_lock.Unlock();
        return true;
    }
    m_lock.Unlock();
        return false;
}

ClientInfo* CClientInfoManager::FindClient(PeerInfo* info)
{
    if (info == NULL) {
        return NULL;
    }

    ClientInfo* client = NULL;
    ClientInfo* temp = NULL;
    size_t client_count = 0;
    m_lock.Lock();
    client_count = m_client_set.size();
    std::set<ClientInfo*>::iterator iter = m_client_set.begin();
    while( iter != m_client_set.end() ) {

        temp = *iter;

        if(  info->name == temp->RemoteSipSrvName ) {
           client = temp;
            break;
        }

		if (info->ip == temp->RemoteIp && info->port == temp->RemotePort ) {
			client = temp;
			break;
		}
            iter++;
    }
    m_lock.Unlock();

    if (client != NULL) {
        TVT_LOG_INFO("sip peer matched client"
                     << " peer_name=" << info->name
                     << " peer=" << info->ip << ":" << info->port
                     << " remote_name=" << client->RemoteSipSrvName
                     << " remote=" << client->RemoteIp << ":" << client->RemotePort);
    }
    else {
        TVT_LOG_ERROR("sip peer match failed"
                      << " peer_name=" << info->name
                      << " peer=" << info->ip << ":" << info->port
                      << " client_count=" << client_count);
    }

    return client;
}

PeerInfo* CClientInfoManager::GetPeerInfo(osip_message_t *pMsg, bool is_request)
{
	if (!pMsg) {
		return NULL;
	}

	PeerInfo* peer = new PeerInfo;
    peer->port = 0;
    std::string name_source = "none";
    std::string addr_source = "none";
    struct PeerSourceTracker {
        static void Apply(PeerInfo* peer,
                          osip_uri_t* uri,
                          const char* source,
                          std::string* name_source,
                          std::string* addr_source)
        {
            if (peer == NULL) {
                return;
            }
            const std::string old_name = peer->name;
            const std::string old_ip = peer->ip;
            const int old_port = peer->port;
            FillPeerFromUri(peer, uri);
            if (name_source != NULL && old_name.empty() && !peer->name.empty()) {
                *name_source = source;
            }
            if (addr_source != NULL &&
                ((old_ip.empty() && !peer->ip.empty()) || (old_port <= 0 && peer->port > 0))) {
                *addr_source = source;
            }
        }
    };
	osip_contact_t * pContact = NULL;
	if (OSIP_SUCCESS == osip_message_get_contact(pMsg, 0, &pContact)
		&& pContact && pContact->url)
	{
        PeerSourceTracker::Apply(peer, pContact->url, "contact", &name_source, &addr_source);
	}

    if (is_request) {
        if (pMsg->from) {
            osip_uri_t * pUri = osip_to_get_url(pMsg->from);
            PeerSourceTracker::Apply(peer, pUri, "from", &name_source, &addr_source);
        }
    }
    else {
        if (pMsg->to) {
            osip_uri_t * pUri = osip_to_get_url(pMsg->to);
            PeerSourceTracker::Apply(peer, pUri, "to", &name_source, &addr_source);
        }
    }

    if (peer->name.empty() && pMsg->from) {
        osip_uri_t * pUri = osip_to_get_url(pMsg->from);
        PeerSourceTracker::Apply(peer, pUri, "from_fallback", &name_source, &addr_source);
    }

    if ((peer->ip.empty() || peer->port <= 0) && pMsg->to) {
        osip_uri_t * pUri = osip_to_get_url(pMsg->to);
        PeerSourceTracker::Apply(peer, pUri, "to_fallback", &name_source, &addr_source);
    }

    if ((peer->name.empty() && peer->ip.empty()) || peer->port < 0) {
        delete peer;
        return NULL;
    }

    TVT_LOG_INFO("sip peer parsed"
                 << " is_request=" << (is_request ? 1 : 0)
                 << " peer_name=" << peer->name
                 << " peer=" << peer->ip << ":" << peer->port
                 << " name_source=" << name_source
                 << " addr_source=" << addr_source);

    return peer;
}
