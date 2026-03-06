#define WIN32_LEAN_AND_MEAN

#include <cstring>
#include <iostream>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// #pragma comment(lib, "ws2_32.lib")

#include "socket_tools.h"

// https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket
static int get_dgram_socket(addrinfo* addr, bool should_bind, addrinfo* res_addr)
{
	for (addrinfo* ptr = addr; ptr != nullptr; ptr = ptr->ai_next)
	{
		if (ptr->ai_family != AF_INET || ptr->ai_socktype != SOCK_DGRAM || ptr->ai_protocol != IPPROTO_UDP)
			continue;

		SOCKET sfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (sfd == INVALID_SOCKET)
			continue;

		u_long mode = 1;
		ioctlsocket(sfd, FIONBIO, &mode);

		int true_val = 1;
		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&true_val, sizeof(int));

		if (res_addr)
			*res_addr = *ptr;
		if (!should_bind)
			return (int)sfd;

		if (bind(sfd, ptr->ai_addr, ptr->ai_addrlen) == 0)
			return (int)sfd;

		closesocket(sfd);
	}
	return -1;
}

int create_dgram_socket(const char* address, const char* port, addrinfo* res_addr)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));

	bool is_listener = !address;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (is_listener)
		hints.ai_flags = AI_PASSIVE;

	addrinfo* result = nullptr;
	if (getaddrinfo(address, port, &hints, &result) != 0)
		return -1;

	int sfd = get_dgram_socket(result, is_listener, res_addr);

	return sfd;
}

int create_server(const char* port)
{
	return create_dgram_socket(nullptr, port, nullptr);
}

int create_client(const char* address, const char* port, addrinfo* res_addr)
{
	return create_dgram_socket(address, port, res_addr);
}



// https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup
bool WSA::wsa_initialized = false;

WSA::WSA()
{
	if (!wsa_initialized)
	{
		WSADATA wsa_data;
		int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
		if (result != 0)
		{
			std::cout << "Failed to startup WSA" << std::endl;
			return;
		}
		if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion != 2))
		{
			std::cout << "Failed to find a v2 WSA version" << std::endl;
			return;
		}

		wsa_initialized = true;
	}
}

WSA::~WSA()
{
	if (wsa_initialized)
	{
		WSACleanup();
		wsa_initialized = false;
	}
}
