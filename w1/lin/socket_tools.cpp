#include "socket_tools.h"

#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>

// https://linux.die.net/man/3/getaddrinfo
static int get_dgram_socket(addrinfo* addr, bool isServer, addrinfo* resAddr)
{
	for (addrinfo* ptr = addr; ptr != nullptr; ptr = ptr->ai_next)
	{
		if (ptr->ai_family != AF_INET || ptr->ai_socktype != SOCK_DGRAM || ptr->ai_protocol != IPPROTO_UDP)
			continue;

		int socketFd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (socketFd == -1)
			continue;

		fcntl(socketFd, F_SETFL, O_NONBLOCK);

		int trueVal = 1;
		setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &trueVal, sizeof(int));

		if (resAddr)
			*resAddr = *ptr;
		if (!isServer)
			return socketFd;

		if (bind(socketFd, ptr->ai_addr, ptr->ai_addrlen) == 0)
			return socketFd;

		close(socketFd);
	}
	return -1;
}

int create_dgram_socket(const char* address, const char* port, addrinfo* resAddr)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));

	bool isServer = !address;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (isServer)
		hints.ai_flags = AI_PASSIVE;

	addrinfo* addrResult = nullptr;
	if (getaddrinfo(address, port, &hints, &addrResult) != 0)
		return -1;

	int socketFd = get_dgram_socket(addrResult, isServer, resAddr);

	// freeaddrinfo(result);
	return socketFd;
}

int create_server(const char* port)
{
	return create_dgram_socket(nullptr, port, nullptr); // 2026
}

int create_client(const char* address, const char* port, addrinfo* resAddr)
{
	return create_dgram_socket(address, port, resAddr); // localhost:2026
}
