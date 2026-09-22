#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace arcana::online {

using nlohmann::json;

const std::array<const char*, 23> kEnemyTypes = {
  "slime", "slimelet", "bat", "eye", "brute", "mushroom", "beetle", "skeleton", "wraith", "imp", "scorpion", "treant",
  "lich", "demon", "spore", "revenant", "sentinel", "seer", "voidling", "voidscarab", "bogwarden", "archon", "umbra"};
const std::array<const char*, 6> kDropTypes = {"gem", "heart", "greenGem", "coin", "magnet", "chest"};
const std::array<const char*, 4> kSprites = {"bolt", "fire", "thorn", "blade"};
const std::array<const char*, 4> kStatuses = {"horde", "boss", "transition", "complete"};
const std::array<const char*, 3> kEncounterKinds = {"merchant", "shrine", "thief"};

namespace {

// ENEMY_FLAGS / SHOT_FLAGS of server/protocol.js.
enum EnemyFlag : int { FBoss = 1, FElite = 2, FSlowed = 4, FWindup = 8, FFuse = 16, FDashWarn = 32, FRoot = 64, FFreeze = 128, FBurning = 256, FThief = 512 };
enum ShotFlag : int { SSpecial = 1, SShard = 2, SReturning = 4, SFullmoon = 8 };

const json& emptyArray() { static const json empty = json::array(); return empty; }
const json& list(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_array() ? *it : emptyArray();
}
double num(const json& row, std::size_t i, double fallback = 0) {
  return i < row.size() && row[i].is_number() ? row[i].get<double>() : fallback;
}
std::string str(const json& row, std::size_t i) {
  return i < row.size() && row[i].is_string() ? row[i].get<std::string>() : std::string{};
}
bool truthy(const json& v) { return (v.is_boolean() && v.get<bool>()) || (v.is_number() && v.get<double>() != 0); }
bool flag(const json& row, std::size_t i) { return i < row.size() && truthy(row[i]); }
double field(const json& o, const char* key, double fallback = 0) {
  const auto it = o.find(key);
  return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
std::string text(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
template <class Table> const char* lookup(const Table& table, double index) {
  if (!(index >= 0) || index >= static_cast<double>(table.size()) || index != std::floor(index)) return nullptr;
  return table[static_cast<std::size_t>(index)];
}
std::uint64_t id(double v) { return v > 0 ? static_cast<std::uint64_t>(v) : 0; }
template <class List, class T> void add(List& to, T&& item, DecodeStats* stats) {
  if (!to.push_back(std::forward<T>(item)) && stats) ++stats->truncated;
}

// Row layout: PLAYER_KEYS of server/protocol.js.
Player decodePlayer(const json& row) {
  Player p;
  p.id = str(row, 0);
  p.name = sanitizeName(str(row, 1));
  p.color = std::clamp(static_cast<int>(num(row, 2)), 0, 3);
  p.x = num(row, 3); p.y = num(row, 4); p.hp = num(row, 5); p.maxHp = num(row, 6, 100);
  p.xp = num(row, 7); p.level = static_cast<int>(num(row, 8, 1)); p.alive = flag(row, 9); p.speed = num(row, 10, 190);
  if (row.size() > 11 && row[11].is_object())
    for (const auto& [power, rank] : row[11].items()) if (rank.is_number()) p.powers[power] = rank.get<int>();
  if (row.size() > 12 && row[12].is_array()) {
    // pendingPowers is server-controlled with no protocol cap; the server only ever offers 3, so
    // 8 is generous headroom while still bounding a hostile/buggy server's payload.
    constexpr std::size_t kMaxPendingPowers = 8;
    for (const auto& power : row[12]) {
      if (p.pendingPowers.size() >= kMaxPendingPowers) break;
      if (power.is_string()) p.pendingPowers.push_back(power.get<std::string>());
    }
  }
  p.specialCharge = num(row, 13); p.coins = static_cast<int>(num(row, 14)); p.reviveProgress = num(row, 15);
  p.reviveBy = str(row, 16); p.reviving = str(row, 17);
  p.castCount = static_cast<int>(num(row, 18)); p.castAngle = num(row, 19); p.invulnerableFor = num(row, 20);
  p.orbitAngle = num(row, 21); p.rerolls = static_cast<int>(num(row, 22)); p.phoenix = static_cast<int>(num(row, 23));
  p.inputSeq = id(num(row, 24)); p.powerTimer = num(row, 25);
  // Missing field 26 means an older/short row (e.g. pre-"connected" wire format); default such players to connected.
  p.connected = row.size() > 26 ? flag(row, 26) : true;
  p.dashFor = num(row, 27); p.dashCooldown = num(row, 28); p.dashX = num(row, 29); p.dashY = num(row, 30);
  p.moveX = num(row, 31); p.moveY = num(row, 32); p.specialCooldown = num(row, 33); p.motionId = id(num(row, 34));
  if (row.size() > 35 && row[35].is_object()) {
    const json& st = row[35];
    p.stats.damage = field(st, "damage"); p.stats.kills = static_cast<int>(field(st, "kills"));
    p.stats.revives = static_cast<int>(field(st, "revives")); p.stats.taken = field(st, "taken");
    if (const auto by = st.find("by"); by != st.end() && by->is_object())
      for (const auto& [kind, amount] : by->items()) if (amount.is_number()) p.stats.by[kind] = amount.get<double>();
  }
  if (row.size() > 36 && row[36].is_array() && row[36].size() >= 2) p.familiar = Familiar{num(row[36], 0), num(row[36], 1)};
  p.specialVariant = static_cast<int>(num(row, 37)); p.shopProgress = num(row, 38);
  return p;
}

bool decode(const json& c, GameState& s, DecodeStats* stats) {
  if (!c.is_object()) return false;
  const auto players = c.find("p");
  if (players == c.end() || !players->is_array()) return false;
  s.campaign = text(c, "ca").empty() ? "classic" : text(c, "ca");
  if (const auto cu = c.find("cu"); cu != c.end() && cu->is_array())
    for (const auto& curse : *cu) if (curse.is_string()) s.curses.push_back(curse.get<std::string>());
  s.loop = static_cast<int>(field(c, "lp"));
  s.bloodPact = c.contains("bp") && truthy(c["bp"]);
  if (const auto en = c.find("en"); en != c.end() && en->is_array()) {
    Encounter e;
    if (const char* kind = lookup(kEncounterKinds, num(*en, 0, -1))) e.kind = kind;
    e.x = num(*en, 1); e.y = num(*en, 2); e.radius = num(*en, 3); e.progress = num(*en, 4); e.ttl = num(*en, 5); e.status = str(*en, 6);
    if (en->size() > 7 && (*en)[7].is_array())
      for (const auto& buyer : (*en)[7]) if (buyer.is_string()) e.buyers.push_back(buyer.get<std::string>());
    s.encounter = std::move(e);
  }
  if (const auto a = c.find("a"); a != c.end() && a->is_array()) {
    Altar altar;
    altar.x = num(*a, 0); altar.y = num(*a, 1); altar.radius = num(*a, 2); altar.progress = num(*a, 3); altar.ttl = num(*a, 4);
    altar.status = str(*a, 5);
    s.altar = altar;
  }
  s.time = field(c, "t"); s.over = c.contains("o") && truthy(c["o"]); s.victory = c.contains("v") && truthy(c["v"]);
  s.phase = static_cast<int>(field(c, "ph")); s.phaseTime = field(c, "pt");
  const char* status = lookup(kStatuses, field(c, "st"));
  s.phaseStatus = status ? status : "horde";
  s.transitionTime = field(c, "tt");

  for (const auto& row : *players) {
    if (!row.is_array()) continue;
    Player p = decodePlayer(row);
    if (!p.id.empty()) s.players[p.id] = std::move(p);
  }
  for (const auto& row : list(c, "e")) {
    const char* type = row.is_array() ? lookup(kEnemyTypes, num(row, 1, -1)) : nullptr;
    if (!type) continue;
    Enemy e;
    e.id = id(num(row, 0)); e.type = type; e.x = num(row, 2); e.y = num(row, 3); e.hp = num(row, 4); e.maxHp = num(row, 5);
    const int f = static_cast<int>(num(row, 6));
    e.boss = f & FBoss; e.elite = f & FElite; e.thief = f & FThief;
    e.slowFor = f & FSlowed ? 1 : 0; e.windup = f & FWindup ? 1 : 0; e.fuse = f & FFuse ? 1 : 0; e.dashWarn = f & FDashWarn ? 1 : 0;
    e.rootFor = f & FRoot ? 1 : 0; e.freezeFor = f & FFreeze ? 1 : 0; e.burningFor = f & FBurning ? 1 : 0;
    e.stage = static_cast<int>(num(row, 7, 1)); e.dashAngle = num(row, 8);
    add(s.enemies, std::move(e), stats);
  }
  for (const auto& row : list(c, "s")) {
    if (!row.is_array()) continue;
    Shot shot;
    shot.x = num(row, 0); shot.y = num(row, 1); shot.vx = num(row, 2); shot.vy = num(row, 3); shot.color = static_cast<int>(num(row, 4));
    const int f = static_cast<int>(num(row, 5));
    shot.special = f & SSpecial; shot.shard = f & SShard; shot.returning = f & SReturning; shot.fullmoon = f & SFullmoon;
    add(s.shots, std::move(shot), stats);
  }
  for (const auto& row : list(c, "es")) {
    if (!row.is_array()) continue;
    EnemyShot shot;
    shot.x = num(row, 0); shot.y = num(row, 1); shot.vx = num(row, 2); shot.vy = num(row, 3);
    if (const char* sprite = lookup(kSprites, num(row, 4, -1))) shot.sprite = sprite;
    add(s.enemyShots, std::move(shot), stats);
  }
  for (const auto& row : list(c, "g")) {
    const char* type = row.is_array() ? lookup(kDropTypes, num(row, 3, -1)) : nullptr;
    if (!type) continue;
    Drop drop;
    drop.id = id(num(row, 0)); drop.x = num(row, 1); drop.y = num(row, 2); drop.type = type; drop.value = num(row, 4); drop.ttl = 24;
    add(s.gems, std::move(drop), stats);
  }
  for (const auto& row : list(c, "h")) {
    if (!row.is_array()) continue;
    Hazard h;
    h.x = num(row, 0); h.y = num(row, 1); h.radius = num(row, 2); h.warning = num(row, 3); h.warn0 = num(row, 4, 1.3); h.fired = flag(row, 5);
    add(s.hazards, h, stats);
  }
  for (const auto& row : list(c, "r")) {
    if (!row.is_array()) continue;
    Rune r;
    r.id = id(num(row, 0)); r.x = num(row, 1); r.y = num(row, 2); r.color = static_cast<int>(num(row, 3));
    r.arm = flag(row, 4) ? 0 : 1; r.radius = num(row, 5);
    add(s.runes, std::move(r), stats);
  }
  for (const auto& row : list(c, "z")) {
    if (!row.is_array()) continue;
    Zone z;
    z.id = id(num(row, 0)); z.x = num(row, 1); z.y = num(row, 2); z.radius = num(row, 3); z.color = static_cast<int>(num(row, 4));
    z.kind = str(row, 5).empty() ? "burn" : str(row, 5); z.warning = num(row, 6); z.ttl = 1;
    add(s.zones, std::move(z), stats);
  }
  for (const auto& o : list(c, "ev")) {
    if (!o.is_object()) continue;
    Event e;
    e.id = id(field(o, "id")); e.kind = text(o, "kind"); e.t = field(o, "t");
    e.x = field(o, "x"); e.y = field(o, "y"); e.r = field(o, "r"); e.a = field(o, "a");
    e.color = static_cast<int>(field(o, "color")); e.stage = static_cast<int>(field(o, "stage"));
    // The JS events carry their detail under different keys; the C++ Event has one slot for each.
    e.variant = static_cast<int>(std::max({field(o, "variant"), field(o, "team"), field(o, "evolved")}));
    for (const char* key : {"reaction", "signal", "type", "encounter"})
      if (std::string t = text(o, key); !t.empty()) { e.text = std::move(t); break; }
    if (const auto points = o.find("points"); points != o.end() && points->is_array())
      for (const auto& v : *points) if (!v.is_number() || !e.points.push_back(v.get<double>())) break;
    e.player = text(o, "player");
    e.name = sanitizeName(text(o, "name"));
    add(s.events, std::move(e), stats);
  }
  return true;
}

} // namespace

bool decodeSnapshot(const json& compact, GameState& out, DecodeStats* stats) {
  try {
    return decode(compact, out, stats);
  } catch (const json::exception&) {
    return false;
  }
}

namespace {
// C0/DEL/C1 controls, zero-width & bidi format characters (direction override/isolate marks that
// can be used to disguise names), and the BOM. Everything else, including accented/multi-byte
// printable text, is kept byte-for-byte.
bool isBannedCodepoint(std::uint32_t cp) {
  return cp <= 0x1F || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) ||
         (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
         (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
}
} // namespace

std::string sanitizeName(std::string_view in) {
  std::string out;
  int codepoints = 0;
  std::size_t i = 0;
  while (i < in.size() && codepoints < 16) {
    const auto lead = static_cast<unsigned char>(in[i]);
    std::size_t len;
    std::uint32_t cp;
    if (lead < 0x80) { len = 1; cp = lead; }
    else if ((lead & 0xE0) == 0xC0) { len = 2; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { len = 3; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { len = 4; cp = lead & 0x07; }
    else { ++i; continue; } // stray continuation byte or invalid lead byte: drop it and resync

    if (i + len > in.size()) { ++i; continue; } // truncated sequence: drop the lead byte and resync
    bool validContinuations = true;
    for (std::size_t k = 1; k < len; ++k) {
      const auto b = static_cast<unsigned char>(in[i + k]);
      if ((b & 0xC0) != 0x80) { validContinuations = false; break; }
      cp = (cp << 6) | (b & 0x3F);
    }
    if (!validContinuations) { ++i; continue; } // malformed sequence: drop the lead byte and resync

    if (!isBannedCodepoint(cp)) { out.append(in.substr(i, len)); ++codepoints; }
    i += len; // dropped characters (banned or malformed) never count toward the 16-codepoint limit
  }
  return out;
}

namespace {
// Names typed on a keyboard may carry bytes that are not UTF-8: replace them instead of throwing.
std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

std::vector<LobbyPlayer> lobbyPlayers(const json& o) {
  std::vector<LobbyPlayer> out;
  for (const auto& p : list(o, "players")) {
    if (!p.is_object()) continue;
    out.push_back({text(p, "id"), sanitizeName(text(p, "name")), std::clamp(static_cast<int>(field(p, "color")), 0, 3),
                   !p.contains("connected") || truthy(p["connected"])});
  }
  return out;
}
std::vector<std::string> strings(const json& o, const char* key) {
  std::vector<std::string> out;
  for (const auto& v : list(o, key)) if (v.is_string()) out.push_back(v.get<std::string>());
  return out;
}
} // namespace

std::string encodeEntry(const EntryRequest& r) {
  json j = {{"type", r.action}, {"v", kProtocolVersion}, {"name", r.name}, {"visibility", r.visibility},
            {"color", r.color}, {"campaign", r.campaign}, {"curses", r.curses}};
  if (r.action == "join") j["room"] = r.room;
  json meta = json::object();
  for (const auto& [upgrade, rank] : r.meta.rank) meta[upgrade] = rank;
  j["meta"] = std::move(meta);
  j["loadout"] = {{"weapon", r.loadout.weapon}, {"special", r.loadout.special}};
  return dump(j);
}
std::string encodeResume(const std::string& room, const std::string& token) {
  return dump({{"type", "resume"}, {"v", kProtocolVersion}, {"room", room}, {"token", token}});
}
std::string encodeInput(double x, double y, std::uint64_t seq) { return dump({{"type", "input"}, {"x", x}, {"y", y}, {"seq", seq}}); }
std::string encodePing(double t) { return dump({{"type", "ping"}, {"t", t}}); }
std::string encodeSimple(std::string_view type) { return dump({{"type", std::string(type)}}); }
std::string encodeChoosePower(const std::string& power) { return dump({{"type", "choosePower"}, {"power", power}}); }
std::string encodeDash(double x, double y) { return dump({{"type", "dash"}, {"x", x}, {"y", y}}); }
std::string encodeSignal(const std::string& kind, std::optional<Vec2> at) {
  json j = {{"type", "signal"}, {"signal", kind}};
  if (at) { j["x"] = std::lround(at->x); j["y"] = std::lround(at->y); }
  return dump(j);
}
std::string encodeSelectCharacter(int color) { return dump({{"type", "selectCharacter"}, {"color", color}}); }

ServerMessage parseServerMessage(std::string_view raw, DecodeStats* stats) {
  ServerMessage m;
  const json j = json::parse(raw, nullptr, false);
  if (!j.is_object()) return m;
  try {
    const std::string type = text(j, "type");
    if (type == "pong") {
      m.kind = ServerKind::Pong;
      m.pongT = field(j, "t");
    } else if (type == "joined") {
      m.kind = ServerKind::Joined;
      m.joined = {text(j, "room"), text(j, "playerId"), text(j, "token"), text(j, "visibility"),
                  std::clamp(static_cast<int>(field(j, "color")), 0, 3), j.contains("resumed") && truthy(j["resumed"])};
    } else if (type == "lobby") {
      if (j.contains("players") && !j["players"].is_array()) return m;
      m.kind = ServerKind::Lobby;
      m.lobby = {static_cast<int>(field(j, "count")), text(j, "visibility"), text(j, "hostId"), text(j, "campaign"),
                 strings(j, "curses"), lobbyPlayers(j), j.contains("running") && truthy(j["running"])};
    } else if (type == "error") {
      m.kind = ServerKind::Error;
      m.error = {text(j, "code"), text(j, "message"), lobbyPlayers(j)};
      if (m.error.message.empty()) m.error.message = "Erro do servidor.";
    } else if (type == "rooms") {
      m.kind = ServerKind::Rooms;
      for (const auto& r : list(j, "rooms")) {
        if (!r.is_object()) continue;
        m.rooms.rooms.push_back({text(r, "code"), sanitizeName(text(r, "host")), text(r, "campaign"), static_cast<int>(field(r, "count")),
                                 r.contains("running") && truthy(r["running"]), strings(r, "curses")});
      }
      if (const auto cap = j.find("capacity"); cap != j.end() && cap->is_object()) {
        m.rooms.used = static_cast<int>(field(*cap, "used"));
        m.rooms.max = static_cast<int>(field(*cap, "max"));
      }
      m.rooms.version = static_cast<int>(field(j, "v", 1));
    } else if (type == "start" || type == "state") {
      auto state = std::make_shared<GameState>();
      const auto body = j.find("state");
      if (body == j.end() || !decodeSnapshot(*body, *state, stats)) { m.kind = ServerKind::BadSnapshot; return m; }
      m.kind = type == "start" ? ServerKind::Start : ServerKind::State;
      m.state = std::move(state);
    }
  } catch (const json::exception&) {
    m = ServerMessage{};
  }
  return m;
}

} // namespace arcana::online
