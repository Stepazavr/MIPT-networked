#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <ws2tcpip.h>

#include "Common.h"
#include "DuelManager.h"
#include "MessageManager.h"
#include "socket_tools.h"

const char* port = "2026";

std::unordered_map<std::string, ClientInfo> clients;

int sfd = -1;

DuelManager duelManager;
MessageManager messageManager;

std::string make_endpoint(const sockaddr_in& addr)
{
	std::string ip = inet_ntoa(addr.sin_addr);
	unsigned short hostPort = ntohs(addr.sin_port);
	return ip + ":" + std::to_string(hostPort);
}

void touch_client(const std::string& endpoint, const sockaddr_in& addr, std::chrono::steady_clock::time_point now)
{
	auto it = clients.find(endpoint);
	if (it == clients.end())
	{
		clients.emplace(endpoint, ClientInfo{endpoint, addr, now});
		std::cout << "Connected: " << endpoint << std::endl;
		return;
	}

	it->second.addr = addr;
}

const auto disconnectTimeout = std::chrono::seconds(30);
const auto pingInterval = std::chrono::seconds(5);

void sendPings(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point& lastPing,
	std::chrono::steady_clock::duration pingInterval)
{
	if (now - lastPing < pingInterval)
		return;

	for (auto& entry : clients)
	{
		const char* pingMsg = "PING";
		messageManager.SendMessage(entry.second.addr, pingMsg);
	}
	lastPing = now;
}

void handleTimeouts(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration disconnectTimeout)
{
	std::vector<std::string> toDisconnect;
	for (auto& entry : clients)
	{
		if (now - entry.second.lastPong > disconnectTimeout)
		{
			toDisconnect.push_back(entry.first);
		}
	}

	for (const auto& endpoint : toDisconnect)
	{
		std::cout << "Disconnected (timeout): " << endpoint << std::endl;
		std::string opponent;
		duelManager.CleanupDuelByPlayer(endpoint, opponent);
		if (!opponent.empty())
		{
			std::string winMsg = opponent + " is the winner! (Opponent disconnected)";
			auto it = clients.find(opponent);
			if (it != clients.end())
				messageManager.SendMessage(it->second.addr, winMsg);
		}
		clients.erase(endpoint);
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

	int socket = create_server(port);
	if (socket == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}
	sfd = socket;

	std::cout << "ChatServer - Listening!\n";

	auto lastPing = std::chrono::steady_clock::now();

	while (true)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(sfd, &readSet);
		timeval timeout = {0, 100000};
		select((int)sfd + 1, &readSet, NULL, NULL, &timeout);

		if (FD_ISSET((SOCKET)sfd, &readSet))
		{
			constexpr size_t bufSize = 1000;
			static char buffer[bufSize];
			memset(buffer, 0, bufSize);

			sockaddr_in socketIn;
			int socketLen = sizeof(sockaddr_in);
			int numBytes = recvfrom((SOCKET)sfd, buffer, bufSize - 1, 0, (sockaddr*)&socketIn, &socketLen);
			if (numBytes > 0)
			{
				std::string endpoint = make_endpoint(socketIn);
				auto now = std::chrono::steady_clock::now();

				touch_client(endpoint, socketIn, now);
				messageManager.HandleMessage(endpoint, std::string(buffer), now);
			}
		}

		auto now = std::chrono::steady_clock::now();
		sendPings(now, lastPing, pingInterval);
		handleTimeouts(now, disconnectTimeout);
	}
	return 0;
}
