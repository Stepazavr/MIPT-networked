#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
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
	void SendOutOfOrderNotice(const sockaddr_in& addr, std::uint64_t gotId, std::uint64_t expectedId) const;
	void SendOutOfOrderNoticeWithLogging(
		const std::string& endpoint, const sockaddr_in& addr, std::uint64_t gotId, std::uint64_t expectedId) const;
	bool HandleIncomingAck(const std::string& endpoint, std::uint64_t packetId);
	int ProcessReliableRetries(std::chrono::steady_clock::time_point now);
	int ProcessReliableRetriesWithLogging(std::chrono::steady_clock::time_point now);
	void ClearOutgoingQueueForEndpoint(const std::string& endpoint);
	void ClearAllOutgoingQueues();
	bool IsIncomingMessageDuplicate(const std::string& endpoint, std::uint64_t packetId);
	bool IsIncomingMessageDuplicateWithLogging(
		const std::string& endpoint, std::uint64_t packetId, const std::string& payload);
	void RememberIncomingMessageId(const std::string& endpoint, std::uint64_t packetId);
	bool RegisterIncomingMessageId(const std::string& endpoint, std::uint64_t packetId, const std::string& payload);
	bool RegisterIncomingMessageIdWithLogging(
		const std::string& endpoint, std::uint64_t packetId, const std::string& payload);
	void ClearIncomingMessageIdsForEndpoint(const std::string& endpoint);

private:
	struct OutgoingReliablePacket
	{
		std::uint64_t id = 0;
		std::string packet;
		std::string payload;
	};

	struct EndpointOutgoingState
	{
		sockaddr_in addr{};
		std::uint64_t nextPacketId = 0;
		std::deque<OutgoingReliablePacket> queue;
		std::chrono::steady_clock::time_point lastRetrySend{};
		bool hasLastRetrySend = false;
	};

	void SendReliableInternal(const std::string& endpoint, const sockaddr_in& addr, const std::string& msg);
	void SendQueuedMessagesToEndpoint(
		const std::string& endpoint, EndpointOutgoingState& state, bool isRetry, bool shouldLog);
	int ProcessReliableRetriesInternal(std::chrono::steady_clock::time_point now, bool shouldLog);
	bool RegisterIncomingMessageIdInternal(
		const std::string& endpoint, std::uint64_t packetId, const std::string& payload);
	std::string ExtractPrefixedPayload(const std::string& msg, const std::string& prefix) const;
	bool ExtractWhisperArgs(const std::string& msg, unsigned short& port, std::string& payload) const;
	bool TryParseAnswer(const std::string& msg, int& value) const;
	static std::string BuildMessagePacket(std::uint64_t id, const std::string& payload);
	static std::string BuildAckPacket(std::uint64_t id);
	static std::string BuildOutOfOrderPayload(std::uint64_t gotId, std::uint64_t expectedId);

	int socketFd = -1;
	std::unordered_map<std::string, EndpointOutgoingState> outgoingByEndpoint;
	mutable std::mutex outgoingMutex;
	std::unordered_set<std::string> receivedMessageIds;
	mutable std::mutex receivedIdsMutex;
	const std::chrono::milliseconds retryInterval{1000};

	const std::unordered_map<std::string, Command> commandMap = {{"PONG", Command::Pong}, {"/quit", Command::Quit}};
};
