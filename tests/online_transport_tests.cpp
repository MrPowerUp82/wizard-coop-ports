// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "transport.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <mbedtls/version.h>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>

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
  std::puts("online_transport: ok");
  return 0;
}
