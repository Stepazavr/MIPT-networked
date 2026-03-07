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
const auto disconnectTimeout = std::chrono::seconds(30);

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

void handleTimeouts(std::chrono::steady_clock::time_point now)
{
	std::vector<std::string> toDisconnect;
	for (const auto& entry : clients)
	{
		if (now - entry.second.lastPong > disconnectTimeout)
		{
			toDisconnect.push_back(entry.first);
		}
	}

	for (const auto& endpoint : toDisconnect)
	{
		std::cout << "Disconnected (no PING timeout): " << endpoint << std::endl;
		std::string opponent;
		duelManager.CleanupDuelByPlayer(endpoint, opponent);
		if (!opponent.empty())
		{
			std::string winMsg = opponent + " is the winner! (Opponent disconnected)";
			auto it = clients.find(opponent);
			if (it != clients.end())
				messageManager.SendReliable(it->second.addr, winMsg);
		}
		clients.erase(endpoint);
		messageManager.ClearIncomingMessageIdsForEndpoint(endpoint);
		messageManager.ClearOutgoingQueueForEndpoint(endpoint);
	}
}

void handleClientMessage(const std::string& endpoint, const std::string& msg, std::chrono::steady_clock::time_point now)
{
	if (msg == "PING")
	{
		auto it = clients.find(endpoint);
		if (it != clients.end())
		{
			it->second.lastPong = now;
		}
		return;
	}

	switch (messageManager.ParseCommand(msg))
	{
		case Command::Pong:
			break;
		case Command::Quit:
			std::cout << "Disconnected: " << endpoint << std::endl;
			{
				std::string opponent;
				duelManager.CleanupDuelByPlayer(endpoint, opponent);
				if (!opponent.empty())
				{
					std::string winMsg = opponent + " is the winner! (Opponent disconnected)";
					auto it = clients.find(opponent);
					if (it != clients.end())
						messageManager.SendReliable(it->second.addr, winMsg);
				}
			}
			clients.erase(endpoint);
			messageManager.ClearIncomingMessageIdsForEndpoint(endpoint);
			messageManager.ClearOutgoingQueueForEndpoint(endpoint);
			break;
		case Command::Duel:
			duelManager.HandleDuelRequest(endpoint);
			break;
		case Command::Answer:
		{
			auto clientIt = clients.find(endpoint);
			if (clientIt == clients.end())
				break;

			if (!duelManager.IsPlayerInDuel(endpoint))
			{
				messageManager.SendReliable(clientIt->second.addr, "[DUEL] You are not in a duel.");
				break;
			}

			ParsedMessage parsedMessage = messageManager.ParseMessage(msg);
			if (std::holds_alternative<int>(parsedMessage.data))
			{
				duelManager.HandleDuelAnswer(endpoint, std::get<int>(parsedMessage.data));
			}
			else
			{
				messageManager.SendReliable(clientIt->second.addr, "[DUEL] Invalid answer format.");
			}
			break;
		}
		case Command::Whisper:
		{
			ParsedMessage parsedMessage = messageManager.ParseMessage(msg);
			if (std::holds_alternative<WhisperData>(parsedMessage.data))
			{
				WhisperData whisperData = std::get<WhisperData>(parsedMessage.data);
				ClientInfo* target = nullptr;
				for (auto& entry : clients)
				{
					if (ntohs(entry.second.addr.sin_port) == whisperData.port)
					{
						target = &entry.second;
						break;
					}
				}

				auto senderIt = clients.find(endpoint);
				if (target && senderIt != clients.end())
				{
					std::string out = "[W] " + endpoint + ": " + whisperData.text;
					messageManager.SendReliable(target->addr, out);
				}
				else if (senderIt != clients.end())
				{
					std::string out = "[ERROR] Client with port " + std::to_string(whisperData.port) + " not found";
					messageManager.SendReliable(senderIt->second.addr, out);
				}
			}
			break;
		}
		case Command::All:
		{
			ParsedMessage parsedMessage = messageManager.ParseMessage(msg);
			if (std::holds_alternative<std::string>(parsedMessage.data))
			{
				std::string payload = std::get<std::string>(parsedMessage.data);
				std::string out = "[ALL] " + endpoint + ": " + payload;
				for (auto& entry : clients)
				{
					if (entry.first == endpoint)
						continue;
					messageManager.SendReliable(entry.second.addr, out);
				}
			}
			break;
		}
		case Command::None:
		default:
			std::cout << "(" << endpoint << "): " << msg << std::endl;
			break;
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
	messageManager.SetSocket(sfd);

	std::cout << "ChatServer - Listening!\n";

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
				std::string endpoint = MessageManager::MakeEndpoint(socketIn);
				auto now = std::chrono::steady_clock::now();
				std::string rawMessage(buffer, buffer + numBytes);
				TransportPacket packet;

				touch_client(endpoint, socketIn, now);

				if (!messageManager.ParseTransportPacket(rawMessage, packet))
				{
					std::cout << "[NET] malformed packet from " << endpoint << std::endl;
					continue;
				}

				if (packet.type == TransportPacketType::Ack)
				{
					messageManager.HandleIncomingAck(endpoint, packet.id);
					continue;
				}

				if (packet.type == TransportPacketType::Message)
				{
					messageManager.SendAck(socketIn, packet.id);
					if (!messageManager.RegisterIncomingMessageId(endpoint, packet.id, packet.payload))
					{
						continue;
					}
					handleClientMessage(endpoint, packet.payload, now);
					continue;
				}

				handleClientMessage(endpoint, packet.payload, now);
			}
		}

		auto now = std::chrono::steady_clock::now();
		messageManager.ProcessReliableRetries(now);
		handleTimeouts(now);
	}
	return 0;
}
