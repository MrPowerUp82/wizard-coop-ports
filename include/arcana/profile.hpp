#pragma once
#include "arcana/game.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
namespace arcana {
struct DailyRecord { std::string key; int phase{},loop{}; double time{}; bool victory{}; };
struct Profile {
  int coins{}, invested{}; MetaRanks upgrades;
  std::unordered_map<std::string,std::unordered_set<std::string>> codex;
  DailyRecord daily;
};
Profile loadProfile(const std::string& path);
bool saveProfile(const Profile& p,const std::string& path);
int nextUpgradeCost(const std::string& id,int rank);
bool buyUpgrade(Profile& p,const std::string& id);
int respec(Profile& p);
void deposit(Profile& p,int amount);
void observeCodex(Profile& p,const GameState& state,const Player* me=nullptr);
bool saveDailyIfBetter(Profile& p,const DailyRecord& candidate);
}
