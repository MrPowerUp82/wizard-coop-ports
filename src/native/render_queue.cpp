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

void screenPoint(real wx, real wy, const Camera& c, float& sx, float& sy) {
  sx = c.viewportX + c.width * 0.5f + (static_cast<float>(wx) - c.x) * c.zoom;
  sy = c.viewportY + c.height * 0.5f + (static_cast<float>(wy) - c.y) * c.zoom;
}

} // namespace

SpriteId spriteForEnemy(const Enemy& enemy) { return spriteFromName(enemy.type); }
SpriteId spriteForEnemyShot(const EnemyShot& shot) { return spriteFromName(shot.sprite); }

std::uint32_t playerColor(int color, std::uint8_t alpha) {
  // server/weapons.js SPELLS tints; the last two are the Developer (#73ffe4) and the Aurora Guardian (#ffd778).
  static constexpr std::uint32_t colors[CHARACTER_COUNT] = {rgba(118, 223, 255), rgba(255, 153, 85), rgba(146, 237, 104),
                                                            rgba(196, 160, 255), rgba(115, 255, 228), rgba(255, 215, 120)};
  return withAlpha(colors[std::clamp(color, 0, CHARACTER_COUNT - 1)], alpha);
}

SpriteId playerSprite(int color) {
  static constexpr SpriteId sprites[CHARACTER_COUNT] = {SpriteId::Player0, SpriteId::Player1, SpriteId::Player2, SpriteId::Player3,
                                                        SpriteId::PlayerDeveloper, SpriteId::PlayerAurora};
  return sprites[std::clamp(color, 0, CHARACTER_COUNT - 1)];
}

SpriteId shotSprite(int color) {
  static constexpr SpriteId sprites[CHARACTER_COUNT] = {SpriteId::Bolt, SpriteId::Fire, SpriteId::Thorn, SpriteId::Blade,
                                                        SpriteId::BoltDeveloper, SpriteId::BoltAurora};
  return sprites[std::clamp(color, 0, CHARACTER_COUNT - 1)];
}

namespace {

void bar(RenderQueue& out, float cx, float top, float width, real ratio, std::uint32_t fill) {
  const float r = static_cast<float>(std::clamp(ratio, 0.0, 1.0));
  out.bars.push_back({cx - width * 0.5f - 1, top - 1, width + 2, 6, rgba(8, 10, 18, 200)});
  out.bars.push_back({cx - width * 0.5f, top, width * r, 4, fill});
}

void drawWeapons(const Player& p, float px, float py, const Camera& camera, RenderQueue& out) {
  const float z = camera.zoom;
  if (const int rank = std::clamp(rankOf(p, "aura"), 0, 5)) {
    const bool evolved = rankOf(p, "sanctuary") > 0;
    const float radius = static_cast<float>(70 + rank * 10 + (evolved ? 30 : 0)) * z;
    out.circles.push_back({px, py, radius, playerColor(p.color, 28), 1});
    out.circles.push_back({px, py, radius, playerColor(p.color, 90), 1, 2.0f});
  }
  if (const int rank = std::clamp(rankOf(p, "orbit"), 0, 5)) {
    // Mirrors updateOrbit() in game.cpp so the orbs sit exactly where they hit.
    static constexpr int counts[5] = {1, 2, 2, 3, 3};
    const bool evolved = rankOf(p, "constellation") > 0;
    const int count = counts[rank - 1] + (evolved ? 2 : 0);
    const real radius = 78 + rank * 4 + (evolved ? 20 : 0);
    const float orb = static_cast<float>(evolved ? 20 : 14) * z;
    for (int n = 0; n < count; ++n) {
      const real a = p.orbitAngle + n * PI * 2 / count;
      const float x = px + static_cast<float>(std::cos(a) * radius) * z, y = py + static_cast<float>(std::sin(a) * radius) * z;
      out.circles.push_back({x, y, orb * 1.6f, playerColor(p.color, 60), 7});
      out.circles.push_back({x, y, orb, rgba(240, 248, 255, 235), 7});
    }
  }
  if (p.familiar) {
    float x, y; screenPoint(p.familiar->x, p.familiar->y, camera, x, y);
    out.circles.push_back({x, y, 16 * z, playerColor(p.color, 70), 7});
    out.circles.push_back({x, y, 9 * z, playerColor(p.color, 240), 7});
  }
}

void drawEvents(const GameState& state, const Camera& camera, RenderQueue& out) {
  for (const auto& e : state.events) {
    const real age = state.time - e.t;
    if (age < 0 || age > 0.6) continue;
    float x, y; screenPoint(e.x, e.y, camera, x, y);
    const float k = static_cast<float>(age / 0.6);
    const auto fade = static_cast<std::uint8_t>(220 * (1 - k));
    if (e.kind == "boom") {
      const float r = static_cast<float>(e.r > 0 ? e.r : 80) * camera.zoom * (0.4f + 0.6f * k);
      out.circles.push_back({x, y, r, rgba(255, 170, 80, fade), 8, 4});
    } else if (e.kind == "special" || e.kind == "bossDown" || e.kind == "revive" || e.kind == "phoenix") {
      const float r = (e.kind == "bossDown" ? 320.0f : 220.0f) * camera.zoom * k;
      out.circles.push_back({x, y, r, withAlpha(playerColor(e.color), fade), 8, 6});
    } else if (e.kind == "chain" && age < 0.25) {
      for (std::size_t i = 3; i < e.points.size(); i += 2) {
        float x1, y1, x2, y2;
        screenPoint(e.points[i - 3], e.points[i - 2], camera, x1, y1);
        screenPoint(e.points[i - 1], e.points[i], camera, x2, y2);
        out.lines.push_back({x1, y1, x2, y2, 3, rgba(190, 230, 255, static_cast<std::uint8_t>(255 * (1 - age / 0.25)))});
      }
    }
  }
}

} // namespace

void buildRenderQueue(const GameState& state, const Camera& camera, RenderQueue& out) {
  out.clear();
  const float z = camera.zoom;

  for (const auto& zone : state.zones) {
    float x, y; screenPoint(zone.x, zone.y, camera, x, y);
    const float radius = static_cast<float>(zone.radius) * z;
    if (!visible(x, y, radius, camera)) continue;
    const bool warning = zone.warning > 0;
    const auto color = zone.owner.empty() ? rgba(255, 90, 60) : playerColor(zone.color);
    out.circles.push_back({x, y, radius, withAlpha(color, warning ? 24 : 60), 0});
    out.circles.push_back({x, y, radius, withAlpha(color, warning ? 150 : 90), 0, 2});
  }
  for (const auto& hazard : state.hazards) {
    float x, y; screenPoint(hazard.x, hazard.y, camera, x, y);
    const float radius = static_cast<float>(hazard.radius) * z;
    if (!visible(x, y, radius, camera)) continue;
    if (hazard.fired) {
      out.circles.push_back({x, y, radius, rgba(255, 80, 60, 110), 1});
    } else {
      // The telegraph fills up as the warning runs out, like the web version's charge ring.
      const real charge = hazard.warn0 > 0 ? 1 - std::clamp(hazard.warning / hazard.warn0, 0.0, 1.0) : 1;
      out.circles.push_back({x, y, radius * static_cast<float>(charge), rgba(255, 70, 70, 50), 1});
      out.circles.push_back({x, y, radius, rgba(255, 90, 80, 170), 1, 3});
    }
  }
  for (const auto& rune : state.runes) {
    float x, y; screenPoint(rune.x, rune.y, camera, x, y);
    if (!visible(x, y, 24, camera)) continue;
    out.circles.push_back({x, y, 18 * z, playerColor(rune.color, rune.arm > 0 ? 70 : 150), 1, 3});
    out.circles.push_back({x, y, 6 * z, playerColor(rune.color, 230), 1});
  }
  if (state.altar) {
    float x, y; screenPoint(state.altar->x, state.altar->y, camera, x, y);
    const float radius = static_cast<float>(state.altar->radius) * z;
    if (visible(x, y, radius, camera)) {
      out.circles.push_back({x, y, radius, rgba(255, 215, 120, 30), 0});
      out.circles.push_back({x, y, radius, rgba(255, 215, 120, 160), 0, 3});
    }
  }
  if (state.encounter && state.encounter->radius > 0) {
    float x, y; screenPoint(state.encounter->x, state.encounter->y, camera, x, y);
    const float radius = static_cast<float>(state.encounter->radius) * z;
    if (visible(x, y, radius, camera)) out.circles.push_back({x, y, radius, rgba(170, 140, 255, 150), 0, 3});
  }

  for (const auto& drop : state.gems) {
    if (drop.dead) continue;
    float x, y; screenPoint(drop.x, drop.y, camera, x, y);
    const float size = (drop.type == "coin" ? 24.0f : drop.type == "heart" ? 28.0f : drop.type == "greenGem" ? 28.0f : 26.0f) * z;
    if (visible(x, y, size, camera)) out.sprites.push_back({spriteFromName(drop.type), x, y, size, 0, 0xffffffffu, 2});
  }

  for (const auto& enemy : state.enemies) {
    if (enemy.hp <= 0) continue;
    float x, y; screenPoint(enemy.x, enemy.y, camera, x, y);
    const float size = enemySize(enemy) * z;
    if (!visible(x, y, size, camera)) continue;
    auto tint = enemy.elite ? rgba(255, 221, 154) : 0xffffffffu;
    if (enemy.freezeFor > 0) tint = rgba(170, 220, 255);
    else if (enemy.burningFor > 0) tint = rgba(255, 190, 150);
    if (enemy.windup > 0 || enemy.dashWarn > 0) tint = rgba(255, 150, 150);
    out.sprites.push_back({spriteForEnemy(enemy), x, y, size, 0, tint, 3, enemy.hasVelocity && enemy.vx < 0});
    if (enemy.elite && enemy.hp < enemy.maxHp) bar(out, x, y - size * 0.55f, 46 * z, enemy.hp / enemy.maxHp, rgba(255, 200, 80));
  }

  for (const auto& shot : state.enemyShots) {
    float x, y; screenPoint(shot.x, shot.y, camera, x, y);
    const float size = 32.0f * z;
    if (visible(x, y, size, camera)) out.sprites.push_back({spriteForEnemyShot(shot), x, y, size,
      static_cast<float>(std::atan2(shot.vy, shot.vx)), rgba(255, 170, 170), 4});
  }

  for (const auto& shot : state.shots) {
    float x, y; screenPoint(shot.x, shot.y, camera, x, y);
    const float size = static_cast<float>(shot.special ? 58 : shot.shard ? 22 : shot.color == 3 ? 46 : 34) * z;
    if (!visible(x, y, size, camera)) continue;
    out.sprites.push_back({shotSprite(shot.color), x, y, size, static_cast<float>(std::atan2(shot.vy, shot.vx)), 0xffffffffu, 5});
  }

  for (const auto& [_, player] : state.players) {
    float x, y; screenPoint(player.x, player.y, camera, x, y);
    const auto sprite = playerSprite(player.color);
    if (!player.alive) {
      out.sprites.push_back({sprite, x, y, 68.0f * z, 0, rgba(255, 255, 255, 90), 6});
      out.circles.push_back({x, y, 44 * z, rgba(255, 255, 255, 60), 8, 2});
      if (player.reviveProgress > 0)
        out.circles.push_back({x, y, 44 * z * static_cast<float>(player.reviveProgress / cfg::REVIVE_SECONDS), playerColor(player.color, 110), 8});
      continue;
    }
    drawWeapons(player, x, y, camera, out);
    const bool blink = player.invulnerableFor > 0 && std::fmod(state.time * 10, 1.0) < 0.5;
    out.circles.push_back({x, y + 26 * z, 22 * z, rgba(0, 0, 0, 70), 1}); // contact shadow
    out.sprites.push_back({sprite, x, y, 68.0f * z, 0, blink ? rgba(255, 255, 255, 120) : 0xffffffffu, 6, player.moveX < 0});
    if (player.dashFor > 0) out.circles.push_back({x, y, 40 * z, playerColor(player.color, 90), 8, 3});
    bar(out, x, y - 46 * z, 50 * z, player.hp / std::max(1.0, player.maxHp), rgba(90, 230, 120));
  }

  drawEvents(state, camera, out);
  // Sprites are emitted in layer order (drops -> enemies -> projectiles -> players),
  // so sorting every frame would only waste CPU on the handheld targets.
}

} // namespace arcana::native
