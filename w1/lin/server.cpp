#include <arpa/inet.h>
#include <cstring>
#include <iostream>

#include "socket_tools.h"

const char* port = "2026";

int main(int argc, const char** argv)
{

	int sfd = create_server(port);
	if (sfd == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}

	std::cout << "ChatServer - Listening!\n";

	while (true)
	{
		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET(sfd, &read_set);
		timeval timeout = {0, 100000}; // 100 ms
		select(sfd + 1, &read_set, NULL, NULL, &timeout);

		if (FD_ISSET(sfd, &read_set))
		{
			constexpr size_t buf_size = 1000;
			static char buffer[buf_size];
			memset(buffer, 0, buf_size);

			sockaddr_in socket_in;
			socklen_t socket_len = sizeof(sockaddr_in);
			ssize_t num_bytes = recvfrom(sfd, buffer, buf_size - 1, 0, (sockaddr*)&socket_in, &socket_len);
			if (num_bytes > 0)
			{
				std::cout << "(" << inet_ntoa(socket_in.sin_addr) << ":" << socket_in.sin_port << "): " << buffer
						  << std::endl;
			}
		}
	}
	return 0;
}
