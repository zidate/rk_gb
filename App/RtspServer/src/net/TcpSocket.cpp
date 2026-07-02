// PHZ
// 2018-5-15

#include "TcpSocket.h"
#include "Socket.h"
#include "SocketUtil.h"
#include "SocketCompat.h"
#include "Logger.h"

using namespace xop;

TcpSocket::TcpSocket(SOCKET sockfd)
    : sockfd_(sockfd)
{
    
}

TcpSocket::~TcpSocket()
{
	
}

SOCKET TcpSocket::Create()
{
#if RK_ENABLE_IPV6_SOCKET
	sockfd_ = protocol::socket_compat::CreateDualStackSocket(SOCK_STREAM);
#else
	sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
	return sockfd_;
}
bool TcpSocket::Bind(std::string ip, uint16_t port)
{
	if(!SocketUtil::Bind(sockfd_, ip, port)) {
		LOG_DEBUG(" <socket=%d> bind <%s:%u> failed.\n", sockfd_, ip.c_str(), port);
		return false;
	}

	return true;
}

bool TcpSocket::Listen(int backlog)
{
	if(::listen(sockfd_, backlog) == SOCKET_ERROR) {
		LOG_DEBUG("<socket=%d> listen failed.\n", sockfd_);
		return false;
	}

	return true;
}

SOCKET TcpSocket::Accept()
{
	struct sockaddr_in addr = {0};
	socklen_t addrlen = sizeof addr;

	SOCKET socket_fd = ::accept(sockfd_, (struct sockaddr*)&addr, &addrlen);
	return socket_fd;
}

bool TcpSocket::Connect(std::string ip, uint16_t port, int timeout)
{ 
	if(!SocketUtil::Connect(sockfd_, ip, port, timeout)) {
		LOG_DEBUG("<socket=%d> connect failed.\n", sockfd_);
		return false;
	}

	return true;
}

void TcpSocket::Close()
{
#if defined(__linux) || defined(__linux__) 
    ::close(sockfd_);
#elif defined(WIN32) || defined(_WIN32)
    closesocket(sockfd_);
#else
	
#endif
	sockfd_ = 0;
}

void TcpSocket::ShutdownWrite()
{
	shutdown(sockfd_, SHUT_WR);
	sockfd_ = 0;
}
