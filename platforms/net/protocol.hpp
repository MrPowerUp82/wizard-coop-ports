#pragma once

#include "arcana/game.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>
#include <string_view>

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

} // namespace arcana::online
