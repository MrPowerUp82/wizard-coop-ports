#pragma once

#include "transport.hpp"

#include <memory>
#include <string>

namespace arcana::online {

// WebSocket over TLS (Mbed TLS) with permessage-deflate, on IXWebSocket's own thread. Events are
// queued under a mutex and handed out by poll(). Never reconnects by itself (the Session decides).
class WsTransport final : public Transport {
public:
  // `caFile`: PEM bundle used to verify the server (assets/cacert.pem); empty = system store.
  explicit WsTransport(std::string caFile);
  ~WsTransport() override;
  void open(const std::string& url) override;
  void send(std::string text) override;
  void close() override;
  void poll(std::vector<TransportEvent>& out) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string caFile_;
};

} // namespace arcana::online
