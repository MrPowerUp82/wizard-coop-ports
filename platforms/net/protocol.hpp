#pragma once

#include "arcana/game.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arcana::online {

// Must match PROTOCOL_VERSION in meu-game/server/protocol.js.
constexpr int kProtocolVersion = 1;

// Index tables of server/protocol.js, in the same order: the wire sends positions in these lists.
extern const std::array<const char*, 23> kEnemyTypes;
extern const std::array<const char*, 6> kDropTypes;
extern const std::array<const char*, 4> kSprites;
extern const std::array<const char*, 4> kStatuses;
extern const std::array<const char*, 3> kEncounterKinds;

struct DecodeStats { int truncated{}; };  // entities dropped because a fixed-capacity list was full

// Port of decodeState() (server/protocol.js): rebuilds the compact snapshot `c` into `out`, which
// must be freshly constructed. Unknown table indices skip the entity; lists longer than the fixed
// capacities are cut. Returns false for a malformed snapshot. Never throws.
bool decodeSnapshot(const nlohmann::json& compact, GameState& out, DecodeStats* stats = nullptr);

// Player names from the server: control characters removed, at most 16 codepoints.
std::string sanitizeName(std::string_view utf8);

// ---- Messages (server/server.js) -------------------------------------------------------------

// create / join, with the fields src/net.js sends.
struct EntryRequest {
  std::string action{"create"};  // "create" or "join"
  std::string room;              // join only
  std::string name{"Arcanista"}, visibility{"closed"}, campaign{"quick"};
  int color{};
  std::vector<std::string> curses;
  MetaRanks meta;                // Grimório ranks; the server sanitizes them
  Loadout loadout;
};

std::string encodeEntry(const EntryRequest& request);
std::string encodeResume(const std::string& room, const std::string& token);
std::string encodeInput(double x, double y, std::uint64_t seq);
std::string encodePing(double t);
std::string encodeSimple(std::string_view type);  // start, ready, leave, reroll, special, listRooms
std::string encodeChoosePower(const std::string& power);
std::string encodeDash(double x, double y);
std::string encodeSignal(const std::string& kind, std::optional<Vec2> at);
std::string encodeSelectCharacter(int color);

struct LobbyPlayer { std::string id, name; int color{}; bool connected{true}; };
struct LobbyInfo {
  int count{};
  std::string visibility, hostId, campaign;
  std::vector<std::string> curses;
  std::vector<LobbyPlayer> players;
  bool running{};
};
struct RoomInfo { std::string code, host, campaign; int count{}; bool running{}; std::vector<std::string> curses; };
struct RoomsInfo { std::vector<RoomInfo> rooms; int used{}, max{}, version{1}; };
struct Joined { std::string room, playerId, token, visibility; int color{}; bool resumed{}; };
struct ServerError { std::string code, message; std::vector<LobbyPlayer> players; };

enum class ServerKind { Unknown, Pong, Joined, Lobby, Error, Start, State, Rooms, BadSnapshot };
struct ServerMessage {
  ServerKind kind{ServerKind::Unknown};
  double pongT{};
  Joined joined;
  LobbyInfo lobby;
  ServerError error;
  RoomsInfo rooms;
  std::shared_ptr<GameState> state;  // Start / State (heap: a GameState is ~200 KB)
};

// Never throws: malformed text is Unknown, a start/state with a bad snapshot is BadSnapshot.
ServerMessage parseServerMessage(std::string_view text, DecodeStats* stats = nullptr);

} // namespace arcana::online
