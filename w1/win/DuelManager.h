#pragma once

#include <chrono>
#include <list>
#include <string>
#include <unordered_map>

#include "Common.h"

struct Duel
{
	std::string firstPlayer;
	std::string secondPlayer;
	std::string task;
	int answer = 0;
};

using DuelIterator = std::list<Duel>::iterator;

class DuelManager
{
public:
	void HandleDuelRequest(const std::string& endpoint);
	void HandleDuelAnswer(const std::string& endpoint, int answerValue);
	void CleanupDuelByPlayer(const std::string& endpoint, std::string& opponent);

private:
	void GenerateDuelTask(std::string& task, int& answer) const;
	void CleanupDuel(DuelIterator duelIt);

	std::list<Duel> duels;
	std::unordered_map<std::string, DuelIterator> duelByClient;
	DuelIterator pendingDuel = duels.end();
};
