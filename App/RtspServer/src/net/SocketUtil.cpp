// PHZ
// 2018-5-15

#include "SocketUtil.h"
#include "Socket.h"
#include "SocketCompat.h"
#include <iostream>

using namespace xop;

bool SocketUtil::Bind(SOCKET sockfd, std::string ip, uint16_t port)
{
#if RK_ENABLE_IPV6_SOCKET
    const int family = protocol::socket_compat::GetSocketFamily(sockfd);
    protocol::socket_compat::Endpoint endpoint;
    bool ok = false;

    if ((family == AF_INET6 && protocol::socket_compat::IsIpv4AnyText(ip)) ||
        ip == "::") {
        ok = protocol::socket_compat::BuildAnyEndpoint(AF_INET6, port, &endpoint);
    } else if (protocol::socket_compat::IsIpv4AnyText(ip)) {
        ok = protocol::socket_compat::BuildAnyEndpoint(AF_INET, port, &endpoint);
    } else {
        ok = protocol::socket_compat::ResolveEndpoint(ip, port, SOCK_STREAM, &endpoint);
        if (ok && family == AF_INET6 && endpoint.family == AF_INET) {
            protocol::socket_compat::Endpoint mapped;
            if (protocol::socket_compat::MapIpv4ToIpv6(endpoint, &mapped)) {
                endpoint = mapped;
            }
        }
    }

    if (!ok || ::bind(sockfd,
                     protocol::socket_compat::AsSockaddr(endpoint),
                     endpoint.len) == SOCKET_ERROR) {
        return false;
    }

    return true;
#else
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);  

    if(::bind(sockfd, (struct sockaddr*)&addr, sizeof addr) == SOCKET_ERROR) {      
        return false;
    }

    return true;
#endif
}

void SocketUtil::SetNonBlock(SOCKET fd)
{
#if defined(__linux) || defined(__linux__) 
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    unsigned long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#endif
}

void SocketUtil::SetBlock(SOCKET fd, int write_timeout)
{
#if defined(__linux) || defined(__linux__) 
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags&(~O_NONBLOCK));
#elif defined(WIN32) || defined(_WIN32)
    unsigned long on = 0;
    ioctlsocket(fd, FIONBIO, &on);
#else
#endif
    if(write_timeout > 0)
    {
#ifdef SO_SNDTIMEO
#if defined(__linux) || defined(__linux__) 
    struct timeval tv = {write_timeout/1000, (write_timeout%1000)*1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof tv);
#elif defined(WIN32) || defined(_WIN32)
    unsigned long ms = (unsigned long)write_timeout;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&ms, sizeof(unsigned long));
#else
#endif		
#endif
	}
}

void SocketUtil::SetReuseAddr(SOCKET sockfd)
{
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof on);
}

void SocketUtil::SetReusePort(SOCKET sockfd)
{
#ifdef SO_REUSEPORT
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*)&on, sizeof(on));
#endif	
}

void SocketUtil::SetNoDelay(SOCKET sockfd)
{
#ifdef TCP_NODELAY
    int on = 1;
    int ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof(on));
#endif
}

void SocketUtil::SetKeepAlive(SOCKET sockfd)
{
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));
}

void SocketUtil::SetNoSigpipe(SOCKET sockfd)
{
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, (char *)&on, sizeof(on));
#endif
}

void SocketUtil::SetSendBufSize(SOCKET sockfd, int size)
{
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof(size));
}

void SocketUtil::SetRecvBufSize(SOCKET sockfd, int size)
{
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof(size));
}

std::string SocketUtil::GetPeerIp(SOCKET sockfd)
{
    struct sockaddr_storage addr = { 0 };
    socklen_t addrlen = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addrlen) == 0)
    {
        char ip[INET6_ADDRSTRLEN] = {0};
        if (protocol::socket_compat::SockaddrToString((struct sockaddr*)&addr,
                                                      addrlen,
                                                      ip,
                                                      sizeof(ip),
                                                      NULL)) {
            return ip;
        }
    }
    return "0.0.0.0";
}

std::string SocketUtil::GetSocketIp(SOCKET sockfd)
{
    struct sockaddr_storage addr = {0};
    socklen_t addrlen = sizeof(addr);
    char str[INET6_ADDRSTRLEN] = "127.0.0.1";
    if (GetSocketAddr(sockfd, &addr, &addrlen) == 0) {
        protocol::socket_compat::SockaddrToString((struct sockaddr*)&addr,
                                                  addrlen,
                                                  str,
                                                  sizeof(str),
                                                  NULL);
    }
    return str;
}

int SocketUtil::GetSocketAddr(SOCKET sockfd, struct sockaddr_in* addr)
{
    struct sockaddr_storage storage;
    socklen_t addrlen = sizeof(storage);
    memset(&storage, 0, sizeof(storage));
    int ret = getsockname(sockfd, (struct sockaddr*)&storage, &addrlen);
    if (ret != 0 || storage.ss_family != AF_INET) {
        return -1;
    }
    memcpy(addr, &storage, sizeof(*addr));
    return 0;
}

int SocketUtil::GetSocketAddr(SOCKET sockfd, struct sockaddr_storage* addr, socklen_t* addrlen)
{
    if (addr == NULL || addrlen == NULL) {
        return -1;
    }

    *addrlen = sizeof(*addr);
    memset(addr, 0, sizeof(*addr));
    return getsockname(sockfd, (struct sockaddr*)addr, addrlen);
}

uint16_t SocketUtil::GetPeerPort(SOCKET sockfd)
{
    struct sockaddr_storage addr = { 0 };
    socklen_t addrlen = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addrlen) == 0)
    {
        return (uint16_t)protocol::socket_compat::GetPort(addr);
    }
    return 0;
}

int SocketUtil::GetPeerAddr(SOCKET sockfd, struct sockaddr_in *addr)
{
    struct sockaddr_storage storage;
    socklen_t addrlen = sizeof(storage);
    memset(&storage, 0, sizeof(storage));
    int ret = getpeername(sockfd, (struct sockaddr*)&storage, &addrlen);
    if (ret != 0 || storage.ss_family != AF_INET) {
        return -1;
    }
    memcpy(addr, &storage, sizeof(*addr));
    return 0;
}

int SocketUtil::GetPeerAddr(SOCKET sockfd, struct sockaddr_storage* addr, socklen_t* addrlen)
{
    if (addr == NULL || addrlen == NULL) {
        return -1;
    }

    *addrlen = sizeof(*addr);
    memset(addr, 0, sizeof(*addr));
    return getpeername(sockfd, (struct sockaddr*)addr, addrlen);
}

void SocketUtil::Close(SOCKET sockfd)
{
#if defined(__linux) || defined(__linux__) 
    ::close(sockfd);
#elif defined(WIN32) || defined(_WIN32)
    ::closesocket(sockfd);
#endif
}

bool SocketUtil::Connect(SOCKET sockfd, std::string ip, uint16_t port, int timeout)
{
	bool is_connected = true;

	if (timeout > 0) {
		SocketUtil::SetNonBlock(sockfd);
	}

#if RK_ENABLE_IPV6_SOCKET
    protocol::socket_compat::Endpoint endpoint;
    if (!protocol::socket_compat::ResolveEndpoint(ip, port, SOCK_STREAM, &endpoint)) {
        if (timeout > 0) {
            SocketUtil::SetBlock(sockfd);
        }
        return false;
    }

    const int family = protocol::socket_compat::GetSocketFamily(sockfd);
    if (family == AF_INET6 && endpoint.family == AF_INET) {
        protocol::socket_compat::Endpoint mapped;
        if (protocol::socket_compat::MapIpv4ToIpv6(endpoint, &mapped)) {
            endpoint = mapped;
        }
    } else if (family != endpoint.family) {
        if (timeout > 0) {
            SocketUtil::SetBlock(sockfd);
        }
        return false;
    }

    const struct sockaddr* addr = protocol::socket_compat::AsSockaddr(endpoint);
    socklen_t addrlen = endpoint.len;
#else
	struct sockaddr_in addr_storage = { 0 };
	socklen_t addrlen = sizeof(addr_storage);
	addr_storage.sin_family = AF_INET;
	addr_storage.sin_port = htons(port);
	addr_storage.sin_addr.s_addr = inet_addr(ip.c_str());
	const struct sockaddr* addr = (struct sockaddr*)&addr_storage;
#endif

	if (::connect(sockfd, addr, addrlen) == SOCKET_ERROR) {
		if (timeout > 0) {
            is_connected = false;
			fd_set fd_write;
			FD_ZERO(&fd_write);
			FD_SET(sockfd, &fd_write);
			struct timeval tv = { timeout / 1000, timeout % 1000 * 1000 };
			select((int)sockfd + 1, NULL, &fd_write, NULL, &tv);
			if (FD_ISSET(sockfd, &fd_write)) {
                is_connected = true;
			}
			SocketUtil::SetBlock(sockfd);
		}
		else {
            is_connected = false;
		}		
	}
	
	return is_connected;
}

