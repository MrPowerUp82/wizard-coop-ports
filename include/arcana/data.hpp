#pragma once
#include "arcana/real.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
namespace arcana {
struct EnemyDef { std::string name, behavior, sprite, shotSprite; real hp{},speed{},damage{},xp{},size{},radius{}; };
struct PhaseDef { std::string name,floor,color,boss,bossName; std::array<std::string,2> enemies; };
struct CampaignDef { std::string name; real seconds{},xp{},coins{},bossHp{}; bool endless{}; };
struct PowerDef { std::string title,kind; int max{},color{-1},minLevel{}; std::unordered_map<std::string,int> requirements; };
const std::unordered_map<std::string,EnemyDef>& enemyDefs();
const std::array<PhaseDef,6>& phases();
const std::unordered_map<std::string,CampaignDef>& campaigns();
const std::unordered_map<std::string,PowerDef>& powerDefs();
}
