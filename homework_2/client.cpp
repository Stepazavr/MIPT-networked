#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "Common.h"
#include "socket_tools.h"

std::atomic<bool> running{true};

std::string buffered_msg;
// clang-format off
std::vector<std::string> messages = {
    "https://youtu.be/dQw4w9WgXcQ?si=Ox1lQULEyKpisqd",
    "Never gonna give you up",
    "Never gonna let you down",
    "Never gonna run around and desert you",
    "Never gonna make you cry",
    "Never gonna say goodbye",
    "Never gonna tell a lie and hurt you",
    "..."};
// clang-format on

const char* port = "2026";
addrinfo addr_info;
int sfd;

struct PendingPacket
{
	std::string packet;
	std::chrono::steady_clock::time_point lastSend;
	int retries = 0;
};

std::atomic<std::uint64_t> nextPacketId{1};
std::unordered_map<std::uint64_t, PendingPacket> pendingPackets;
std::mutex pendingMutex;

constexpr std::chrono::milliseconds ackTimeout{700};
constexpr int maxRetries = 5;

void send_raw(const std::string& msg)
{
	sendto(sfd, msg.c_str(), (int)msg.size(), 0, addr_info.ai_addr, addr_info.ai_addrlen);
}

void send_ack(std::uint64_t packetId)
{
	send_raw(BuildAckPacket(packetId));
}

void send_reliable(const std::string& payload)
{
	std::uint64_t packetId = nextPacketId.fetch_add(1);
	std::string packet = BuildMessagePacket(packetId, payload);
	send_raw(packet);

	std::lock_guard<std::mutex> lock(pendingMutex);
	pendingPackets[packetId] = PendingPacket{packet, std::chrono::steady_clock::now(), 0};
}

void process_pending_retries()
{
	auto now = std::chrono::steady_clock::now();
	std::vector<std::uint64_t> toDrop;

	std::lock_guard<std::mutex> lock(pendingMutex);
	for (auto& entry : pendingPackets)
	{
		PendingPacket& pending = entry.second;
		if (now - pending.lastSend < ackTimeout)
			continue;

		if (pending.retries >= maxRetries)
		{
			std::cout << "\n[NET] delivery failed for packet " << entry.first << std::endl;
			toDrop.push_back(entry.first);
			continue;
		}

		send_raw(pending.packet);
		pending.lastSend = now;
		++pending.retries;
	}

	for (std::uint64_t packetId : toDrop)
	{
		pendingPackets.erase(packetId);
	}
}

void receive_messages()
{
	constexpr size_t buf_size = 1000;
	char buffer[buf_size];

	while (running)
	{
		process_pending_retries();

		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET((SOCKET)sfd, &read_set);
		timeval timeout = {0, 100000}; // 100 ms
		select((int)sfd + 1, &read_set, NULL, NULL, &timeout);

		if (!FD_ISSET((SOCKET)sfd, &read_set))
			continue;

		memset(buffer, 0, buf_size);
		sockaddr_in socket_in;
		int socket_len = sizeof(sockaddr_in);
		int num_bytes = recvfrom((SOCKET)sfd, buffer, buf_size - 1, 0, (sockaddr*)&socket_in, &socket_len);
		if (num_bytes <= 0)
			continue;

		std::string raw(buffer, buffer + num_bytes);
		TransportPacket packet;
		if (!ParseTransportPacket(raw, packet))
			continue;

		if (packet.type == TransportPacketType::Ack)
		{
			std::lock_guard<std::mutex> lock(pendingMutex);
			pendingPackets.erase(packet.id);
			continue;
		}

		std::string payload;
		if (packet.type == TransportPacketType::Message)
		{
			send_ack(packet.id);
			payload = packet.payload;
		}
		else
		{
			payload = packet.payload;
		}

		if (payload == "PING")
		{
			const char* pong_msg = "PONG";
			send_raw(pong_msg);
			continue;
		}

		std::cout << "\rServer: " << payload << "\n";
		std::cout << "> " << buffered_msg << std::flush;
	}
}

void handle_input()
{
	char ch = std::cin.get();
	bool is_printable = ch >= 32 && ch <= 126;
	bool is_backspace = ch == 127 || ch == '\b';
	bool is_enter = ch == '\n';

	if (is_printable)
	{
		buffered_msg += ch;
		std::cout << ch << std::flush;
	}
	else if (is_backspace)
	{
		if (!buffered_msg.empty())
		{
			buffered_msg.pop_back();
			std::cout << "\b \b" << std::flush;
		}
	}
	else if (is_enter)
	{
		if (buffered_msg == "/quit")
		{
			send_reliable(buffered_msg);
			running = false;
			std::cout << "\nExiting...\n";
		}
		else if (!buffered_msg.empty())
		{
			std::cout << "\rYou sent: " << buffered_msg << "\n";
			send_reliable(buffered_msg);

			buffered_msg.clear();
			std::cout << "> " << std::flush;
		}
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

	sfd = create_client("localhost", port, &addr_info);
	if (sfd == -1)
	{
		std::cout << "Failed to create a socket\n";
		return 1;
	}

	const char* readyMsg = "READY";
	send_raw(readyMsg);

	std::cout << "ChatClient - Type '/quit' to exit\n"
			  << "> ";

	std::thread receiver(receive_messages);
	while (running)
	{
		handle_input();
	}

	receiver.join();
	return 0;
}
