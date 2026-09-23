#pragma once
#include "arcana/game.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
namespace arcana {
// Menu choices remembered between sessions (the web client keeps them in localStorage).
struct Preferences { std::array<int,cfg::MAX_PLAYERS> characters{0,1,2,3}; std::string campaign{"quick"}, weapon; int special{}; bool muted{};
  std::string name, server; }; // online: player name and a server URL overriding the default
struct DailyRecord { std::string key; int phase{},loop{}; double time{}; bool victory{}; };
// Local unlocks, like the web client's localStorage flags: discovering the secret (Developer) and
// clearing the Classic ritual (Aurora Guardian). Not tied to any account.
struct Unlocks { bool developer{}, aurora{}; };
struct Profile {
  int coins{}, invested{}; MetaRanks upgrades;
  Unlocks unlocks;
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
// Standard mages are always playable; the Developer and the Aurora Guardian once unlocked.
bool characterAvailable(const Unlocks& unlocks,int color);
// Unlocks the Aurora Guardian on a Classic victory. True only the first time (src/achievements.js).
bool recordVictory(Profile& p,const GameState& s);
}
