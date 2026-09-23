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

constexpr real PI = 3.14159265358979323846;
struct Vec2 { real x{}, y{}; };
inline real distanceSq(Vec2 a, Vec2 b) { const auto dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy; }

// Characters (Player::color): the four standard mages, then two unlockable ones — the secret
// Developer (server/developer.js) and the Aurora Guardian, the Classic reward (server/aurora.js).
constexpr int STANDARD_CHARACTERS = 4, DEVELOPER = 4, AURORA = 5, CHARACTER_COUNT = 6;

class Random {
public:
  virtual ~Random() = default;
  virtual real next() = 0;
};
class DefaultRandom final : public Random {
  std::mt19937_64 gen_;
  std::uniform_real_distribution<real> dist_{0.0, 1.0};
public:
  explicit DefaultRandom(std::uint64_t seed = std::random_device{}()) : gen_(seed) {}
  real next() override { return dist_(gen_); }
};
class SeededRandom final : public Random {
  std::uint32_t a_;
public:
  explicit SeededRandom(std::uint32_t seed) : a_(seed) {}
  real next() override;
};

struct Input { real x{}, y{}; std::uint64_t seq{}; };
struct Stats { real damage{}, taken{}; int kills{}, revives{}; std::unordered_map<std::string,real> by; };
struct Familiar { real x{}, y{}, timer{0.4}; };

struct Player {
  std::string id, name;
  int color{};
  real x{}, y{}, hp{100}, maxHp{100}, xp{};
  int level{1}; bool alive{true}, connected{true};
  Input input{};
  real speed{190}, damage{14}, attackDelay{0.62}, attackCooldown{}, pickupRadius{105}, armor{};
  int projectiles{1};
  real hitCooldown{}, invulnerableFor{};
  std::unordered_map<std::string,int> powers;
  std::vector<std::string> pendingPowers;
  real powerTimer{}; int pendingChests{};
  real specialCharge{}, specialCooldown{};
  int coins{}; real coinFrac{}, coinMult{1}, xpMult{1}; int rerolls{1}, phoenix{};
  real dashFor{}, dashCooldown{}, dashX{}, dashY{1}, moveX{}, moveY{1}; std::uint64_t motionId{};
  real reviveProgress{}; std::string reviveBy, reviving;
  int castCount{}; real castAngle{}, orbitAngle{}; std::uint64_t inputSeq{};
  int specialVariant{}; real signalAt{-std::numeric_limits<real>::infinity()};
  real lifelinkAt{}, sanctuaryAt{}, auraTimer{}, chainTimer{}, runeTimer{};
  real shopProgress{};
  std::optional<Familiar> familiar;
  Stats stats;
};

struct Enemy {
  std::uint64_t id{}; std::string type; real x{},y{},hp{},maxHp{},age{};
  bool boss{}, elite{}, thief{}, minion{}, escaped{}, exploded{}, distant{};
  int stage{1};
  real slowFor{}, burningFor{}, rootFor{}, freezeFor{};
  std::string slowBy, burnBy, rootBy;
  real comboAt{-1}, touchCooldown{}, chargeTimer{}, windup{}, dash{}, dashAngle{}, dashSpeed{}, dashWarn{}, dashTimer{}, shootTimer{}, fuse{};
  real attackCooldown{2.5}, rangedCooldown{1.5};
  real vx{}, vy{}; bool hasVelocity{};
  std::array<real, CHARACTER_COUNT> orbitHitUntil{}; // per character: unique within a run
};
struct Shot {
  real x{},y{},vx{},vy{},ttl{},damage{}; int color{}; int pierce{1};
  bool special{}, shard{}, boomerang{}, returning{}, fullmoon{};
  std::string owner; native::StaticVector<std::uint64_t, 16> hitIds;
};
struct EnemyShot { real x{},y{},vx{},vy{},ttl{},damage{},radius{12}; std::string sprite; };
struct Drop { std::uint64_t id{}; real x{},y{},value{},ttl{24}; std::string type{"gem"}, pull; bool dead{}; };
struct Hazard { real x{},y{},radius{},warning{1.3},ttl{1.65},damage{},warn0{1.3}; bool fired{}; };
struct Rune { std::uint64_t id{}; real x{},y{},radius{},damage{},ttl{},arm{}; std::string owner; int color{}; };
struct Zone { std::uint64_t id{}; real x{},y{},radius{},ttl{},warning{},damage{},dps{},pull{}; std::string kind, owner; int color{}; bool slow{}, follow{}; };
struct Event { std::uint64_t id{}; std::string kind; real t{},x{},y{},r{},a{}; int color{},stage{},variant{}; std::string text; native::StaticVector<real, 32> points;
  std::string player, name; }; // player/name: who sent a signal (filled by the online decoder)
struct Altar { real x{},y{},radius{120},progress{},ttl{45}; std::string status{"waiting"}; int wave{}; };
struct Encounter { std::string kind,status{"waiting"}; real x{},y{},radius{},progress{},ttl{}; std::uint64_t enemyId{}; native::StaticVector<std::string, cfg::MAX_PLAYERS> buyers; };
struct RecentSpecial { std::string id; real t{},x{},y{}; };

struct GameState {
  std::string campaign{"classic"}; std::vector<std::string> curses; std::uint32_t dailySeed{}; int loop{};
  real time{}; std::unordered_map<std::string,Player> players;
  native::StaticVector<Enemy, cfg::MAX_ENEMIES> enemies;
  native::StaticVector<Shot, cfg::MAX_SHOTS> shots;
  native::StaticVector<EnemyShot, cfg::MAX_ENEMY_SHOTS> enemyShots;
  native::StaticVector<Drop, cfg::MAX_DROPS> gems;
  native::StaticVector<Hazard, cfg::MAX_HAZARDS> hazards;
  native::StaticVector<Rune, cfg::MAX_RUNES> runes;
  native::StaticVector<Zone, cfg::MAX_ZONES> zones;
  native::StaticVector<Event, 64> events;
  native::StaticVector<RecentSpecial, 4> recentSpecials;
  real spawn{}, cleanup{}; std::size_t spawnCursor{}; bool over{}, victory{}, altarSpawned{}, encounterSpawned{}, bloodPact{};
  std::uint64_t nextId{},eventSeq{},tick{}; std::size_t scheduleCursor{};
  int phase{}; real phaseTime{},transitionTime{}; std::string phaseStatus{"horde"};
  std::optional<Altar> altar; std::optional<Encounter> encounter;
};

struct MetaRanks { std::unordered_map<std::string,int> rank; };
struct Loadout { std::string weapon; int special{}; };
struct Difficulty { real spawnInterval{},hpScale{},damageScale{},speedScale{}; int spawnCount{}; };
struct DailyChallenge { std::string key; std::uint32_t seed{}; std::vector<std::string> curses; int character{}; };

GameState createGameState(std::string campaign="classic", std::vector<std::string> curses={});
Player createPlayer(std::string id, std::string name, int color=0, const MetaRanks* meta=nullptr, const Loadout* loadout=nullptr);
// Lobby-only change: the character's bonuses are reversible multipliers, so permanent upgrades survive
// any number of swaps (server/developer.js selectPlayerCharacter).
void selectPlayerCharacter(Player& p, int color);
// The result that unlocks the Aurora Guardian: the sixth realm cleared in the Classic ritual.
bool earnsAurora(const GameState& s);
void addLatePlayer(GameState& s, Player player);
void updateGame(GameState& s, real dt, Random& random);
inline void updateGame(GameState& s, real dt) { static DefaultRandom rng; updateGame(s,dt,rng); }

int xpNeeded(int level);
int rankOf(const Player& p, const std::string& id);
std::vector<std::string> availablePowers(const Player& p, Random& random, bool coop, const std::vector<std::string>& exclude={});
bool offerPowers(Player& p, Random& random, bool coop);
bool applyPower(Player& p, const std::string& id);
bool rerollPowers(Player& p, Random& random, bool coop);
bool activateSpecial(GameState& s, const std::string& playerId, Random& random);
bool activateSpecial(GameState& s, const std::string& playerId);
bool activateDash(GameState& s, const std::string& playerId, Vec2 input);
// One step of a player's own movement (walk + dash burst) from p.input, as the server applies it.
// The online client uses it to predict the local player between snapshots.
Vec2 playerMovement(const Player& p, real dt);
bool sendSignal(GameState& s, const std::string& playerId, const std::string& kind, std::optional<Vec2> at={});
Difficulty difficultyAt(real phaseClock, int playerCount=1, int phase=0, const GameState* s=nullptr);

real phaseDuration(const GameState& s);
real phaseClock(const GameState& s);
real curseReward(const GameState& s);
std::uint32_t dailySeedFromKey(const std::string& yyyyMmDd);
DailyChallenge dailyChallenge(const std::string& yyyyMmDd);
std::string stateToJson(const GameState& s, const std::string& viewerId="");

} // namespace arcana
