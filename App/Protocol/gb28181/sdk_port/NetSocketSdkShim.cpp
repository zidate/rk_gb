#include "NetSocketSDK.h"
#include "SocketCompat.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>

namespace {

struct SocketCtx {
    int fd;
    bool connected;
};

std::mutex g_socket_mutex;
std::map<tint32, SocketCtx> g_sockets;
std::atomic<tint32> g_next_socket_id(1);

SocketCtx* FindSocketLocked(tint32 handle) {
    std::map<tint32, SocketCtx>::iterator it = g_sockets.find(handle);
    if (it == g_sockets.end()) {
        return NULL;
    }
    return &it->second;
}

void ApplySendTimeout(int fd, tuint32 timeout_ms) {
    if (timeout_ms == INFINITE || timeout_ms == 0) {
        return;
    }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

}  // namespace

tint32 NET_SOCKET_AddConnect(tuint32 localIP, const char* pStrServerIP, tuint16 netPort) {
    (void)localIP;
    if (pStrServerIP == NULL || netPort == 0) {
        return -1;
    }

#if RK_ENABLE_GB_IPV6_SOCKET
    protocol::socket_compat::Endpoint endpoint;
    if (!protocol::socket_compat::ResolveEndpoint(pStrServerIP,
                                                   netPort,
                                                   SOCK_STREAM,
                                                   &endpoint)) {
        return -1;
    }

    const int fd = protocol::socket_compat::CreateSocket(endpoint.family, SOCK_STREAM, false);
    if (fd < 0) {
        return -1;
    }

    if (connect(fd,
                protocol::socket_compat::AsSockaddr(endpoint),
                endpoint.len) != 0) {
        close(fd);
        return -1;
    }
#else
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(netPort);
    if (inet_pton(AF_INET, pStrServerIP, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
#endif

    const tint32 handle = g_next_socket_id.fetch_add(1);
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    SocketCtx ctx;
    ctx.fd = fd;
    ctx.connected = true;
    g_sockets[handle] = ctx;
    return handle;
}

bool NET_SOCKET_PopConnectResult(tint32 hNetCommunication, tint32& result, tuint32 maxWaitMSec) {
    (void)maxWaitMSec;
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    SocketCtx* ctx = FindSocketLocked(hNetCommunication);
    result = (ctx != NULL && ctx->connected) ? 1 : -1;
    return true;
}

bool NET_SOCKET_RegisterNode(tint32 hNetCommunication,
                             CSocketDataObserver* pSocketDataObserver,
                             LPVOID lpParameterRecv,
                             tint32 RecvBufCount,
                             tint32 SendBufCount) {
    (void)hNetCommunication;
    (void)pSocketDataObserver;
    (void)lpParameterRecv;
    (void)RecvBufCount;
    (void)SendBufCount;
    return true;
}

bool NET_SOCKET_Start(tint32 hNetCommunication) {
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    return FindSocketLocked(hNetCommunication) != NULL;
}

bool NET_SOCKET_CheckConnectState(tint32 hNetCommunication) {
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    SocketCtx* ctx = FindSocketLocked(hNetCommunication);
    return ctx != NULL && ctx->connected;
}

void NET_SOCKET_Stop(tint32 hNetCommunication) {
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    SocketCtx* ctx = FindSocketLocked(hNetCommunication);
    if (ctx == NULL || ctx->fd < 0) {
        return;
    }
    shutdown(ctx->fd, SHUT_RDWR);
}

void NET_SOCKET_UnRegisterNode(tint32 hNetCommunication) {
    (void)hNetCommunication;
}

void NET_SOCKET_DestroyHNetCommunication(tint32 hNetCommunication) {
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    std::map<tint32, SocketCtx>::iterator it = g_sockets.find(hNetCommunication);
    if (it == g_sockets.end()) {
        return;
    }
    if (it->second.fd >= 0) {
        close(it->second.fd);
    }
    g_sockets.erase(it);
}

int NET_SOCKET_Send_Immediate(tint32 hNetCommunication, const char* pBuf, tint32 nLen, tuint32 maxWaitMSec) {
    if (pBuf == NULL || nLen <= 0) {
        return -1;
    }

    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        SocketCtx* ctx = FindSocketLocked(hNetCommunication);
        if (ctx == NULL || ctx->fd < 0) {
            return -1;
        }
        fd = ctx->fd;
    }

    ApplySendTimeout(fd, maxWaitMSec);

    int total = 0;
    while (total < nLen) {
        const ssize_t sent = send(fd, pBuf + total, static_cast<size_t>(nLen - total), 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return total > 0 ? total : -1;
        }
        if (sent == 0) {
            break;
        }
        total += static_cast<int>(sent);
    }
    return total;
}

int NET_SOCKET_Send_Immediate(tint32 hNetCommunication, const char* pBuf, tint32 nLen, bool& bErr) {
    const int ret = NET_SOCKET_Send_Immediate(hNetCommunication, pBuf, nLen, 500);
    bErr = ret < 0 || ret != nLen;
    return ret;
}
