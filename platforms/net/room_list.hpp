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
  // Cancels any in-flight request (so it doesn't linger, e.g. once the player leaves the room
  // list page) and schedules the next fetch for `nowMs`.
  void refresh(double nowMs);
  [[nodiscard]] const RoomsInfo& rooms() const { return rooms_; }
  [[nodiscard]] bool loaded() const { return loaded_; }
  [[nodiscard]] const std::string& error() const { return error_; }
  // True while a request is in flight (a transport is open). Callers off the room-list page can
  // use this to keep pumping update() just long enough to let the request finish or time out,
  // without starting new requests while off that page.
  [[nodiscard]] bool pending() const { return transport_ != nullptr; }

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
