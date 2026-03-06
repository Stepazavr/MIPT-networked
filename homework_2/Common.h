#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <winsock2.h>

struct ClientInfo
{
	std::string endpoint;
	sockaddr_in addr;
	std::chrono::steady_clock::time_point lastPong;
};

enum class Command
{
	None,
	All,
	Whisper,
	Duel,
	Answer,
	Pong,
	Quit
};

struct WhisperData
{
	unsigned short port;
	std::string text;
};

struct ParsedMessage
{
	Command command;
	std::variant<std::monostate, int, WhisperData, std::string> data;
};

enum class TransportPacketType
{
	Raw,
	Message,
	Ack
};

struct TransportPacket
{
	TransportPacketType type = TransportPacketType::Raw;
	std::uint64_t id = 0;
	std::string payload;
};

inline std::string BuildMessagePacket(std::uint64_t id, const std::string& payload)
{
	return "MSG|" + std::to_string(id) + "|" + payload;
}

inline std::string BuildAckPacket(std::uint64_t id)
{
	return "ACK|" + std::to_string(id);
}

inline bool ParseTransportPacket(const std::string& raw, TransportPacket& out)
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
