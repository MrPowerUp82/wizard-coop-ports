#pragma once

#include "arcana/game.hpp"

#include <deque>
#include <optional>
#include <string>

namespace arcana::online {

// Stick input as it goes on the wire: 1/32 steps, length <= 1 (the prediction runs on exactly what
// the server receives).
Vec2 quantizeInput(Vec2 input);

// Rate-limits `input` messages: a change is sent at most every 50 ms and, without changes, a
// keep-alive goes every 100 ms (src/net.js). Keeps a player under the server's 50 messages/s.
class InputSender {
public:
  static constexpr double kMinGapMs = 50, kKeepAliveMs = 100;
  std::optional<std::string> update(Vec2 input, double nowMs);
  [[nodiscard]] std::uint64_t seq() const { return seq_; }

private:
  Vec2 last_{99, 99};
  double lastAt_{-1e9};
  std::uint64_t seq_{};
};

// Client-side prediction of the local player (src/net.js frame()/reconcile()).
class Predictor {
public:
  static constexpr double kHistoryMs = 1500, kSnapDistance = 220, kBlend = 0.35;
  void reset() { has_ = false; history_.clear(); offset_ = {}; motionId_ = 0; }
  // A snapshot arrived: compare with where we predicted ourselves about one round trip ago.
  void reconcile(const Player& server, double nowMs, double rttMs);
  // One render frame; returns where to draw the local player. `ageSeconds`: how old the newest
  // snapshot is (its dash has partly elapsed).
  Vec2 step(const Player& server, double ageSeconds, Vec2 input, double dt, bool canMove, double nowMs);

private:
  struct Sample { double at; Vec2 pos; };
  bool has_{};
  Vec2 predicted_{}, offset_{};
  std::uint64_t motionId_{};
  std::deque<Sample> history_;
};

} // namespace arcana::online
