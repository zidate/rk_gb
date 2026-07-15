#ifndef __DEMO_SOCKET_H__
#define __DEMO_SOCKET_H__


#define DEMO_KEEPALIVE_CONNECT_MAX_TIME_S    (5)
#define DEMO_KEEPALIVE_SELECT_TIMEOUT_MS     (50)

typedef struct
{
    cmiotKeepaliveInfo_t *info;
    cmiot_bool_t stopFlag;
} demoKeepaliveThreadInfo_t;

cmiot_int32_t demo_connect_nonb(cmiot_int32_t sockfd, struct sockaddr *saptr, socklen_t salen, cmiot_int32_t timeout);
cmiot_int32_t demo_tcp_connect(cmiot_char_t *ip, cmiot_uint32_t port);
cmiot_int32_t demo_tcp_writen(cmiot_int32_t fd, cmiot_char_t *cptr, cmiot_int32_t nsize);
cmiot_int32_t demo_tcp_readn(cmiot_int32_t fd, cmiot_char_t *cptr, cmiot_int32_t nsize);

void *demo_keepalive_thread(void* args);

#endif