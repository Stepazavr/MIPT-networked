#include "MessageManager.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <variant>
#include <vector>
#include <winsock2.h>

#include "Common.h"

MessageManager::MessageManager(int socketFdValue)
	: socketFd(socketFdValue)
{
}

void MessageManager::SetSocket(int socketFdValue)
{
	socketFd = socketFdValue;
}

std::string MessageManager::MakeEndpoint(const sockaddr_in& addr)
{
	std::string ip = inet_ntoa(addr.sin_addr);
	unsigned short hostPort = ntohs(addr.sin_port);
	return ip + ":" + std::to_string(hostPort);
}

bool MessageManager::ParseTransportPacket(const std::string& raw, TransportPacket& out) const
{
	if (raw.rfind("ACK|", 0) == 0)
	{
		std::string idPart = raw.substr(4);
		if (idPart.empty())
			return false;

		try
		{
			out.id = std::stoull(idPart);
		}
		catch (...)
		{
			return false;
		}

		out.type = TransportPacketType::Ack;
		out.payload.clear();
		return true;
	}

	if (raw.rfind("MSG|", 0) == 0)
	{
		size_t idStart = 4;
		size_t idEnd = raw.find('|', idStart);
		if (idEnd == std::string::npos)
			return false;

		std::string idPart = raw.substr(idStart, idEnd - idStart);
		if (idPart.empty())
			return false;

		try
		{
			out.id = std::stoull(idPart);
		}
		catch (...)
		{
			return false;
		}

		out.type = TransportPacketType::Message;
		out.payload = raw.substr(idEnd + 1);
		return true;
	}

	out.type = TransportPacketType::Raw;
	out.id = 0;
	out.payload = raw;
	return true;
}

std::string MessageManager::BuildMessagePacket(std::uint64_t id, const std::string& payload)
{
	return "MSG|" + std::to_string(id) + "|" + payload;
}

std::string MessageManager::BuildAckPacket(std::uint64_t id)
{
	return "ACK|" + std::to_string(id);
}

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
	if (socketFd < 0)
		return;

	sendto((SOCKET)socketFd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr, sizeof(sockaddr_in));
}

void MessageManager::SendAck(const sockaddr_in& addr, std::uint64_t packetId) const
{
	std::string ack = BuildAckPacket(packetId);
	SendRawMessage(addr, ack);
}

void MessageManager::SendReliableInternal(const std::string& endpoint, const sockaddr_in& addr, const std::string& msg)
{
	std::lock_guard<std::mutex> lock(pendingMutex);
	std::uint64_t packetId = nextPacketId++;
	std::string packet = BuildMessagePacket(packetId, msg);
	SendRawMessage(addr, packet);
	pendingPackets[packetId] = PendingReliablePacket{addr, endpoint, packet, std::chrono::steady_clock::now(), 0};
}

void MessageManager::SendReliable(const sockaddr_in& addr, const std::string& msg)
{
	SendReliableInternal(MakeEndpoint(addr), addr, msg);
}

bool MessageManager::HandleIncomingAck(const std::string& endpoint, std::uint64_t packetId)
{
	std::lock_guard<std::mutex> lock(pendingMutex);
	auto it = pendingPackets.find(packetId);
	if (it == pendingPackets.end())
		return false;

	if (it->second.endpoint != endpoint)
		return false;

	pendingPackets.erase(it);
	return true;
}

int MessageManager::ProcessReliableRetries(std::chrono::steady_clock::time_point now)
{
	std::lock_guard<std::mutex> lock(pendingMutex);
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

	return (int)toDrop.size();
}
