#include "arcana/native/render_queue.hpp"
#include "arcana/data.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace arcana::native {
namespace {

SpriteId spriteFromName(std::string_view name) {
  if (name == "slime" || name == "slimelet") return SpriteId::Slime;
  if (name == "bat") return SpriteId::Bat;
  if (name == "brute") return SpriteId::Brute;
  if (name == "eye") return SpriteId::Eye;
  if (name == "mushroom") return SpriteId::Mushroom;
  if (name == "beetle") return SpriteId::Beetle;
  if (name == "skeleton") return SpriteId::Skeleton;
  if (name == "wraith") return SpriteId::Wraith;
  if (name == "imp") return SpriteId::Imp;
  if (name == "scorpion") return SpriteId::Scorpion;
  if (name == "spore") return SpriteId::Spore;
  if (name == "revenant") return SpriteId::Revenant;
  if (name == "sentinel") return SpriteId::Sentinel;
  if (name == "seer") return SpriteId::Seer;
  if (name == "voidling") return SpriteId::Voidling;
  if (name == "voidscarab") return SpriteId::VoidScarab;
  if (name == "treant") return SpriteId::Treant;
  if (name == "lich") return SpriteId::Lich;
  if (name == "demon") return SpriteId::Demon;
  if (name == "bogwarden") return SpriteId::BogWarden;
  if (name == "archon") return SpriteId::Archon;
  if (name == "umbra") return SpriteId::Umbra;
  if (name == "bolt") return SpriteId::Bolt;
  if (name == "fire") return SpriteId::Fire;
  if (name == "thorn") return SpriteId::Thorn;
  if (name == "blade" || name == "bladePurple") return SpriteId::Blade;
  if (name == "gem") return SpriteId::Gem;
  if (name == "greenGem") return SpriteId::GreenGem;
  if (name == "coin") return SpriteId::Coin;
  if (name == "heart") return SpriteId::Heart;
  return SpriteId::Unknown;
}

float enemySize(const Enemy& e) {
  if (e.boss) {
    const auto it = enemyDefs().find(e.type);
    return it == enemyDefs().end() ? 150.0f : static_cast<float>(it->second.size > 0 ? it->second.size : 150.0);
  }
  if (e.elite) return 74.0f;
  const auto it = enemyDefs().find(e.type);
  if (it != enemyDefs().end() && it->second.size > 0) return static_cast<float>(it->second.size);
  return 58.0f;
}

bool visible(float sx, float sy, float radius, const Camera& c) {
  return sx + radius >= c.viewportX && sx - radius <= c.viewportX + c.width &&
         sy + radius >= c.viewportY && sy - radius <= c.viewportY + c.height;
}

void screenPoint(double wx, double wy, const Camera& c, float& sx, float& sy) {
  sx = c.viewportX + c.width * 0.5f + (static_cast<float>(wx) - c.x) * c.zoom;
  sy = c.viewportY + c.height * 0.5f + (static_cast<float>(wy) - c.y) * c.zoom;
}

} // namespace

SpriteId spriteForEnemy(const Enemy& enemy) { return spriteFromName(enemy.type); }
SpriteId spriteForEnemyShot(const EnemyShot& shot) { return spriteFromName(shot.sprite); }

void buildRenderQueue(const GameState& state, const Camera& camera, RenderQueue& out) {
  out.clear();

  for (const auto& zone : state.zones) {
    float x, y; screenPoint(zone.x, zone.y, camera, x, y);
    const float radius = static_cast<float>(zone.radius) * camera.zoom;
    if (visible(x, y, radius, camera)) out.circles.push_back({x, y, radius, 0x40ff6020u, 0});
  }
  for (const auto& hazard : state.hazards) {
    float x, y; screenPoint(hazard.x, hazard.y, camera, x, y);
    const float radius = static_cast<float>(hazard.radius) * camera.zoom;
    if (visible(x, y, radius, camera)) out.circles.push_back({x, y, radius, hazard.fired ? 0x553030ffu : 0x4540b0ffu, 1});
  }

  for (const auto& drop : state.gems) {
    if (drop.dead) continue;
    float x, y; screenPoint(drop.x, drop.y, camera, x, y);
    const float size = (drop.type == "coin" ? 24.0f : drop.type == "heart" ? 28.0f : drop.type == "greenGem" ? 28.0f : 26.0f) * camera.zoom;
    if (visible(x, y, size, camera)) out.sprites.push_back({spriteFromName(drop.type), x, y, size, 0, 0xffffffffu, 2});
  }

  for (const auto& enemy : state.enemies) {
    if (enemy.hp <= 0) continue;
    float x, y; screenPoint(enemy.x, enemy.y, camera, x, y);
    const float size = enemySize(enemy) * camera.zoom;
    if (!visible(x, y, size, camera)) continue;
    const auto tint = enemy.elite ? 0xff9addffu : 0xffffffffu;
    out.sprites.push_back({spriteForEnemy(enemy), x, y, size, 0, tint, 3});
  }

  for (const auto& shot : state.enemyShots) {
    float x, y; screenPoint(shot.x, shot.y, camera, x, y);
    const float size = 32.0f * camera.zoom;
    if (visible(x, y, size, camera)) out.sprites.push_back({spriteForEnemyShot(shot), x, y, size,
      static_cast<float>(std::atan2(shot.vy, shot.vx)), 0xffffffffu, 4});
  }

  for (const auto& shot : state.shots) {
    float x, y; screenPoint(shot.x, shot.y, camera, x, y);
    const float size = static_cast<float>(shot.special ? 58 : shot.shard ? 22 : shot.color == 3 ? 46 : 34) * camera.zoom;
    if (!visible(x, y, size, camera)) continue;
    const SpriteId sprites[4] = {SpriteId::Bolt, SpriteId::Fire, SpriteId::Thorn, SpriteId::Blade};
    const auto index = std::clamp(shot.color, 0, 3);
    out.sprites.push_back({sprites[index], x, y, size, static_cast<float>(std::atan2(shot.vy, shot.vx)), 0xffffffffu, 5});
  }

  for (const auto& [_, player] : state.players) {
    if (!player.alive) continue;
    float x, y; screenPoint(player.x, player.y, camera, x, y);
    const SpriteId players[4] = {SpriteId::Player0, SpriteId::Player1, SpriteId::Player2, SpriteId::Player3};
    out.sprites.push_back({players[std::clamp(player.color, 0, 3)], x, y, 68.0f * camera.zoom, 0, 0xffffffffu, 6});
  }

  // Commands are emitted in layer order (drops -> enemies -> projectiles -> players),
  // so sorting every frame would only waste CPU on the handheld targets.
}

} // namespace arcana::native
