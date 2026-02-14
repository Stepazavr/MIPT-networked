#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <ws2tcpip.h>

#include "socket_tools.h"

const char* port = "2026";

struct ClientInfo
{
	std::string endpoint;
	sockaddr_in addr;
	std::chrono::steady_clock::time_point last_pong;
};

std::unordered_map<std::string, ClientInfo> clients;

int sfd = -1;

struct Duel
{
	std::string first_player;
	std::string second_player;
	std::string task;
	int answer = 0;

	void start()
	{
		std::string msg = "[DUEL] Task: " + task + " ?";
		auto it_a = clients.find(first_player);
		if (it_a != clients.end())
			sendto((SOCKET)::sfd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&it_a->second.addr, sizeof(sockaddr_in));
		auto it_b = clients.find(second_player);
		if (it_b != clients.end())
			sendto((SOCKET)::sfd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&it_b->second.addr, sizeof(sockaddr_in));
	}
};

std::list<Duel> duels;
using DuelIterator = std::list<Duel>::iterator;
std::unordered_map<std::string, DuelIterator> duel_by_client;
DuelIterator pending_duel = duels.end();

std::string make_endpoint(const sockaddr_in& addr)
{
	std::string ip = inet_ntoa(addr.sin_addr);
	unsigned short host_port = ntohs(addr.sin_port);
	return ip + ":" + std::to_string(host_port);
}

bool extract_prefixed_payload(const std::string& msg, const std::string& prefix, std::string& payload)
{
	if (msg.rfind(prefix, 0) != 0)
		return false;

	size_t pos = prefix.size();
	if (msg.size() > pos + 1 && msg[pos] == ' ')
		payload = msg.substr(pos + 1);
	else if (msg.size() == pos)
		payload = "";
	else
		payload = msg.substr(pos);

	return true;
}

bool extract_whisper_args(const std::string& msg, unsigned short& port, std::string& payload)
{
	std::string tail;
	if (!extract_prefixed_payload(msg, "/w", tail))
		return false;

	if (tail.empty())
		return false;

	size_t port_end = 0;
	while (port_end < tail.size() && std::isdigit(static_cast<unsigned char>(tail[port_end])))
		++port_end;

	if (port_end == 0)
		return false;

	unsigned long parsed_port = 0;
	try
	{
		parsed_port = std::stoul(tail.substr(0, port_end));
	}
	catch (...)
	{
		return false;
	}

	if (parsed_port > 65535)
		return false;

	port = static_cast<unsigned short>(parsed_port);
	if (port_end < tail.size() && tail[port_end] == ' ')
		payload = tail.substr(port_end + 1);
	else
		payload = "";

	return true;
}

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

const std::unordered_map<std::string, Command> command_map = {{"PONG", Command::Pong}, {"/quit", Command::Quit}};

Command parse_command(const std::string& msg)
{
	auto it = command_map.find(msg);
	if (it != command_map.end())
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

void sendToClient(const std::string& endpoint, const std::string& msg)
{
	auto it = clients.find(endpoint);
	if (it == clients.end())
		return;
	sendto((SOCKET)sfd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&it->second.addr, sizeof(sockaddr_in));
}

int random_int(int min_val, int max_val)
{
	return min_val + (rand() % (max_val - min_val + 1));
}

void generate_duel_task(std::string& task, int& answer)
{
	int x = random_int(10, 100);
	int y = random_int(10, 100);
	int z = random_int(10, 100);
	bool plus = random_int(0, 1) == 1;
	answer = plus ? (x * y + z) : (x * y - z);
	char op = plus ? '+' : '-';
	task = std::to_string(x) + "*" + std::to_string(y) + " " + op + " " + std::to_string(z);
}

bool try_parse_answer(const std::string& msg, int& value)
{
	std::string payload;
	if (!extract_prefixed_payload(msg, "/answer", payload))
		return false;

	size_t pos = 0;
	while (pos < payload.size() && std::isspace(static_cast<unsigned char>(payload[pos])))
		++pos;
	if (pos == payload.size())
		return false;

	size_t end = pos;
	if (payload[end] == '+' || payload[end] == '-')
		++end;
	if (end == payload.size())
		return false;

	while (end < payload.size() && std::isdigit(static_cast<unsigned char>(payload[end])))
		++end;

	try
	{
		value = std::stoi(payload.substr(pos, end - pos));
	}
	catch (...)
	{
		return false;
	}

	return true;
}

void cleanup_duel(DuelIterator duel_it)
{
	if (duel_it == duels.end())
		return;
	duel_by_client.erase(duel_it->first_player);
	duel_by_client.erase(duel_it->second_player);
	if (pending_duel == duel_it)
		pending_duel = duels.end();
	duels.erase(duel_it);
}

void disconnect_client(const std::string& endpoint)
{
	auto duel_it = duel_by_client.find(endpoint);
	if (duel_it != duel_by_client.end())
	{
		DuelIterator active_duel = duel_it->second;
		if (active_duel != duels.end())
		{
			std::string opponent =
				(active_duel->first_player == endpoint) ? active_duel->second_player : active_duel->first_player;

			std::string win_msg = opponent + " is the winner! (Opponent disconnected)";
			sendToClient(opponent, win_msg);
		}

		cleanup_duel(active_duel);
	}

	clients.erase(endpoint);
}

void handle_duel_request(const std::string& endpoint)
{
	if (duel_by_client.find(endpoint) != duel_by_client.end())
	{
		sendToClient(endpoint, "[DUEL] You are already in a duel.");
		return;
	}

	if (pending_duel == duels.end())
	{
		duels.emplace_back();
		auto new_duel_it = std::prev(duels.end());
		new_duel_it->first_player = endpoint;
		duel_by_client[endpoint] = new_duel_it;
		pending_duel = new_duel_it;
		sendToClient(endpoint, "[DUEL] Waiting for opponent...");
		return;
	}

	DuelIterator duel_it = pending_duel;
	duel_it->second_player = endpoint;
	duel_by_client[endpoint] = duel_it;
	generate_duel_task(duel_it->task, duel_it->answer);
	duel_it->start();
	pending_duel = duels.end();
}

void handle_duel_answer(const std::string& endpoint, const std::string& msg)
{
	auto it = duel_by_client.find(endpoint);
	if (it == duel_by_client.end())
	{
		sendToClient(endpoint, "[DUEL] You are not in a duel.");
		return;
	}

	int answer_value = 0;
	if (!try_parse_answer(msg, answer_value))
	{
		sendToClient(endpoint, "[DUEL] Invalid answer format.");
		return;
	}

	DuelIterator duel_it = it->second;
	if (answer_value != duel_it->answer)
	{
		sendToClient(endpoint, "[DUEL] Wrong answer.");
		return;
	}

	std::string win = endpoint + " is the winner!";
	sendToClient(duel_it->first_player, win);
	sendToClient(duel_it->second_player, win);
	cleanup_duel(duel_it);
}

void touch_client(std::unordered_map<std::string, ClientInfo>& clients, const std::string& endpoint,
	const sockaddr_in& addr, std::chrono::steady_clock::time_point now)
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

void handle_message(const std::string& endpoint, const std::string& msg, std::chrono::steady_clock::time_point now)
{
	switch (parse_command(msg))
	{
		case Command::Pong:
		{
			auto pong_it = clients.find(endpoint);
			if (pong_it != clients.end())
				pong_it->second.last_pong = now;
			break;
		}
		case Command::Quit:
			std::cout << "Disconnected: " << endpoint << std::endl;
			disconnect_client(endpoint);
			break;
		case Command::Whisper:
		{
			std::string payload;
			unsigned short target_port = 0;
			if (!extract_whisper_args(msg, target_port, payload))
				break;

			ClientInfo* target = nullptr;
			for (auto& entry : clients)
			{
				if (ntohs(entry.second.addr.sin_port) == target_port)
				{
					target = &entry.second;
					break;
				}
			}

			auto sender_it = clients.find(endpoint);
			if (target && sender_it != clients.end())
			{
				std::string out = "[W] " + endpoint + ": " + payload;
				sendToClient(endpoint, out);
			}
			else if (sender_it != clients.end())
			{
				std::string out = "[ERROR] Client with port " + std::to_string(target_port) + " not found";
				sendToClient(endpoint, out);
			}
			break;
		}
		case Command::Duel:
			handle_duel_request(endpoint);
			break;
		case Command::Answer:
			handle_duel_answer(endpoint, msg);
			break;
		case Command::All:
		{
			std::string payload;
			if (!extract_prefixed_payload(msg, "/all", payload))
				break;
			std::string out = "[ALL] " + endpoint + ": " + payload;
			for (auto& entry : clients)
			{
				if (entry.first == endpoint)
					continue;
				sendToClient(entry.first, out);
			}
			break;
		}
		case Command::None:
		default:
			std::cout << "(" << endpoint << "): " << msg << std::endl;
			break;
	}
}

const auto disconnect_timeout = std::chrono::seconds(6);
const auto ping_interval = std::chrono::seconds(2);

void send_pings(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point& last_ping,
	std::chrono::steady_clock::duration ping_interval)
{
	if (now - last_ping < ping_interval)
		return;

	for (auto& entry : clients)
	{
		const char* ping_msg = "PING";
		sendto((SOCKET)sfd, ping_msg, (int)strlen(ping_msg), 0, (sockaddr*)&entry.second.addr, sizeof(sockaddr_in));
	}
	last_ping = now;
}

void handle_timeouts(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration disconnect_timeout)
{
	std::vector<std::string> to_disconnect;
	for (auto& entry : clients)
	{
		if (now - entry.second.last_pong > disconnect_timeout)
		{
			to_disconnect.push_back(entry.first);
		}
	}

	for (const auto& endpoint : to_disconnect)
	{
		std::cout << "Disconnected (timeout): " << endpoint << std::endl;
		disconnect_client(endpoint);
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

	int srv_socket = create_server(port);
	if (srv_socket == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}
	sfd = srv_socket;

	std::cout << "ChatServer - Listening!\n";

	auto last_ping = std::chrono::steady_clock::now();

	while (true)
	{
		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET(sfd, &read_set);
		timeval timeout = {0, 100000}; // 100 ms
		select((int)sfd + 1, &read_set, NULL, NULL, &timeout);

		if (FD_ISSET((SOCKET)sfd, &read_set))
		{
			constexpr size_t buf_size = 1000;
			static char buffer[buf_size];
			memset(buffer, 0, buf_size);

			sockaddr_in socket_in;
			int socket_len = sizeof(sockaddr_in);
			int num_bytes = recvfrom((SOCKET)sfd, buffer, buf_size - 1, 0, (sockaddr*)&socket_in, &socket_len);
			if (num_bytes > 0)
			{
				std::string endpoint = make_endpoint(socket_in);
				auto now = std::chrono::steady_clock::now();

				touch_client(clients, endpoint, socket_in, now);
				handle_message(endpoint, std::string(buffer), now);
			}
		}

		auto now = std::chrono::steady_clock::now();
		send_pings(now, last_ping, ping_interval);
		handle_timeouts(now, disconnect_timeout);
	}
	return 0;
}
