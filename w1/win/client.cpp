#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "socket_tools.h"

std::atomic<bool> running{true};

std::string buffered_msg;
// clang-format off
std::vector<std::string> messages = {
    "https://youtu.be/dQw4w9WgXcQ?si=Ox1lQULEyKpisqd",
    "Never gonna give you up",
    "Never gonna let you down",
    "Never gonna run around and desert you",
    "Never gonna make you cry",
    "Never gonna say goodbye",
    "Never gonna tell a lie and hurt you",
    "..."};
// clang-format on

const char* port = "2026";
addrinfo addr_info;
int sfd;

void receive_messages()
{
	constexpr size_t buf_size = 1000;
	char buffer[buf_size];

	while (running)
	{
		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET((SOCKET)sfd, &read_set);
		timeval timeout = {0, 100000}; // 100 ms
		select((int)sfd + 1, &read_set, NULL, NULL, &timeout);

		if (!FD_ISSET((SOCKET)sfd, &read_set))
			continue;

		memset(buffer, 0, buf_size);
		sockaddr_in socket_in;
		int socket_len = sizeof(sockaddr_in);
		int num_bytes = recvfrom((SOCKET)sfd, buffer, buf_size - 1, 0, (sockaddr*)&socket_in, &socket_len);
		if (num_bytes <= 0)
			continue;

		if (std::string(buffer) == "PING")
		{
			const char* pong_msg = "PONG";
			sendto(sfd, pong_msg, (int)strlen(pong_msg), 0, addr_info.ai_addr, addr_info.ai_addrlen);
			continue;
		}

		std::cout << "\rServer: " << buffer << "\n";
		std::cout << "> " << buffered_msg << std::flush;
	}
}

void handle_input()
{
	char ch = std::cin.get();
	bool is_printable = ch >= 32 && ch <= 126;
	bool is_backspace = ch == 127 || ch == '\b';
	bool is_enter = ch == '\n';

	if (is_printable)
	{
		buffered_msg += ch;
		std::cout << ch << std::flush;
	}
	else if (is_backspace)
	{
		if (!buffered_msg.empty())
		{
			buffered_msg.pop_back();
			std::cout << "\b \b" << std::flush;
		}
	}
	else if (is_enter)
	{
		if (buffered_msg == "/quit")
		{
			sendto(sfd, buffered_msg.c_str(), buffered_msg.size(), 0, addr_info.ai_addr, addr_info.ai_addrlen);
			running = false;
			std::cout << "\nExiting...\n";
		}
		else if (!buffered_msg.empty())
		{
			std::cout << "\rYou sent: " << buffered_msg << "\n";
			int res =
				sendto(sfd, buffered_msg.c_str(), buffered_msg.size(), 0, addr_info.ai_addr, addr_info.ai_addrlen);
			if (res == SOCKET_ERROR)
			{
				int err = WSAGetLastError();
				std::cout << "Error: " << err << std::endl;
			}

			buffered_msg.clear();
			std::cout << "> " << std::flush;
		}
	}
}

int main(int argc, const char** argv)
{
	std::unique_ptr<WSA> wsa = std::make_unique<WSA>();
	if (!wsa->is_initialized())
	{
		std::cout << "Failed to initialize WSA\n";
		return 1;
	}

	sfd = create_client("localhost", port, &addr_info);
	if (sfd == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}

	const char* readyMsg = "READY";
	sendto(sfd, readyMsg, (int)strlen(readyMsg), 0, addr_info.ai_addr, addr_info.ai_addrlen);

	std::cout << "ChatClient - Type '/quit' to exit\n"
			  << "> ";

	std::thread receiver(receive_messages);
	while (running)
	{
		handle_input();
	}

	receiver.join();
	return 0;
}
