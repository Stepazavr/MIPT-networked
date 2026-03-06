#include "DuelManager.h"

#include <cstring>
#include <iostream>
#include <winsock2.h>

#include "Common.h"
#include "MessageManager.h"

extern std::unordered_map<std::string, ClientInfo> clients;
extern MessageManager messageManager;

void DuelManager::GenerateDuelTask(std::string& task, int& answer) const
{
	auto randomInt = [](int minVal, int maxVal)
	{
		return minVal + (rand() % (maxVal - minVal + 1));
	};

	int x = randomInt(10, 100);
	int y = randomInt(10, 100);
	int z = randomInt(10, 100);
	bool plus = randomInt(0, 1) == 1;
	answer = plus ? (x * y + z) : (x * y - z);
	char op = plus ? '+' : '-';
	task = std::to_string(x) + "*" + std::to_string(y) + " " + op + " " + std::to_string(z);
}

void DuelManager::CleanupDuel(DuelIterator duelIt)
{
	if (duelIt == duels.end())
		return;
	duelByClient.erase(duelIt->firstPlayer);
	duelByClient.erase(duelIt->secondPlayer);
	if (pendingDuel == duelIt)
		pendingDuel = duels.end();
	duels.erase(duelIt);
}

void DuelManager::HandleDuelRequest(const std::string& endpoint)
{
	if (duelByClient.find(endpoint) != duelByClient.end())
	{
		auto it = clients.find(endpoint);
		if (it != clients.end())
			messageManager.SendMessage(it->second.addr, "[DUEL] You are already in a duel.");
		return;
	}

	if (pendingDuel == duels.end())
	{
		duels.emplace_back();
		auto newDuelIt = std::prev(duels.end());
		newDuelIt->firstPlayer = endpoint;
		duelByClient[endpoint] = newDuelIt;
		pendingDuel = newDuelIt;

		auto it = clients.find(endpoint);
		if (it != clients.end())
			messageManager.SendMessage(it->second.addr, "[DUEL] Waiting for opponent...");
		return;
	}

	DuelIterator duelIt = pendingDuel;
	duelIt->secondPlayer = endpoint;
	duelByClient[endpoint] = duelIt;
	GenerateDuelTask(duelIt->task, duelIt->answer);

	std::string msg = "[DUEL] Task: " + duelIt->task + " ?";
	auto itA = clients.find(duelIt->firstPlayer);
	if (itA != clients.end())
		messageManager.SendMessage(itA->second.addr, msg);
	auto itB = clients.find(duelIt->secondPlayer);
	if (itB != clients.end())
		messageManager.SendMessage(itB->second.addr, msg);

	pendingDuel = duels.end();
}

bool DuelManager::IsPlayerInDuel(const std::string& endpoint) const
{
	return duelByClient.find(endpoint) != duelByClient.end();
}

void DuelManager::HandleDuelAnswer(const std::string& endpoint, int answerValue)
{
	auto it = duelByClient.find(endpoint);
	if (it == duelByClient.end())
	{
		auto clientIt = clients.find(endpoint);
		if (clientIt != clients.end())
			messageManager.SendMessage(clientIt->second.addr, "[DUEL] You are not in a duel.");
		return;
	}

	DuelIterator duelIt = it->second;
	if (answerValue != duelIt->answer)
	{
		auto clientIt = clients.find(endpoint);
		if (clientIt != clients.end())
			messageManager.SendMessage(clientIt->second.addr, "[DUEL] Wrong answer.");
		return;
	}

	std::string win = endpoint + " is the winner!";
	auto itA = clients.find(duelIt->firstPlayer);
	if (itA != clients.end())
		messageManager.SendMessage(itA->second.addr, win);
	auto itB = clients.find(duelIt->secondPlayer);
	if (itB != clients.end())
		messageManager.SendMessage(itB->second.addr, win);

	CleanupDuel(duelIt);
}

void DuelManager::CleanupDuelByPlayer(const std::string& endpoint, std::string& opponent)
{
	auto duelIt = duelByClient.find(endpoint);
	if (duelIt != duelByClient.end())
	{
		DuelIterator activeDuel = duelIt->second;
		if (activeDuel != duels.end())
		{
			opponent = (activeDuel->firstPlayer == endpoint) ? activeDuel->secondPlayer : activeDuel->firstPlayer;
		}
		CleanupDuel(activeDuel);
	}
}
