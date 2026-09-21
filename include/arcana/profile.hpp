#pragma once
#include "arcana/game.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
namespace arcana {
// Menu choices remembered between sessions (the web client keeps them in localStorage).
struct Preferences { std::array<int,cfg::MAX_PLAYERS> characters{0,1,2,3}; std::string campaign{"quick"}, weapon; int special{}; bool muted{}; };
struct DailyRecord { std::string key; int phase{},loop{}; double time{}; bool victory{}; };
struct Profile {
  int coins{}, invested{}; MetaRanks upgrades;
  std::unordered_map<std::string,std::unordered_set<std::string>> codex;
  DailyRecord daily;
  Preferences prefs;
};
Profile loadProfile(const std::string& path);
bool saveProfile(const Profile& p,const std::string& path);
// Writes to `path`.tmp then renames it over `path`, so a crash or power loss never leaves a torn save.
bool saveProfileAtomic(const Profile& p,const std::string& path);
int nextUpgradeCost(const std::string& id,int rank);
bool buyUpgrade(Profile& p,const std::string& id);
int respec(Profile& p);
void deposit(Profile& p,int amount);
void observeCodex(Profile& p,const GameState& state,const Player* me=nullptr);
bool saveDailyIfBetter(Profile& p,const DailyRecord& candidate);
}
