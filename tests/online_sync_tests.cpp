// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "interpolation.hpp"
#include "prediction.hpp"

#include <nlohmann/json.hpp>

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

  // A long stall/reconnect must not extrapolate shots for tens of seconds: `ahead` clamps to 0.5s.
  interp.apply(snaps, 1.1 + 40.0, *out);
  assert(close(out->shots[0].x, 10 + 100 * 0.5));
  assert(close(out->players.at("u1").x, 10));                // positions still just hold, unaffected

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

  // Test familiar interpolation: the two bracketing snapshots have familiars, but the newest does not.
  // The interpolated result should have a familiar with lerped position.
  {
    std::deque<Snapshot> familiarSnaps;
    auto snap1 = std::make_shared<GameState>();
    snap1->time = 1.0;
    Player p1 = createPlayer("u1", "Ana", 0);
    p1.x = 0;
    p1.orbitAngle = 3.0;
    p1.familiar = Familiar{0, 0};
    snap1->players["u1"] = p1;
    familiarSnaps.push_back({1.0, 0, snap1});

    auto snap2 = std::make_shared<GameState>();
    snap2->time = 1.1;
    Player p2 = createPlayer("u1", "Ana", 0);
    p2.x = 10;
    p2.orbitAngle = 3.0;
    p2.familiar = Familiar{10, 0};
    snap2->players["u1"] = p2;
    familiarSnaps.push_back({1.1, 0, snap2});

    auto snap3 = std::make_shared<GameState>();
    snap3->time = 1.2;
    Player p3 = createPlayer("u1", "Ana", 0);
    p3.x = 10;
    p3.orbitAngle = 3.0;
    // no familiar
    snap3->players["u1"] = p3;
    familiarSnaps.push_back({1.2, 0, snap3});

    auto familiarOut = std::make_unique<GameState>();
    interp.apply(familiarSnaps, 1.05, *familiarOut);
    assert(familiarOut->players.at("u1").familiar.has_value());
    assert(close(familiarOut->players.at("u1").familiar->x, 5));
  }

  // Input quantization: 1/32 steps, never longer than 1.
  {
    const Vec2 q = quantizeInput({0.7071, 0.7071});
    assert(std::hypot(q.x, q.y) <= 1.0 + 1e-12);
    assert(close(quantizeInput({0.5, -0.26}).x, 0.5) && close(quantizeInput({0.5, -0.26}).y, -8.0 / 32));
    assert(close(quantizeInput({3, 0}).x, 1));
  }
  // Input sender: sends on change (at most every 50 ms) and as a 100 ms keep-alive; seq counts changes.
  {
    InputSender sender;
    auto first = sender.update({1, 0}, 0);
    assert(first && nlohmann::json::parse(*first)["seq"] == 1);
    assert(!sender.update({1, 0}, 30));                     // nothing new, keep-alive not due
    auto keepAlive = sender.update({1, 0}, 100);
    assert(keepAlive && nlohmann::json::parse(*keepAlive)["seq"] == 1);
    assert(!sender.update({0, 1}, 110));                    // changed, but only 10 ms after the last send
    auto changed = sender.update({0, 1}, 150);
    assert(changed && nlohmann::json::parse(*changed)["seq"] == 2);
    int sent = 0;
    for (int frame = 0; frame < 60; ++frame)                // a stick that changes every frame for a second
      if (sender.update({std::sin(frame * 0.3), 0}, 1000 + frame * (1000.0 / 60))) ++sent;
    assert(sent <= 21);
  }
  // Prediction: walks with the server formula, reconciles softly, snaps on big errors and on dashes.
  {
    Player server = createPlayer("u1", "Ana", 0);
    Predictor predictor;
    Vec2 pos{};
    for (int i = 0; i < 60; ++i) pos = predictor.step(server, 0, {1, 0}, 1.0 / 60, true, i * (1000.0 / 60));
    assert(std::fabs(pos.x - server.speed) < 1e-6);         // one second at full speed
    // The server says we are 50 units behind what we predicted ~now (rtt 0): 35% of it is applied,
    // and a visual offset hides the jump.
    Player behind = server;
    behind.x = pos.x - 50;
    predictor.reconcile(behind, 1000, 0);
    const Vec2 after = predictor.step(server, 0, {0, 0}, 0, true, 1000);
    assert(std::fabs(after.x - pos.x) < 1e-6);               // no visible jump this frame
    // More than 220 units off: take the server position.
    Player far = server;
    far.x = pos.x + 500;
    predictor.reconcile(far, 1000, 0);
    assert(std::fabs(predictor.step(server, 0, {0, 0}, 0, true, 1000).x - far.x) < 1e-6);
    // A dash on the server (motionId changes) restarts from the server position.
    Player dashed = server;
    dashed.x = 42; dashed.motionId = 1;
    predictor.reconcile(dashed, 2000, 0);
    assert(std::fabs(predictor.step(dashed, 0, {0, 0}, 0, true, 2000).x - 42) < 1e-6);
    // Cannot move (dead, choosing a power...): show the server position.
    Player still = server;
    still.x = -5;
    assert(std::fabs(predictor.step(still, 0, {1, 0}, 1.0 / 60, false, 3000).x + 5) < 1e-6);
  }

  std::puts("online_sync: ok");
  return 0;
}
