// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fake_transport.hpp"
#include "room_list.hpp"
#include "session.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

using namespace arcana;
using namespace arcana::online;
using namespace arcana::online::testing;
using nlohmann::json;

namespace {
// A server snapshot with one player at (x, 0); `type` is "start" or "state".
std::string snapshot(const char* type, double t, double x, bool over = false, const char* id = "u1") {
  json player = json::array({id, "Ana", 0, x, 0, 100, 100, 0, 1, true, 190, json::object(), nullptr, 0, 0, 0, nullptr, nullptr, 0, 0, 0, 0, 1, 0, 0, 0,
                             true, 0, 0, 0, 1, 0, 1, 0, 0, {{"damage", 0}, {"kills", 0}, {"revives", 0}, {"taken", 0}}, nullptr, 0, 0});
  json state = {{"t", t}, {"o", over ? 1 : 0}, {"v", 0}, {"ph", 0}, {"pt", t}, {"st", 0}, {"tt", 0}, {"p", json::array({player})},
                {"e", json::array()}, {"s", json::array()}, {"es", json::array()}, {"g", json::array()}, {"h", json::array()},
                {"r", json::array()}, {"z", json::array()}, {"ev", json::array()}};
  return json{{"type", type}, {"state", state}}.dump();
}
std::string joined(bool resumed = false) {
  return json{{"type", "joined"}, {"room", "ABC234"}, {"playerId", "u1"}, {"token", "tok"}, {"color", 0}, {"visibility", "open"},
              {"resumed", resumed}}.dump();
}
bool sentType(const FakeWire& w, const char* type) {
  for (const auto& s : w.sent) if (json::parse(s)["type"] == type) return true;
  return false;
}
json lastSent(const FakeWire& w) { return json::parse(w.sent.back()); }
SessionConfig config(FakeNet& net, const char* action) {
  SessionConfig c;
  c.url = "wss://example.com/ws";
  c.entry.action = action;
  c.entry.room = "ABC234";
  c.entry.name = "Ana";
  c.transport = net.factory();
  return c;
}
} // namespace

int main() {
  // Create -> lobby -> start -> in game, with pings and interpolation.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    assert(net.wires.size() == 1 && net.wires[0]->url == "wss://example.com/ws" && s.status() == SessionStatus::Connecting);
    FakeWire& w = *net.wires[0];
    open(w);
    s.update(10);
    assert(json::parse(w.sent[0])["type"] == "create" && json::parse(w.sent[0])["v"] == kProtocolVersion);
    assert(sentType(w, "ping"));
    message(w, joined());
    message(w, R"({"type":"lobby","count":1,"visibility":"open","hostId":"u1","running":false,"campaign":"quick","curses":[],"players":[{"id":"u1","name":"Ana","color":0}]})");
    s.update(20);
    assert(s.status() == SessionStatus::Lobby && s.playerId() == "u1" && s.room() == "ABC234" && s.isHost());
    assert(!sentType(w, "ready"));                    // only joiners ask for the running state
    s.startMatch();
    assert(lastSent(w)["type"] == "start");
    message(w, R"({"type":"pong","t":990})");
    message(w, snapshot("start", 1.0, 0));
    s.update(1000);
    assert(s.status() == SessionStatus::Playing);
    assert(s.rttMs() < 120);                          // 0.7*120 + 0.3*(1000-990) = 87
    message(w, snapshot("state", 1.1, 10));
    s.update(1100);
    auto view = std::make_unique<GameState>();
    assert(s.frame(1150, 1.0 / 60, {0, 0}, *view));
    assert(view->players.at("u1").x >= 0 && view->players.at("u1").x <= 10);
    assert(sentType(w, "input"));
    s.update(2100);                                   // 2 s after the last ping: another one
    int pings = 0;
    for (const auto& m : w.sent) pings += json::parse(m)["type"] == "ping";
    assert(pings >= 2);
    s.special(); assert(lastSent(w)["type"] == "special");
    s.choosePower("arcane"); assert(lastSent(w)["power"] == "arcane");
    s.signal("help"); assert(lastSent(w)["signal"] == "help");
  }
  // Join: asks for the state with `ready`; a taken character switches to a free one and retries.
  {
    FakeNet net;
    Session s(config(net, "join"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    s.update(1);
    message(w, R"({"type":"error","code":"CHARACTER_TAKEN","message":"Em uso","players":[{"id":"x","name":"Beto","color":0},{"id":"y","name":"Cris","color":1}]})");
    s.update(2);
    assert(lastSent(w)["type"] == "join" && lastSent(w)["color"] == 2 && s.color() == 2);
    assert(s.takeNotice().has_value());
    message(w, joined());
    s.update(3);
    assert(lastSent(w)["type"] == "ready");
  }
  // Errors before joining end the session with the server's message.
  {
    FakeNet net;
    Session s(config(net, "join"));
    s.start(0);
    open(*net.wires[0]);
    message(*net.wires[0], R"({"type":"error","message":"Sala não encontrada."})");
    s.update(1);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Sala não encontrada.");
  }
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    error(*net.wires[0], true);
    s.update(1);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Certificado do servidor inválido.");
  }
  // A drop mid-game reconnects with resume after 400 ms; a close code >= 4000 does not.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    message(w, joined());
    message(w, snapshot("start", 1.0, 0));
    s.update(10);
    closeWith(w, 1006);
    s.update(20);
    assert(s.status() == SessionStatus::Reconnecting && net.wires.size() == 1);
    s.update(300);
    assert(net.wires.size() == 1);                    // still waiting for the first retry
    s.update(421);
    assert(net.wires.size() == 2);
    FakeWire& w2 = *net.wires[1];
    open(w2);
    s.update(430);
    assert(json::parse(w2.sent[0])["type"] == "resume" && json::parse(w2.sent[0])["token"] == "tok");
    message(w2, joined(true));
    message(w2, snapshot("start", 2.0, 5));
    s.update(440);
    assert(s.status() == SessionStatus::Playing);
    closeWith(w2, 4002, "Partida encerrada");
    s.update(450);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Partida encerrada");
  }
  // Leaving sends `leave` and never reconnects; a finished match closes without a failure.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    open(*net.wires[0]);
    s.update(1);
    s.leave();
    assert(net.wires[0]->closed && sentType(*net.wires[0], "leave") && s.status() == SessionStatus::Closed);
  }
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    message(w, joined());
    message(w, snapshot("start", 1.0, 0, true));
    s.update(10);
    assert(s.over());
    closeWith(w, 1000);
    s.update(20);
    assert(s.status() == SessionStatus::Closed && s.failure().empty() && net.wires.size() == 1);
  }
  // Broken snapshots are counted and skipped.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    open(*net.wires[0]);
    message(*net.wires[0], joined());
    message(*net.wires[0], R"({"type":"state","state":[]})");
    s.update(1);
    assert(s.droppedSnapshots() == 1);
  }
  // Room list: a short connection per refresh, every 5 s; another protocol version hides the rooms.
  {
    FakeNet net;
    RoomList list("wss://example.com/ws", net.factory());
    list.update(0);
    assert(net.wires.size() == 1 && !list.loaded());
    open(*net.wires[0]);
    list.update(10);
    assert(json::parse(net.wires[0]->sent[0])["type"] == "listRooms");
    message(*net.wires[0], R"({"type":"rooms","rooms":[{"code":"XYZ789","count":1,"running":false,"host":"Ana","campaign":"quick","curses":[]}],"capacity":{"used":1,"max":3},"v":1})");
    list.update(20);
    assert(list.loaded() && list.rooms().rooms.size() == 1 && list.error().empty() && net.wires[0]->closed);
    list.update(3000);
    assert(net.wires.size() == 1);                     // next refresh only 5 s later
    list.update(5021);
    assert(net.wires.size() == 2);
    open(*net.wires[1]);
    message(*net.wires[1], R"({"type":"rooms","rooms":[{"code":"XYZ789"}],"capacity":{"used":1,"max":3},"v":2})");
    list.update(5030);
    assert(list.rooms().rooms.empty() && list.error().find("Atualize") != std::string::npos);
    list.refresh(5040);
    list.update(5040);
    assert(net.wires.size() == 3);
    error(*net.wires[2]);
    list.update(5050);
    assert(list.error() == "Não foi possível alcançar o servidor.");
  }
  std::puts("online_session: ok");
  return 0;
}
