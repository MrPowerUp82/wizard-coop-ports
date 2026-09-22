#include "interpolation.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::online {
namespace {
real lerp(real a, real b, real t) { return a + (b - a) * t; }
real lerpAngle(real a, real b, real t) {
  real delta = std::fmod(b - a, PI * 2);
  if (delta > PI) delta -= PI * 2;
  if (delta < -PI) delta += PI * 2;
  return a + delta * t;
}
} // namespace

void Interpolator::apply(const std::deque<Snapshot>& snaps, double renderT, GameState& out) {
  if (snaps.empty()) return;
  const Snapshot* a = &snaps.back();
  const Snapshot* b = a;
  for (std::size_t i = snaps.size() - 1; i > 0; --i) {
    if (snaps[i - 1].t <= renderT) { a = &snaps[i - 1]; b = &snaps[i]; break; }
    a = b = &snaps[i - 1];
  }
  const real alpha = b->t > a->t ? std::clamp((renderT - a->t) / (b->t - a->t), 0.0, 1.0) : 0.0;
  // Clamp extrapolation so a long reconnect/stall doesn't fling shots/hazards tens of seconds
  // ahead of their last known snapshot.
  const real ahead = std::min<real>(renderT - b->t, 0.5);
  const GameState& latest = *snaps.back().state;
  const GameState& from = *a->state;
  const GameState& to = *b->state;

  out = latest;
  for (auto& [id, p] : out.players) {
    const auto pa = from.players.find(id);
    if (pa == from.players.end()) continue;
    const auto pbIt = to.players.find(id);
    const Player& pb = pbIt == to.players.end() ? latest.players.at(id) : pbIt->second;
    p.x = lerp(pa->second.x, pb.x, alpha);
    p.y = lerp(pa->second.y, pb.y, alpha);
    p.orbitAngle = lerpAngle(pa->second.orbitAngle, pb.orbitAngle, alpha);
    if (pa->second.familiar && pb.familiar) {
      Familiar f = *pb.familiar;
      f.x = lerp(pa->second.familiar->x, pb.familiar->x, alpha);
      f.y = lerp(pa->second.familiar->y, pb.familiar->y, alpha);
      p.familiar = f;
    }
  }
  enemies_.clear();
  for (const auto& e : from.enemies) enemies_[e.id] = {e.x, e.y};
  out.enemies = to.enemies;
  for (auto& e : out.enemies)
    if (const auto it = enemies_.find(e.id); it != enemies_.end()) { e.x = lerp(it->second.x, e.x, alpha); e.y = lerp(it->second.y, e.y, alpha); }
  gems_.clear();
  for (const auto& g : from.gems) gems_[g.id] = {g.x, g.y};
  out.gems = to.gems;
  for (auto& g : out.gems)
    if (const auto it = gems_.find(g.id); it != gems_.end()) { g.x = lerp(it->second.x, g.x, alpha); g.y = lerp(it->second.y, g.y, alpha); }
  out.shots = to.shots;
  for (auto& s : out.shots) { s.x += s.vx * ahead; s.y += s.vy * ahead; }
  out.enemyShots = to.enemyShots;
  for (auto& s : out.enemyShots) { s.x += s.vx * ahead; s.y += s.vy * ahead; }
  out.hazards = to.hazards;
  for (auto& h : out.hazards) h.warning -= std::max<real>(0, ahead);
  out.runes = to.runes;
  out.zones = to.zones;
}

} // namespace arcana::online
