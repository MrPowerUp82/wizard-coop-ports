// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "transport.hpp"
#include "ws_transport.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <mbedtls/version.h>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace arcana::online;

int main() {
  // Server URL policy: TLS always; plain ws:// only for this machine unless explicitly allowed.
  assert(checkServerUrl("wss://vps65228.publiccloud.com.br/ws", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://localhost:8081", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://127.0.0.1:8081/ws", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://example.com/ws", false) == UrlCheck::InsecureBlocked);
  assert(checkServerUrl("ws://example.com/ws", true) == UrlCheck::Ok);
  assert(checkServerUrl("https://example.com", false) == UrlCheck::Invalid);
  assert(checkServerUrl("wss://", false) == UrlCheck::Invalid);
  assert(checkServerUrl("", false) == UrlCheck::Invalid);

  // The fetched dependencies build and link (versions pinned in CMakeLists.txt).
  assert(std::strcmp(MBEDTLS_VERSION_STRING, "3.6.4") == 0);
  assert(std::strcmp(zlibVersion(), "1.3.1") == 0);
  assert(nlohmann::json::parse(R"({"a":[1,2]})")["a"][1] == 2);
  ix::WebSocket socket;
  socket.setUrl("wss://localhost/ws");

  // A connection nobody answers ends in one Error event, delivered through poll() (no reconnect loop).
  {
    WsTransport transport("");
    transport.open("ws://127.0.0.1:1/");
    std::vector<TransportEvent> events;
    for (int i = 0; i < 100 && events.empty(); ++i) {
      transport.poll(events);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(events.size() == 1 && events[0].type == TransportEvent::Type::Error && !events[0].tls);
    transport.close();
  }

  std::puts("online_transport: ok");
  return 0;
}
