#pragma once

#include "arcana/game.hpp"

#include <deque>
#include <memory>
#include <unordered_map>

namespace arcana::online {

struct Snapshot {
  double t{};   // server time (seconds)
  double at{};  // local arrival (ms)
  std::shared_ptr<const GameState> state;
};

// Port of src/net.js interpolate(): `out` gets the newest snapshot, with players, enemies, drops and
// familiars blended by id between the two snapshots around `renderT` (server seconds), projectiles
// advanced by their velocity past the newer one and hazard warnings counted down.
class Interpolator {
public:
  void apply(const std::deque<Snapshot>& snapshots, double renderT, GameState& out);

private:
  std::unordered_map<std::uint64_t, Vec2> enemies_, gems_;  // reused lookups (no per-frame rehash growth)
};

} // namespace arcana::online
