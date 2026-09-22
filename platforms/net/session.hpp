#pragma once

#include "interpolation.hpp"
#include "prediction.hpp"
#include "protocol.hpp"
#include "transport.hpp"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arcana::online {

enum class SessionStatus { Connecting, Lobby, Playing, Reconnecting, Closed };

struct SessionConfig {
  std::string url;
  EntryRequest entry;          // action "create" or "join"
  TransportFactory transport;
};

// One co-op connection (port of src/net.js createSession): lobby messages, a snapshot buffer drawn
// 100 ms in the past, prediction of the local player and automatic resume after a dropped
// connection. Time is passed in (monotonic milliseconds) so tests can drive it.
class Session {
public:
  static constexpr double kInterpolationDelay = 0.1;  // seconds
  static constexpr std::size_t kMaxSnapshots = 30;
  static constexpr double kPingEveryMs = 2000;
  static constexpr std::array<double, 8> kRetryDelaysMs{400, 800, 1500, 2500, 4000, 6000, 8000, 8000};

  explicit Session(SessionConfig config);
  void start(double nowMs);
  // Pumps transport events, pings and reconnect timers. Call every frame.
  void update(double nowMs);
  // In game: sends input (rate-limited) and writes the interpolated, predicted view into `out`.
  // False until the first snapshot arrived.
  bool frame(double nowMs, double dt, Vec2 input, GameState& out);

  void selectCharacter(int color);
  void startMatch();
  void choosePower(const std::string& id);
  void reroll();
  void special();
  void dash(Vec2 direction);
  void signal(const std::string& kind, std::optional<Vec2> at = {});
  void leave();

  [[nodiscard]] SessionStatus status() const { return status_; }
  [[nodiscard]] const std::string& playerId() const { return playerId_; }
  [[nodiscard]] const std::string& room() const { return room_; }
  [[nodiscard]] int color() const { return config_.entry.color; }
  [[nodiscard]] const LobbyInfo& lobby() const { return lobby_; }
  [[nodiscard]] bool isHost() const { return !playerId_.empty() && lobby_.hostId == playerId_; }
  [[nodiscard]] double rttMs() const { return rtt_; }
  [[nodiscard]] bool over() const { return over_; }
  // Why the session closed (empty when the player left or the match simply ended).
  [[nodiscard]] const std::string& failure() const { return failure_; }
  [[nodiscard]] int droppedSnapshots() const { return dropped_; }
  // One-line message for a toast (server errors after joining, character switched...).
  std::optional<std::string> takeNotice();

private:
  void openTransport(bool resume);
  void handle(const TransportEvent& event, double nowMs);
  void receive(ServerMessage& message, double nowMs);
  void closed(int code, const std::string& reason, double nowMs);
  void fail(std::string message);
  bool send(const std::string& text);

  SessionConfig config_;
  std::unique_ptr<Transport> transport_;
  bool transportOpen_{}, resumeOnOpen_{}, closedByUser_{}, inGame_{}, over_{};
  SessionStatus status_{SessionStatus::Connecting};
  std::string playerId_, room_, token_, failure_;
  std::optional<std::string> notice_;
  LobbyInfo lobby_;
  std::deque<Snapshot> snapshots_;
  std::optional<double> clockOffset_;
  double rtt_{120}, nextPingAt_{}, reconnectAt_{};
  std::size_t retries_{};
  int dropped_{};
  Interpolator interpolator_;
  InputSender inputSender_;
  Predictor predictor_;
  std::vector<TransportEvent> events_;
};

} // namespace arcana::online
