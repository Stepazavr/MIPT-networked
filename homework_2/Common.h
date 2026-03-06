#pragma once

#include <chrono>
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
