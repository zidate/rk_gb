#ifndef __PROTOCOL_SOCKET_COMPAT_H__
#define __PROTOCOL_SOCKET_COMPAT_H__

#ifndef RK_ENABLE_IPV6_SOCKET
#define RK_ENABLE_IPV6_SOCKET 0
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace protocol
{
namespace socket_compat
{

struct Endpoint
{
    struct sockaddr_storage addr;
    socklen_t len;
    int family;

    Endpoint() : len(0), family(AF_UNSPEC)
    {
        memset(&addr, 0, sizeof(addr));
    }
};

inline const struct sockaddr* AsSockaddr(const Endpoint& endpoint)
{
    return reinterpret_cast<const struct sockaddr*>(&endpoint.addr);
}

inline struct sockaddr* AsSockaddr(Endpoint& endpoint)
{
    return reinterpret_cast<struct sockaddr*>(&endpoint.addr);
}

inline bool IsIpv6Enabled()
{
#if RK_ENABLE_IPV6_SOCKET
    return true;
#else
    return false;
#endif
}

inline bool IsIpv4AnyText(const std::string& ip)
{
    return ip.empty() || ip == "0.0.0.0" || ip == "*";
}

inline bool IsIpv6Text(const std::string& ip)
{
    return ip.find(':') != std::string::npos;
}

inline int CreateSocket(int family, int sockType, bool dualStack)
{
    const int fd = socket(family, sockType, 0);
    if (fd < 0) {
        return fd;
    }

#if RK_ENABLE_IPV6_SOCKET
    if (family == AF_INET6 && dualStack) {
        const int off = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    }
#else
    (void)dualStack;
#endif

    return fd;
}

inline int CreateDualStackSocket(int sockType)
{
#if RK_ENABLE_IPV6_SOCKET
    return CreateSocket(AF_INET6, sockType, true);
#else
    return socket(AF_INET, sockType, 0);
#endif
}

inline bool BuildAnyEndpoint(int family, int port, Endpoint* out)
{
    if (out == NULL || port < 0 || port > 65535) {
        errno = EINVAL;
        return false;
    }

    *out = Endpoint();
#if RK_ENABLE_IPV6_SOCKET
    if (family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(static_cast<uint16_t>(port));
        memcpy(&out->addr, &addr6, sizeof(addr6));
        out->len = sizeof(addr6);
        out->family = AF_INET6;
        return true;
    }
#endif

    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_addr.s_addr = htonl(INADDR_ANY);
    addr4.sin_port = htons(static_cast<uint16_t>(port));
    memcpy(&out->addr, &addr4, sizeof(addr4));
    out->len = sizeof(addr4);
    out->family = AF_INET;
    return true;
}

inline bool ResolveEndpoint(const std::string& host, int port, int sockType, Endpoint* out)
{
    if (out == NULL || host.empty() || port <= 0 || port > 65535) {
        errno = EINVAL;
        return false;
    }

    *out = Endpoint();

#if RK_ENABLE_IPV6_SOCKET
    char service[16];
    snprintf(service, sizeof(service), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = sockType;
    hints.ai_flags = AI_NUMERICSERV;

    struct addrinfo* result = NULL;
    const int ret = getaddrinfo(host.c_str(), service, &hints, &result);
    if (ret != 0 || result == NULL) {
        errno = EINVAL;
        return false;
    }

    bool resolved = false;
    for (struct addrinfo* it = result; it != NULL; it = it->ai_next) {
        if ((it->ai_family != AF_INET && it->ai_family != AF_INET6) ||
            it->ai_addr == NULL ||
            it->ai_addrlen > sizeof(out->addr)) {
            continue;
        }

        memcpy(&out->addr, it->ai_addr, it->ai_addrlen);
        out->len = static_cast<socklen_t>(it->ai_addrlen);
        out->family = it->ai_family;
        resolved = true;
        break;
    }

    freeaddrinfo(result);
    if (!resolved) {
        errno = EAFNOSUPPORT;
    }
    return resolved;
#else
    (void)sockType;
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr4.sin_addr) != 1) {
        errno = EINVAL;
        return false;
    }

    memcpy(&out->addr, &addr4, sizeof(addr4));
    out->len = sizeof(addr4);
    out->family = AF_INET;
    return true;
#endif
}

inline bool MapIpv4ToIpv6(const Endpoint& ipv4, Endpoint* out)
{
    if (out == NULL || ipv4.family != AF_INET) {
        errno = EINVAL;
        return false;
    }

    const struct sockaddr_in* src =
        reinterpret_cast<const struct sockaddr_in*>(&ipv4.addr);
    struct sockaddr_in6 dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;
    dst.sin6_port = src->sin_port;
    dst.sin6_addr.s6_addr[10] = 0xff;
    dst.sin6_addr.s6_addr[11] = 0xff;
    memcpy(&dst.sin6_addr.s6_addr[12], &src->sin_addr, sizeof(src->sin_addr));

    *out = Endpoint();
    memcpy(&out->addr, &dst, sizeof(dst));
    out->len = sizeof(dst);
    out->family = AF_INET6;
    return true;
}

inline int GetPort(const struct sockaddr_storage& addr)
{
    if (addr.ss_family == AF_INET) {
        const struct sockaddr_in* a = reinterpret_cast<const struct sockaddr_in*>(&addr);
        return ntohs(a->sin_port);
    }
#if RK_ENABLE_IPV6_SOCKET
    if (addr.ss_family == AF_INET6) {
        const struct sockaddr_in6* a = reinterpret_cast<const struct sockaddr_in6*>(&addr);
        return ntohs(a->sin6_port);
    }
#endif
    return 0;
}

inline bool CopyAddressWithPort(const struct sockaddr_storage& src,
                                int port,
                                struct sockaddr_storage* dst,
                                socklen_t* dstLen)
{
    if (dst == NULL || dstLen == NULL || port < 0 || port > 65535) {
        errno = EINVAL;
        return false;
    }

    memset(dst, 0, sizeof(*dst));
    if (src.ss_family == AF_INET) {
        const struct sockaddr_in* in = reinterpret_cast<const struct sockaddr_in*>(&src);
        struct sockaddr_in out = *in;
        out.sin_port = htons(static_cast<uint16_t>(port));
        memcpy(dst, &out, sizeof(out));
        *dstLen = sizeof(out);
        return true;
    }

#if RK_ENABLE_IPV6_SOCKET
    if (src.ss_family == AF_INET6) {
        const struct sockaddr_in6* in = reinterpret_cast<const struct sockaddr_in6*>(&src);
        struct sockaddr_in6 out = *in;
        out.sin6_port = htons(static_cast<uint16_t>(port));
        memcpy(dst, &out, sizeof(out));
        *dstLen = sizeof(out);
        return true;
    }
#endif

    errno = EAFNOSUPPORT;
    return false;
}

inline bool SockaddrToString(const struct sockaddr* addr,
                             socklen_t addrLen,
                             char* ip,
                             size_t ipLen,
                             int* port)
{
    (void)addrLen;
    if (addr == NULL || ip == NULL || ipLen == 0) {
        errno = EINVAL;
        return false;
    }

    ip[0] = '\0';
    if (port != NULL) {
        *port = 0;
    }

    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* in = reinterpret_cast<const struct sockaddr_in*>(addr);
        if (inet_ntop(AF_INET, &in->sin_addr, ip, ipLen) == NULL) {
            return false;
        }
        if (port != NULL) {
            *port = ntohs(in->sin_port);
        }
        return true;
    }

#if RK_ENABLE_IPV6_SOCKET
    if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
            struct in_addr v4;
            memcpy(&v4, &in6->sin6_addr.s6_addr[12], sizeof(v4));
            if (inet_ntop(AF_INET, &v4, ip, ipLen) == NULL) {
                return false;
            }
        } else if (inet_ntop(AF_INET6, &in6->sin6_addr, ip, ipLen) == NULL) {
            return false;
        }

        if (port != NULL) {
            *port = ntohs(in6->sin6_port);
        }
        return true;
    }
#endif

    errno = EAFNOSUPPORT;
    return false;
}

inline bool ExtractComparableAddress(const struct sockaddr_storage& addr,
                                     int* family,
                                     uint8_t* bytes,
                                     size_t* bytesLen)
{
    if (family == NULL || bytes == NULL || bytesLen == NULL) {
        return false;
    }

    if (addr.ss_family == AF_INET) {
        const struct sockaddr_in* in = reinterpret_cast<const struct sockaddr_in*>(&addr);
        *family = AF_INET;
        *bytesLen = sizeof(in->sin_addr);
        memcpy(bytes, &in->sin_addr, *bytesLen);
        return true;
    }

#if RK_ENABLE_IPV6_SOCKET
    if (addr.ss_family == AF_INET6) {
        const struct sockaddr_in6* in6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
            *family = AF_INET;
            *bytesLen = sizeof(struct in_addr);
            memcpy(bytes, &in6->sin6_addr.s6_addr[12], *bytesLen);
            return true;
        }

        *family = AF_INET6;
        *bytesLen = sizeof(in6->sin6_addr);
        memcpy(bytes, &in6->sin6_addr, *bytesLen);
        return true;
    }
#endif

    return false;
}

inline bool AddressMatchesText(const struct sockaddr_storage& addr, const std::string& expected)
{
    if (expected.empty()) {
        return true;
    }

    char actual[INET6_ADDRSTRLEN] = {0};
    if (SockaddrToString(reinterpret_cast<const struct sockaddr*>(&addr),
                         sizeof(addr),
                         actual,
                         sizeof(actual),
                         NULL) &&
        expected == actual) {
        return true;
    }

    Endpoint expectedEndpoint;
    if (!ResolveEndpoint(expected, 1, SOCK_DGRAM, &expectedEndpoint)) {
        return false;
    }

    int actualFamily = AF_UNSPEC;
    int expectedFamily = AF_UNSPEC;
    uint8_t actualBytes[16] = {0};
    uint8_t expectedBytes[16] = {0};
    size_t actualLen = 0;
    size_t expectedLen = 0;
    if (!ExtractComparableAddress(addr, &actualFamily, actualBytes, &actualLen) ||
        !ExtractComparableAddress(expectedEndpoint.addr, &expectedFamily, expectedBytes, &expectedLen)) {
        return false;
    }

    return actualFamily == expectedFamily &&
           actualLen == expectedLen &&
           memcmp(actualBytes, expectedBytes, actualLen) == 0;
}

inline int GetSocketFamily(int sockfd)
{
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(sockfd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen) == 0 &&
        (addr.ss_family == AF_INET || addr.ss_family == AF_INET6)) {
        return addr.ss_family;
    }
    return AF_INET;
}

}
}

#endif
