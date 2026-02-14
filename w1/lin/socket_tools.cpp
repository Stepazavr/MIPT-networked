#include "socket_tools.h"

#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>

// https://linux.die.net/man/3/getaddrinfo
static int get_dgram_socket(addrinfo* addr, bool is_server, addrinfo* res_addr)
{
	for (addrinfo* ptr = addr; ptr != nullptr; ptr = ptr->ai_next)
	{
		if (ptr->ai_family != AF_INET || ptr->ai_socktype != SOCK_DGRAM || ptr->ai_protocol != IPPROTO_UDP)
			continue;

		int sfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (sfd == -1)
			continue;

		fcntl(sfd, F_SETFL, O_NONBLOCK);

		int true_val = 1;
		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &true_val, sizeof(int));

		if (res_addr)
			*res_addr = *ptr;
		if (!is_server)
			return sfd;

		if (bind(sfd, ptr->ai_addr, ptr->ai_addrlen) == 0)
			return sfd;

		close(sfd);
	}
	return -1;
}

int create_dgram_socket(const char* address, const char* port, addrinfo* res_addr)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));

	bool is_server = !address;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (is_server)
		hints.ai_flags = AI_PASSIVE;

	addrinfo* result = nullptr;
	if (getaddrinfo(address, port, &hints, &result) != 0)
		return -1;

	int sfd = get_dgram_socket(result, is_server, res_addr);

	// freeaddrinfo(result);
	return sfd;
}

int create_server(const char* port)
{
	return create_dgram_socket(nullptr, port, nullptr); // 2026
}

int create_client(const char* address, const char* port, addrinfo* res_addr)
{
	return create_dgram_socket(address, port, res_addr); // localhost:2026
}
