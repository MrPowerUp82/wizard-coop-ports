#include "world_renderer.hpp"
#include "fastmath.hpp"
#include "arcana/data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arcana::sdl {
namespace {

using native::SpriteId;
using native::rgba;
constexpr float TAU = static_cast<float>(2 * PI);
constexpr float HALF_PI = static_cast<float>(PI / 2);

constexpr std::uint32_t hex(std::uint32_t rgb, std::uint8_t a = 255) {
  return rgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb), a);
}
// Colour with a float alpha multiplier (canvas globalAlpha).
std::uint32_t A(std::uint32_t color, float alpha) {
  const float base = static_cast<float>(color >> 24) / 255.0f;
  return native::withAlpha(color, static_cast<std::uint8_t>(std::clamp(alpha * base, 0.0f, 1.0f) * 255.0f));
}
float easeOut(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }
// Visual-only trig (software sin/cos are expensive on the PSP); see fastmath.hpp.
float fsin(float v) { return fastSin(v); }
float fcos(float v) { return fastCos(v); }

constexpr std::uint32_t kElements[4] = {hex(0x76dfff), hex(0xff9955), hex(0x92ed68), hex(0xc4a0ff)};

struct Frame {
  BatchRenderer& b;
  const GameState& game;
  const Animator& anim;
  const WorldView& view;
  float time;
  float left, top, right, bottom; // visible world rect
  float textScale;                // keeps world labels readable when the co-op camera zooms out
  bool visible(float x, float y, float margin = 110) const {
    return x >= left - margin && x <= right + margin && y >= top - margin && y <= bottom + margin;
  }
};

float enemyBaseSize(const Enemy& e) {
  const auto it = enemyDefs().find(e.type);
  const double base = it != enemyDefs().end() && it->second.size > 0 ? it->second.size : 64.0;
  return static_cast<float>(base * (e.elite ? 1.35 : 1.0));
}

// ------------------------------------------------------------------------------------------------
// Floor and ambience

void drawAtmosphere(Frame& f) {
  static constexpr std::uint32_t colors[6] = {hex(0xb8f57f), hex(0xb9cbff), hex(0xffad68), hex(0xa6e5bd), hex(0xffe49b), hex(0xc4a0ff)};
  const std::uint32_t color = colors[std::clamp(f.game.phase, 0, 5) % 6];
  constexpr float spacing = 190;
  for (int gx = static_cast<int>(std::floor((f.left - 50) / spacing)); gx <= static_cast<int>(std::ceil((f.right + 50) / spacing)); ++gx) {
    for (int gy = static_cast<int>(std::floor((f.top - 50) / spacing)); gy <= static_cast<int>(std::ceil((f.bottom + 50) / spacing)); ++gy) {
      const float seed = fsin(static_cast<float>(gx) * 127.1f + static_cast<float>(gy) * 311.7f) * 43758.5453f;
      const float offset = seed - std::floor(seed);
      const float x = gx * spacing + offset * spacing + fsin(f.time * 0.35 + seed) * 24;
      const float y = gy * spacing + std::fmod(offset * 7, 1.0f) * spacing + fcos(f.time * 0.45 + seed) * 30;
      const float pulse = 0.5f + fsin(f.time * 1.2 + seed) * 0.5f;
      f.b.glow(x, y, 9 + offset * 5, color, 0.07f + pulse * 0.16f);
      const float dot = 0.8f + offset;
      f.b.rect(x - dot, y - dot, dot * 2, dot * 2, A(color, 0.12f + pulse * 0.3f));
    }
  }
}

// ------------------------------------------------------------------------------------------------
// Ground objects

void drawAltar(Frame& f) {
  const auto& altar = *f.game.altar;
  const float x = static_cast<float>(altar.x), y = static_cast<float>(altar.y), r = static_cast<float>(altar.radius);
  if (!f.visible(x, y, 180)) return;
  const std::uint32_t color = altar.status == "complete" ? hex(0x8dffcc) : altar.status == "expired" ? hex(0x61706b) : hex(0xffd36b);
  auto& b = f.b;
  b.circle(x, y, r, A(color, 0.094f));
  b.circle(x, y, r, color, 2);
  b.arc(x, y, r, r, -HALF_PI, -HALF_PI + TAU * static_cast<float>(std::clamp(altar.progress / 15, 0.0, 1.0)), color, 5);
  b.quad(x, y - 37, x + 37, y, x, y + 37, x - 37, y, hex(0x17342f)); // the rotated square
  b.beginPath(); b.lineTo(x, y - 37); b.lineTo(x + 37, y); b.lineTo(x, y + 37); b.lineTo(x - 37, y); b.stroke(2, color, true);
  b.beginPath(); b.lineTo(x, y - 12); b.lineTo(x + 9, y); b.lineTo(x, y + 12); b.lineTo(x - 9, y); b.stroke(2.5f, color, true);
  const char* label = altar.status == "complete" ? "PURIFICADO" : altar.status == "expired" ? "APAGADO" : "DEFENDA O ALTAR";
  b.text(x, y - 60, label, 12, color, Align::Center);
}

void drawEncounter(Frame& f, const Encounter& enc) {
  if (enc.kind == "thief") return;
  const float x = static_cast<float>(enc.x), y = static_cast<float>(enc.y), r = static_cast<float>(enc.radius);
  if (!f.visible(x, y, 200)) return;
  const bool merchant = enc.kind == "merchant";
  const bool done = enc.status == "complete" || enc.status == "expired";
  const std::uint32_t color = done ? hex(0x61706b) : merchant ? hex(0xffd36b) : hex(0xff7aa8);
  auto& b = f.b;
  b.circle(x, y, r, A(color, 0.08f));
  b.dashedCircle(x, y, r, 10, 8, -f.time * 20, color, 2);
  // Merchant progress is per player (shop progress); show the best one.
  real progress = enc.progress;
  if (merchant) { progress = 0; for (const auto& [_, p] : f.game.players) progress = std::max(progress, p.shopProgress); }
  const real seconds = merchant ? 1.5 : 3.0;
  if (progress > 0 && !done) b.arc(x, y, r, r, -HALF_PI, -HALF_PI + TAU * static_cast<float>(std::min<real>(1.0, progress / seconds)), color, 5);
  const float bob = fsin(f.time * 2.4) * 3;
  if (merchant) {
    b.rect(x - 30, y - 2, 60, 20, hex(0x3b2a1a));
    b.rect(x - 34, y - 6, 68, 6, hex(0x7a4a1c));
    b.quad(x - 40, y - 34, x + 40, y - 34, x + 32, y - 18, x - 32, y - 18, done ? hex(0x4d5552) : hex(0xb3413c));
    for (int n = -30; n < 30; n += 20)
      b.quad(x + n, y - 34, x + n + 10, y - 34, x + n + 8, y - 18, x + n + 2, y - 18, hex(0xf0e2c0));
    b.line(x - 34, y - 18, x - 34, y + 18, 3, hex(0x5b3a1f));
    b.line(x + 34, y - 18, x + 34, y + 18, 3, hex(0x5b3a1f));
  } else {
    b.beginPath(); b.lineTo(x - 18, y + 20); b.lineTo(x - 11, y - 46); b.lineTo(x, y - 58); b.lineTo(x + 11, y - 46); b.lineTo(x + 18, y + 20);
    b.fill(done ? hex(0x3a3f3e) : hex(0x2a1826));
    b.stroke(1.5f, color, true);
    if (!done) b.glow(x, y - 24, 22 + fsin(f.time * 5) * 5, hex(0xff7aa8), 0.9f);
  }
  b.text(x, y - 88 + bob, merchant ? "$" : "+", 22, color, Align::Center);
  const char* status = enc.status == "complete" ? (merchant ? "ESGOTADO" : "PACTO SELADO") : enc.status == "expired" ? "PARTIU"
                     : merchant ? "MERCADOR" : "SANTUÁRIO AMALDIÇOADO";
  b.text(x, y + r + 6, status, 12, color, Align::Center);
}

void drawSpecialZone(Frame& f, const Zone& z) {
  auto& b = f.b;
  const float x = static_cast<float>(z.x), y = static_cast<float>(z.y), r = static_cast<float>(z.radius);
  const float spin = f.time;
  if (z.kind == "hail") {
    b.glow(x, y, r, hex(0xc8f5ff), 0.28f);
    b.circle(x, y, r, A(hex(0xbff4ff), 0.6f), 1.5f);
    // Hailstones: deterministic pseudo-random streaks falling inside the circle.
    for (int n = 0; n < 22; ++n) {
      const float cycle = std::fmod(f.time * 1.8f + n * 0.137f, 1.0f);
      const float a = n * 2.399f, rr = r * std::sqrt(std::fmod(n * 0.618f, 1.0f));
      const float hx = x + fcos(a) * rr, hy = y + fsin(a) * rr;
      b.line(hx - 8 + cycle * 8, hy - 40 + cycle * 40, hx + cycle * 8, hy - 24 + cycle * 40, 3, A(hex(0xe8fbff), 1 - cycle));
      if (cycle > 0.85f) b.circle(hx + 8, hy + 16, 6 + (cycle - 0.85f) * 60, A(hex(0xe8fbff), 1 - cycle), 1);
    }
  } else if (z.kind == "vortex") {
    b.circle(x, y, r * 0.42f, rgba(20, 6, 40, 148));
    b.glow(x, y, r, hex(0x6f45aa), 0.5f);
    for (int arm = 0; arm < 4; ++arm) {
      b.beginPath();
      for (int k = 0; k <= 24; ++k) {
        const float t = k / 24.0f, a = arm * TAU / 4 - spin * 4 + t * 3.4f, rr = r * (1 - t) + 8;
        b.lineTo(x + fcos(a) * rr, y + fsin(a) * rr);
      }
      b.stroke(2.5f, A(hex(0xd9c2ff), 0.7f));
    }
    b.circle(x, y, 6 + std::max(0.0f, 1 - static_cast<float>(z.ttl)) * 10, hex(0xf1e6ff));
  }
}

void drawZone(Frame& f, const Zone& z) {
  auto& b = f.b;
  const float x = static_cast<float>(z.x), y = static_cast<float>(z.y), r = static_cast<float>(z.radius);
  const float alpha = 0.32f + fsin(f.time * 14 + x) * 0.08f;
  const bool roots = z.kind == "roots";
  b.glow(x, y, r, roots ? hex(0x76d35f) : hex(0xff7d35), alpha * 1.5f);
  if (z.warning > 0 || roots) {
    const std::uint32_t c = A(roots ? hex(0x92ed68) : hex(0xffd36b), 0.8f);
    b.circle(x, y, r, c, 2);
    if (roots) for (int n = 0; n < 8; ++n) { const float a = n * TAU / 8; b.line(x, y, x + fcos(a) * r, y + fsin(a) * r, 2, c); }
    else b.text(x, y - 7, "METEORO", 12, hex(0xffe8a8), Align::Center);
  }
}

void drawRunesAndHazards(Frame& f) {
  auto& b = f.b;
  for (const auto& rune : f.game.runes) {
    const float x = static_cast<float>(rune.x), y = static_cast<float>(rune.y);
    if (!f.visible(x, y)) continue;
    const bool armed = rune.arm <= 0;
    const std::uint32_t c = A(kElements[std::clamp(rune.color, 0, 3)], armed ? 0.85f : 0.35f);
    b.circle(x, y, 15 + (armed ? fsin(f.time * 8 + static_cast<double>(rune.id)) * 2 : 0), c, 2);
    b.beginPath();
    for (int n = 0; n < 5; ++n) {
      const float a = n * TAU * 2 / 5 - HALF_PI + f.time;
      b.lineTo(x + fcos(a) * 11, y + fsin(a) * 11);
    }
    b.stroke(2, c, true);
  }
  for (const auto& h : f.game.hazards) {
    const float x = static_cast<float>(h.x), y = static_cast<float>(h.y), r = static_cast<float>(h.radius);
    if (!f.visible(x, y, r)) continue;
    b.circle(x, y, r, h.fired ? rgba(255, 150, 75, 140) : rgba(245, 85, 80, 31));
    b.circle(x, y, r, h.fired ? hex(0xffc778) : hex(0xef7f78), 2);
    if (!h.fired) {
      const float k = static_cast<float>(std::clamp(1 - h.warning / (h.warn0 > 0 ? h.warn0 : 1.3), 0.0, 1.0));
      b.circle(x, y, r * k, hex(0xef7f78), 2);
    }
  }
}

void drawChest(BatchRenderer& b, float x, float y, float time) {
  b.circle(x, y, 26, rgba(255, 211, 107, static_cast<std::uint8_t>((0.35f + fsin(time * 5) * 0.15f) * 255)));
  b.rect(x - 15, y - 8, 30, 20, hex(0x7a4a1c));
  b.rect(x - 15, y - 14, 30, 8, hex(0x9c6428));
  b.rect(x - 15, y - 7, 30, 3, hex(0xffd36b));
  b.rect(x - 3, y - 10, 6, 9, hex(0xffd36b));
}

void drawMagnet(BatchRenderer& b, float x, float y, float time) {
  const float rot = fsin(time * 4) * 0.25f, c = fcos(rot), s = fsin(rot);
  auto P = [&](float px, float py) { return SDL_FPoint{x + px * c - py * s, y + px * s + py * c}; };
  b.beginPath();
  for (int i = 0; i <= 10; ++i) { const float a = static_cast<float>(PI) + static_cast<float>(PI) * i / 10; const auto p = P(fcos(a) * 10, -2 + fsin(a) * 10); b.lineTo(p.x, p.y); }
  b.stroke(7, hex(0xe0585f));
  for (float side : {-10.0f, 10.0f}) {
    const auto a = P(side, -2), m = P(side, 8), e = P(side, 13);
    b.line(a.x, a.y, m.x, m.y, 7, hex(0xe0585f));
    b.line(m.x, m.y, e.x, e.y, 7, hex(0xdfe8ef));
  }
}

void drawGems(Frame& f) {
  auto& b = f.b;
  for (const auto& gem : f.game.gems) {
    if (gem.dead) continue;
    const float x = static_cast<float>(gem.x), y = static_cast<float>(gem.y);
    if (!f.visible(x, y)) continue;
    const float phase = f.time * 2.9f + x * 0.017f + y * 0.013f;
    const float bob = fsin(phase) * 3;
    if (gem.type == "chest") { drawChest(b, x, y + bob, f.time); continue; }
    if (gem.type == "magnet") { drawMagnet(b, x, y + bob, f.time); continue; }
    SpriteId sprite = SpriteId::Gem;
    float size = 28;
    if (gem.type == "gem") {
      sprite = gem.value >= 20 ? SpriteId::GemEpic : gem.value >= 5 ? SpriteId::GemRare : SpriteId::Gem;
      size = gem.value >= 20 ? 40.0f : gem.value >= 5 ? 33.0f : 26.0f;
    } else if (gem.type == "greenGem") sprite = SpriteId::GreenGem;
    else if (gem.type == "coin") sprite = SpriteId::Coin;
    else if (gem.type == "heart") sprite = SpriteId::Heart;
    const float spin = gem.type == "coin" ? 0.2f + std::abs(fcos(phase)) * 0.8f : 1.0f;
    b.sprite(sprite, x, y + bob, size, 0, std::min(0.95f, static_cast<float>(gem.ttl) / 4), spin, 1);
  }
}

// ------------------------------------------------------------------------------------------------
// Projectiles

void trail(BatchRenderer& b, float x, float y, float vx, float vy, float maxLength, bool special, std::uint32_t color) {
  const float speed = std::max(1.0f, std::sqrt(vx * vx + vy * vy));
  const float length = std::min(maxLength, speed * 0.14f);
  const float tx = x - vx / speed * length, ty = y - vy / speed * length;
  b.lineGradient(x, y, tx, ty, special ? 14.0f : 7.0f, A(color, 0.3f), A(color, 0));
  b.lineGradient(x, y, tx, ty, special ? 4.0f : 2.0f, A(color, 0.85f), A(color, 0));
}

void drawShotTrails(Frame& f) {
  for (const auto& s : f.game.shots) {
    const float x = static_cast<float>(s.x), y = static_cast<float>(s.y);
    if (!f.visible(x, y)) continue;
    trail(f.b, x, y, static_cast<float>(s.vx), static_cast<float>(s.vy), s.special ? 95.0f : s.shard ? 25.0f : 58.0f, s.special, kElements[std::clamp(s.color, 0, 3)]);
  }
  for (const auto& s : f.game.enemyShots) {
    const float x = static_cast<float>(s.x), y = static_cast<float>(s.y);
    if (f.visible(x, y)) trail(f.b, x, y, static_cast<float>(s.vx), static_cast<float>(s.vy), 58, false, hex(0xff7777));
  }
}

void drawShots(Frame& f) {
  auto& b = f.b;
  static constexpr SpriteId sprites[4] = {SpriteId::Bolt, SpriteId::Fire, SpriteId::Thorn, SpriteId::Blade};
  for (const auto& s : f.game.shots) {
    const float x = static_cast<float>(s.x), y = static_cast<float>(s.y);
    if (!f.visible(x, y)) continue;
    const int c = std::clamp(s.color, 0, 3);
    const float spin = c == 3 ? f.time * 9 : 0;
    const float pulse = 1 + fsin(f.time * 15 + x * 0.05f) * 0.06f;
    const float size = (s.special ? 65.0f : s.shard ? 24.0f : c == 3 ? 52.0f : 38.0f) * (s.fullmoon && s.returning ? 1.35f : 1.0f);
    b.sprite(sprites[c], x, y, size * pulse, static_cast<float>(std::atan2(s.vy, s.vx)) + spin);
  }
  for (const auto& s : f.game.enemyShots) {
    const float x = static_cast<float>(s.x), y = static_cast<float>(s.y);
    if (!f.visible(x, y)) continue;
    b.circle(x, y, 19, hex(0xff6666), 2);
    SpriteId id = native::spriteForEnemyShot(s);
    if (id == SpriteId::Unknown) id = SpriteId::Bolt;
    b.sprite(id, x, y, 38, static_cast<float>(std::atan2(s.vy, s.vx)));
  }
}

// ------------------------------------------------------------------------------------------------
// Creatures

void drawEnemies(Frame& f) {
  auto& b = f.b;
  for (const auto& e : f.game.enemies) {
    if (e.hp <= 0) continue;
    const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
    if (!e.boss && !f.visible(x, y)) continue;
    const Pose pose = f.anim.enemyPose(e);
    const float size = pose.known ? pose.size : enemyBaseSize(e);
    const SpriteId sprite = pose.known ? pose.sprite : native::spriteForEnemy(e);
    if (e.freezeFor > 0 || e.rootFor > 0) b.circle(x, y, size * 0.45f, e.freezeFor > 0 ? hex(0x8cdfff) : hex(0x92ed68), 3);
    if (e.thief) {
      b.dashedCircle(x, y, size * 0.55f, 6, 6, f.time * 30, hex(0xffd36b), 2);
      b.icon(SpriteId::Coin, x, y - size * 0.62f + fsin(f.time * 8) * 3, 22);
    }
    if (e.elite) b.circle(x, y + size * 0.3f, size * 0.42f, A(hex(0xffd36b), 0.45f + fsin(f.time * 6 + static_cast<double>(e.id)) * 0.15f), 3);
    if (e.fuse > 0) b.circle(x, y, 70, rgba(255, 90, 60, static_cast<std::uint8_t>((0.15f + std::abs(fsin(f.time * 22)) * 0.2f) * 255)));
    if (e.dashWarn > 0) {
      const float c = fcos(e.dashAngle), s = fsin(e.dashAngle);
      auto P = [&](float px, float py) { return SDL_FPoint{x + px * c - py * s, y + px * s + py * c}; };
      const SDL_FPoint p0 = P(0, -45), p1 = P(300, -45), p2 = P(300, 45), p3 = P(0, 45);
      b.quad(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, rgba(245, 85, 80, 46));
      b.beginPath(); b.lineTo(p0.x, p0.y); b.lineTo(p1.x, p1.y); b.lineTo(p2.x, p2.y); b.lineTo(p3.x, p3.y); b.stroke(2, hex(0xef7f78), true);
    }
    b.ellipse(x, y + size * 0.36f, size * 0.28f, size * 0.09f, rgba(0, 0, 0, static_cast<std::uint8_t>(pose.alpha * 0.24f * 255)));
    b.sprite(sprite, x + pose.x, y + pose.y, size, pose.rotation, pose.alpha, pose.sx, pose.sy, std::max(pose.flash, e.windup > 0 ? 0.45f : 0.0f));
    if (e.windup > 0) b.textOutlined(x, y - size * 0.55f - 20, "!", 20, hex(0xff6b5e), rgba(10, 10, 16, 200), Align::Center);
    if (e.boss) continue;
    if (e.elite || e.hp < e.maxHp) {
      const float w = e.elite ? 56.0f : 40.0f, h = e.elite ? 5.0f : 3.0f, by = y - size * 0.53f;
      b.rect(x - w / 2, by, w, h, hex(0x10151a));
      b.rect(x - w / 2, by, w * static_cast<float>(std::clamp(e.hp / e.maxHp, 0.0, 1.0)), h, e.elite ? hex(0xffc34d) : hex(0xb95465));
    }
  }
}

void drawFamiliar(BatchRenderer& b, float x, float y, std::uint32_t color, float time, bool evolved) {
  const float flap = fsin(time * 16) * 0.5f + 0.5f;
  const float scale = evolved ? 1.3f : 1.0f;
  y += fsin(time * 3.2) * 4;
  b.glow(x, y, 34 * scale, color, 0.45f);
  // Wispy tail.
  b.triangle(x - 7 * scale, y + 4 * scale, x + 7 * scale, y + 4 * scale, x + fsin(time * 6 + 1) * 5 * scale, y + 26 * scale, A(color, 0.55f));
  for (float side : {-1.0f, 1.0f}) {
    const float a = -0.25f - flap * 0.55f, c = fcos(a), s = fsin(a);
    auto P = [&](float px, float py) { return SDL_FPoint{x + side * (px * c - py * s) * scale, y + (px * s + py * c) * scale}; };
    b.beginPath();
    for (const auto& p : {P(4, -2), P(14, -12), P(26, -4), P(16, 0), P(4, 3)}) b.lineTo(p.x, p.y);
    b.fill(A(color, 0.8f));
  }
  b.circle(x, y, 8.5f * scale, hex(0xf4fbff));
  b.circle(x, y + 1.5f * scale, 6 * scale, color);
  b.circle(x - 2.6f * scale, y - 0.5f * scale, 1.5f * scale, hex(0x0d1420));
  b.circle(x + 2.6f * scale, y - 0.5f * scale, 1.5f * scale, hex(0x0d1420));
  if (evolved) {
    b.ellipse(x, y - 14 * scale, 10 * scale, 3.5f * scale, A(hex(0xffe49b), 0.85f), 1.5f);
    for (int n = 0; n < 3; ++n) {
      const float a = time * 2 + n * TAU / 3;
      b.circle(x + fcos(a) * 20 * scale, y + fsin(a) * 20 * scale, 2 * scale, hex(0xffe49b));
    }
  }
}

void drawPlayers(Frame& f) {
  auto& b = f.b;
  for (const auto& [_, p] : f.game.players) {
    const float x = static_cast<float>(p.x), y = static_cast<float>(p.y);
    if (!f.visible(x, y, 260)) continue;
    const int c = std::clamp(p.color, 0, 3);
    const std::uint32_t color = kElements[c];
    if (p.alive) {
      if (const int aura = rankOf(p, "aura")) {
        const bool sanctuary = rankOf(p, "sanctuary") > 0;
        const float r = static_cast<float>(70 + aura * 10 + (sanctuary ? 30 : 0));
        const std::uint32_t ac = sanctuary ? hex(0x9dffca) : color;
        b.circle(x, y, r, A(ac, 0.1f + fsin(f.time * 4) * 0.03f));
        b.circle(x, y, r, A(ac, 0.35f), 1.5f);
      }
    } else if (!f.game.over) {
      b.circle(x, y, static_cast<float>(cfg::REVIVE_RADIUS), hex(0xeaaa91), 2);
      b.arc(x, y, static_cast<float>(cfg::REVIVE_RADIUS), static_cast<float>(cfg::REVIVE_RADIUS), -HALF_PI,
            -HALF_PI + TAU * static_cast<float>(p.reviveProgress / cfg::REVIVE_SECONDS), hex(0x8dffcc), 5);
      if (p.reviveProgress > 0)
        for (int i = 0; i < 3; ++i) {
          const float a = f.time * 2.2f + i * TAU / 3;
          b.circle(x + fcos(a) * static_cast<float>(cfg::REVIVE_RADIUS), y + fsin(a) * static_cast<float>(cfg::REVIVE_RADIUS), 3, hex(0x9dffca));
        }
    }
    if (p.alive && (!p.pendingPowers.empty() || p.invulnerableFor > 0)) {
      const float pulse = 40 + fsin(f.time * 7.7) * 4;
      b.circle(x, y, pulse, rgba(87, 215, 180, 20));
      b.circle(x, y, pulse, rgba(126, 237, 205, 209), 2);
    }
    const Pose pose = f.anim.playerPose(p);
    if (p.alive && p.specialCharge >= cfg::SPECIAL_MAX) {
      const float alpha = 0.55f + fsin(f.time * 3) * 0.15f;
      for (int i = 0; i < 3; ++i) {
        const float a0 = f.time * 0.65f + i * TAU / 3;
        b.arc(x, y + 24, 33, 13.2f, a0, a0 + 1.5f, A(color, alpha), 2);
      }
    }
    b.ellipse(x, y + 24, 19, 6, rgba(0, 0, 0, static_cast<std::uint8_t>(pose.alpha * 0.24f * 255)));
    b.sprite(static_cast<SpriteId>(c), x + pose.x, y + pose.y, 68, pose.rotation, pose.alpha, pose.sx, pose.sy, pose.flash);
    if (p.alive) {
      if (const int orbit = std::clamp(rankOf(p, "orbit"), 0, 5)) {
        static constexpr int counts[5] = {1, 2, 2, 3, 3};
        const bool evolved = rankOf(p, "constellation") > 0;
        const int count = counts[orbit - 1] + (evolved ? 2 : 0);
        const double radius = 78 + orbit * 4 + (evolved ? 20 : 0);
        for (int n = 0; n < count; ++n) {
          const double a = p.orbitAngle + n * PI * 2 / count;
          const float ox = x + static_cast<float>(fcos(a) * radius), oy = y + static_cast<float>(fsin(a) * radius);
          b.circle(ox, oy, evolved ? 17.0f : 12.0f, A(color, 0.3f));
          b.circle(ox, oy, evolved ? 8.0f : 5.5f, hex(0xf4fbff));
        }
      }
      if (p.familiar && rankOf(p, "familiar") > 0)
        drawFamiliar(b, static_cast<float>(p.familiar->x), static_cast<float>(p.familiar->y), color, f.time + static_cast<float>(c), rankOf(p, "covenant") > 0);
    }
    char label[64];
    if (!p.alive) {
      if (f.game.over) std::snprintf(label, sizeof label, "DERROTADO");
      else std::snprintf(label, sizeof label, "REVIVER · %ds", static_cast<int>(std::ceil(cfg::REVIVE_SECONDS - p.reviveProgress)));
    } else std::snprintf(label, sizeof label, "%s", p.name.c_str());
    b.textOutlined(x, y - (p.alive ? 54.0f : 78.0f) - (f.textScale - 1) * 10, label, 11 * f.textScale, p.alive ? hex(0xc6eee2) : hex(0xffb58e), rgba(6, 10, 14, 170), Align::Center);
  }
}

void drawAfterimages(Frame& f) {
  for (const auto& fx : f.anim.effects()) {
    if (fx.kind != FxKind::Afterimage || !f.visible(fx.x, fx.y)) continue;
    const float fade = (1 - fx.age / fx.life) * (1 - fx.age / fx.life);
    f.b.sprite(fx.sprite, fx.x, fx.y, 68, 0, fade * 0.3f, fx.facing, 1, 0.5f);
    f.b.ellipse(fx.x, fx.y + 24, 20, 6, A(fx.color, fade * 0.25f), 2);
  }
}

// ------------------------------------------------------------------------------------------------
// Effects

void jagged(BatchRenderer& b, const Effect& fx, float progress, float width, std::uint32_t color) {
  const auto& pts = fx.points;
  if (pts.size() < 4) return;
  b.beginPath();
  b.lineTo(pts[0], pts[1]);
  for (std::size_t i = 2; i + 1 < pts.size(); i += 2) {
    const float x0 = pts[i - 2], y0 = pts[i - 1], x1 = pts[i], y1 = pts[i + 1];
    const float nx = -(y1 - y0), ny = x1 - x0, len = std::max(1.0f, std::sqrt(nx * nx + ny * ny));
    for (int k = 1; k <= 3; ++k) {
      const float t = k / 4.0f, wobble = fsin(fx.seed * 12.9898 + i * 78.233 + k * 3.1 + progress * 20) * 12;
      b.lineTo(x0 + (x1 - x0) * t + nx / len * wobble, y0 + (y1 - y0) * t + ny / len * wobble);
    }
    b.lineTo(x1, y1);
  }
  b.stroke(width, color);
}

void crescent(BatchRenderer& b, float x, float y, float radius, float angle, std::uint32_t color) {
  // Canvas crescent = outer arc minus an offset inner arc; a thick arc reads the same at this size.
  b.arc(x, y, radius * 0.8f, radius * 0.8f, angle - static_cast<float>(PI) * 0.62f, angle + static_cast<float>(PI) * 0.62f, color, radius * 0.4f);
}

// Pass 1 (normal blending): ghosts, thorns, flakes/leaves/heals/smoke, meteor reticle, labels.
void drawEffectsNormal(Frame& f) {
  auto& b = f.b;
  for (const auto& fx : f.anim.effects()) {
    const float progress = fx.age / fx.life, fade = 1 - progress;
    const bool unculled = fx.kind == FxKind::Chain || fx.kind == FxKind::FamiliarStrike || fx.kind == FxKind::Lunar || fx.kind == FxKind::Convergence;
    if (!unculled && !f.visible(fx.x, fx.y)) continue;
    switch (fx.kind) {
      case FxKind::Ghost:
        b.sprite(fx.sprite, fx.x, fx.y + progress * 10, fx.size * (1 - progress * 0.35f), progress * 0.3f, fade * 0.65f);
        break;
      case FxKind::Combo:
        b.textOutlined(fx.x, fx.y - 48 - progress * 25, fx.text ? fx.text : "", 12, A(fx.color, fade), A(rgba(10, 10, 16), fade * 0.8f), Align::Center);
        break;
      case FxKind::Chain:
        jagged(b, fx, progress, 5, A(fx.color, fade * 0.35f));
        jagged(b, fx, progress, 2, A(hex(0xf4fbff), fade));
        break;
      case FxKind::Thorns: {
        const float grow = easeOut(std::min(1.0f, progress * 2.5f));
        const float alpha = progress > 0.7f ? 1 - (progress - 0.7f) / 0.3f : 1;
        b.glow(fx.x, fx.y, std::max(11.0f, fx.radius * grow), hex(0xd1ff93), alpha * 0.35f);
        for (int n = 0; n < 14; ++n) {
          const float a = n * TAU / 14 + fx.seed, bend = (n % 2 ? 1.0f : -1.0f) * 0.5f;
          const float reach = fx.radius * grow * (0.7f + static_cast<float>(n * 37 % 10) / 33.0f);
          const float cx = fx.x + fcos(a + bend) * reach * 0.5f, cy = fx.y + fsin(a + bend) * reach * 0.5f;
          const float ex = fx.x + fcos(a) * reach, ey = fx.y + fsin(a) * reach;
          auto Q = [&](float t) { const float u = 1 - t; return SDL_FPoint{u * u * fx.x + 2 * u * t * cx + t * t * ex, u * u * fx.y + 2 * u * t * cy + t * t * ey}; };
          for (int pass = 0; pass < 2; ++pass) {
            b.beginPath();
            for (int k = 0; k <= 6; ++k) { const auto p = Q(k / 6.0f); b.lineTo(p.x, p.y); }
            b.stroke(pass ? 3.0f : 7.0f, A(pass ? hex(0x92ed68) : hex(0x2f7a3a), alpha));
          }
          for (int k = 1; k <= 3; ++k) {
            const auto p = Q(k / 4.0f);
            const float side = a + (k % 2 ? 1.0f : -1.0f) * HALF_PI;
            b.triangle(p.x + fcos(a) * 4, p.y + fsin(a) * 4, p.x + fcos(side) * 9, p.y + fsin(side) * 9,
                       p.x - fcos(a) * 4, p.y - fsin(a) * 4, A(hex(0xd1ff93), alpha));
          }
          b.triangle(ex + fcos(a) * 16, ey + fsin(a) * 16, ex + fcos(a + 1.3f) * 6, ey + fsin(a + 1.3f) * 6,
                     ex + fcos(a - 1.3f) * 6, ey + fsin(a - 1.3f) * 6, A(hex(0xf0ffd2), alpha));
        }
        break;
      }
      case FxKind::Meteor: {
        // The target reticle tightens as the rock falls.
        const std::uint32_t c = A(hex(0xffd36b), 0.5f + progress * 0.5f);
        b.dashedCircle(fx.x, fx.y, 165 * (1.25f - progress * 0.25f), 14, 10, -progress * 60, c, 2);
        b.circle(fx.x, fx.y, 18 + (1 - progress) * 30, c, 2);
        break;
      }
      case FxKind::Mote: {
        const float r = fx.size, rot = fx.spin * fx.age;
        if (fx.shape == MoteShape::Flake) {
          for (int n = 0; n < 3; ++n) {
            const float a = rot + n * static_cast<float>(PI) / 3;
            b.line(fx.x - fcos(a) * r, fx.y - fsin(a) * r, fx.x + fcos(a) * r, fx.y + fsin(a) * r, 1.5f, A(fx.color, fade));
          }
        } else if (fx.shape == MoteShape::Leaf) {
          b.ellipse(fx.x, fx.y, r, r * 0.42f, A(fx.color, fade), 0, rot);
          b.line(fx.x - fcos(rot) * r, fx.y - fsin(rot) * r, fx.x + fcos(rot) * r, fx.y + fsin(rot) * r, 1, rgba(40, 90, 40, static_cast<std::uint8_t>(153 * fade)));
        } else if (fx.shape == MoteShape::Heal) {
          b.rect(fx.x - r / 2, fx.y - r / 6, r, r / 3, A(fx.color, fade));
          b.rect(fx.x - r / 6, fx.y - r / 2, r / 3, r, A(fx.color, fade));
        } else if (fx.shape == MoteShape::Smoke) {
          b.circle(fx.x, fx.y, r * (0.6f + progress * 0.8f), A(fx.color, fade * 0.5f));
        }
        break;
      }
      case FxKind::Ascend: {
        const float y = fx.y - 54 - easeOut(progress) * 26;
        b.textOutlined(fx.x, y - 14, "NÍVEL +", 13, A(fx.color, std::min(1.0f, fade * 3)), A(hex(0x15151d), std::min(1.0f, fade * 3)), Align::Center);
        break;
      }
      default: break;
    }
  }
}

// Pass 2 (additive, the canvas "lighter" operator): light, fire, magic.
void drawEffectsAdditive(Frame& f) {
  auto& b = f.b;
  for (const auto& fx : f.anim.effects()) {
    const float progress = fx.age / fx.life, fade = 1 - progress;
    const bool unculled = fx.kind == FxKind::Chain || fx.kind == FxKind::FamiliarStrike || fx.kind == FxKind::Lunar || fx.kind == FxKind::Convergence;
    if (!unculled && !f.visible(fx.x, fx.y)) continue;
    switch (fx.kind) {
      case FxKind::Ring:
        b.circle(fx.x, fx.y, 5 + easeOut(progress) * fx.radius, A(fx.color, fade * fade), 1 + fade * 2);
        break;
      case FxKind::Spark: {
        const float x = fx.x + fx.vx * fx.age, y = fx.y + fx.vy * fx.age + 35 * fx.age * fx.age;
        b.line(x, y, x - fx.vx * 0.045f, y - (fx.vy + 70 * fx.age) * 0.045f, 2 * fade + 0.5f, A(fx.color, fade));
        break;
      }
      case FxKind::Mote: {
        const float r = fx.size;
        if (fx.shape == MoteShape::Ember) {
          b.glow(fx.x, fx.y, r * 2.4f, fx.color, fade * 0.7f);
          b.circle(fx.x, fx.y, r * 0.45f * fade + 0.8f, A(hex(0xfff1c4), fade));
        } else if (fx.shape == MoteShape::Star) {
          const float alpha = fade * (0.6f + std::abs(fsin(fx.age * 18 + fx.size)) * 0.4f);
          const float rot = fx.spin * fx.age;
          b.beginPath();
          for (int n = 0; n < 8; ++n) {
            const float a = rot + n * static_cast<float>(PI) / 4, d = n % 2 ? r * 0.28f : r;
            b.lineTo(fx.x + fcos(a) * d, fx.y + fsin(a) * d);
          }
          b.fill(A(fx.color, alpha));
        }
        break;
      }
      case FxKind::Nova: {
        const float radius = fx.radius * easeOut(std::min(1.0f, progress * 1.6f));
        b.circle(fx.x, fx.y, radius * 0.8f, A(rgba(160, 236, 255), fade * 0.3f), radius * 0.35f);
        b.circle(fx.x, fx.y, radius, A(hex(0xe8fbff), fade), 3 * fade + 1);
        for (int n = 0; n < 12; ++n) {
          const float a = n * TAU / 12 + fx.seed, reach = radius * (0.72f + (n % 3) * 0.1f), len = 26.0f + (n % 4) * 8;
          const float cx = fx.x + fcos(a) * reach, cy = fx.y + fsin(a) * reach, ca = fcos(a), sa = fsin(a);
          b.quad(cx + ca * len, cy + sa * len, cx + sa * 6, cy - ca * 6, cx - ca * len * 0.4f, cy - sa * len * 0.4f, cx - sa * 6, cy + ca * 6,
                 A(n % 2 ? hex(0xbff4ff) : hex(0x76dfff), fade * 0.9f));
        }
        b.glow(fx.x, fx.y, 90 * (1 - progress * 0.5f), hex(0xe8fbff), fade * 0.8f);
        break;
      }
      case FxKind::Meteor: {
        const float t = progress * progress;
        const float sx = fx.x + 260, sy = fx.y - 520;
        const float x = sx + (fx.x - sx) * t, y = sy + (fx.y - sy) * t;
        const float dx = fx.x - sx, dy = fx.y - sy, len = std::sqrt(dx * dx + dy * dy);
        const float tx = x - dx / len * 220, ty = y - dy / len * 220;
        b.lineGradient(x, y, tx, ty, 26, rgba(255, 200, 110, 242), rgba(255, 60, 20, 0));
        b.glow(x, y, 60, hex(0xff9955), 0.9f);
        b.circle(x, y, 15, hex(0xfff1c4));
        break;
      }
      case FxKind::Impact:
        b.glow(fx.x, fx.y, fx.radius * (0.6f + progress * 0.6f), hex(0xffb347), fade * 0.9f);
        for (int n = 0; n < 2; ++n)
          b.circle(fx.x, fx.y, fx.radius * easeOut(std::min(1.0f, progress * (1.8f - n * 0.5f))) * (1.1f - n * 0.3f), A(hex(0xffe1a0), fade), (8 - n * 4) * fade + 1);
        break;
      case FxKind::Lunar: {
        const float angle = std::atan2(fx.y - fx.fromY, fx.x - fx.fromX);
        for (int n = 0; n < 5; ++n) {
          const float t = n / 4.0f;
          crescent(b, fx.fromX + (fx.x - fx.fromX) * t, fx.fromY + (fx.y - fx.fromY) * t, 18 + t * 10, angle,
                   A(n == 4 ? hex(0xf1e6ff) : hex(0xc4a0ff), fade * (0.2f + t * 0.5f)));
        }
        const float sweep = easeOut(std::min(1.0f, progress * 1.5f)), pi = static_cast<float>(PI);
        b.arc(fx.x, fx.y, 40 + sweep * 150, 40 + sweep * 150, angle - pi * sweep, angle + pi * sweep, A(hex(0xe4d4ff), fade), 5 * fade + 1);
        b.arc(fx.x, fx.y, 20 + sweep * 110, 20 + sweep * 110, angle + pi - pi * sweep, angle + pi + pi * sweep, A(hex(0xc4a0ff), fade), 2);
        b.glow(fx.x, fx.y, 110, hex(0xc4a0ff), fade * 0.6f);
        break;
      }
      case FxKind::Bloom: {
        const float grow = easeOut(std::min(1.0f, progress * 1.8f));
        b.glow(fx.x, fx.y, fx.radius * grow, hex(0x9dffca), fade * 0.45f);
        b.circle(fx.x, fx.y, fx.radius * grow, A(hex(0xd1ff93), fade), 3 * fade + 1);
        for (int n = 0; n < 8; ++n) {
          const float a = n * TAU / 8 + fx.seed + progress;
          b.ellipse(fx.x + fcos(a) * 60 * grow, fx.y + fsin(a) * 60 * grow, 26 * grow, 10 * grow, A(n % 2 ? hex(0xb8f57f) : hex(0xf7ffd9), fade * 0.85f), 0, a);
        }
        break;
      }
      case FxKind::Implode:
        for (int n = 0; n < 3; ++n) {
          const float r = fx.radius * std::max(0.0f, 1 - easeOut(std::min(1.0f, progress * 1.6f + n * 0.15f)));
          b.circle(fx.x, fx.y, r + 4, A(hex(0xd9c2ff), fade * (0.8f - n * 0.2f)), 3.0f - n);
        }
        b.glow(fx.x, fx.y, 70, hex(0xc4a0ff), fade * 0.7f);
        break;
      case FxKind::Convergence: {
        const float grow = easeOut(std::min(1.0f, progress * 1.5f));
        b.glow(fx.x, fx.y, fx.radius * (0.4f + grow * 0.7f), fx.color, fade * 0.6f);
        b.glow(fx.x, fx.y, fx.radius * (0.2f + grow * 0.5f), fx.color2, fade * 0.6f);
        for (int n = 0; n < 2; ++n) b.circle(fx.x, fx.y, fx.radius * grow * (1 - n * 0.25f), A(n ? fx.color2 : fx.color, fade), 6 * fade + 1);
        for (int n = 0; n < 12; ++n) {
          const float a = n * TAU / 12 + progress * 0.8f;
          b.line(fx.x + fcos(a) * 30, fx.y + fsin(a) * 30, fx.x + fcos(a) * fx.radius * grow, fx.y + fsin(a) * fx.radius * grow,
                 4 * fade, A(n % 2 ? fx.color2 : fx.color, fade));
        }
        break;
      }
      case FxKind::FamiliarStrike: {
        const float head = std::min(1.0f, progress * 3.5f), u = 1 - head;
        for (std::size_t i = 0; i + 1 < fx.points.size(); i += 2) {
          const float tx = fx.points[i], ty = fx.points[i + 1];
          const float mx = (fx.x + tx) / 2 + fsin(fx.seed + static_cast<float>(i)) * 40, my = (fx.y + ty) / 2 - 50;
          for (int pass = 0; pass < 2; ++pass) {
            b.beginPath();
            for (int k = 0; k <= 8; ++k) {
              const float t = k / 8.0f, w = 1 - t;
              b.lineTo(w * w * fx.x + 2 * w * t * mx + t * t * tx, w * w * fx.y + 2 * w * t * my + t * t * ty);
            }
            b.stroke(pass ? 2.0f : (fx.evolved ? 10.0f : 7.0f), pass ? A(0xffffffffu, fade) : A(fx.color, fade * 0.4f));
          }
          b.glow(u * u * fx.x + 2 * u * head * mx + head * head * tx, u * u * fx.y + 2 * u * head * my + head * head * ty, 22, fx.color, fade);
        }
        break;
      }
      case FxKind::Sigil: {
        const float radius = fx.radius * (0.7f + easeOut(progress) * 0.3f), rot = fx.seed + progress * 0.5f;
        const std::uint32_t c = A(fx.color, fade * fade * 0.7f);
        auto P = [&](float a, float r) { return SDL_FPoint{fx.x + fcos(a + rot) * r, fx.y + fsin(a + rot) * r * 0.42f}; };
        b.ellipse(fx.x, fx.y, radius, radius * 0.42f, c, 2);
        b.beginPath();
        for (int i = 0; i < 6; ++i) { const auto p = P(i * TAU / 6, radius * 0.72f); b.lineTo(p.x, p.y); }
        b.stroke(2, c, true);
        for (int i = 0; i < 6; ++i) {
          const auto p0 = P(i * TAU / 6, radius * 0.88f), p1 = P(i * TAU / 6, radius * 1.15f);
          b.line(p0.x, p0.y, p1.x, p1.y, 2, c);
        }
        break;
      }
      case FxKind::Ascend: {
        const float alpha = fsin(static_cast<float>(PI) * progress) * fade;
        const float radius = fx.radius * (0.4f + easeOut(progress) * 0.6f);
        b.quadGradient(fx.x - 28, fx.y + 24, fx.x - 48, fx.y - 150, fx.x + 48, fx.y - 150, fx.x + 28, fx.y + 24,
                       A(hex(0xffe49b), alpha * 0.3f), A(hex(0xffe49b), 0), A(hex(0xffe49b), 0), A(hex(0xffe49b), alpha * 0.3f));
        b.ellipse(fx.x, fx.y + 24, radius, radius * 0.38f, A(fx.color, fade * fade), 2);
        break;
      }
      default: break;
    }
  }
}

void drawNumbers(Frame& f) {
  char text[16];
  for (const auto& n : f.anim.numbers()) {
    if (!f.visible(n.x, n.y)) continue;
    const float progress = n.age / n.life;
    const float rise = easeOut(progress) * 32;
    const bool big = n.value >= 60 || n.boss;
    const float pop = 1 + std::round(fsin(std::min(1.0f, progress / 0.3f) * static_cast<float>(PI)) * 3) * 0.1f;
    const float size = (big ? 19.0f : 14.0f) * pop * f.textScale;
    std::snprintf(text, sizeof text, "%d", static_cast<int>(std::lround(n.value)));
    const float alpha = std::min(1.0f, (1 - progress) * 1.6f);
    f.b.textOutlined(n.x, n.y - rise - size * 0.8f, text, size, A(big ? hex(0xffd36b) : hex(0xfff3e6), alpha), A(rgba(8, 10, 16, 217), alpha), Align::Center);
  }
}

// ------------------------------------------------------------------------------------------------
// Screen space

void edgeArrow(BatchRenderer& b, const WorldView& v, float worldX, float worldY, std::uint32_t color, const char* label) {
  const float dx = worldX - v.camX, dy = worldY - v.camY;
  const float angle = std::atan2(dy, dx), c = fcos(angle), s = fsin(angle);
  // Small screens (PSP) keep the arrows near the edges instead of the 720p margins.
  const float mx = v.height < 400 ? 22.0f : 55.0f, my = v.height < 400 ? 26.0f : 75.0f;
  const float reach = std::min((v.width / 2 - mx) / std::max(0.001f, std::abs(c)), (v.height / 2 - my) / std::max(0.001f, std::abs(s)));
  const float x = v.width / 2 + c * reach, y = v.height / 2 + s * reach;
  auto P = [&](float px, float py) { return SDL_FPoint{x + px * c - py * s, y + px * s + py * c}; };
  b.glow(x, y, 26, color, 0.35f);
  const auto p0 = P(15, 0), p1 = P(-9, -9), p2 = P(-5, 0), p3 = P(-9, 9);
  b.triangle(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, color);
  b.triangle(p0.x, p0.y, p2.x, p2.y, p3.x, p3.y, color);
  char text[64];
  std::snprintf(text, sizeof text, "%s · %dm", label, static_cast<int>(std::sqrt(dx * dx + dy * dy) / 10));
  b.textOutlined(x, y + 14, text, 12, hex(0xe6f5ef), rgba(6, 10, 14, 180), Align::Center);
}

} // namespace

std::uint32_t elementColor(int color, std::uint8_t alpha) { return native::withAlpha(kElements[std::clamp(color, 0, 3)], alpha); }

void drawWorld(BatchRenderer& b, const GameState& game, const Animator& anim, const WorldView& v) {
  const float zoom = v.zoom;
  const float halfW = v.width / 2 / zoom, halfH = v.height / 2 / zoom;
  Frame f{b, game, anim, v, anim.time(), v.camX - halfW, v.camY - halfH, v.camX + halfW, v.camY + halfH, std::max(1.0f, 0.85f / zoom)};
  // Shake is in world units (like the web client, where 1 world unit = 1 css pixel).
  b.setTransform(v.width / 2 - (v.camX - anim.shakeX()) * zoom, v.height / 2 - (v.camY - anim.shakeY()) * zoom, zoom);

  // Floor, then the web client's darkening wash.
  b.terrain(game.phase, v.width, v.height);
  b.resetTransform();
  b.rect(0, 0, v.width, v.height, rgba(3, 8, 13, 77));
  b.setTransform(v.width / 2 - (v.camX - anim.shakeX()) * zoom, v.height / 2 - (v.camY - anim.shakeY()) * zoom, zoom);

  b.setAdditive(true);
  drawAtmosphere(f);
  for (const auto& z : game.zones)
    if (z.kind == "flameshield" && f.visible(static_cast<float>(z.x), static_cast<float>(z.y), static_cast<float>(z.radius))) {
      const float x = static_cast<float>(z.x), y = static_cast<float>(z.y), r = static_cast<float>(z.radius);
      b.glow(x, y, r, hex(0xff8c3c), 0.34f);
      for (int n = 0; n < 10; ++n) {
        const float a = f.time * 3 + n * TAU / 10, fx = x + fcos(a) * r * 0.85f, fy = y + fsin(a) * r * 0.85f;
        b.glow(fx, fy, 20, hex(0xffb06b), 0.9f);
        b.circle(fx, fy, 5, A(hex(0xfff1c4), 0.72f));
      }
    }
  b.setAdditive(false);

  if (game.altar) drawAltar(f);
  if (game.encounter) drawEncounter(f, *game.encounter);
  for (const auto& z : game.zones) {
    if (!f.visible(static_cast<float>(z.x), static_cast<float>(z.y), static_cast<float>(z.radius))) continue;
    if (z.kind == "flameshield") continue;
    if (z.kind == "hail" || z.kind == "vortex") drawSpecialZone(f, z);
    else drawZone(f, z);
  }
  drawRunesAndHazards(f);
  drawGems(f);

  b.setAdditive(true);
  drawShotTrails(f);
  drawAfterimages(f);
  b.setAdditive(false);
  drawShots(f);
  drawEnemies(f);
  drawPlayers(f);

  drawEffectsNormal(f);
  b.setAdditive(true);
  drawEffectsAdditive(f);
  b.setAdditive(false);
  drawNumbers(f);

  b.resetTransform();
  if (const float flash = anim.flashAlpha(); flash > 0.01f) b.rect(0, 0, v.width, v.height, A(anim.flashColor(), flash));

  // Off-screen guides: boss, altar, active encounter.
  auto inView = [&](double wx, double wy) {
    const float sx = (static_cast<float>(wx) - v.camX) * zoom + v.width / 2, sy = (static_cast<float>(wy) - v.camY) * zoom + v.height / 2;
    return sx >= 45 && sx <= v.width - 45 && sy >= 65 && sy <= v.height - 55;
  };
  for (const auto& e : game.enemies)
    if (e.boss && e.hp > 0) { if (!inView(e.x, e.y)) edgeArrow(b, v, static_cast<float>(e.x), static_cast<float>(e.y), hex(0xff6b5e), "GUARDIÃO"); break; }
  if (game.encounter && game.phaseStatus == "horde" && game.encounter->status != "complete" && game.encounter->status != "expired" &&
      !inView(game.encounter->x, game.encounter->y)) {
    const auto& enc = *game.encounter;
    const bool merchant = enc.kind == "merchant", thief = enc.kind == "thief";
    double ex = enc.x, ey = enc.y;
    if (thief) for (const auto& e : game.enemies) if (e.thief) { ex = e.x; ey = e.y; break; }
    if (!inView(ex, ey)) edgeArrow(b, v, static_cast<float>(ex), static_cast<float>(ey), merchant || thief ? hex(0xffd36b) : hex(0xff7aa8),
                                   merchant ? "MERCADOR" : thief ? "LADRÃO" : "SANTUÁRIO");
  }
  if (game.altar && (game.altar->status == "waiting" || game.altar->status == "active") && !inView(game.altar->x, game.altar->y))
    edgeArrow(b, v, static_cast<float>(game.altar->x), static_cast<float>(game.altar->y), hex(0xffd36b), "ALTAR");
}

} // namespace arcana::sdl
