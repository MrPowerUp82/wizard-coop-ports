#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arcana::online {

// What a network thread observed, handed to the game thread by Transport::poll().
struct TransportEvent {
  enum class Type { Open, Message, Close, Error };
  Type type{Type::Message};
  std::string text;  // Message: payload; Close: reason; Error: description
  int code{};        // Close: WebSocket close code
  bool tls{};         // Error: the TLS handshake/certificate failed
};

// A WebSocket connection. Implementations deliver events from their own thread through poll(), so
// the game (and the tests, with a fake) consume them on the main thread only.
class Transport {
public:
  virtual ~Transport() = default;
  virtual void open(const std::string& url) = 0;
  virtual void send(std::string text) = 0;
  // Polite close; no further events are delivered.
  virtual void close() = 0;
  // Appends the events queued since the last call.
  virtual void poll(std::vector<TransportEvent>& out) = 0;
};

using TransportFactory = std::function<std::unique_ptr<Transport>()>;

enum class UrlCheck { Ok, Invalid, InsecureBlocked };
// wss:// always; plain ws:// only for localhost/127.0.0.1 unless `allowInsecure` (--insecure-ws).
UrlCheck checkServerUrl(std::string_view url, bool allowInsecure);

} // namespace arcana::online
