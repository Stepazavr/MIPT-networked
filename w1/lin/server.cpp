#include <arpa/inet.h>
#include <cstring>
#include <iostream>

#include "socket_tools.h"

const char* port = "2026";

int main(int argc, const char** argv)
{

	int socketFd = create_server(port);
	if (socketFd == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}

	std::cout << "ChatServer - Listening!\n";

	while (true)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socketFd, &readSet);
		timeval timeout = {0, 100000}; // 100 ms
		select(socketFd + 1, &readSet, NULL, NULL, &timeout);

		if (FD_ISSET(socketFd, &readSet))
		{
			constexpr size_t bufSize = 1000;
			static char buffer[bufSize];
			memset(buffer, 0, bufSize);

			sockaddr_in socketIn;
			socklen_t socketLen = sizeof(sockaddr_in);
			ssize_t numBytes = recvfrom(socketFd, buffer, bufSize - 1, 0, (sockaddr*)&socketIn, &socketLen);
			if (numBytes > 0)
			{
				std::cout << "(" << inet_ntoa(socketIn.sin_addr) << ":" << socketIn.sin_port << "): " << buffer
						  << std::endl;
			}
		}
	}
	return 0;
}
