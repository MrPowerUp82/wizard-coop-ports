// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "interpolation.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

using namespace arcana;
using namespace arcana::online;

namespace {
bool close(double a, double b) { return std::fabs(a - b) < 1e-9; }

std::shared_ptr<GameState> snapshot(double t, double px, double ex, double shotX) {
  auto s = std::make_shared<GameState>();
  s->time = t;
  Player p = createPlayer("u1", "Ana", 0);
  p.x = px; p.orbitAngle = 3.0;
  s->players["u1"] = p;
  Enemy e; e.id = 7; e.type = "slime"; e.x = ex;
  s->enemies.push_back(e);
  Shot shot; shot.x = shotX; shot.vx = 100;
  s->shots.push_back(shot);
  Hazard h; h.warning = 1;
  s->hazards.push_back(h);
  return s;
}
} // namespace

int main() {
  std::deque<Snapshot> snaps;
  snaps.push_back({1.0, 0, snapshot(1.0, 0, 100, 0)});
  snaps.push_back({1.1, 0, snapshot(1.1, 10, 200, 10)});
  auto out = std::make_unique<GameState>();
  Interpolator interp;

  // Halfway between the two snapshots.
  interp.apply(snaps, 1.05, *out);
  assert(close(out->players.at("u1").x, 5));
  assert(close(out->enemies[0].x, 150));
  assert(close(out->shots[0].x, 10 + 100 * (1.05 - 1.1)));   // shots come from the newer one, moved by velocity
  assert(close(out->hazards[0].warning, 1));                 // renderT behind the newest: no countdown yet
  assert(close(out->time, 1.1));                             // everything else from the newest snapshot

  // Past the newest snapshot: hold positions, extrapolate shots, count hazards down.
  interp.apply(snaps, 1.3, *out);
  assert(close(out->players.at("u1").x, 10));
  assert(close(out->shots[0].x, 10 + 100 * 0.2));
  assert(close(out->hazards[0].warning, 0.8));

  // Before the oldest snapshot: the oldest is shown.
  interp.apply(snaps, 0.5, *out);
  assert(close(out->enemies[0].x, 100));

  // Angles take the short way round (3.0 -> -3.0 crosses pi).
  auto wrap = snapshot(1.2, 10, 200, 10);
  wrap->players["u1"].orbitAngle = -3.0;
  snaps.push_back({1.2, 0, wrap});
  interp.apply(snaps, 1.15, *out);
  assert(std::fabs(out->players.at("u1").orbitAngle) > 3.0);

  // A new enemy (not in the older snapshot) appears where the server put it.
  auto spawn = snapshot(1.3, 10, 200, 10);
  Enemy fresh; fresh.id = 8; fresh.type = "bat"; fresh.x = 999;
  spawn->enemies.push_back(fresh);
  snaps.push_back({1.3, 0, spawn});
  interp.apply(snaps, 1.25, *out);
  assert(out->enemies.size() == 2 && close(out->enemies[1].x, 999));

  std::puts("online_sync: ok");
  return 0;
}
