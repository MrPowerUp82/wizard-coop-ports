#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "arcana/constants.hpp"
#include "arcana/native/static_vector.hpp"

namespace arcana {

constexpr double PI = 3.14159265358979323846;
struct Vec2 { double x{}, y{}; };
inline double distanceSq(Vec2 a, Vec2 b) { const auto dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy; }

class Random {
public:
  virtual ~Random() = default;
  virtual double next() = 0;
};
class DefaultRandom final : public Random {
  std::mt19937_64 gen_;
  std::uniform_real_distribution<double> dist_{0.0, 1.0};
public:
  explicit DefaultRandom(std::uint64_t seed = std::random_device{}()) : gen_(seed) {}
  double next() override { return dist_(gen_); }
};
class SeededRandom final : public Random {
  std::uint32_t a_;
public:
  explicit SeededRandom(std::uint32_t seed) : a_(seed) {}
  double next() override;
};

struct Input { double x{}, y{}; std::uint64_t seq{}; };
struct Stats { double damage{}, taken{}; int kills{}, revives{}; std::unordered_map<std::string,double> by; };
struct Familiar { double x{}, y{}, timer{0.4}; };

struct Player {
  std::string id, name;
  int color{};
  double x{}, y{}, hp{100}, maxHp{100}, xp{};
  int level{1}; bool alive{true}, connected{true};
  Input input{};
  double speed{190}, damage{14}, attackDelay{0.62}, attackCooldown{}, pickupRadius{105}, armor{};
  int projectiles{1};
  double hitCooldown{}, invulnerableFor{};
  std::unordered_map<std::string,int> powers;
  std::vector<std::string> pendingPowers;
  double powerTimer{}; int pendingChests{};
  double specialCharge{}, specialCooldown{};
  int coins{}; double coinFrac{}, coinMult{1}, xpMult{1}; int rerolls{1}, phoenix{};
  double dashFor{}, dashCooldown{}, dashX{}, dashY{1}, moveX{}, moveY{1}; std::uint64_t motionId{};
  double reviveProgress{}; std::string reviveBy, reviving;
  int castCount{}; double castAngle{}, orbitAngle{}; std::uint64_t inputSeq{};
  int specialVariant{}; double signalAt{-std::numeric_limits<double>::infinity()};
  double lifelinkAt{}, sanctuaryAt{}, auraTimer{}, chainTimer{}, runeTimer{};
  double shopProgress{};
  std::optional<Familiar> familiar;
  Stats stats;
};

struct Enemy {
  std::uint64_t id{}; std::string type; double x{},y{},hp{},maxHp{},age{};
  bool boss{}, elite{}, thief{}, minion{}, escaped{}, exploded{}, distant{};
  int stage{1};
  double slowFor{}, burningFor{}, rootFor{}, freezeFor{};
  std::string slowBy, burnBy, rootBy;
  double comboAt{-1}, touchCooldown{}, chargeTimer{}, windup{}, dash{}, dashAngle{}, dashSpeed{}, dashWarn{}, dashTimer{}, shootTimer{}, fuse{};
  double attackCooldown{2.5}, rangedCooldown{1.5};
  double vx{}, vy{}; bool hasVelocity{};
  std::array<double, cfg::MAX_PLAYERS> orbitHitUntil{};
};
struct Shot {
  double x{},y{},vx{},vy{},ttl{},damage{}; int color{}; int pierce{1};
  bool special{}, shard{}, boomerang{}, returning{}, fullmoon{};
  std::string owner; native::StaticVector<std::uint64_t, 16> hitIds;
};
struct EnemyShot { double x{},y{},vx{},vy{},ttl{},damage{},radius{12}; std::string sprite; };
struct Drop { std::uint64_t id{}; double x{},y{},value{},ttl{24}; std::string type{"gem"}, pull; bool dead{}; };
struct Hazard { double x{},y{},radius{},warning{1.3},ttl{1.65},damage{},warn0{1.3}; bool fired{}; };
struct Rune { std::uint64_t id{}; double x{},y{},radius{},damage{},ttl{},arm{}; std::string owner; int color{}; };
struct Zone { std::uint64_t id{}; double x{},y{},radius{},ttl{},warning{},damage{},dps{},pull{}; std::string kind, owner; int color{}; bool slow{}, follow{}; };
struct Event { std::uint64_t id{}; std::string kind; double t{},x{},y{},r{},a{}; int color{},stage{},variant{}; std::string text; native::StaticVector<double, 32> points; };
struct Altar { double x{},y{},radius{120},progress{},ttl{45}; std::string status{"waiting"}; int wave{}; };
struct Encounter { std::string kind,status{"waiting"}; double x{},y{},radius{},progress{},ttl{}; std::uint64_t enemyId{}; native::StaticVector<std::string, cfg::MAX_PLAYERS> buyers; };
struct RecentSpecial { std::string id; double t{},x{},y{}; };

struct GameState {
  std::string campaign{"classic"}; std::vector<std::string> curses; std::uint32_t dailySeed{}; int loop{};
  double time{}; std::unordered_map<std::string,Player> players;
  native::StaticVector<Enemy, cfg::MAX_ENEMIES> enemies;
  native::StaticVector<Shot, cfg::MAX_SHOTS> shots;
  native::StaticVector<EnemyShot, cfg::MAX_ENEMY_SHOTS> enemyShots;
  native::StaticVector<Drop, cfg::MAX_DROPS> gems;
  native::StaticVector<Hazard, cfg::MAX_HAZARDS> hazards;
  native::StaticVector<Rune, cfg::MAX_RUNES> runes;
  native::StaticVector<Zone, cfg::MAX_ZONES> zones;
  native::StaticVector<Event, 64> events;
  native::StaticVector<RecentSpecial, 4> recentSpecials;
  double spawn{}, cleanup{}; std::size_t spawnCursor{}; bool over{}, victory{}, altarSpawned{}, encounterSpawned{}, bloodPact{};
  std::uint64_t nextId{},eventSeq{},tick{}; std::size_t scheduleCursor{};
  int phase{}; double phaseTime{},transitionTime{}; std::string phaseStatus{"horde"};
  std::optional<Altar> altar; std::optional<Encounter> encounter;
};

struct MetaRanks { std::unordered_map<std::string,int> rank; };
struct Loadout { std::string weapon; int special{}; };
struct Difficulty { double spawnInterval{},hpScale{},damageScale{},speedScale{}; int spawnCount{}; };
struct DailyChallenge { std::string key; std::uint32_t seed{}; std::vector<std::string> curses; int character{}; };

GameState createGameState(std::string campaign="classic", std::vector<std::string> curses={});
Player createPlayer(std::string id, std::string name, int color=0, const MetaRanks* meta=nullptr, const Loadout* loadout=nullptr);
void addLatePlayer(GameState& s, Player player);
void updateGame(GameState& s, double dt, Random& random);
inline void updateGame(GameState& s, double dt) { static DefaultRandom rng; updateGame(s,dt,rng); }

int xpNeeded(int level);
int rankOf(const Player& p, const std::string& id);
std::vector<std::string> availablePowers(const Player& p, Random& random, bool coop, const std::vector<std::string>& exclude={});
bool offerPowers(Player& p, Random& random, bool coop);
bool applyPower(Player& p, const std::string& id);
bool rerollPowers(Player& p, Random& random, bool coop);
bool activateSpecial(GameState& s, const std::string& playerId, Random& random);
bool activateSpecial(GameState& s, const std::string& playerId);
bool activateDash(GameState& s, const std::string& playerId, Vec2 input);
bool sendSignal(GameState& s, const std::string& playerId, const std::string& kind, std::optional<Vec2> at={});
Difficulty difficultyAt(double phaseClock, int playerCount=1, int phase=0, const GameState* s=nullptr);

double phaseDuration(const GameState& s);
double phaseClock(const GameState& s);
double curseReward(const GameState& s);
std::uint32_t dailySeedFromKey(const std::string& yyyyMmDd);
DailyChallenge dailyChallenge(const std::string& yyyyMmDd);
std::string stateToJson(const GameState& s, const std::string& viewerId="");

} // namespace arcana
