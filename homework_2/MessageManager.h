#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Common.h"

class MessageManager
{
public:
	Command ParseCommand(const std::string& msg) const;
	ParsedMessage ParseMessage(const std::string& msg) const;
	void SendMessage(const sockaddr_in& addr, const std::string& msg);
	void SendRawMessage(const sockaddr_in& addr, const std::string& msg) const;
	void SendAck(const sockaddr_in& addr, std::uint64_t packetId) const;
	void HandleIncomingAck(const std::string& endpoint, std::uint64_t packetId);
	void ProcessReliableRetries(std::chrono::steady_clock::time_point now);
	void HandleMessage(const std::string& endpoint, const std::string& msg, std::chrono::steady_clock::time_point now);

private:
	struct PendingReliablePacket
	{
		sockaddr_in addr;
		std::string endpoint;
		std::string packet;
		std::chrono::steady_clock::time_point lastSend;
		int retries = 0;
	};

	void SendReliableMessage(const std::string& endpoint, const sockaddr_in& addr, const std::string& msg);
	std::string ExtractPrefixedPayload(const std::string& msg, const std::string& prefix) const;
	bool ExtractWhisperArgs(const std::string& msg, unsigned short& port, std::string& payload) const;
	bool TryParseAnswer(const std::string& msg, int& value) const;

	std::uint64_t nextPacketId = 1;
	std::unordered_map<std::uint64_t, PendingReliablePacket> pendingPackets;
	const std::chrono::milliseconds ackTimeout{700};
	const int maxRetries = 5;

	const std::unordered_map<std::string, Command> commandMap = {{"PONG", Command::Pong}, {"/quit", Command::Quit}};
};
