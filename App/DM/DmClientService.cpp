#include "DmClientService.h"

#include "DmConfig.h"
#include "DmLwm2mObjects.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

extern "C" {
#include "lwm2mclient.h"
#include "udp/connection.h"
}

#ifndef RK_ENABLE_IPV6_SOCKET
#define RK_ENABLE_IPV6_SOCKET 0
#endif

namespace
{

struct DmWakaamaClientData
{
    lwm2m_object_t* securityObjP;
    int sock;
    lwm2m_connection_t* connList;
    int addressFamily;

    DmWakaamaClientData()
        : securityObjP(NULL), sock(-1), connList(NULL), addressFamily(AF_INET)
    {
    }
};

bool ParseCoapUri(char* uri, char** host, char** port)
{
    if (uri == NULL || host == NULL || port == NULL) {
        return false;
    }

    if (strncmp(uri, "coap://", strlen("coap://")) == 0) {
        *host = uri + strlen("coap://");
    } else {
        return false;
    }

    *port = strrchr(*host, ':');
    if (*port == NULL) {
        return false;
    }

    if ((*host)[0] == '[') {
        ++(*host);
        if (*(*port - 1) != ']') {
            return false;
        }
        *(*port - 1) = '\0';
    }

    **port = '\0';
    ++(*port);
    return **host != '\0' && **port != '\0';
}

std::string LocalPortToString(int localPort)
{
    char buffer[16] = {0};
    snprintf(buffer, sizeof(buffer), "%d", localPort);
    return buffer;
}

int GetDmAddressFamilyPreference()
{
#if RK_ENABLE_IPV6_SOCKET
    return AF_UNSPEC;
#else
    return AF_INET;
#endif
}

void FreeWakaamaObjects(std::vector<lwm2m_object_t*>& objects)
{
    for (size_t i = 0; i < objects.size(); ++i) {
        lwm2m_object_t* objectP = objects[i];
        if (objectP == NULL) {
            continue;
        }
        if (objectP->objID == LWM2M_SECURITY_OBJECT_ID) {
            clean_security_object(objectP);
            lwm2m_free(objectP);
        } else if (objectP->objID == LWM2M_SERVER_OBJECT_ID) {
            clean_server_object(objectP);
            lwm2m_free(objectP);
        } else {
            dm::FreeDmObject(objectP);
        }
    }
    objects.clear();
}

bool IsReady(lwm2m_context_t* context)
{
    return context != NULL && context->state == STATE_READY;
}

} // namespace

extern "C" void* lwm2m_connect_server(uint16_t secObjInstID, void* userData)
{
    DmWakaamaClientData* dataP = static_cast<DmWakaamaClientData*>(userData);
    if (dataP == NULL || dataP->securityObjP == NULL) {
        return NULL;
    }

    char* uri = get_server_uri(dataP->securityObjP, secObjInstID);
    if (uri == NULL) {
        return NULL;
    }

    char* host = NULL;
    char* port = NULL;
    lwm2m_connection_t* newConnP = NULL;
    if (ParseCoapUri(uri, &host, &port)) {
        printf("[DM] Opening LwM2M connection to %s:%s\n", host, port);
        newConnP = lwm2m_connection_create(dataP->connList,
                                           dataP->sock,
                                           host,
                                           port,
                                           dataP->addressFamily);
        if (newConnP != NULL) {
            dataP->connList = newConnP;
        }
    }

    lwm2m_free(uri);
    return newConnP;
}

extern "C" void lwm2m_close_connection(void* sessionH, void* userData)
{
    DmWakaamaClientData* dataP = static_cast<DmWakaamaClientData*>(userData);
    lwm2m_connection_t* targetP = static_cast<lwm2m_connection_t*>(sessionH);
    if (dataP == NULL || targetP == NULL) {
        return;
    }

    if (targetP == dataP->connList) {
        dataP->connList = targetP->next;
        lwm2m_free(targetP);
        return;
    }

    lwm2m_connection_t* parentP = dataP->connList;
    while (parentP != NULL && parentP->next != targetP) {
        parentP = parentP->next;
    }
    if (parentP != NULL) {
        parentP->next = targetP->next;
        lwm2m_free(targetP);
    }
}

namespace dm
{

DmClientService& DmClientService::Instance()
{
    static DmClientService s_instance;
    return s_instance;
}

DmClientService::DmClientService()
    : m_thread(),
      m_thread_started(false),
      m_stop_requested(false),
      m_running(false)
{
}

DmClientService::~DmClientService()
{
    Stop();
}

int DmClientService::Start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_thread_started) {
        return 0;
    }

    DmConfig cfg;
    const int loadRet = LoadDmConfig(cfg);
    if (loadRet != 0) {
        printf("[DM] load dm.ini failed ret=%d path=%s\n", loadRet, kDmConfigFile);
        return loadRet;
    }

    if (cfg.enabled == 0) {
        printf("[DM] disabled by dm.ini path=%s\n", kDmConfigFile);
        return 0;
    }

    std::string reason;
    if (!ValidateDmConfig(cfg, reason)) {
        printf("[DM] invalid dm.ini: %s\n", reason.c_str());
        return -2;
    }

    m_stop_requested.store(false);
    const int ret = pthread_create(&m_thread, NULL, &DmClientService::ThreadEntry, this);
    if (ret != 0) {
        printf("[DM] create thread failed ret=%d\n", ret);
        return -3;
    }
    m_thread_started = true;
    return 0;
}

void DmClientService::Stop()
{
    pthread_t thread;
    bool join = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_thread_started) {
            m_stop_requested.store(true);
            thread = m_thread;
            join = true;
            m_thread_started = false;
        }
    }

    if (join) {
        pthread_join(thread, NULL);
    }
    m_running.store(false);
}

int DmClientService::Restart()
{
    Stop();
    return Start();
}

bool DmClientService::IsRunning() const
{
    return m_running.load();
}

void* DmClientService::ThreadEntry(void* arg)
{
    DmClientService* service = static_cast<DmClientService*>(arg);
    if (service != NULL) {
        service->ThreadLoop();
    }
    return NULL;
}

void DmClientService::ThreadLoop()
{
    m_running.store(true);
    while (!m_stop_requested.load()) {
        DmConfig cfg;
        LoadDmConfig(cfg);
        const int ret = RunLwm2mClientOnce();
        if (m_stop_requested.load()) {
            break;
        }
        printf("[DM] LwM2M client stopped ret=%d, retry in %d sec\n",
               ret,
               std::max(1, cfg.startup_retry_interval_sec));
        for (int i = 0; i < std::max(1, cfg.startup_retry_interval_sec) &&
                        !m_stop_requested.load(); ++i) {
            sleep(1);
        }
    }
    m_running.store(false);
}

int DmClientService::RunLwm2mClientOnce()
{
    DmConfig cfg;
    int ret = LoadDmConfig(cfg);
    if (ret != 0 || cfg.enabled == 0) {
        return ret;
    }

    std::string reason;
    if (!ValidateDmConfig(cfg, reason)) {
        printf("[DM] invalid config in client thread: %s\n", reason.c_str());
        return -2;
    }

    DmObjectState state;
    state.config = cfg;

    DmWakaamaClientData data;
    data.addressFamily = GetDmAddressFamilyPreference();
    const std::string localPort = LocalPortToString(cfg.local_port);
    data.sock = lwm2m_create_socket(localPort.c_str(), data.addressFamily);
    if (data.sock < 0) {
        printf("[DM] create UDP socket failed local_port=%s\n", localPort.c_str());
        return -3;
    }

    std::vector<lwm2m_object_t*> objects;
    data.securityObjP = get_security_object(cfg.short_server_id,
                                            cfg.server_uri.c_str(),
                                            NULL,
                                            NULL,
                                            0,
                                            false);
    objects.push_back(data.securityObjP);
    objects.push_back(get_server_object(cfg.short_server_id, "U", cfg.lifetime_sec, false));
    objects.push_back(CreateDmDeviceObject(&state));
    objects.push_back(CreateDmLocationObject(&state));
    objects.push_back(CreateDmConfigObject(&state));
    objects.push_back(CreateDmInfoObject(&state));

    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i] == NULL) {
            printf("[DM] create LwM2M object failed index=%zu\n", i);
            FreeWakaamaObjects(objects);
            close(data.sock);
            return -4;
        }
    }

    lwm2m_context_t* context = lwm2m_init(&data);
    if (context == NULL) {
        printf("[DM] lwm2m_init failed\n");
        FreeWakaamaObjects(objects);
        close(data.sock);
        return -5;
    }

    const std::string endpoint = BuildDmEndpoint(cfg);
    ret = lwm2m_configure(context,
                          endpoint.c_str(),
                          NULL,
                          NULL,
                          static_cast<uint16_t>(objects.size()),
                          objects.empty() ? NULL : &objects[0]);
    if (ret != 0) {
        printf("[DM] lwm2m_configure failed ret=%d endpoint=%s\n", ret, endpoint.c_str());
        lwm2m_close(context);
        FreeWakaamaObjects(objects);
        close(data.sock);
        return -6;
    }

    printf("[DM] LwM2M client started endpoint=%s server=%s lifetime=%d\n",
           endpoint.c_str(),
           cfg.server_uri.c_str(),
           cfg.lifetime_sec);

    time_t nextHeartbeat = time(NULL) + ParseDmRuleConfig(state.ruleConfig).heartbeat_time_min * 60;
    while (!m_stop_requested.load()) {
        time_t timeout = 60;
        ret = lwm2m_step(context, &timeout);
        if (ret != 0) {
            printf("[DM] lwm2m_step failed ret=%d\n", ret);
            break;
        }

        const time_t now = time(NULL);
        const DmRuleConfig rule = ParseDmRuleConfig(state.ruleConfig);
        if (IsReady(context) && now >= nextHeartbeat) {
            if (DmReportAllowed(state, now)) {
                const int updateRet = lwm2m_update_registration(context,
                                                                cfg.short_server_id,
                                                                true);
                printf("[DM] heartbeat update ret=%d\n", updateRet);
            } else {
                printf("[DM] heartbeat skipped by reportNum/reportTime rule\n");
            }
            nextHeartbeat = now + static_cast<time_t>(rule.heartbeat_time_min) * 60;
        }

        if (state.addressChanged && !state.addressConfig.empty()) {
            printf("[DM] addressConfig changed to %s\n", state.addressConfig.c_str());
            SaveDmServerUri(state.addressConfig);
            break;
        }

        struct timeval tv;
        tv.tv_sec = std::max<time_t>(1, std::min<time_t>(timeout, 1));
        tv.tv_usec = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(data.sock, &readfds);

        ret = select(data.sock + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno != EINTR) {
                printf("[DM] select failed errno=%d\n", errno);
                break;
            }
            continue;
        }

        if (ret > 0 && FD_ISSET(data.sock, &readfds)) {
            uint8_t buffer[LWM2M_COAP_MAX_MESSAGE_SIZE];
            struct sockaddr_storage addr;
            socklen_t addrLen = sizeof(addr);
            const ssize_t numBytes = recvfrom(data.sock,
                                              buffer,
                                              sizeof(buffer),
                                              0,
                                              reinterpret_cast<struct sockaddr*>(&addr),
                                              &addrLen);
            if (numBytes > 0 && numBytes < static_cast<ssize_t>(sizeof(buffer))) {
                lwm2m_connection_t* connP = lwm2m_connection_find(data.connList, &addr, addrLen);
                if (connP != NULL) {
                    lwm2m_handle_packet(context, buffer, static_cast<size_t>(numBytes), connP);
                }
            }
        }
    }

    lwm2m_deregister(context);
    lwm2m_close(context);
    lwm2m_connection_free(data.connList);
    FreeWakaamaObjects(objects);
    close(data.sock);
    return 0;
}

int RestartDmClientService()
{
    return DmClientService::Instance().Restart();
}

}
