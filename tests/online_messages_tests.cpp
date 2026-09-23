// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>

using namespace arcana;
using namespace arcana::online;
using nlohmann::json;

int main() {
  // Client -> server: the same fields src/net.js sends, plus the protocol version.
  {
    EntryRequest r;
    r.action = "join"; r.room = "ABC234"; r.name = "Ana"; r.color = 2; r.campaign = "classic"; r.curses = {"swarm"};
    r.meta.rank["vigor"] = 3; r.loadout = {"orbit", 1};
    const json j = json::parse(encodeEntry(r));
    assert(j["type"] == "join" && j["v"] == kProtocolVersion && j["room"] == "ABC234" && j["name"] == "Ana");
    assert(j["color"] == 2 && j["campaign"] == "classic" && j["curses"][0] == "swarm" && j["visibility"] == "closed");
    assert(j["meta"]["vigor"] == 3 && j["loadout"]["weapon"] == "orbit" && j["loadout"]["special"] == 1);
    // server/server.js refuses the Aurora Guardian unless `unlocks.aurora` is true (src/net.js sends it always).
    assert(j["unlocks"]["aurora"] == false);
    r.auroraUnlocked = true; r.color = AURORA;
    assert(json::parse(encodeEntry(r))["unlocks"]["aurora"] == true && json::parse(encodeEntry(r))["color"] == AURORA);
    r.godUnlocked = true; r.color = GOD;
    assert(json::parse(encodeEntry(r))["unlocks"]["god"] == true && json::parse(encodeEntry(r))["color"] == GOD);
    r.action = "create";
    assert(!json::parse(encodeEntry(r)).contains("room"));
    r.name = std::string("Ana\xFF");  // invalid UTF-8 never throws
    assert(!encodeEntry(r).empty());
  }
  {
    const json resume = json::parse(encodeResume("ABC234", "tok"));
    assert(resume["type"] == "resume" && resume["v"] == kProtocolVersion && resume["token"] == "tok");
    const json input = json::parse(encodeInput(0.5, -1, 7));
    assert(input["type"] == "input" && input["x"] == 0.5 && input["y"] == -1 && input["seq"] == 7);
    assert(json::parse(encodeSimple("start"))["type"] == "start");
    assert(json::parse(encodeChoosePower("arcane"))["power"] == "arcane");
    assert(json::parse(encodeDash(0, 1))["y"] == 1);
    const json look = json::parse(encodeSignal("look", Vec2{10.4, 20.6}));
    assert(look["signal"] == "look" && look["x"] == 10 && look["y"] == 21);
    assert(!json::parse(encodeSignal("help", std::nullopt)).contains("x"));
    assert(json::parse(encodeSelectCharacter(3))["color"] == 3);
    assert(json::parse(encodePing(12.5))["t"] == 12.5);
  }
  // Server -> client.
  {
    const auto pong = parseServerMessage(R"({"type":"pong","t":42})");
    assert(pong.kind == ServerKind::Pong && pong.pongT == 42);
    const auto joined = parseServerMessage(R"({"type":"joined","room":"ABC234","playerId":"u1","token":"t1","color":1,"count":1,"visibility":"open","resumed":true})");
    assert(joined.kind == ServerKind::Joined && joined.joined.room == "ABC234" && joined.joined.playerId == "u1");
    assert(joined.joined.token == "t1" && joined.joined.color == 1 && joined.joined.resumed && joined.joined.visibility == "open");
    const auto lobby = parseServerMessage(R"({"type":"lobby","count":2,"visibility":"open","hostId":"u1","running":false,"campaign":"quick","curses":["tyrant"],
      "players":[{"id":"u1","name":"Ana","color":0,"connected":true},{"id":"u2","name":"Beto","color":3,"connected":false}]})");
    assert(lobby.kind == ServerKind::Lobby && lobby.lobby.hostId == "u1" && lobby.lobby.players.size() == 2);
    assert(lobby.lobby.players[1].color == 3 && !lobby.lobby.players[1].connected && lobby.lobby.curses[0] == "tyrant");
    // The unlockable characters (4, 5) travel as they are; out-of-range colours are clamped.
    const auto secret = parseServerMessage(R"({"type":"lobby","players":[{"id":"u1","name":"Dev","color":4},{"id":"u2","name":"Sol","color":5},{"id":"u3","name":"X","color":9}]})");
    assert(secret.lobby.players[0].color == DEVELOPER && secret.lobby.players[1].color == AURORA && secret.lobby.players[2].color == GOD);
    assert(parseServerMessage(R"({"type":"joined","room":"A","playerId":"u1","token":"t","color":5})").joined.color == AURORA);
    const auto locked = parseServerMessage(R"({"type":"error","code":"CHARACTER_LOCKED","message":"Vença o modo Clássico para desbloquear o Guardião da Aurora."})");
    assert(locked.kind == ServerKind::Error && locked.error.code == "CHARACTER_LOCKED" && !locked.error.message.empty());
    const auto error = parseServerMessage(R"({"type":"error","code":"CHARACTER_TAKEN","message":"Em uso","players":[{"id":"u1","name":"Ana","color":0}]})");
    assert(error.kind == ServerKind::Error && error.error.code == "CHARACTER_TAKEN" && error.error.players.size() == 1);
    const auto rooms = parseServerMessage(R"({"type":"rooms","rooms":[{"code":"XYZ789","count":2,"running":true,"host":"Ana","campaign":"classic","curses":[]}],
      "capacity":{"used":1,"max":3},"v":1})");
    assert(rooms.kind == ServerKind::Rooms && rooms.rooms.rooms.size() == 1 && rooms.rooms.rooms[0].code == "XYZ789");
    assert(rooms.rooms.rooms[0].running && rooms.rooms.used == 1 && rooms.rooms.max == 3 && rooms.rooms.version == 1);
    assert(parseServerMessage(R"({"type":"rooms","rooms":[],"capacity":{"used":0,"max":3}})").rooms.version == 1); // pre-versioning server
    const auto state = parseServerMessage(R"({"type":"state","state":{"t":2.5,"p":[["u1","Ana",0,1,2,100,100,0,1,true,190,{},null]],
      "e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]}})");
    assert(state.kind == ServerKind::State && state.state && state.state->time == 2.5 && state.state->players.at("u1").x == 1);
    const auto dev = parseServerMessage(R"({"type":"state","state":{"t":1,"p":[["u1","Dev",4,1,2,500,500,0,1,true,190,{},null]],
      "e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]}})");
    assert(dev.state && dev.state->players.at("u1").color == DEVELOPER && dev.state->players.at("u1").maxHp == 500);
    assert(parseServerMessage(R"({"type":"start","state":[]})").kind == ServerKind::BadSnapshot);
    assert(parseServerMessage("{nope").kind == ServerKind::Unknown);
    assert(parseServerMessage(R"({"type":"lobby","players":"x"})").kind == ServerKind::Unknown);
  }
  std::puts("online_messages: ok");
  return 0;
}
