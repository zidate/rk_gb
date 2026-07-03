/*******************************************************************************
 *
 * Copyright (c) 2013, 2014 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    Pascal Rieux - Please refer to git log
 *
 *******************************************************************************/

#include "udp/connection.h"
#include "commandline.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

static int set_dual_stack(int s) {
#ifdef IPV6_V6ONLY
    const int off = 0;
    return setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
#else
    (void)s;
    return 0;
#endif
}

static int find_and_bind_to_address(struct addrinfo *res, int dualStack) {
    int s = -1;
    for (struct addrinfo *p = res; p != NULL && s == -1; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s >= 0) {
            if (dualStack && p->ai_family == AF_INET6 && set_dual_stack(s) == -1) {
                close(s);
                s = -1;
                continue;
            }
            if (-1 == bind(s, p->ai_addr, p->ai_addrlen)) {
                close(s);
                s = -1;
            }
        }
    }
    return s;
}

static int create_bound_socket(const char *portStr, int addressFamily, int dualStack) {
    int s = -1;
    struct addrinfo hints;
    struct addrinfo *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = addressFamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    const int ret = getaddrinfo(NULL, portStr, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "[DM] local getaddrinfo failed port=%s family=%d ret=%d %s\n",
                portStr, addressFamily, ret, gai_strerror(ret));
        return -1;
    }

    s = find_and_bind_to_address(res, dualStack);

    freeaddrinfo(res);

    return s;
}

static int create_dual_stack_socket(const char *portStr) {
    return create_bound_socket(portStr, AF_INET6, 1);
}

int lwm2m_create_socket(const char *portStr, int addressFamily) {
    if (addressFamily == AF_UNSPEC) {
        int s = create_dual_stack_socket(portStr);
        if (s >= 0) {
            return s;
        }
        return create_bound_socket(portStr, AF_INET, 0);
    }

    const int dualStack = (addressFamily == AF_INET6);
    return create_bound_socket(portStr, addressFamily, dualStack);
}

static int get_socket_family(int sock) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(sock, (struct sockaddr *)&addr, &len) == -1) {
        return AF_UNSPEC;
    }
    return addr.ss_family;
}

static int remote_family_matches_socket(int socketFamily, int remoteFamily) {
    if (socketFamily == AF_INET6) {
        return remoteFamily == AF_INET || remoteFamily == AF_INET6;
    }
    return socketFamily == remoteFamily;
}

static void map_ipv4_to_ipv6(const struct sockaddr_in *src, struct sockaddr_in6 *dst) {
    memset(dst, 0, sizeof(*dst));
    dst->sin6_family = AF_INET6;
    dst->sin6_port = src->sin_port;
    dst->sin6_addr.s6_addr[10] = 0xff;
    dst->sin6_addr.s6_addr[11] = 0xff;
    memcpy(&dst->sin6_addr.s6_addr[12], &src->sin_addr, sizeof(src->sin_addr));
}

static int copy_remote_for_socket(int socketFamily,
                                  const struct sockaddr *remote,
                                  socklen_t remoteLen,
                                  struct sockaddr_storage *out,
                                  socklen_t *outLen) {
    memset(out, 0, sizeof(*out));
    if (socketFamily == AF_INET && remote->sa_family == AF_INET) {
        memcpy(out, remote, remoteLen);
        *outLen = remoteLen;
        return 0;
    }

    if (socketFamily == AF_INET6 && remote->sa_family == AF_INET6) {
        memcpy(out, remote, remoteLen);
        *outLen = remoteLen;
        return 0;
    }

    if (socketFamily == AF_INET6 && remote->sa_family == AF_INET) {
        map_ipv4_to_ipv6((const struct sockaddr_in *)remote, (struct sockaddr_in6 *)out);
        *outLen = sizeof(struct sockaddr_in6);
        return 0;
    }

    return -1;
}

lwm2m_connection_t *lwm2m_connection_find(lwm2m_connection_t *connList, struct sockaddr_storage *addr, size_t addrLen) {
    lwm2m_connection_t *connP;

    connP = connList;
    while (connP != NULL) {
        if ((connP->addrLen == addrLen) && (memcmp(&(connP->addr), addr, addrLen) == 0)) { // NOSONAR
            return connP;
        }
        connP = connP->next;
    }

    return connP;
}

lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, int sock, struct sockaddr *addr,
                                                  size_t addrLen) {
    lwm2m_connection_t *connP;

    connP = (lwm2m_connection_t *)lwm2m_malloc(sizeof(lwm2m_connection_t));
    if (connP != NULL) {
        connP->sock = sock;
        memcpy(&(connP->addr), addr, addrLen);
        connP->addrLen = addrLen;
        connP->next = connList;
    }

    return connP;
}

lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, int sock, char *host, char *port,
                                            int addressFamily) {
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    lwm2m_connection_t *connP = NULL;
    const int socketFamily = get_socket_family(sock);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addressFamily;
    hints.ai_socktype = SOCK_DGRAM;

    const int ret = getaddrinfo(host, port, &hints, &servinfo);
    if (ret != 0 || servinfo == NULL) {
        fprintf(stderr, "[DM] remote getaddrinfo failed host=%s port=%s family=%d ret=%d %s\n",
                host, port, addressFamily, ret, gai_strerror(ret));
        return NULL;
    }

    for (struct addrinfo *p = servinfo; p != NULL && connP == NULL; p = p->ai_next) {
        if (!remote_family_matches_socket(socketFamily, p->ai_family)) {
            continue;
        }

        int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) {
            continue;
        }

        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            fprintf(stderr, "[DM] probe connect failed host=%s port=%s family=%d errno=%d %s\n",
                    host, port, p->ai_family, errno, strerror(errno));
            close(s);
            continue;
        }

        struct sockaddr_storage remote;
        socklen_t remoteLen = 0;
        if (copy_remote_for_socket(socketFamily, p->ai_addr, p->ai_addrlen, &remote, &remoteLen) == 0) {
            connP = lwm2m_connection_new_incoming(connList, sock, (struct sockaddr *)&remote, remoteLen);
        }
        close(s);
    }
    if (NULL != servinfo) {
        freeaddrinfo(servinfo);
    }

    return connP;
}

void lwm2m_connection_free(lwm2m_connection_t *connList) {
    while (connList != NULL) {
        lwm2m_connection_t *nextP;

        nextP = connList->next;
        lwm2m_free(connList);

        connList = nextP;
    }
}

static int get_address_and_port(const lwm2m_connection_t *connP, char *str, size_t str_len, in_port_t *port) {
    if (AF_INET == connP->addr.sin6_family) {
        struct sockaddr_in *saddr = (struct sockaddr_in *)&connP->addr;
        inet_ntop(saddr->sin_family, &saddr->sin_addr, str, INET6_ADDRSTRLEN);
        *port = saddr->sin_port;
    } else if (AF_INET6 == connP->addr.sin6_family) {
        struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)&connP->addr;
        inet_ntop(saddr->sin6_family, &saddr->sin6_addr, str, INET6_ADDRSTRLEN);
        *port = saddr->sin6_port;
    } else {
        return -1;
    }
    return 0;
}

int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length) {
    int nbSent;
    size_t offset;

    char s[INET6_ADDRSTRLEN];
    in_port_t port;

    s[0] = 0;

    int ret = get_address_and_port(connP, s, INET6_ADDRSTRLEN, &port);
    if (ret < 0) {
        return ret;
    }

    fprintf(stderr, "Sending %zu bytes to [%s]:%hu\r\n", length, s, ntohs(port));

    output_buffer(stderr, buffer, length, 0);

    offset = 0;
    while (offset != length) {
        nbSent =
            sendto(connP->sock, buffer + offset, length - offset, 0, (struct sockaddr *)&(connP->addr), connP->addrLen);
        if (nbSent == -1)
            return -1;
        offset += nbSent;
    }
    return 0;
}

uint8_t lwm2m_buffer_send(void *sessionH, uint8_t *buffer, size_t length, void *userdata) {
    lwm2m_connection_t *connP = (lwm2m_connection_t *)sessionH;

    (void)userdata; /* unused */

    if (connP == NULL) {
        fprintf(stderr, "#> failed sending %zu bytes, missing connection\r\n", length);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    if (-1 == lwm2m_connection_send(connP, buffer, length)) {
        fprintf(stderr, "#> failed sending %zu bytes\r\n", length);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    return COAP_NO_ERROR;
}

bool lwm2m_session_is_equal(void *session1, void *session2, void *userData) {
    (void)userData; /* unused */

    return (session1 == session2);
}

void lwm2m_session_remove(void *session_h) { (void)session_h; /* unused */ }
