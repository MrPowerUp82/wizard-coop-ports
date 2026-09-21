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

// Colors are packed as 0xAABBGGRR (vita2d's RGBA8 layout); backends unpack as needed.
constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
}
constexpr std::uint32_t withAlpha(std::uint32_t color, std::uint8_t a) { return (color & 0x00ffffffu) | (static_cast<std::uint32_t>(a) << 24); }
std::uint32_t playerColor(int color, std::uint8_t alpha = 255);

struct SpriteCommand {
  SpriteId sprite{SpriteId::Unknown};
  float x{}, y{}, size{};
  float rotation{};
  std::uint32_t rgba{0xffffffffu};
  std::int16_t layer{};
  bool flip{};
};

// Circles below kSpriteLayerMin are ground decals (drawn before sprites); the rest are effects on top.
struct CircleCommand {
  float x{}, y{}, radius{};
  std::uint32_t rgba{0xffffffffu};
  std::int16_t layer{};
  float thickness{}; // 0 = filled disc, otherwise ring width in pixels
};

struct LineCommand {
  float x1{}, y1{}, x2{}, y2{}, width{2};
  std::uint32_t rgba{0xffffffffu};
};

struct RectCommand {
  float x{}, y{}, w{}, h{};
  std::uint32_t rgba{0xffffffffu};
};

constexpr std::int16_t kSpriteLayerMin = 2;

struct RenderQueue {
  StaticVector<SpriteCommand, 1024> sprites;
  StaticVector<CircleCommand, 256> circles;
  StaticVector<LineCommand, 128> lines;
  StaticVector<RectCommand, 128> bars; // world-space health bars, drawn above sprites
  void clear() noexcept { sprites.clear(); circles.clear(); lines.clear(); bars.clear(); }
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
