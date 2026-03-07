#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Common.h"

class MessageManager
{
public:
	MessageManager() = default;
	explicit MessageManager(int socketFd);

	void SetSocket(int socketFd);
	static std::string MakeEndpoint(const sockaddr_in& addr);
	bool ParseTransportPacket(const std::string& raw, TransportPacket& out) const;

	Command ParseCommand(const std::string& msg) const;
	ParsedMessage ParseMessage(const std::string& msg) const;
	void SendReliable(const sockaddr_in& addr, const std::string& msg);
	void SendRawMessage(const sockaddr_in& addr, const std::string& msg) const;
	void SendAck(const sockaddr_in& addr, std::uint64_t packetId) const;
	bool HandleIncomingAck(const std::string& endpoint, std::uint64_t packetId);
	int ProcessReliableRetries(std::chrono::steady_clock::time_point now);
	bool RegisterIncomingMessageId(const std::string& endpoint, std::uint64_t packetId, const std::string& payload);
	void ClearIncomingMessageIdsForEndpoint(const std::string& endpoint);

private:
	struct PendingReliablePacket
	{
		sockaddr_in addr;
		std::string endpoint;
		std::string packet;
		std::string payload;
		std::chrono::steady_clock::time_point lastSend;
		int retries = 0;
	};

	void SendReliableInternal(const std::string& endpoint, const sockaddr_in& addr, const std::string& msg);
	std::string ExtractPrefixedPayload(const std::string& msg, const std::string& prefix) const;
	bool ExtractWhisperArgs(const std::string& msg, unsigned short& port, std::string& payload) const;
	bool TryParseAnswer(const std::string& msg, int& value) const;
	static std::string BuildMessagePacket(std::uint64_t id, const std::string& payload);
	static std::string BuildAckPacket(std::uint64_t id);

	int socketFd = -1;
	std::uint64_t nextPacketId = 1;
	std::unordered_map<std::uint64_t, PendingReliablePacket> pendingPackets;
	mutable std::mutex pendingMutex;
	std::unordered_set<std::string> receivedMessageIds;
	mutable std::mutex receivedIdsMutex;
	const std::chrono::milliseconds ackTimeout{700};
	const int maxRetries = 5;

	const std::unordered_map<std::string, Command> commandMap = {{"PONG", Command::Pong}, {"/quit", Command::Quit}};
};
