#include	"TPBase/TPSocket/TPSocket.h"

socket_t
tp_udp_server(const port_t port)
{
	socket_t listenfd;
	struct sockaddr_in my_addr;
	int yes;

	listenfd = tp_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == listenfd)
	{
		return INVALID_SOCKET;
	}

	yes = 1;
	if (INVALID_SOCKET
	    == tp_setsockopt(listenfd,
	                     SOL_SOCKET,
	                     SO_REUSEADDR,
	                     (char *) &yes,
	                     sizeof(int)) )
	{
		goto failure_ez_dup_listen_port_t;
	}

	bzero(&my_addr, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (INVALID_SOCKET
	    == tp_bind(listenfd, (SA *)&my_addr, sizeof(SA)) )
	{
		goto failure_ez_dup_listen_port_t;
	}
	else
	{
		return listenfd;
	}

failure_ez_dup_listen_port_t:
	tp_close(listenfd);
	return INVALID_SOCKET;
}

/**
 *  - 
 * @ : 
 * @ :
 * Desc
 */
socket_t
tp_udp_client(const char * hostip, const port_t port, SA *saptr, socklen_t *lenp)
{
	socket_t socketfd;
	struct sockaddr_in servaddr;

	socketfd = tp_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == socketfd)
	{
		return INVALID_SOCKET;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, hostip, &servaddr.sin_addr) <= 0)
	{
		goto tp_udp_client_failure;
	}

	if (NULL == saptr)
	{
		goto tp_udp_client_failure;
	}

	memcpy(saptr, &servaddr, sizeof(servaddr));
	
	if (NULL != lenp)
	{
		*lenp = sizeof(servaddr);
	}
	
	return socketfd;

tp_udp_client_failure:
	tp_close(socketfd);
	return INVALID_SOCKET;
}

/**
 *  - 
 * @ : 
 * @ :
 * Desc
 *	For a connected udp socket:
 *	we can call sendto for a connected UDP socket, 
 *	 but we cannot specify a destination address. 
 *	 The fifth argument to sendto (the pointer to the 
 *	 socket address structure) must be a null pointer, 
 *	 and the sixth argument (the size of the socket 
 *	 address structure) should be 0
 */
socket_t
tp_udp_connect(const char * hostip, const port_t port)
{
	socket_t socketfd;
	struct sockaddr_in servaddr;

	socketfd = tp_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == socketfd)
	{
		return INVALID_SOCKET;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, hostip, &servaddr.sin_addr) <= 0)
	{
		goto tp_udp_connect_failure;
	}

	if (INVALID_SOCKET 
		== tp_connect(socketfd, (SA *)&servaddr, sizeof(servaddr)))
	{
		goto tp_udp_connect_failure;
	}

	return socketfd;

tp_udp_connect_failure:
	tp_close(socketfd);
	return INVALID_SOCKET;
}

int
mcast_set_loop(socket_t sockfd, unsigned char onoff)
{
#ifndef WIN32
	unsigned char flag;

	flag = onoff;
	return(setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP,
	                  &flag, sizeof(flag)));
#else 
	return 0;
#endif
}
