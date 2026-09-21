#pragma once
// CPU probe shared by the host runner (tools/probe/host_probe.cpp) and the PSP EBOOT
// (platforms/psp/probe): the same scenarios on both, so results compare directly.
//
// Every scenario reports microseconds per simulation tick (or per loop for the FPU test).
// A 60 fps frame is 16 667 us; the simulation should stay well under ~6 000 us so rendering and
// audio still fit.
#include "arcana/game.hpp"
#include "arcana/native/render_queue.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace arcana::probe {

struct Result { const char* name; double avgUs; double worstUs; int enemies; int shots; };

struct FixedRandom final : Random { real next() override { return 0.5; } };

// Separates the cost of `double` (software-emulated on the PSP's Allegrex) from `float` (FPU).
template <class Now>
void fpuTest(Now now, Result& dbl, Result& flt) {
  constexpr int N = 200000;
  volatile double dsink = 0;
  volatile float fsink = 0;
  auto t0 = now();
  double d = 1.0001;
  for (int i = 0; i < N; ++i) { d = d * 1.0000001 + 0.5; d = d / 1.0000003 - 0.25; d = std::sqrt(d * d + 1.0); }
  dsink = d;
  auto t1 = now();
  float f = 1.0001f;
  for (int i = 0; i < N; ++i) { f = f * 1.0000001f + 0.5f; f = f / 1.0000003f - 0.25f; f = std::sqrt(f * f + 1.0f); }
  fsink = f;
  auto t2 = now();
  (void)dsink; (void)fsink;
  dbl = {"double: 200k x (mul+add, div+sub, sqrt)", static_cast<double>(t1 - t0), 0, 0, 0};
  flt = {"float:  200k x (mul+add, div+sub, sqrt)", static_cast<double>(t2 - t1), 0, 0, 0};
}

inline void addEnemies(GameState& s, int count, double spacing) {
  for (int i = 0; i < count; ++i) {
    Enemy e;
    e.id = static_cast<std::uint64_t>(i + 1);
    e.type = "slime";
    e.x = (i % 15) * spacing;
    e.y = (i / 15) * spacing;
    e.hp = e.maxHp = 1e12;
    s.enemies.push_back(std::move(e));
  }
}

inline void addShots(GameState& s, int count, double x0, double y0) {
  for (int i = 0; i < count; ++i) {
    Shot shot;
    shot.x = x0 + (i % 20) * 5;
    shot.y = y0 + (i / 20) * 5;
    shot.vx = 1;
    shot.ttl = 100;
    shot.damage = 1;
    shot.pierce = 1;
    shot.owner = "probe";
    s.shots.push_back(std::move(shot));
  }
}

// Frozen worst cases (no spawning, enemies cannot die): the pure cost of movement, separation and collisions.
template <class Now, class Setup>
Result stress(Now now, const char* name, Setup&& setup, int ticks) {
  FixedRandom rng;
  auto game = std::make_unique<GameState>(createGameState("classic"));
  game->players.emplace("probe", createPlayer("probe", "Probe", 0));
  game->phaseStatus = "horde";
  game->spawn = 1e9;
  game->players.at("probe").pendingPowers = {"arcane"};
  setup(*game);
  double total = 0, worst = 0;
  for (int i = 0; i < ticks; ++i) {
    game->players.at("probe").powerTimer = 0;
    const auto t0 = now();
    updateGame(*game, 1.0 / 60.0, rng);
    const double us = static_cast<double>(now() - t0);
    total += us; worst = std::max(worst, us);
  }
  return {name, total / ticks, worst, static_cast<int>(game->enemies.size()), static_cast<int>(game->shots.size())};
}

// A real run: a bot plays the quick ritual (orbit + aura, picks powers, casts specials) for
// `seconds` of game time. This is what a player actually costs, spawns and deaths included.
template <class Now>
Result realRun(Now now, double seconds, Result& renderQueue) {
  SeededRandom rng(12345);
  auto game = std::make_unique<GameState>(createGameState("quick"));
  auto p = createPlayer("bot", "Bot", 0);
  p.powers["orbit"] = 2; p.powers["aura"] = 1;
  game->players[p.id] = p;
  static native::RenderQueue queue; // ~40 KB: keep it off the stack
  native::Camera camera;
  camera.width = 480; camera.height = 272;
  double total = 0, worst = 0, queueTotal = 0;
  int ticks = 0, maxEnemies = 0, maxShots = 0;
  for (; ticks < static_cast<int>(seconds * 60) && !game->over; ++ticks) {
    auto& me = game->players.at("bot");
    const double t = ticks / 60.0;
    me.input.x = std::cos(t * 0.7); me.input.y = std::sin(t * 0.7);
    const auto t0 = now();
    updateGame(*game, 1.0 / 60.0, rng);
    const double us = static_cast<double>(now() - t0);
    total += us; worst = std::max(worst, us);
    if (!me.pendingPowers.empty()) applyPower(me, me.pendingPowers.front());
    if (me.specialCharge >= 100) activateSpecial(*game, "bot", rng);
    camera.x = static_cast<float>(me.x); camera.y = static_cast<float>(me.y);
    const auto q0 = now();
    native::buildRenderQueue(*game, camera, queue);
    queueTotal += static_cast<double>(now() - q0);
    maxEnemies = std::max(maxEnemies, static_cast<int>(game->enemies.size()));
    maxShots = std::max(maxShots, static_cast<int>(game->shots.size()));
  }
  renderQueue = {"render queue (culling + sprite list)", queueTotal / std::max(1, ticks), 0, 0, 0};
  return {"partida real (bot, ritual rapido)", total / std::max(1, ticks), worst, maxEnemies, maxShots};
}

// Runs everything; `emit` receives each result as soon as it is ready (the PSP prints it live).
template <class Now, class Emit>
void runAll(Now now, Emit emit, double realSeconds = 90) {
  Result dbl, flt;
  fpuTest(now, dbl, flt);
  emit(dbl); emit(flt);
  emit(stress(now, "180 inimigos amontoados", [](GameState& s) { addEnemies(s, cfg::MAX_ENEMIES, 10); }, 120));
  emit(stress(now, "180 inimigos + 320 tiros longe", [](GameState& s) {
    addEnemies(s, cfg::MAX_ENEMIES, 40); addShots(s, cfg::MAX_SHOTS, 10000, 10000); }, 120));
  emit(stress(now, "320 tiros acertando 180 (ver pior)", [](GameState& s) {
    addEnemies(s, cfg::MAX_ENEMIES, 40); addShots(s, cfg::MAX_SHOTS, 100, 100); }, 120));
  Result queue{};
  const Result real = realRun(now, realSeconds, queue);
  emit(real); emit(queue);
}

inline void format(char* out, std::size_t n, const Result& r) {
  if (r.worstUs > 0)
    std::snprintf(out, n, "%-36s %9.0f us/tick (pior %6.0f) E%d S%d", r.name, r.avgUs, r.worstUs, r.enemies, r.shots);
  else
    std::snprintf(out, n, "%-36s %9.0f us", r.name, r.avgUs);
}

} // namespace arcana::probe
