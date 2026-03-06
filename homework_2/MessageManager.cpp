#include "MessageManager.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <variant>
#include <vector>
#include <winsock2.h>

#include "Common.h"
#include "DuelManager.h"

extern std::unordered_map<std::string, ClientInfo> clients;
extern int sfd;
extern DuelManager duelManager;

namespace
{
	std::string MakeEndpointFromAddr(const sockaddr_in& addr)
	{
		std::string ip = inet_ntoa(addr.sin_addr);
		unsigned short hostPort = ntohs(addr.sin_port);
		return ip + ":" + std::to_string(hostPort);
	}
} // namespace

std::string MessageManager::ExtractPrefixedPayload(const std::string& msg, const std::string& prefix) const
{
	if (msg.rfind(prefix, 0) != 0)
		return "";

	size_t pos = prefix.size();
	if (msg.size() > pos + 1 && msg[pos] == ' ')
		return msg.substr(pos + 1);
	else if (msg.size() == pos)
		return "";
	else
		return msg.substr(pos);
}

bool MessageManager::ExtractWhisperArgs(const std::string& msg, unsigned short& port, std::string& payload) const
{
	std::string tail = ExtractPrefixedPayload(msg, "/w");
	if (tail.empty())
		return false;

	size_t portEnd = 0;
	while (portEnd < tail.size() && std::isdigit(static_cast<unsigned char>(tail[portEnd])))
		++portEnd;

	if (portEnd == 0)
		return false;

	unsigned long parsedPort = 0;
	try
	{
		parsedPort = std::stoul(tail.substr(0, portEnd));
	}
	catch (...)
	{
		return false;
	}

	if (parsedPort > 65535)
		return false;

	port = static_cast<unsigned short>(parsedPort);
	if (portEnd < tail.size() && tail[portEnd] == ' ')
		payload = tail.substr(portEnd + 1);
	else
		payload = "";

	return true;
}

bool MessageManager::TryParseAnswer(const std::string& msg, int& value) const
{
	std::string payload = ExtractPrefixedPayload(msg, "/answer");
	if (payload.empty())
		return false;

	for (char ch : payload)
	{
		if (!std::isdigit(static_cast<unsigned char>(ch)))
			return false;
	}

	value = std::stoi(payload);
	return true;
}

Command MessageManager::ParseCommand(const std::string& msg) const
{
	auto it = commandMap.find(msg);
	if (it != commandMap.end())
		return it->second;

	if (msg.rfind("/all", 0) == 0)
		return Command::All;
	if (msg.rfind("/w", 0) == 0)
		return Command::Whisper;
	if (msg.rfind("/duel", 0) == 0)
		return Command::Duel;
	if (msg.rfind("/answer", 0) == 0)
		return Command::Answer;

	return Command::None;
}

ParsedMessage MessageManager::ParseMessage(const std::string& msg) const
{
	Command cmd = ParseCommand(msg);
	ParsedMessage result{cmd, std::monostate()};

	switch (cmd)
	{
		case Command::Answer:
		{
			int value = 0;
			if (TryParseAnswer(msg, value))
				result.data = value;
			break;
		}
		case Command::Whisper:
		{
			unsigned short port = 0;
			std::string payload;
			if (ExtractWhisperArgs(msg, port, payload))
				result.data = WhisperData{port, payload};
			break;
		}
		case Command::All:
		{
			std::string payload = ExtractPrefixedPayload(msg, "/all");
			result.data = payload;
			break;
		}
		case Command::Pong:
		case Command::Quit:
		case Command::Duel:
		case Command::None:
		default:
			break;
	}

	return result;
}

void MessageManager::SendRawMessage(const sockaddr_in& addr, const std::string& msg) const
{
	sendto((SOCKET)sfd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr, sizeof(sockaddr_in));
}

void MessageManager::SendAck(const sockaddr_in& addr, std::uint64_t packetId) const
{
	std::string ack = BuildAckPacket(packetId);
	SendRawMessage(addr, ack);
}

void MessageManager::SendReliableMessage(const std::string& endpoint, const sockaddr_in& addr, const std::string& msg)
{
	std::uint64_t packetId = nextPacketId++;
	std::string packet = BuildMessagePacket(packetId, msg);
	SendRawMessage(addr, packet);
	pendingPackets[packetId] = PendingReliablePacket{addr, endpoint, packet, std::chrono::steady_clock::now(), 0};
}

void MessageManager::SendMessage(const sockaddr_in& addr, const std::string& msg)
{
	SendReliableMessage(MakeEndpointFromAddr(addr), addr, msg);
}

void MessageManager::HandleIncomingAck(const std::string& endpoint, std::uint64_t packetId)
{
	auto it = pendingPackets.find(packetId);
	if (it == pendingPackets.end())
		return;

	if (it->second.endpoint != endpoint)
		return;

	pendingPackets.erase(it);
}

void MessageManager::ProcessReliableRetries(std::chrono::steady_clock::time_point now)
{
	std::vector<std::uint64_t> toDrop;
	for (auto& entry : pendingPackets)
	{
		PendingReliablePacket& pending = entry.second;
		if (now - pending.lastSend < ackTimeout)
			continue;

		if (pending.retries >= maxRetries)
		{
			std::cout << "[NET][LOSS] no ACK from " << pending.endpoint << " for packet " << entry.first
					  << ", drop after " << maxRetries << " retries" << std::endl;
			toDrop.push_back(entry.first);
			continue;
		}

		SendRawMessage(pending.addr, pending.packet);
		pending.lastSend = now;
		++pending.retries;
		std::cout << "[NET][LOSS] retry packet " << entry.first << " to " << pending.endpoint << " (attempt "
				  << pending.retries << ")" << std::endl;
	}

	for (std::uint64_t packetId : toDrop)
	{
		pendingPackets.erase(packetId);
	}
}

void MessageManager::HandleMessage(
	const std::string& endpoint, const std::string& msg, std::chrono::steady_clock::time_point now)
{
	switch (ParseCommand(msg))
	{
		case Command::Pong:
		{
			auto pongIt = clients.find(endpoint);
			if (pongIt != clients.end())
				pongIt->second.lastPong = now;
			break;
		}
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
						SendMessage(it->second.addr, winMsg);
				}
			}
			clients.erase(endpoint);
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
				SendMessage(clientIt->second.addr, "[DUEL] You are not in a duel.");
				break;
			}

			ParsedMessage parsedMessage = ParseMessage(msg);
			if (std::holds_alternative<int>(parsedMessage.data))
			{
				duelManager.HandleDuelAnswer(endpoint, std::get<int>(parsedMessage.data));
			}
			else
			{
				SendMessage(clientIt->second.addr, "[DUEL] Invalid answer format.");
			}
			break;
		}
		case Command::Whisper:
		{
			ParsedMessage parsedMessage = ParseMessage(msg);
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
					SendMessage(target->addr, out);
				}
				else if (senderIt != clients.end())
				{
					std::string out = "[ERROR] Client with port " + std::to_string(whisperData.port) + " not found";
					SendMessage(senderIt->second.addr, out);
				}
			}
			break;
		}
		case Command::All:
		{
			ParsedMessage parsedMessage = ParseMessage(msg);
			if (std::holds_alternative<std::string>(parsedMessage.data))
			{
				std::string payload = std::get<std::string>(parsedMessage.data);
				std::string out = "[ALL] " + endpoint + ": " + payload;
				for (auto& entry : clients)
				{
					if (entry.first == endpoint)
						continue;
					SendMessage(entry.second.addr, out);
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
