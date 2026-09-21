#pragma once

#include "arcana/game.hpp"
#include "arcana/native/render_queue.hpp"
#include "arcana/native/static_vector.hpp"

#include <array>
#include <cstdint>

namespace arcana::sdl {

// Port of the web client's src/animation.js: purely visual state derived from the simulation
// (never feeds back into it). Actor poses (walk bob, breathing, squash on hit, casting recoil,
// falling when downed), a bounded effect pool (sparks, rings, particles and one layered effect per
// special), floating damage numbers, screen shake, flash and hit-stop.
// All storage is fixed-capacity; update() does not allocate.

enum class FxKind : std::uint8_t {
  Ring, Spark, Mote, Nova, Meteor, Impact, Thorns, Lunar, Bloom, Implode, Convergence,
  Chain, FamiliarStrike, Sigil, Ascend, Afterimage, Ghost, Combo,
};
enum class MoteShape : std::uint8_t { Flake, Ember, Leaf, Star, Heal, Smoke };

struct Effect {
  FxKind kind{};
  MoteShape shape{};
  bool major{}, evolved{};
  float x{}, y{}, vx{}, vy{}, age{}, life{1}, radius{}, size{}, spin{}, gravity{}, drag{}, seed{};
  float fromX{}, fromY{}, facing{1};
  std::uint32_t color{0xffffffffu}, color2{0xffffffffu};
  native::SpriteId sprite{native::SpriteId::Unknown};
  const char* text{};
  native::StaticVector<float, 24> points;
};

struct DamageNumber { float x{}, y{}, value{}, age{}, life{0.75f}; bool boss{}; std::uint32_t serial{}; };

// `known` is false until the animator has seen the entity once (first frame): callers fall back.
struct Pose { float x{}, y{}, rotation{}, sx{1}, sy{1}, alpha{1}, flash{}; native::SpriteId sprite{native::SpriteId::Unknown}; float size{}; bool known{}; };

class Animator {
public:
  static constexpr std::size_t kMaxEffects = 192;
  static constexpr std::size_t kMaxNumbers = 64;
  static constexpr std::size_t kMaxActors = 256;

  void reset();
  // `paused` freezes animation time (menus, power choice), like the web client.
  void update(const GameState& game, double dt, bool paused);

  [[nodiscard]] Pose enemyPose(const Enemy& e) const { return pose(e.id); }
  [[nodiscard]] Pose playerPose(const Player& p) const { return pose(playerKey(p)); }
  [[nodiscard]] float time() const { return time_; }
  [[nodiscard]] float shakeX() const;
  [[nodiscard]] float shakeY() const;
  [[nodiscard]] float flashAlpha() const { return flashLife_ > 0 ? flashAlpha_ * (1 - flashAge_ / flashLife_) : 0; }
  [[nodiscard]] std::uint32_t flashColor() const { return flashColor_; }
  [[nodiscard]] const native::StaticVector<Effect, kMaxEffects>& effects() const { return effects_; }
  [[nodiscard]] const native::StaticVector<DamageNumber, kMaxNumbers>& numbers() const { return numbers_; }
  // Initial warning of a meteor zone (its falling rock animates over it), 0 if unknown.
  [[nodiscard]] float zoneWarning(std::uint64_t zoneId) const;
  void shake(float amount) { shake_ = std::max(shake_, amount); }

private:
  struct Actor {
    std::uint64_t key{};
    float x{}, y{}, hp{};
    bool alive{true}, boss{}, elite{}, player{}, floating{};
    native::SpriteId sprite{native::SpriteId::Unknown};
    float size{64};
    std::uint32_t color{};
    float seed{}, movedAt{-10}, dx{}, stride{}, walking{}, hit{}, cast{}, down{}, facing{1}, castAngle{}, trailAt{-1};
    int castCount{}, level{1}, character{};
    double charge{}, bossCooldown{}, rangedCooldown{}, dashFor{};
    std::uint32_t seen{}, numberSerial{};
  };

  static std::uint64_t playerKey(const Player& p);
  Pose pose(std::uint64_t key) const;
  const Actor* find(std::uint64_t key) const;
  Actor* find(std::uint64_t key);
  void rebuildIndex();
  void track(const GameState& game, const Player* player, const Enemy* enemy, float dt, bool samePhase);
  void handleEvent(const Event& e);
  void special(const Event& e);
  void altSpecial(const Event& e, float seed);
  void burst(float x, float y, std::uint32_t color, int count, float radius);
  void motes(float x, float y, MoteShape shape, std::uint32_t color, int count, float speed, float spread, float life,
             float gravity, float size, float drag);
  Effect* push(FxKind kind, float x, float y, float life, bool major = false);
  void addNumber(Actor& actor, float x, float y, float amount);
  void setFlash(std::uint32_t color, float alpha, float life);
  float random();

  native::StaticVector<Actor, kMaxActors> actors_;
  std::array<std::int16_t, 512> index_{};
  native::StaticVector<Effect, kMaxEffects> effects_;
  native::StaticVector<DamageNumber, kMaxNumbers> numbers_;
  struct ZoneWarn { std::uint64_t id{}; float warning{}; };
  native::StaticVector<ZoneWarn, cfg::MAX_ZONES * 2> zoneWarnings_;
  float time_{}, shake_{}, freeze_{};
  float flashAge_{}, flashLife_{}, flashAlpha_{};
  std::uint32_t flashColor_{0xffffffffu};
  std::uint64_t lastEventId_{};
  bool haveEventId_{};
  std::uint32_t stamp_{}, numberSerial_{};
  int lastPhase_{-1};
  std::string lastStatus_;
  std::uint32_t rng_{0x9e3779b9u};
};

} // namespace arcana::sdl
