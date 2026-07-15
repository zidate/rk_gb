#include "demo_public.h"

cmiot_int32_t demo_connect_nonb(cmiot_int32_t sockfd, struct sockaddr *saptr, socklen_t salen, cmiot_int32_t timeout)
{
    cmiot_int32_t flags = 0;
    fd_set		  rset, wset;
    socklen_t     len = 0;
    struct timeval tval;

    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    cmiot_int32_t n = 0;
    cmiot_int32_t error = 0;
    if((n = connect(sockfd, saptr, salen)) < 0)
    {
        if(errno != EINPROGRESS)
        {
            DEMO_PRINT("connect failed, errno %d, %s\n", errno, strerror(errno));
            return -1;
        }
    }

    if(n == 0)
    {
        goto DONE;
    }

    FD_ZERO(&rset);
    FD_SET(sockfd, &rset);
    wset = rset;
	tval.tv_sec = timeout;
	tval.tv_usec = 0;

    if((n = select(sockfd + 1, &rset, &wset, NULL, &tval)) == 0)
    {
        DEMO_PRINT("select failed, errno %d, %s\n", errno, strerror(errno));
        errno = ETIMEDOUT;
        return -1;
    }

    if(FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset))
    {
        len = sizeof(error);
        if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
        {
            return -1;
        }
    }
    else
    {
        DEMO_PRINT("select error: sockfd not set\n");
    }

DONE:
    fcntl(sockfd, F_SETFL, flags);

	if (error) 
	{
		errno = error;
		return -1;
	}
	return 0;
}


cmiot_int32_t demo_tcp_connect(cmiot_char_t *ip, cmiot_uint32_t port)
{
    cmiot_int32_t sockfd;

    struct addrinfo *res = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    cmiot_char_t portStr[8] = {0};
    snprintf(portStr, sizeof(portStr) - 1, "%d", port);

    cmiot_int32_t ret = 0;
    if((ret = getaddrinfo(ip, portStr, &hints, &res)) != 0)
    {
        DEMO_PRINT("getaddrinfo error for %s, %s:%s\n", ip, portStr, gai_strerror(ret));
        return -1;
    }

    do
    {
        if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
        {
            continue;
        }
        struct linger linger_opt;
        linger_opt.l_onoff = 1;     /* cause RST to be sent on close() */
        linger_opt.l_linger = 0;

        if(setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt)) != 0)
        {
            DEMO_PRINT("setsockopt SO_LINGER failed, errno %d, %s\n", errno, strerror(errno));
            close(sockfd);
            sockfd = -1;
            continue;
        }

        if(demo_connect_nonb(sockfd, res->ai_addr, res->ai_addrlen, DEMO_KEEPALIVE_CONNECT_MAX_TIME_S) != 0)
        {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        else
        {
            freeaddrinfo(res);
            return sockfd;
        }
    }while((res = res->ai_next) != NULL);

    DEMO_PRINT("create socket failed!\n");
    freeaddrinfo(res);
    return -1;
}

cmiot_int32_t demo_tcp_writen(cmiot_int32_t fd, cmiot_char_t *cptr, cmiot_int32_t nsize)
{
    cmiot_int32_t	nleft;
	ssize_t		    nwritten;
	cmiot_char_t    *tmpPtr;

	tmpPtr = cptr;
	nleft = nsize;
	while (nleft > 0) 
    {
		if((nwritten = write(fd, (cmiot_uint8_t*)tmpPtr, nleft)) <= 0) 
        {
			if (nwritten < 0 && errno == EINTR)
				nwritten = 0;
			else
				return -1;
		}

		nleft -= nwritten;
		tmpPtr += nwritten;
	}
	return nsize;
}

cmiot_int32_t demo_tcp_readn(cmiot_int32_t fd, cmiot_char_t *cptr, cmiot_int32_t nsize)
{
    cmiot_int32_t	nleft;
	ssize_t	        nread;
	cmiot_char_t	*tmpPtr;

	tmpPtr = cptr;
	nleft = nsize;
	while (nleft > 0) 
    {
		if((nread = read(fd, (cmiot_uint8_t*)tmpPtr, nleft)) < 0) 
        {
			if (errno == EINTR)
				nread = 0;
			else
				return -1;
		} 
        else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		tmpPtr += nread;
	}
	return nsize - nleft;
}