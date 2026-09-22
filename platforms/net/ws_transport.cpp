#include "ws_transport.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace arcana::online {
namespace {
bool looksLikeTls(std::string reason) {
  std::transform(reason.begin(), reason.end(), reason.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* word : {"certificate", "x509", "handshake", "tls", "ssl"})
    if (reason.find(word) != std::string::npos) return true;
  return false;
}
// permessage-deflate lets a hostile server inflate a small frame into a huge message. Real
// snapshots are tens of KB, so cap well above that: comfortably headroom for growth, but small
// enough to bound the damage. The vendored ixwebsocket (build-linux/_deps/ixwebsocket-src) has no
// setMaxMessageSize()-style API on ix::WebSocket to reject this before it decompresses, so the
// bound is enforced here instead: an oversized message is dropped and the connection is closed
// rather than being handed to the JSON parser.
constexpr std::size_t kMaxMessageBytes = 4 * 1024 * 1024; // 4 MB
} // namespace

struct WsTransport::Impl {
  ix::WebSocket socket;
  std::mutex mutex;
  std::vector<TransportEvent> queue;
};

WsTransport::WsTransport(std::string caFile) : impl_(std::make_unique<Impl>()), caFile_(std::move(caFile)) {
  static const bool netReady = ix::initNetSystem(); // WSAStartup on Windows, once per process
  (void)netReady;
}

WsTransport::~WsTransport() { close(); }

void WsTransport::open(const std::string& url) {
  auto& s = impl_->socket;
  s.setUrl(url);
  ix::SocketTLSOptions tls;
  tls.caFile = caFile_.empty() ? "SYSTEM" : caFile_;
  s.setTLSOptions(tls);
  s.disableAutomaticReconnection();
  s.setHandshakeTimeout(10);
  s.setPerMessageDeflateOptions(ix::WebSocketPerMessageDeflateOptions(true));
  s.setOnMessageCallback([impl = impl_.get()](const ix::WebSocketMessagePtr& msg) {
    TransportEvent e;
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: e.type = TransportEvent::Type::Open; break;
      case ix::WebSocketMessageType::Message:
        if (msg->str.size() > kMaxMessageBytes) {
          e.type = TransportEvent::Type::Error;
          e.text = "Message exceeds maximum size";
          impl->socket.close(); // don't keep talking to a server sending oversized frames
        } else {
          e.type = TransportEvent::Type::Message;
          e.text = msg->str;
        }
        break;
      case ix::WebSocketMessageType::Close:
        e.type = TransportEvent::Type::Close; e.code = msg->closeInfo.code; e.text = msg->closeInfo.reason; break;
      case ix::WebSocketMessageType::Error:
        e.type = TransportEvent::Type::Error; e.text = msg->errorInfo.reason; e.tls = looksLikeTls(msg->errorInfo.reason); break;
      default: return; // ping, pong, fragments
    }
    std::lock_guard lock(impl->mutex);
    impl->queue.push_back(std::move(e));
  });
  s.start();
}

void WsTransport::send(std::string text) { impl_->socket.sendText(text); }

void WsTransport::close() {
  impl_->socket.stop(); // joins IXWebSocket's thread: no callback runs after this
  std::lock_guard lock(impl_->mutex);
  impl_->queue.clear();
}

void WsTransport::poll(std::vector<TransportEvent>& out) {
  std::lock_guard lock(impl_->mutex);
  for (auto& e : impl_->queue) out.push_back(std::move(e));
  impl_->queue.clear();
}

} // namespace arcana::online
