#include "transport.hpp"

namespace arcana::online {

UrlCheck checkServerUrl(std::string_view url, bool allowInsecure) {
  bool secure = false;
  std::string_view rest;
  if (url.substr(0, 6) == "wss://") { secure = true; rest = url.substr(6); }
  else if (url.substr(0, 5) == "ws://") rest = url.substr(5);
  else return UrlCheck::Invalid;
  const std::string_view host = rest.substr(0, rest.find_first_of(":/"));
  if (host.empty()) return UrlCheck::Invalid;
  if (secure || allowInsecure || host == "localhost" || host == "127.0.0.1") return UrlCheck::Ok;
  return UrlCheck::InsecureBlocked;
}

} // namespace arcana::online
