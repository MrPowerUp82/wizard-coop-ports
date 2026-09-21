#pragma once

#include "arcana/game.hpp"
#include "arcana/native/static_vector.hpp"
#include <cstdint>

namespace arcana::native {

enum class SpriteId : std::uint8_t {
  Player0, Player1, Player2, Player3,
  Slime, Bat, Brute, Eye, Mushroom, Beetle, Skeleton, Wraith, Imp, Scorpion,
  Spore, Revenant, Sentinel, Seer, Voidling, VoidScarab,
  Treant, Lich, Demon, BogWarden, Archon, Umbra,
  Bolt, Fire, Thorn, Blade,
  Gem, GreenGem, Coin, Heart,
  Unknown,
  Count
};

struct SpriteCommand {
  SpriteId sprite{SpriteId::Unknown};
  float x{}, y{}, size{};
  float rotation{};
  std::uint32_t rgba{0xffffffffu};
  std::int16_t layer{};
};

struct CircleCommand {
  float x{}, y{}, radius{};
  std::uint32_t rgba{0xffffffffu};
  std::int16_t layer{};
};

struct RenderQueue {
  StaticVector<SpriteCommand, 1024> sprites;
  StaticVector<CircleCommand, 256> circles;
  void clear() noexcept { sprites.clear(); circles.clear(); }
};

struct Camera {
  float x{}, y{};
  float viewportX{}, viewportY{};
  float width{960}, height{544};
  float zoom{1.0f};
};

SpriteId spriteForEnemy(const Enemy& enemy);
SpriteId spriteForEnemyShot(const EnemyShot& shot);
void buildRenderQueue(const GameState& state, const Camera& camera, RenderQueue& out);

} // namespace arcana::native
