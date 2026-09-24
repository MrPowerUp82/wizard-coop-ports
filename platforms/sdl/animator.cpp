#include "animator.hpp"
#include "fastmath.hpp"
#include "arcana/data.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace arcana::sdl {
namespace {

constexpr float TAU = static_cast<float>(2 * PI);
using native::rgba;

constexpr std::uint32_t hex(std::uint32_t rgb, std::uint8_t a = 255) {
  return rgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb), a);
}
std::uint32_t element(int c) { return native::playerColor(c); }

float sign(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }

bool floatingSprite(native::SpriteId id) {
  using S = native::SpriteId;
  return id == S::Wraith || id == S::Eye || id == S::Bat || id == S::Lich || id == S::Revenant || id == S::Seer ||
         id == S::Voidling || id == S::Archon;
}

float enemySize(const Enemy& e) {
  const auto it = enemyDefs().find(e.type);
  const double base = it != enemyDefs().end() && it->second.size > 0 ? it->second.size : 64.0;
  return static_cast<float>(base * (e.elite ? 1.35 : 1.0));
}

} // namespace

float Animator::random() {
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return static_cast<float>(rng_ >> 8) / 16777216.0f;
}

std::uint64_t Animator::playerKey(const Player& p) {
  return (1ull << 63) | (std::hash<std::string>{}(p.id) & 0x7fffffffffffffffull);
}

void Animator::reset() {
  actors_.clear();
  index_.fill(-1);
  effects_.clear();
  numbers_.clear();
  zoneWarnings_.clear();
  time_ = shake_ = freeze_ = 0;
  flashLife_ = 0;
  lastEventId_ = 0;
  haveEventId_ = false;
  lastPhase_ = -1;
  lastStatus_.clear();
}

// ---- actor index (open addressing, rebuilt on removal) ----------------------------------------

void Animator::rebuildIndex() {
  index_.fill(-1);
  for (std::size_t i = 0; i < actors_.size(); ++i) {
    std::size_t slot = static_cast<std::size_t>(actors_[i].key * 0x9E3779B97F4A7C15ull >> 55) & (index_.size() - 1);
    while (index_[slot] >= 0) slot = (slot + 1) & (index_.size() - 1);
    index_[slot] = static_cast<std::int16_t>(i);
  }
}

const Animator::Actor* Animator::find(std::uint64_t key) const {
  std::size_t slot = static_cast<std::size_t>(key * 0x9E3779B97F4A7C15ull >> 55) & (index_.size() - 1);
  for (std::size_t n = 0; n < index_.size(); ++n) {
    const int i = index_[slot];
    if (i < 0) return nullptr;
    if (actors_[static_cast<std::size_t>(i)].key == key) return &actors_[static_cast<std::size_t>(i)];
    slot = (slot + 1) & (index_.size() - 1);
  }
  return nullptr;
}
Animator::Actor* Animator::find(std::uint64_t key) { return const_cast<Actor*>(static_cast<const Animator*>(this)->find(key)); }

// ---- effect helpers ----------------------------------------------------------------------------

Effect* Animator::push(FxKind kind, float x, float y, float life, bool major) {
  if (effects_.size() == effects_.capacity()) {
    // Signature spell visuals are few and long-lived; drop the oldest sparks before them.
    auto it = std::find_if(effects_.begin(), effects_.end(), [](const Effect& fx) { return !fx.major; });
    effects_.erase(it == effects_.end() ? effects_.begin() : it);
  }
  Effect* fx = effects_.emplace_back();
  if (!fx) return nullptr;
  *fx = Effect{};
  fx->kind = kind; fx->x = x; fx->y = y; fx->life = life; fx->major = major;
  return fx;
}

void Animator::burst(float x, float y, std::uint32_t color, int count, float radius) {
  if (Effect* ring = push(FxKind::Ring, x, y, 0.45f)) { ring->color = color; ring->radius = radius; }
  for (int i = 0; i < count; ++i) {
    float sa, ca;
    fastSinCos(TAU * static_cast<float>(i) / static_cast<float>(count), sa, ca);
    if (Effect* s = push(FxKind::Spark, x, y, 0.3f + static_cast<float>(i % 3) * 0.1f)) {
      s->color = color;
      s->vx = ca * (35 + static_cast<float>(i % 3) * 18);
      s->vy = sa * 65 - 18;
    }
  }
}

void Animator::motes(float x, float y, MoteShape shape, std::uint32_t color, int count, float speed, float spread, float life,
                     float gravity, float size, float drag) {
  for (int i = 0; i < count; ++i) {
    const float angle = TAU * (static_cast<float>(i) + random() * 0.6f) / static_cast<float>(count);
    const float velocity = speed * (0.45f + random() * 0.75f);
    const float offset = spread * random();
    Effect* m = push(FxKind::Mote, x + std::cos(angle) * offset, y + std::sin(angle) * offset, life * (0.7f + random() * 0.3f));
    if (!m) return;
    m->shape = shape; m->color = color;
    m->vx = std::cos(angle) * velocity; m->vy = std::sin(angle) * velocity;
    m->gravity = gravity; m->drag = drag; m->size = size * (0.6f + random() * 0.8f);
    m->spin = (random() - 0.5f) * 8;
  }
}

void Animator::setFlash(std::uint32_t color, float alpha, float life) {
  flashColor_ = color; flashAlpha_ = alpha; flashLife_ = life; flashAge_ = 0;
}

void Animator::addNumber(Actor& actor, float x, float y, float amount) {
  for (auto& n : numbers_) {
    if (n.serial == actor.numberSerial && n.age < 0.18f) {
      n.value += amount; n.age = std::min(n.age, 0.08f); n.x = x; n.y = y;
      return;
    }
  }
  if (numbers_.size() == numbers_.capacity()) numbers_.erase(numbers_.begin());
  DamageNumber n;
  n.x = x + (random() - 0.5f) * 14; n.y = y; n.value = amount; n.boss = actor.boss;
  n.serial = actor.numberSerial = ++numberSerial_;
  numbers_.push_back(n);
}

float Animator::zoneWarning(std::uint64_t zoneId) const {
  for (const auto& z : zoneWarnings_) if (z.id == zoneId) return z.warning;
  return 0;
}

float Animator::shakeX() const { return shake_ > 0 ? std::sin(time_ * 91) * shake_ : 0; }
float Animator::shakeY() const { return shake_ > 0 ? std::cos(time_ * 67) * shake_ : 0; }

// ---- specials ----------------------------------------------------------------------------------

void Animator::altSpecial(const Event& e, float seed) {
  const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
  if (e.color == 0) {
    if (Effect* fx = push(FxKind::Nova, x, y, 0.6f, true)) { fx->radius = 230; fx->seed = seed; }
    motes(x, y, MoteShape::Flake, hex(0xe8fbff), 18, 160, 120, 1.4f, 60, 6, 1.2f);
    setFlash(hex(0xbdf3ff), 0.18f, 0.3f);
  } else if (e.color == 1) {
    if (Effect* fx = push(FxKind::Impact, x, y, 0.5f, true)) fx->radius = 125;
    motes(x, y, MoteShape::Ember, hex(0xffb347), 22, 220, 40, 0.9f, -80, 5, 2);
    setFlash(hex(0xffb36b), 0.16f, 0.25f);
  } else if (e.color == 2) {
    if (Effect* fx = push(FxKind::Bloom, x, y, 1.1f, true)) { fx->radius = 320; fx->seed = seed; }
    motes(x, y, MoteShape::Leaf, hex(0xb8f57f), 16, 240, 0, 1, 30, 7, 2);
    motes(x, y, MoteShape::Heal, hex(0x9dffca), 14, 90, 90, 1.2f, -70, 7, 2);
    setFlash(hex(0x9dffca), 0.18f, 0.35f);
  } else {
    if (Effect* fx = push(FxKind::Implode, x, y, 0.7f, true)) fx->radius = 230;
    motes(x, y, MoteShape::Star, hex(0xf1e6ff), 14, -160, 200, 0.8f, 0, 5, 2);
    setFlash(hex(0xcdb4ff), 0.14f, 0.25f);
  }
  shake(5);
}

void Animator::special(const Event& e) {
  const float seed = static_cast<float>(e.id) * 7.31f;
  const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
  if (e.color == AURORA || e.color == GOD) {
    // Alvorada's solar burst, or the tighter crown of Coroa da aurora (seed = variant).
    const auto color=e.color==GOD?hex(0x36bfff):hex(0xffd778);
    if (Effect* fx = push(FxKind::Aurora, x, y, 0.9f, true)) { fx->color = color; fx->radius = e.variant == 1 ? 160 : 300; fx->seed = static_cast<float>(e.variant); }
    motes(x, y, MoteShape::Star, color, 16, 220, 25, 0.8f, 0, 5, 2);
    return;
  }
  if (e.color == DEVELOPER) {
    if (e.variant == 1) {
      // Restauração do sistema: a magenta scan across the whole view, unlike the cyan radial wave.
      if (Effect* fx = push(FxKind::SystemReset, x, y, 1.1f, true)) { fx->color = hex(0xff638f); fx->radius = 1200; }
      motes(x, y, MoteShape::Star, hex(0xff638f), 24, -240, 300, 0.9f, 0, 8, 2);
      setFlash(hex(0xff638f), 0.16f, 0.3f);
      shake(6);
    } else {
      if (Effect* fx = push(FxKind::Ring, x, y, 0.8f, true)) { fx->color = hex(0x73ffe4); fx->radius = 600; }
      motes(x, y, MoteShape::Star, hex(0x73ffe4), 24, 400, 60, 1, 0, 7, 2);
    }
    return;
  }
  if (e.variant == 1) { altSpecial(e, seed); return; }
  if (e.color == 0) {
    if (Effect* fx = push(FxKind::Nova, x, y, 0.9f, true)) { fx->radius = 280; fx->seed = seed; }
    motes(x, y, MoteShape::Flake, hex(0xe8fbff), 26, 330, 0, 1, 0, 7, 2.6f);
    setFlash(hex(0xbdf3ff), 0.28f, 0.35f);
    shake(7);
  } else if (e.color == 1) {
    // The core reports the meteor's target as the event position; the rock takes 0.6 s to land.
    push(FxKind::Meteor, x, y, 0.6f, true);
    motes(x, y, MoteShape::Ember, hex(0xffcf6b), 10, 90, 0, 0.6f, -60, 4, 2);
    shake(3);
  } else if (e.color == 2) {
    if (Effect* fx = push(FxKind::Thorns, x, y, 1.1f, true)) { fx->radius = 190; fx->seed = seed; }
    motes(x, y, MoteShape::Leaf, hex(0xb8f57f), 18, 210, 40, 1.1f, 40, 7, 1.8f);
    setFlash(hex(0x9cf58a), 0.16f, 0.3f);
    shake(5);
  } else {
    const float fromX = e.points.size() >= 2 ? static_cast<float>(e.points[0]) : x;
    const float fromY = e.points.size() >= 2 ? static_cast<float>(e.points[1]) : y;
    if (Effect* fx = push(FxKind::Lunar, x, y, 0.75f, true)) { fx->fromX = fromX; fx->fromY = fromY; }
    motes((fromX + x) / 2, (fromY + y) / 2, MoteShape::Star, hex(0xf1e6ff), 16, 140, 80, 0.9f, 0, 6, 2);
    setFlash(hex(0xcdb4ff), 0.2f, 0.3f);
    shake(5);
  }
}

// ---- events ------------------------------------------------------------------------------------

void Animator::handleEvent(const Event& e) {
  const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
  const std::string& k = e.kind;
  if (k == "combo") {
    const bool team = e.variant == 1;
    burst(x, y, hex(0xffe49b), 8, 90);
    if (team) burst(x, y, 0xffffffffu, 10, 120);
    static constexpr const char* labels[2][3] = {{"CHOQUE TÉRMICO", "CONDUÇÃO", "ECLIPSE"},
                                                 {"CHOQUE TÉRMICO · EM EQUIPE", "CONDUÇÃO · EM EQUIPE", "ECLIPSE · EM EQUIPE"}};
    const int which = e.text == "thermal" ? 0 : e.text == "conduction" ? 1 : 2;
    if (Effect* fx = push(FxKind::Combo, x, y, team ? 1.1f : 0.8f)) { fx->color = team ? 0xffffffffu : hex(0xffe49b); fx->text = labels[team][which]; }
  } else if (k == "signal") {
    // An ally's call: a pulse where they pointed, with an off-screen arrow while it lasts.
    static constexpr const char* labels[] = {"VENHAM AQUI", "AJUDA", "CUIDADO", "OLHEM ALI"};
    const int which = e.text == "help" ? 1 : e.text == "danger" ? 2 : e.text == "look" ? 3 : 0;
    if (Effect* fx = push(FxKind::Signal, x, y, 3.0f, true)) { fx->color = native::playerColor(e.color); fx->text = labels[which]; }
  } else if (k == "convergence") {
    if (Effect* fx = push(FxKind::Convergence, x, y, 1, true)) { fx->radius = e.r > 0 ? static_cast<float>(e.r) : 320; fx->color = element(e.color); fx->color2 = 0xffffffffu; }
    motes(x, y, MoteShape::Star, 0xffffffffu, 24, 380, 0, 1, 0, 7, 2.2f);
    setFlash(0xffffffffu, 0.35f, 0.4f);
    shake(16);
  } else if (k == "thiefDown") {
    motes(x, y, MoteShape::Ember, hex(0xffd36b), 20, 260, 0, 0.9f, 120, 5, 2);
    burst(x, y, hex(0xffd36b), 14, 110);
  } else if (k == "encounter" || k == "shrineAccepted") {
    burst(x, y, k == "shrineAccepted" ? hex(0xff7aa8) : hex(0xffd36b), 16, 140);
  } else if (k == "loop") {
    setFlash(hex(0xffd36b), 0.25f, 0.6f);
    shake(10);
  } else if (k == "evade") {
    burst(x, y, element(e.color), 5, 50);
  } else if (k == "auroraRay" && e.points.size() >= 2) {
    if (Effect* fx = push(FxKind::SolarRay, x, y, 0.3f)) {
      fx->color = hex(0xffd778);
      fx->points.push_back(static_cast<float>(e.points[0]));
      fx->points.push_back(static_cast<float>(e.points[1]));
    }
  } else if (k == "chain") {
    if (Effect* fx = push(FxKind::Chain, x, y, 0.28f)) {
      fx->color = element(e.color); fx->seed = static_cast<float>(e.id);
      for (double v : e.points) fx->points.push_back(static_cast<float>(v));
    }
  } else if (k == "familiar") {
    if (Effect* fx = push(FxKind::FamiliarStrike, x, y, 0.32f)) {
      fx->color = element(e.color); fx->seed = static_cast<float>(e.id);
      for (double v : e.points) fx->points.push_back(static_cast<float>(v));
    }
    for (std::size_t i = 0; i + 1 < e.points.size(); i += 2) burst(static_cast<float>(e.points[i]), static_cast<float>(e.points[i + 1]), element(e.color), 4, 26);
  } else if (k == "boom" && e.color == 1 && e.r >= 150) {
    // Meteor impact: a heavier shockwave than an ordinary rune explosion.
    if (Effect* fx = push(FxKind::Impact, x, y, 0.7f, true)) fx->radius = static_cast<float>(e.r);
    motes(x, y, MoteShape::Ember, hex(0xffb347), 28, 360, 30, 1.1f, -90, 6, 2.4f);
    motes(x, y, MoteShape::Smoke, rgba(70, 45, 40, 128), 8, 70, 50, 1.2f, -30, 26, 2);
    setFlash(hex(0xffb36b), 0.32f, 0.35f);
    shake(14);
  } else if (k == "boom") {
    // Bomber explosions carry no radius or owner; runes and zones carry both.
    burst(x, y, e.r > 0 ? element(e.color) : hex(0xffb36b), 12, e.r > 0 ? static_cast<float>(e.r) : 70);
    shake(3);
  } else if (k == "stage") {
    burst(x, y, hex(0xff8a7a), 18, 260);
    shake(10);
  } else if (k == "bossDown") {
    shake(18);
    freeze_ = 0.22f;
  } else if (k == "special") {
    if (e.color != DEVELOPER || e.variant != 1) burst(x, y, element(e.color), 16, 120);
    special(e);
  } else if (k == "elite" || k == "chest" || k == "revive" || k == "phoenix" || k == "magnet") {
    const std::uint32_t color = k == "elite" ? hex(0xffd36b) : k == "chest" ? hex(0xffe08a) : k == "magnet" ? hex(0x8fd8ff)
                              : k == "revive" ? hex(0x9dffca) : hex(0xffb35c);
    if (e.x != 0 || e.y != 0) burst(x, y, color, k == "magnet" ? 20 : 12, k == "magnet" ? 220 : 80);
  }
}

// ---- per-frame tracking ------------------------------------------------------------------------

void Animator::track(const GameState& game, const Player* player, const Enemy* enemy, float dt, bool samePhase) {
  const std::uint64_t key = player ? playerKey(*player) : enemy->id;
  const float ex = static_cast<float>(player ? player->x : enemy->x), ey = static_cast<float>(player ? player->y : enemy->y);
  const float hp = static_cast<float>(player ? player->hp : enemy->hp);
  const bool alive = player ? player->alive : enemy->hp > 0;
  Actor* old = find(key);
  if (!old) {
    if (actors_.size() == actors_.capacity()) return;
    Actor a;
    a.key = key; a.x = ex; a.y = ey; a.hp = hp; a.alive = alive; a.player = player != nullptr;
    a.seed = static_cast<float>(actors_.size()) * 2.39f;
    a.down = alive ? 0.0f : 1.0f;
    if (player) {
      a.color = element(player->color); a.character = player->color; a.castCount = player->castCount;
      a.charge = player->specialCharge; a.level = player->level; a.dashFor = player->dashFor;
      a.sprite = native::playerSprite(player->color); a.size = 68;
    } else {
      a.boss = enemy->boss; a.elite = enemy->elite;
      a.color = enemy->elite ? hex(0xffd36b) : hex(0xffbc86);
      a.bossCooldown = enemy->attackCooldown; a.rangedCooldown = enemy->rangedCooldown;
      a.sprite = native::spriteForEnemy(*enemy); a.size = enemySize(*enemy);
      a.floating = floatingSprite(a.sprite);
    }
    a.seen = stamp_;
    actors_.push_back(a);
    rebuildIndex();
    return;
  }
  old->seen = stamp_;
  if (!player && enemy->distant) {
    if (hp < old->hp) { old->hit = 1; addNumber(*old, ex, ey - 26, old->hp - std::max(0.0f, hp)); ++hits_; }
    old->x = ex; old->y = ey; old->hp = hp; old->alive = alive;
    return;
  }
  const float dxm = ex - old->x, dym = ey - old->y, distance = std::sqrt(dxm * dxm + dym * dym);
  // Afterimages along real dash movement; never bridge teleports or phase changes.
  if (player && alive && !game.over && distance > 0.5f && distance < 220 && (player->dashFor > 0 || old->dashFor > 0) &&
      time_ - old->trailAt >= 0.025f && samePhase) {
    const int count = std::min(4, std::max(1, static_cast<int>(std::ceil(distance / 18))));
    for (int i = 0; i < count; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(count);
      if (Effect* fx = push(FxKind::Afterimage, old->x + dxm * t, old->y + dym * t, 0.24f)) {
        fx->color = old->color; fx->sprite = old->sprite; fx->facing = old->facing;
      }
    }
    old->trailAt = time_;
  }
  if (distance > 0.1f && alive && !game.over) {
    old->movedAt = time_; old->dx = sign(dxm);
    if (player && std::abs(dxm) > 0.1f) old->facing = sign(dxm);
  }
  old->walking += ((time_ - old->movedAt < 0.14f && alive && !game.over ? 1.0f : 0.0f) - old->walking) * std::min(1.0f, dt * 14);
  old->stride += dt * (player ? 13.0f : old->boss ? 6.0f : 11.0f) * old->walking;
  old->hit = std::max(0.0f, old->hit - dt * 6);
  old->cast = std::max(0.0f, old->cast - dt * 5);
  old->down += ((alive ? 0.0f : 1.0f) - old->down) * std::min(1.0f, dt * 12);
  if (hp < old->hp) {
    old->hit = 1;
    burst(ex, ey, player ? hex(0xffc0bd) : old->color, 4, old->boss ? 65 : 25);
    if (!player) { addNumber(*old, ex, ey - (old->boss ? 70 : 26), old->hp - std::max(0.0f, hp)); ++hits_; }
  }
  if (alive && !old->alive) burst(ex, ey, hex(0x9dffca), 12, 65);
  if (!alive && old->alive) burst(ex, ey, old->color, 8, 40);
  const bool cast = player ? player->castCount != old->castCount
                           : enemy->attackCooldown > old->bossCooldown || enemy->rangedCooldown > old->rangedCooldown;
  if (cast && alive) {
    old->cast = 1;
    old->castAngle = player ? static_cast<float>(player->castAngle) : 0.0f;
    burst(ex + (player ? 17 * old->facing : 0), ey - (player ? 12 : 0), old->color, 3, old->boss ? 75 : 20);
    if (player) if (Effect* fx = push(FxKind::Sigil, ex, ey + 22, 0.32f)) { fx->color = old->color; fx->radius = 31; fx->seed = old->castAngle; }
  }
  if (player && (player->specialCharge < old->charge || player->level > old->level)) burst(ex, ey, old->color, 12, 95);
  if (player && player->level > old->level) {
    if (Effect* fx = push(FxKind::Ascend, ex, ey, 1.15f, true)) { fx->color = hex(0xffe49b); fx->radius = 85; }
    motes(ex, ey, MoteShape::Star, hex(0xffe49b), 12, 70, 45, 1.1f, -100, 5, 2);
  }
  old->x = ex; old->y = ey; old->hp = hp; old->alive = alive;
  if (player) { old->castCount = player->castCount; old->charge = player->specialCharge; old->level = player->level; old->dashFor = player->dashFor; }
  else { old->bossCooldown = enemy->attackCooldown; old->rangedCooldown = enemy->rangedCooldown; }
}

void Animator::update(const GameState& game, double dtIn, bool paused) {
  hits_ = kills_ = 0;
  if (paused) return;
  const float dt = static_cast<float>(std::clamp<double>(dtIn, 0.0, 0.05));
  if (freeze_ > 0) { freeze_ -= dt; return; }
  time_ += dt;
  shake_ = std::max(0.0f, shake_ - dt * 28);
  if (flashLife_ > 0 && (flashAge_ += dt) >= flashLife_) flashLife_ = 0;

  std::size_t keep = 0;
  for (std::size_t i = 0; i < effects_.size(); ++i) {
    Effect& fx = effects_[i];
    fx.age += dt;
    if (fx.age >= fx.life) continue;
    if (fx.kind == FxKind::Mote) {
      const float damping = std::exp(-fx.drag * dt);
      fx.vx *= damping; fx.vy = fx.vy * damping + fx.gravity * dt;
      fx.x += fx.vx * dt; fx.y += fx.vy * dt;
    }
    if (keep != i) effects_[keep] = effects_[i];
    ++keep;
  }
  effects_.resize(keep);
  keep = 0;
  for (std::size_t i = 0; i < numbers_.size(); ++i) {
    numbers_[i].age += dt;
    if (numbers_[i].age >= numbers_[i].life) continue;
    if (keep != i) numbers_[keep] = numbers_[i];
    ++keep;
  }
  numbers_.resize(keep);

  if (!haveEventId_) {
    for (const auto& e : game.events) lastEventId_ = std::max(lastEventId_, e.id);
    haveEventId_ = true;
  }
  for (const auto& e : game.events) if (e.id > lastEventId_) { lastEventId_ = e.id; handleEvent(e); }

  // Meteor zones remember their initial warning so the falling rock can be timed.
  keep = 0;
  for (std::size_t i = 0; i < zoneWarnings_.size(); ++i) {
    const auto id = zoneWarnings_[i].id;
    if (std::none_of(game.zones.begin(), game.zones.end(), [&](const Zone& z) { return z.id == id; })) continue;
    zoneWarnings_[keep++] = zoneWarnings_[i];
  }
  zoneWarnings_.resize(keep);
  for (const auto& z : game.zones)
    if (z.warning > 0 && zoneWarning(z.id) == 0) zoneWarnings_.push_back({z.id, static_cast<float>(z.warning)});

  ++stamp_;
  const bool samePhase = game.phase == lastPhase_ && game.phaseStatus == lastStatus_;
  for (const auto& [_, p] : game.players) track(game, &p, nullptr, dt, samePhase);
  for (const auto& e : game.enemies) track(game, nullptr, &e, dt, samePhase);

  bool removed = false;
  keep = 0;
  for (std::size_t i = 0; i < actors_.size(); ++i) {
    const Actor& a = actors_[i];
    if (a.seen == stamp_) { if (keep != i) actors_[keep] = a; ++keep; continue; }
    removed = true;
    if (a.player || !samePhase) continue;
    bool nearPlayer = false;
    for (const auto& [_, p] : game.players) {
      const double dx = p.x - a.x, dy = p.y - a.y;
      if (dx * dx + dy * dy < 1000000) { nearPlayer = true; break; }
    }
    if (!nearPlayer) continue;
    burst(a.x, a.y, a.color, 5, 30);
    ++kills_;
    if (Effect* fx = push(FxKind::Ghost, a.x, a.y, 0.24f)) { fx->sprite = a.sprite; fx->size = a.size; }
  }
  actors_.resize(keep);
  if (removed) rebuildIndex();
  lastPhase_ = game.phase;
  if (lastStatus_ != game.phaseStatus) lastStatus_ = game.phaseStatus;
}

Pose Animator::pose(std::uint64_t key) const {
  const Actor* a = find(key);
  Pose out;
  if (!a) return out;
  const float down = a->down;
  const float step = fastSin(a->stride + a->seed) * a->walking * (1 - down);
  const float breath = fastSin(time_ * 2.8f + a->seed) * (1 - down);
  const float bounce = a->floating ? fastSin(time_ * 3.5f + a->seed) * 4 : -std::abs(step) * (a->boss ? 2.0f : 3.5f);
  out.x = -fastCos(a->castAngle) * a->cast * 3;
  out.y = bounce + breath * 0.7f + down * 12;
  out.rotation = step * (a->boss ? 0.015f : 0.045f) + a->dx * a->walking * 0.025f + down * 0.65f - a->cast * 0.07f;
  out.sx = a->facing * (1 + breath * 0.015f + std::abs(step) * 0.025f + a->cast * 0.06f + a->hit * 0.1f);
  out.sy = 1 - breath * 0.015f - std::abs(step) * 0.035f - down * 0.18f - a->hit * 0.08f;
  out.alpha = 1 - down * 0.72f;
  out.flash = a->hit;
  out.sprite = a->sprite; out.size = a->size; out.known = true;
  return out;
}

} // namespace arcana::sdl
