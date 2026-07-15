#include	"TPBase/TPSocket/TPSocket.h"

socket_t tp_socket(int family, int type, int protocol)
{
	socket_t		n;

	n = socket(family, type, protocol);

	if (INVALID_SOCKET == n)
	{
		ez_err_sys("socket error");
	}

	return n;
}

int tp_bind(socket_t fd, const SA *sa, socklen_t salen)
{
	socket_t n;

	n = bind(fd, sa, salen);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("bind error");
	}

	return n;
}

int tp_listen(socket_t fd, int backlog)
{
	int n;

	n = listen(fd, backlog);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("listen error");
	}

	return n;
}

socket_t tp_accept(socket_t fd, SA *sa, socklen_t *salenptr)
{
	socket_t		n;

	n = accept(fd, sa, salenptr);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("accept error");
	}

	return n;
}

int tp_connect(socket_t fd, const SA *sa, socklen_t salen)
{
	int n;

	n = connect(fd, sa, salen);
	if (SOCKET_ERROR == n)
	{
		ez_err_sys("connect error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_socket_init(void)
{
#if defined (WIN32)
	WSADATA 	wsaData;
	ushort		wVersionRequested;

	wVersionRequested = MAKEUINT16(2,2);
	if (__socket_initialized == 1)
	{
		return 0;
	}

	if (WSAStartup(wVersionRequested, &wsaData) != 0)
	{
		return -1;
	}

	if (wsaData.wVersion != wVersionRequested)
	{
		WSACleanup();
		return -1;
	}

	__socket_initialized = 1;
#endif

	return 0;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_socket_cleanup(void)
{

#ifdef WIN32
	if (__socket_initialized == 0)
	{
		return 0;
	}

	if (WSACleanup () != 0)
	{
		return -1;
	}

	__socket_initialized = 0;
#endif

	return 0;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_close(socket_t fd)
{
#if (defined (WINCE) || defined (WIN32) || defined(PSOS))
	return closesocket(fd);
#else

	return close(fd);
#endif
}

int tp_close_socket(socket_t *fd)
{
	int i;

	i = tp_close(*fd);
	*fd = INVALID_SOCKET;

	return i;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/
int tp_gethostbyname(const char * pName, struct in_addr *pAddr)
{
#ifdef WIN32
	struct hostent *pHost = NULL;
	pHost = gethostbyname(pName);
	if (pHost != NULL)
	{
		pAddr->s_addr = *(unsigned long *)pHost->h_addr_list[0];
	}
	else
	{
		return -1;
	}
	return 0;
#elif defined(USE_DNS_SRC)
	return tp_gethostbyname2(pName, pAddr);
#else
	struct hostent HostInfo = {0};
	struct hostent *pHost = NULL;
	int iError = -1;
	char cBuf[2048] = {0};
	struct in_addr	sin_addr;

	if (pName == NULL || pAddr == NULL)
	{
		return -1;
	}

	res_init();
	if(0 == gethostbyname_r(pName, &HostInfo, cBuf, sizeof(cBuf), &pHost, &iError))
	{
		if (pHost != NULL)
		{
			pAddr->s_addr = *(unsigned long *)HostInfo.h_addr_list[0];
		}
		else if (inet_pton(AF_INET, pName, &sin_addr) > 0) 
		{
			*pAddr = sin_addr;
		}
		else
		{
			return -1;
			//pAddr->s_addr = inet_addr(pName);
		}
		return 0;
	}
	else
	{
		ez_err_sys("gethostbyname error, iError=%d\n", iError);
		return -1;
	}
#endif
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_getpeername(socket_t fd, SA *sa, socklen_t *salenptr)
{
	int n;

	n = getpeername(fd, sa, salenptr);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("getpeername error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_getsockname(socket_t fd, SA *sa, socklen_t *salenptr)
{
	int n;

	n = getsockname(fd, sa, salenptr);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("getsockname error");
	}

	return n;
}

int	tp_gethostname(char *name, size_t len)
{
	return gethostname(name, len);
}

int	tp_sethostname(char *name, size_t len)
{
#ifndef WIN32
	return sethostname(name, len);
#else
	return 0;
#endif
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_getsockopt(socket_t fd, int level, int optname, void *optval, socklen_t *optlenptr)
{
	int n;

	n = getsockopt(fd, level, optname, optval, optlenptr);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("getsockopt error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_setsockopt(socket_t fd, int level, int optname, const void *optval, socklen_t optlen)
{
	int n;

	n = setsockopt(fd, level, optname, optval, optlen);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("setsockopt error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_shutdown(socket_t fd, int how)
{
	int n;

	n = shutdown(fd, how);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("shutdown error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
          struct timeval *timeout)
{
	int		n;

	n = select(nfds, readfds, writefds, exceptfds, timeout);

	if (SOCKET_ERROR == n)
	{
		ez_err_sys("select error");
	}

	return n;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/
int tp_set_nodelay(socket_t s)
{
	const int optval = 1;

	if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
	               (char *)&optval, sizeof(optval)) < 0)
	{
		ez_err_sys("error setsockopt nodelay");
		return -1;
	}

	return 0;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_set_nonblock(int bFlag, socket_t s)
{
#ifdef WIN32
	ulong l = bFlag ? 1 : 0;
	int n = ioctlsocket(s, FIONBIO, &l);
	if (n != 0)
	{
		int errcode;
		errcode = WSAGetLastError();
		ez_err_sys("ioctlsocket(FIONBIO) (%d) ", errcode);
		return -1;
	}
#else
	int flags = 0;
	if ((flags = fcntl(s, F_GETFL, 0)) == -1)
	{
		ez_err_sys("fcntl(F_GETFL, O_NONBLOCK)");
		return -1;
	}

	if (bFlag)
	{
		if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1)
		{
			ez_err_sys("fcntl(F_SETFL, O_NONBLOCK)");
			return -1;
		}
	}
	else
	{
		if (fcntl(s, F_SETFL, 0) == -1)
		{
			ez_err_sys("fcntl(F_SETFL, 0)");
			return -1;
		}
	}
#endif

	return 0;
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/
#ifndef __trip
#define __trip printf("-W-%s(%d)\n", __FILE__, __LINE__);
#endif
#ifndef __fline
#define __fline printf("%s(%d)--", __FILE__, __LINE__);
#endif

int tp_writen(socket_t fd, const void *vptr, size_t n)
{
	size_t		nleft;
	int		nwritten;
	char	*ptr;

	ptr = (char *)vptr;
	nleft = n;

	while (nleft > 0)
	{
		nwritten = send(fd, ptr, nleft, 0);

		if ( nwritten < 0)
		{
			int ierrno = 0;
#ifdef WIN32
			ierrno = WSAGetLastError();
#else
			ierrno = errno;
#endif
			if (EWOULDBLOCK == ierrno)
			{
				nwritten = 0;		/* and call write() again */
				//__trip;
				continue;
			}
			else if (EINTR == ierrno)
			{
				__trip;
			}
			else
			{
				perror ("send");
				return -1;
			}
		}
		else if (0 == nwritten)
		{
			__trip;
			break;
		}
		else
		{
			nleft -= nwritten;
			ptr   += nwritten;
		}
	}

	return (ptr-(char *)vptr);
}

/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/

int tp_writeton( socket_t fd, const void *vptr, size_t n, const SA *saRemote, int struct_size)
{
	size_t nleft    = 0;
	int nwritten    = 0;
	const char *ptr = NULL;
	ptr = vptr;

	if(NULL == vptr || NULL == saRemote || NULL == ptr)
	{
		return -1;
	}

	nleft = n;
	while (nleft > 0)
	{
		if ( (nwritten = sendto(fd, (char *)ptr, nleft, 0, (SA *)saRemote, struct_size)) <= 0)
		{
			int ierrno = 0;
#ifdef WIN32
			ierrno = WSAGetLastError();
#else
			ierrno = errno;
#endif
			if (nwritten < 0 && EINTR == ierrno)
			{
				nwritten = 0;// and call write() again
			}
			else
			{
				perror ("send");
				return(-1);// error
			}
		}

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

int tp_socketpair(int family, int type, int protocol, int fd[2])
{
#ifndef WIN32
	return socketpair(family, type, protocol, fd);
#else
	/* This code is originally from Tor.  Used with permission. */

	/* This socketpair does not work when localhost is down. So
	* it's really not the same thing at all. But it's close enough
	* for now, and really, when localhost is down sometimes, we
	* have other problems too.
	*/
	int listener = -1;
	int connector = -1;
	int acceptor = -1;
	struct sockaddr_in listen_addr;
	struct sockaddr_in connect_addr;
	int size;
	int saved_errno = -1;

	if (protocol
#ifdef AF_UNIX
		|| family != AF_UNIX
#endif
		) {
			return -1;
	}
	if (!fd) {
		return -1;
	}

	listener = socket(AF_INET, type, 0);
	if (listener < 0)
		return -1;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listen_addr.sin_port = 0;	/* kernel chooses port.	 */
	if (bind(listener, (struct sockaddr *) &listen_addr, sizeof (listen_addr))
		== -1)
		goto tidy_up_and_fail;
	if (listen(listener, 1) == -1)
		goto tidy_up_and_fail;

	connector = socket(AF_INET, type, 0);
	if (connector < 0)
		goto tidy_up_and_fail;
	/* We want to find out the port number to connect to.  */
	size = sizeof(connect_addr);
	if (getsockname(listener, (struct sockaddr *) &connect_addr, &size) == -1)
		goto tidy_up_and_fail;
	if (size != sizeof (connect_addr))
		goto abort_tidy_up_and_fail;
	if (connect(connector, (struct sockaddr *) &connect_addr,
		sizeof(connect_addr)) == -1)
		goto tidy_up_and_fail;

	size = sizeof(listen_addr);
	acceptor = accept(listener, (struct sockaddr *) &listen_addr, &size);
	if (acceptor < 0)
		goto tidy_up_and_fail;
	if (size != sizeof(listen_addr))
		goto abort_tidy_up_and_fail;
	closesocket(listener);
	/* Now check we are talking to ourself by matching port and host on the
	two sockets.	 */
	if (getsockname(connector, (struct sockaddr *) &connect_addr, &size) == -1)
		goto tidy_up_and_fail;
	if (size != sizeof (connect_addr)
		|| listen_addr.sin_family != connect_addr.sin_family
		|| listen_addr.sin_addr.s_addr != connect_addr.sin_addr.s_addr
		|| listen_addr.sin_port != connect_addr.sin_port)
		goto abort_tidy_up_and_fail;
	fd[0] = connector;
	fd[1] = acceptor;

	return 0;

abort_tidy_up_and_fail:
	saved_errno = WSAECONNABORTED;
tidy_up_and_fail:
	if (saved_errno < 0)
		saved_errno = WSAGetLastError();
	if (listener != -1)
		close(listener);
	if (connector != -1)
		close(connector);
	if (acceptor != -1)
		close(acceptor);

	saved_errno = -1;
	return -1;
#endif
}
/*-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-*/
