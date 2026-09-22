// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

using namespace arcana;
using namespace arcana::online;
using nlohmann::json;

namespace {
std::string current;
[[noreturn]] void fail(const std::string& what) { std::fprintf(stderr, "[%s] %s\n", current.c_str(), what.c_str()); std::exit(1); }
void near(double actual, double expected, const std::string& what) {
  if (std::fabs(actual - expected) > 1e-6) fail(what + ": " + std::to_string(actual) + " != " + std::to_string(expected));
}
void same(const std::string& actual, const std::string& expected, const std::string& what) {
  if (actual != expected) fail(what + ": '" + actual + "' != '" + expected + "'");
}
double num(const json& o, const char* key, double fallback = 0) {
  const auto it = o.find(key);
  return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
bool yes(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && ((it->is_boolean() && it->get<bool>()) || (it->is_number() && it->get<double>() != 0));
}
std::string str(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

void comparePlayer(const Player& p, const json& e) {
  const std::string at = "player " + p.id + " ";
  same(p.id, str(e, "id"), at + "id");
  same(p.name, str(e, "name"), at + "name");
  near(p.color, num(e, "color"), at + "color");
  for (const auto& [key, value] : std::initializer_list<std::pair<const char*, double>>{
         {"x", p.x}, {"y", p.y}, {"hp", p.hp}, {"maxHp", p.maxHp}, {"xp", p.xp}, {"level", p.level}, {"speed", p.speed},
         {"specialCharge", p.specialCharge}, {"coins", p.coins}, {"reviveProgress", p.reviveProgress}, {"castCount", p.castCount},
         {"castAngle", p.castAngle}, {"invulnerableFor", p.invulnerableFor}, {"orbitAngle", p.orbitAngle}, {"rerolls", p.rerolls},
         {"phoenix", p.phoenix}, {"inputSeq", static_cast<double>(p.inputSeq)}, {"powerTimer", p.powerTimer}, {"dashFor", p.dashFor},
         {"dashCooldown", p.dashCooldown}, {"dashX", p.dashX}, {"dashY", p.dashY}, {"moveX", p.moveX}, {"moveY", p.moveY},
         {"specialCooldown", p.specialCooldown}, {"motionId", static_cast<double>(p.motionId)}, {"specialVariant", p.specialVariant},
         {"shopProgress", p.shopProgress}})
    near(value, num(e, key), at + key);
  near(p.alive, yes(e, "alive"), at + "alive");
  near(p.connected, yes(e, "connected"), at + "connected");
  same(p.reviveBy, str(e, "reviveBy"), at + "reviveBy");
  same(p.reviving, str(e, "reviving"), at + "reviving");
  const json& powers = e.at("powers");
  if (p.powers.size() != powers.size()) fail(at + "powers size");
  for (const auto& [id, rank] : powers.items()) near(p.powers.at(id), rank.get<double>(), at + "power " + id);
  const json& pending = e.at("pendingPowers");
  if (pending.is_null() ? !p.pendingPowers.empty() : pending.get<std::vector<std::string>>() != p.pendingPowers) fail(at + "pendingPowers");
  const json& stats = e.at("stats");
  near(p.stats.kills, num(stats, "kills"), at + "kills");
  near(p.stats.damage, num(stats, "damage"), at + "damage");
  near(p.stats.revives, num(stats, "revives"), at + "revives");
  near(p.stats.taken, num(stats, "taken"), at + "taken");
  if (stats.contains("by")) for (const auto& [kind, v] : stats["by"].items()) near(p.stats.by.at(kind), v.get<double>(), at + "by " + kind);
  const json& familiar = e.at("familiar");
  if (familiar.is_null() != !p.familiar.has_value()) fail(at + "familiar presence");
  if (p.familiar) { near(p.familiar->x, num(familiar, "x"), at + "familiar.x"); near(p.familiar->y, num(familiar, "y"), at + "familiar.y"); }
}

void compareCase(const json& c) {
  current = c.at("name").get<std::string>();
  auto state = std::make_unique<GameState>();
  if (!decodeSnapshot(c.at("encoded"), *state)) fail("decode failed");
  const json& d = c.at("decoded");
  near(state->time, num(d, "time"), "time");
  same(state->campaign, str(d, "campaign"), "campaign");
  near(state->over, yes(d, "over"), "over");
  near(state->victory, yes(d, "victory"), "victory");
  near(state->phase, num(d, "phase"), "phase");
  near(state->phaseTime, num(d, "phaseTime"), "phaseTime");
  same(state->phaseStatus, str(d, "phaseStatus"), "phaseStatus");
  near(state->transitionTime, num(d, "transitionTime"), "transitionTime");
  near(state->loop, num(d, "loop"), "loop");
  near(state->bloodPact, yes(d, "bloodPact"), "bloodPact");
  if (state->curses != d.at("curses").get<std::vector<std::string>>()) fail("curses");

  const bool hasAltar = d.contains("altar") && d["altar"].is_object();
  if (state->altar.has_value() != hasAltar) fail("altar presence");
  if (hasAltar) {
    const json& a = d["altar"];
    near(state->altar->x, num(a, "x"), "altar.x"); near(state->altar->y, num(a, "y"), "altar.y");
    near(state->altar->radius, num(a, "radius"), "altar.radius"); near(state->altar->progress, num(a, "progress"), "altar.progress");
    near(state->altar->ttl, num(a, "ttl"), "altar.ttl"); same(state->altar->status, str(a, "status"), "altar.status");
  }
  const bool hasEncounter = d.contains("encounter") && d["encounter"].is_object();
  if (state->encounter.has_value() != hasEncounter) fail("encounter presence");
  if (hasEncounter) {
    const json& en = d["encounter"];
    same(state->encounter->kind, str(en, "kind"), "encounter.kind"); same(state->encounter->status, str(en, "status"), "encounter.status");
    near(state->encounter->x, num(en, "x"), "encounter.x"); near(state->encounter->progress, num(en, "progress"), "encounter.progress");
    near(state->encounter->buyers.size(), en.at("buyers").size(), "encounter.buyers");
  }

  const json& players = d.at("players");
  if (state->players.size() != players.size()) fail("player count");
  for (const auto& [id, e] : players.items()) {
    const auto it = state->players.find(id);
    if (it == state->players.end()) fail("missing player " + id);
    comparePlayer(it->second, e);
  }

  const json& enemies = d.at("enemies");
  if (state->enemies.size() != enemies.size()) fail("enemy count");
  for (std::size_t i = 0; i < enemies.size(); ++i) {
    const Enemy& g = state->enemies[i];
    const json& e = enemies[i];
    const std::string at = "enemy " + std::to_string(i) + " ";
    near(static_cast<double>(g.id), num(e, "id"), at + "id"); same(g.type, str(e, "type"), at + "type");
    near(g.x, num(e, "x"), at + "x"); near(g.y, num(e, "y"), at + "y"); near(g.hp, num(e, "hp"), at + "hp"); near(g.maxHp, num(e, "maxHp"), at + "maxHp");
    near(g.boss, yes(e, "boss"), at + "boss"); near(g.elite, yes(e, "elite"), at + "elite"); near(g.thief, yes(e, "thief"), at + "thief");
    near(g.slowFor, num(e, "slowFor"), at + "slowFor"); near(g.windup, num(e, "windup"), at + "windup"); near(g.fuse, num(e, "fuse"), at + "fuse");
    near(g.dashWarn, num(e, "dashWarn"), at + "dashWarn"); near(g.rootFor, num(e, "rootFor"), at + "rootFor");
    near(g.freezeFor, num(e, "freezeFor"), at + "freezeFor"); near(g.burningFor, num(e, "burningFor"), at + "burningFor");
    if (g.boss) { near(g.stage, num(e, "stage", 1), at + "stage"); near(g.dashAngle, num(e, "dashAngle"), at + "dashAngle"); }
  }

  const json& shots = d.at("shots");
  if (state->shots.size() != shots.size()) fail("shot count");
  for (std::size_t i = 0; i < shots.size(); ++i) {
    const Shot& g = state->shots[i];
    const json& e = shots[i];
    near(g.x, num(e, "x"), "shot.x"); near(g.vy, num(e, "vy"), "shot.vy"); near(g.color, num(e, "color"), "shot.color");
    near(g.special, yes(e, "special"), "shot.special"); near(g.shard, yes(e, "shard"), "shot.shard");
    near(g.returning, yes(e, "returning"), "shot.returning"); near(g.fullmoon, yes(e, "fullmoon"), "shot.fullmoon");
  }
  const json& enemyShots = d.at("enemyShots");
  if (state->enemyShots.size() != enemyShots.size()) fail("enemy shot count");
  for (std::size_t i = 0; i < enemyShots.size(); ++i) {
    near(state->enemyShots[i].x, num(enemyShots[i], "x"), "enemyShot.x");
    same(state->enemyShots[i].sprite, str(enemyShots[i], "sprite"), "enemyShot.sprite");
  }
  const json& gems = d.at("gems");
  if (state->gems.size() != gems.size()) fail("gem count");
  for (std::size_t i = 0; i < gems.size(); ++i) {
    near(static_cast<double>(state->gems[i].id), num(gems[i], "id"), "gem.id");
    same(state->gems[i].type, str(gems[i], "type"), "gem.type");
    near(state->gems[i].value, num(gems[i], "value"), "gem.value");
  }
  const json& hazards = d.at("hazards");
  if (state->hazards.size() != hazards.size()) fail("hazard count");
  for (std::size_t i = 0; i < hazards.size(); ++i) {
    near(state->hazards[i].radius, num(hazards[i], "radius"), "hazard.radius");
    near(state->hazards[i].warning, num(hazards[i], "warning"), "hazard.warning");
    near(state->hazards[i].fired, yes(hazards[i], "fired"), "hazard.fired");
  }
  const json& runes = d.at("runes");
  if (state->runes.size() != runes.size()) fail("rune count");
  for (std::size_t i = 0; i < runes.size(); ++i) near(state->runes[i].arm, num(runes[i], "arm"), "rune.arm");
  const json& zones = d.at("zones");
  if (state->zones.size() != zones.size()) fail("zone count");
  for (std::size_t i = 0; i < zones.size(); ++i) {
    same(state->zones[i].kind, str(zones[i], "kind"), "zone.kind");
    near(state->zones[i].warning, num(zones[i], "warning"), "zone.warning");
  }
  const json& events = d.at("events");
  if (state->events.size() != events.size()) fail("event count");
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Event& g = state->events[i];
    const json& e = events[i];
    const std::string at = "event " + std::to_string(i) + " ";
    near(static_cast<double>(g.id), num(e, "id"), at + "id"); same(g.kind, str(e, "kind"), at + "kind");
    near(g.x, num(e, "x"), at + "x"); near(g.y, num(e, "y"), at + "y");
    near(g.color, num(e, "color"), at + "color"); near(g.r, num(e, "r"), at + "r"); near(g.stage, num(e, "stage"), at + "stage");
    same(g.player, str(e, "player"), at + "player");
    same(g.name, sanitizeName(str(e, "name")), at + "name");
    near(g.variant, std::max({num(e, "variant"), num(e, "team"), num(e, "evolved")}), at + "variant");
    std::string expectedText;
    for (const char* key : {"reaction", "signal", "type", "encounter"})
      if (std::string t = str(e, key); !t.empty()) { expectedText = std::move(t); break; }
    same(g.text, expectedText, at + "text");
    if (e.contains("points")) {
      const json& pts = e["points"];
      near(g.points.size(), pts.size(), at + "points size");
      for (std::size_t k = 0; k < g.points.size() && k < pts.size(); ++k)
        near(g.points[k], pts[k].get<double>(), at + "points[" + std::to_string(k) + "]");
    }
  }
}
} // namespace

int main() {
  std::ifstream file(ARCANA_FIXTURES_DIR "/protocol/snapshots.json");
  if (!file) fail("fixtures ausentes: rode `npm run fixtures:cpp` no meu-game");
  const json fixtures = json::parse(file);

  current = "tables";
  near(fixtures.at("version").get<int>(), kProtocolVersion, "version");
  auto table = [](const json& list, const auto& ours, const char* name) {
    if (list.size() != ours.size()) fail(std::string(name) + " size");
    for (std::size_t i = 0; i < ours.size(); ++i) same(ours[i], list[i].get<std::string>(), name);
  };
  const json& tables = fixtures.at("tables");
  table(tables.at("enemyTypes"), kEnemyTypes, "enemyTypes");
  table(tables.at("dropTypes"), kDropTypes, "dropTypes");
  table(tables.at("sprites"), kSprites, "sprites");
  table(tables.at("statuses"), kStatuses, "statuses");
  table(tables.at("encounterKinds"), kEncounterKinds, "encounterKinds");

  for (const auto& c : fixtures.at("cases")) compareCase(c);

  current = "robustness";
  {
    auto s = std::make_unique<GameState>();
    assert(!decodeSnapshot(json::parse("[]"), *s));
    assert(!decodeSnapshot(json::parse(R"({"t":1})"), *s)); // no players list
    assert(!decodeSnapshot(json::parse(R"({"t":1,"p":"nope"})"), *s));
  }
  {
    // 200 enemies, one with an unknown type index: skipped; the rest fills the fixed capacity.
    json c = json::parse(R"({"t":1,"p":[],"e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]})");
    for (int i = 0; i < 200; ++i) c["e"].push_back(json::array({i + 1, i == 5 ? 99 : 0, 0, 0, 10, 10, 0}));
    auto s = std::make_unique<GameState>();
    DecodeStats stats;
    assert(decodeSnapshot(c, *s, &stats));
    assert(s->enemies.size() == static_cast<std::size_t>(cfg::MAX_ENEMIES));
    assert(stats.truncated == 199 - cfg::MAX_ENEMIES);
  }
  {
    // pendingPowers is server-controlled with no protocol cap; decodePlayer must cap it at 8
    // (the server only ever offers 3) rather than trusting an arbitrarily long list.
    json c = json::parse(R"({"t":1,"p":[],"e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]})");
    json pending = json::array();
    for (int i = 0; i < 10; ++i) pending.push_back("power" + std::to_string(i));
    c["p"].push_back(json::array({"p1", "Ana", 0, 0, 0, 100, 100, 0, 1, true, 190, json::object(), pending}));
    auto s = std::make_unique<GameState>();
    assert(decodeSnapshot(c, *s));
    assert(s->players.at("p1").pendingPowers.size() == 8);
  }
  assert(sanitizeName("Ana\x01L\xC3\xBA") == "AnaL\xC3\xBA");
  assert(sanitizeName("abcdefghijklmnopqrstu") == "abcdefghijklmnop");
  assert(sanitizeName(std::string(20, 'a') + "\xC3").size() == 16);
  assert(sanitizeName("A\xC2\x85" "B") == "AB");        // C1 control (U+0085)
  assert(sanitizeName("A\xE2\x80\xAE" "RB") == "ARB");  // RTL override (U+202E)
  assert(sanitizeName("A\x80""B") == "AB");          // stray UTF-8 continuation byte
  std::puts("online_protocol: ok");
  return 0;
}
