#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "MessageManager.h"
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

MessageManager messageManager;
sockaddr_in serverAddr{};
bool isConnected = false;
const auto heartbeatInterval = std::chrono::seconds(10);

void SetConnectionState(bool connected, const std::string& message)
{
	if (isConnected == connected)
		return;

	isConnected = connected;
	std::cout << "\r" << message << "\n"
			  << "> " << buffered_msg << std::flush;
}

void receive_messages()
{
	constexpr size_t buf_size = 1000;
	char buffer[buf_size];
	auto lastHeartbeat = std::chrono::steady_clock::now();

	while (running)
	{
		auto now = std::chrono::steady_clock::now();
		if (now - lastHeartbeat >= heartbeatInterval)
		{
			messageManager.SendReliable(serverAddr, "PING");
			lastHeartbeat = now;
		}

		int droppedCount = messageManager.ProcessReliableRetries(now);
		if (droppedCount > 0)
		{
			SetConnectionState(false, "[NET] server does not respond");
		}

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

		std::string raw(buffer, buffer + num_bytes);
		TransportPacket packet;
		if (!messageManager.ParseTransportPacket(raw, packet))
			continue;

		std::string endpoint = MessageManager::MakeEndpoint(socket_in);

		if (packet.type == TransportPacketType::Ack)
		{
			bool ackAccepted = messageManager.HandleIncomingAck(endpoint, packet.id);
			if (ackAccepted)
			{
				SetConnectionState(true, "[NET] connected successfully");
			}
			continue;
		}

		std::string payload;
		if (packet.type == TransportPacketType::Message)
		{
			messageManager.SendAck(socket_in, packet.id);
			if (!messageManager.RegisterIncomingMessageId(endpoint, packet.id, packet.payload))
				continue;
			payload = packet.payload;
		}
		else
		{
			payload = packet.payload;
		}

		std::cout << "\rServer: " << payload << "\n";
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
			messageManager.SendReliable(serverAddr, buffered_msg);
			running = false;
			std::cout << "\nExiting...\n";
		}
		else if (!buffered_msg.empty())
		{
			std::cout << "\rYou sent: " << buffered_msg << "\n";
			messageManager.SendReliable(serverAddr, buffered_msg);

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

	messageManager.SetSocket(sfd);
	memcpy(&serverAddr, addr_info.ai_addr, sizeof(sockaddr_in));

	std::cout << "ChatClient - Type '/quit' to exit\n";

	const char* readyMsg = "READY";
	messageManager.SendReliable(serverAddr, readyMsg);

	std::cout << "Trying to connect server\n"
			  << "> ";

	std::thread receiver(receive_messages);
	while (running)
	{
		handle_input();
	}

	receiver.join();
	return 0;
}
