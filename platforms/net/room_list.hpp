#pragma once

#include "protocol.hpp"
#include "transport.hpp"

#include <memory>
#include <string>
#include <vector>

namespace arcana::online {

// Open rooms for the online menu: a short-lived connection sends `listRooms` every 5 s (like the
// web menu's refresh). A server speaking another protocol version shows no rooms and an error.
class RoomList {
public:
  static constexpr double kRefreshMs = 5000, kTimeoutMs = 10000;
  RoomList(std::string url, TransportFactory factory);
  void update(double nowMs);
  void refresh(double nowMs) { if (!transport_) nextAt_ = nowMs; }
  [[nodiscard]] const RoomsInfo& rooms() const { return rooms_; }
  [[nodiscard]] bool loaded() const { return loaded_; }
  [[nodiscard]] const std::string& error() const { return error_; }

private:
  void finish(double nowMs);
  std::string url_;
  TransportFactory factory_;
  std::unique_ptr<Transport> transport_;
  double nextAt_{}, startedAt_{};
  RoomsInfo rooms_;
  bool loaded_{};
  std::string error_;
  std::vector<TransportEvent> events_;
};

} // namespace arcana::online
