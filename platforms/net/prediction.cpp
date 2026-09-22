#include "prediction.hpp"
#include "protocol.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::online {

Vec2 quantizeInput(Vec2 v) {
  auto q = [](real x) { return std::round(std::clamp<real>(x, -1, 1) * 32) / 32; };
  Vec2 out{q(v.x), q(v.y)};
  const real len = std::hypot(out.x, out.y);
  if (len > 1) { out.x /= len; out.y /= len; }
  return out;
}

std::optional<std::string> InputSender::update(Vec2 in, double now) {
  const bool changed = in.x != last_.x || in.y != last_.y;
  if (now - lastAt_ < (changed ? kMinGapMs : kKeepAliveMs)) return std::nullopt;
  if (changed) ++seq_;
  last_ = in;
  lastAt_ = now;
  return encodeInput(in.x, in.y, seq_);
}

void Predictor::reconcile(const Player& me, double now, double rtt) {
  if (!has_) return;
  if (motionId_ != me.motionId) {
    motionId_ = me.motionId;
    predicted_ = {me.x, me.y};
    offset_ = {};
    history_.clear();
    return;
  }
  if (history_.empty()) return;
  const double target = now - rtt;
  const Sample* past = &history_.front();
  for (const auto& s : history_) { if (s.at > target) break; past = &s; }
  const real ex = me.x - past->pos.x, ey = me.y - past->pos.y;
  if (std::hypot(ex, ey) > kSnapDistance) {
    offset_ = {};
    predicted_ = {me.x, me.y};
    history_.clear();
    return;
  }
  predicted_.x += ex * kBlend; predicted_.y += ey * kBlend;
  for (auto& s : history_) { s.pos.x += ex * kBlend; s.pos.y += ey * kBlend; }
  offset_.x -= ex * kBlend; offset_.y -= ey * kBlend;
}

Vec2 Predictor::step(const Player& server, double age, Vec2 input, double dt, bool canMove, double now) {
  if (!canMove || !has_) {
    predicted_ = {server.x, server.y};
    offset_ = {};
    history_.clear();
    if (!has_) motionId_ = server.motionId;
    has_ = true;
  }
  if (canMove) {
    Player p = server;
    p.input.x = input.x; p.input.y = input.y;
    p.dashFor = std::max<real>(0, server.dashFor - age);
    const Vec2 d = playerMovement(p, dt);
    predicted_.x += d.x; predicted_.y += d.y;
    history_.push_back({now, predicted_});
    while (!history_.empty() && history_.front().at < now - kHistoryMs) history_.pop_front();
  }
  const real decay = std::min<real>(1, dt * 10);
  offset_.x -= offset_.x * decay; offset_.y -= offset_.y * decay;
  return {predicted_.x + offset_.x, predicted_.y + offset_.y};
}

} // namespace arcana::online
