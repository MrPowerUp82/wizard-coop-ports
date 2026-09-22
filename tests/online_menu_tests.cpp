// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fake_transport.hpp"
#include "online_menu.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>
#include <memory>

using namespace arcana;
using namespace arcana::online;
using namespace arcana::online::testing;
using arcana::sdl::OnlinePort;
using arcana::sdl::TextInput;
using nlohmann::json;

namespace {
double now = 0;
OnlinePort::MenuResult step(OnlineMenu& m, Profile& p, std::uint32_t pressed = 0, TextInput text = {}) {
  now += 16;
  return m.updateMenu(pressed, 0, text, p);
}
} // namespace

int main() {
  FakeNet net;
  OnlineMenuConfig config;
  config.transport = net.factory();
  OnlineMenu menu(config);
  menu.setClockForTests([] { return now; });
  Profile profile;

  // First visit asks for a name.
  menu.openMenu(profile);
  assert(menu.page() == OnlineMenu::Page::Name && menu.wantsText());
  step(menu, profile, 0, TextInput{"Ana", 0, true});
  assert(profile.prefs.name == "Ana" && menu.page() == OnlineMenu::Page::Home && !menu.wantsText());

  // The room list is fetched; confirming the first room joins it.
  step(menu, profile);
  FakeWire& list = *net.wires.back();
  assert(list.url == std::string(kDefaultServer));
  open(list);
  step(menu, profile);
  message(list, R"({"type":"rooms","rooms":[{"code":"XYZ789","count":1,"running":true,"host":"Beto","campaign":"quick","curses":[]}],"capacity":{"used":1,"max":3},"v":1})");
  step(menu, profile);
  step(menu, profile, sdl::ActConfirm);
  assert(menu.page() == OnlineMenu::Page::Connecting);
  FakeWire& game = *net.wires.back();
  open(game);
  step(menu, profile);
  const json entry = json::parse(game.sent[0]);
  assert(entry["type"] == "join" && entry["room"] == "XYZ789" && entry["name"] == "Ana");

  // Joined a running room: the first snapshot starts the match.
  message(game, R"({"type":"joined","room":"XYZ789","playerId":"u2","token":"t","color":1})");
  message(game, R"({"type":"start","state":{"t":1,"p":[["u2","Ana",1,0,0,100,100,0,1,true,190,{},null]],"e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]}})");
  assert(step(menu, profile) == OnlinePort::MenuResult::Play);
  assert(menu.localId() == "u2");
  auto view = std::make_unique<GameState>();
  now += 200;
  assert(menu.frame(1.0 / 60, {0, 0}, *view) && view->players.count("u2") == 1);

  // Leaving goes back to the room list.
  menu.leave();
  assert(menu.page() == OnlineMenu::Page::Home && menu.closed());

  // Create: toggle visibility, then "Criar sala" (row 8).
  const int rooms = 1;
  for (int i = 0; i < rooms; ++i) step(menu, profile, sdl::ActDown); // skip the room row
  step(menu, profile, sdl::ActConfirm);                              // "Criar sala"
  assert(menu.page() == OnlineMenu::Page::Create);
  step(menu, profile, sdl::ActConfirm);                              // visibility: aberta -> fechada
  for (int i = 0; i < 8; ++i) step(menu, profile, sdl::ActDown);
  step(menu, profile, sdl::ActConfirm);
  assert(menu.page() == OnlineMenu::Page::Connecting);
  FakeWire& created = *net.wires.back();
  open(created);
  step(menu, profile);
  assert(json::parse(created.sent[0])["visibility"] == "closed");
  message(created, R"({"type":"joined","room":"NEW234","playerId":"u1","token":"t","color":0})");
  message(created, R"({"type":"lobby","count":1,"visibility":"closed","hostId":"u1","running":false,"campaign":"quick","curses":[],"players":[{"id":"u1","name":"Ana","color":0}]})");
  step(menu, profile);
  assert(menu.page() == OnlineMenu::Page::Lobby);
  step(menu, profile, sdl::ActRight);                               // next free character
  assert(json::parse(created.sent.back())["type"] == "selectCharacter");
  step(menu, profile, sdl::ActDown);                                // "Iniciar"
  step(menu, profile, sdl::ActConfirm);
  assert(json::parse(created.sent.back())["type"] == "start");

  // Cancel while in the lobby leaves the room.
  step(menu, profile, sdl::ActCancel);
  assert(menu.page() == OnlineMenu::Page::Home && created.closed);

  // Plain ws:// to a remote host is refused before connecting.
  {
    FakeNet other;
    OnlineMenuConfig c;
    c.url = "ws://example.com/ws";
    c.forceUrl = true;
    c.transport = other.factory();
    OnlineMenu insecure(c);
    insecure.setClockForTests([] { return now; });
    Profile named;
    named.prefs.name = "Ana";
    insecure.openMenu(named);
    step(insecure, named);
    assert(other.wires.empty() && !insecure.message().empty());
  }
  std::puts("online_menu: ok");
  return 0;
}
