#include "session.hpp"

#include <algorithm>
#include <utility>

namespace arcana::online {

Session::Session(SessionConfig config) : config_(std::move(config)) {}

void Session::start(double) { openTransport(false); }

void Session::openTransport(bool resume) {
  transport_ = config_.transport();
  transportOpen_ = false;
  resumeOnOpen_ = resume;
  transport_->open(config_.url);
}

bool Session::send(const std::string& text) {
  if (!transport_ || !transportOpen_) return false;
  transport_->send(text);
  return true;
}

std::optional<std::string> Session::takeNotice() { return std::exchange(notice_, std::nullopt); }

void Session::update(double now) {
  if (status_ == SessionStatus::Reconnecting && !transport_ && now >= reconnectAt_) openTransport(true);
  if (!transport_) return;
  events_.clear();
  transport_->poll(events_);
  for (const auto& e : events_) {
    handle(e, now);
    if (!transport_) return;
  }
  if (transportOpen_ && now >= nextPingAt_) {
    send(encodePing(now));
    nextPingAt_ = now + kPingEveryMs;
  }
}

void Session::handle(const TransportEvent& e, double now) {
  switch (e.type) {
    case TransportEvent::Type::Open:
      transportOpen_ = true;
      send(resumeOnOpen_ ? encodeResume(room_, token_) : encodeEntry(config_.entry));
      send(encodePing(now));
      nextPingAt_ = now + kPingEveryMs;
      break;
    case TransportEvent::Type::Message: {
      ServerMessage m = parseServerMessage(e.text);
      receive(m, now);
      break;
    }
    case TransportEvent::Type::Close: closed(e.code, e.text, now); break;
    case TransportEvent::Type::Error:
      if (!inGame_) failure_ = e.tls ? "Certificado do servidor inválido." : "Não foi possível alcançar o servidor.";
      closed(1006, {}, now);
      break;
  }
}

void Session::receive(ServerMessage& m, double now) {
  switch (m.kind) {
    case ServerKind::Pong: rtt_ = rtt_ * 0.7 + (now - m.pongT) * 0.3; break;
    case ServerKind::Joined:
      retries_ = 0;
      failure_.clear();
      playerId_ = m.joined.playerId;
      room_ = m.joined.room;
      token_ = m.joined.token;
      config_.entry.color = m.joined.color;
      status_ = inGame_ ? SessionStatus::Playing : SessionStatus::Lobby;
      if (config_.entry.action == "join" && !m.joined.resumed) send(encodeSimple("ready"));
      break;
    case ServerKind::Lobby: lobby_ = std::move(m.lobby); break;
    case ServerKind::Error:
      if (m.error.code == "CHARACTER_TAKEN" && playerId_.empty()) {
        // Like the web client: pick a character nobody in the room uses and ask again.
        for (int c = 0; c < 4; ++c) {
          const bool taken = std::any_of(m.error.players.begin(), m.error.players.end(), [&](const LobbyPlayer& p) { return p.color == c; });
          if (taken) continue;
          config_.entry.color = c;
          send(encodeEntry(config_.entry));
          notice_ = "Seu personagem já estava em uso; você entrou com outro.";
          return;
        }
      }
      if (playerId_.empty() || m.error.code == "RESUME_FAILED") fail(m.error.message);
      else notice_ = m.error.message;
      break;
    case ServerKind::Start:
    case ServerKind::State: {
      if (m.kind == ServerKind::Start) { snapshots_.clear(); predictor_.reset(); clockOffset_.reset(); }
      const GameState& state = *m.state;
      const double sample = now - state.time * 1000;
      // Track the fastest-arriving snapshot, drifting slowly so a lag spike does not stall rendering.
      clockOffset_ = clockOffset_ ? std::min(sample, *clockOffset_ + 4) : sample;
      snapshots_.push_back({state.time, now, m.state});
      if (snapshots_.size() > kMaxSnapshots) snapshots_.pop_front();
      if (const auto me = state.players.find(playerId_); me != state.players.end()) predictor_.reconcile(me->second, now, rtt_);
      over_ = state.over;
      if (m.kind == ServerKind::Start) inGame_ = true;
      if (inGame_) status_ = SessionStatus::Playing;
      break;
    }
    case ServerKind::BadSnapshot: ++dropped_; break;
    case ServerKind::Rooms:
    case ServerKind::Unknown: break;
  }
}

void Session::closed(int code, const std::string& reason, double now) {
  if (transport_) transport_->close();
  transport_.reset();
  transportOpen_ = false;
  if (closedByUser_ || status_ == SessionStatus::Closed) { status_ = SessionStatus::Closed; return; }
  if (inGame_ && !over_ && !token_.empty() && retries_ < kRetryDelaysMs.size() && code < 4000) {
    status_ = SessionStatus::Reconnecting;
    reconnectAt_ = now + kRetryDelaysMs[retries_++];
    return;
  }
  if (failure_.empty() && !over_) failure_ = code >= 4000 && !reason.empty() ? reason : "A conexão com o servidor caiu.";
  status_ = SessionStatus::Closed;
}

void Session::fail(std::string message) {
  failure_ = std::move(message);
  closedByUser_ = true;
  if (transport_) transport_->close();
  transport_.reset();
  transportOpen_ = false;
  status_ = SessionStatus::Closed;
}

bool Session::frame(double now, double dt, Vec2 input, GameState& out) {
  if (snapshots_.empty() || !clockOffset_) return false;
  const Vec2 q = quantizeInput(input);
  if (status_ == SessionStatus::Playing)
    if (auto message = inputSender_.update(q, now)) send(*message);
  const double serverNow = (now - *clockOffset_) / 1000;
  interpolator_.apply(snapshots_, serverNow - kInterpolationDelay, out);
  const GameState& latest = *snapshots_.back().state;
  const auto server = latest.players.find(playerId_);
  const auto mine = out.players.find(playerId_);
  if (server == latest.players.end() || mine == out.players.end()) return true;
  const Player& me = server->second;
  const bool canMove = me.alive && me.pendingPowers.empty() && !out.over && out.phaseStatus != "transition" && status_ != SessionStatus::Reconnecting;
  const Vec2 pos = predictor_.step(me, std::max(0.0, serverNow - latest.time), q, dt, canMove, now);
  mine->second.x = pos.x;
  mine->second.y = pos.y;
  return true;
}

void Session::selectCharacter(int color) {
  if (status_ != SessionStatus::Lobby) return;
  config_.entry.color = color;
  send(encodeSelectCharacter(color));
}
void Session::startMatch() { if (status_ == SessionStatus::Lobby && isHost()) send(encodeSimple("start")); }
void Session::choosePower(const std::string& id) { if (status_ == SessionStatus::Playing) send(encodeChoosePower(id)); }
void Session::reroll() { if (status_ == SessionStatus::Playing) send(encodeSimple("reroll")); }
void Session::special() { if (status_ == SessionStatus::Playing) send(encodeSimple("special")); }
void Session::dash(Vec2 d) { if (status_ == SessionStatus::Playing) send(encodeDash(d.x, d.y)); }
void Session::signal(const std::string& kind, std::optional<Vec2> at) { if (status_ == SessionStatus::Playing) send(encodeSignal(kind, at)); }

void Session::leave() {
  closedByUser_ = true;
  if (transport_) {
    send(encodeSimple("leave"));
    transport_->close();
    transport_.reset();
  }
  transportOpen_ = false;
  status_ = SessionStatus::Closed;
}

} // namespace arcana::online
