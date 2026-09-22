#include "transport.hpp"

#include <cctype>

namespace arcana::online {
namespace {

bool ciEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

} // namespace

UrlCheck checkServerUrl(std::string_view url, bool allowInsecure) {
  bool secure = false;
  std::string_view rest;
  if (ciEquals(url.substr(0, 6), "wss://")) { secure = true; rest = url.substr(6); }
  else if (ciEquals(url.substr(0, 5), "ws://")) rest = url.substr(5);
  else return UrlCheck::Invalid;
  // Reject userinfo in the authority (e.g. "ws://localhost:8081@evil.com/ws"): without this, the
  // host check below would see "localhost" while the connection actually goes to evil.com.
  const std::string_view authority = rest.substr(0, rest.find_first_of('/'));
  if (authority.find('@') != std::string_view::npos) return UrlCheck::Invalid;
  const std::string_view host = rest.substr(0, rest.find_first_of(":/"));
  if (host.empty()) return UrlCheck::Invalid;
  if (secure || allowInsecure || host == "localhost" || host == "127.0.0.1") return UrlCheck::Ok;
  return UrlCheck::InsecureBlocked;
}

} // namespace arcana::online
