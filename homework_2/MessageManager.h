#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "Common.h"

class MessageManager
{
public:
	Command ParseCommand(const std::string& msg) const;
	ParsedMessage ParseMessage(const std::string& msg) const;
	void SendMessage(const sockaddr_in& addr, const std::string& msg) const;
	void HandleMessage(const std::string& endpoint, const std::string& msg, std::chrono::steady_clock::time_point now);

private:
	std::string ExtractPrefixedPayload(const std::string& msg, const std::string& prefix) const;
	bool ExtractWhisperArgs(const std::string& msg, unsigned short& port, std::string& payload) const;
	bool TryParseAnswer(const std::string& msg, int& value) const;

	const std::unordered_map<std::string, Command> commandMap = {{"PONG", Command::Pong}, {"/quit", Command::Quit}};
};
