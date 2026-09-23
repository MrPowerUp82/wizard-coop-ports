// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
// Port of meu-game server/developer.test.js and server/aurora.test.js: the secret Developer and the
// Aurora Guardian (the Classic reward).
#include "arcana/game.hpp"
#include "arcana/profile.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
using namespace arcana;

namespace {
struct FixedRandom : Random { real next() override { return 0.9; } };
// The PSP build (ARCANA_REAL_FLOAT) runs the same checks in single precision.
bool near(double a, double b) { return std::fabs(a - b) <= (sizeof(real) == 4 ? 1e-4 : 1e-9) * std::max<double>(1.0, std::fabs(b)); }
Enemy enemy(std::uint64_t id, real x, real hp = 10000) { Enemy e; e.id = id; e.type = "slime"; e.x = x; e.hp = e.maxHp = hp; return e; }
GameState fixture(int color, const char* id = "p") {
  GameState s = createGameState("classic");
  Player p = createPlayer(id, "Arcanista", color);
  p.x = 0;
  s.players[id] = p;
  s.spawn = 999;
  return s;
}
bool sameStats(const Player& a, const Player& b) {
  return near(a.hp, b.hp) && near(a.maxHp, b.maxHp) && near(a.damage, b.damage) && near(a.speed, b.speed) &&
         near(a.armor, b.armor) && a.projectiles == b.projectiles && near(a.attackDelay, b.attackDelay);
}
} // namespace

int main() {
  FixedRandom rng;
  MetaRanks meta;
  meta.rank = {{"vigor", 3}, {"might", 2}, {"ward", 2}, {"stride", 2}, {"celerity", 2}};

  // Developer: 5x life, 4x damage, three shots; lobby swaps keep upgrades without stacking bonuses.
  {
    const Player base = createPlayer("p", "Mago", 0, &meta);
    Player p = createPlayer("p", "Mago", DEVELOPER, &meta);
    assert(near(p.maxHp, base.maxHp * 5) && near(p.damage, base.damage * 4) && p.projectiles == 3);
    assert(near(p.armor, base.armor + 12) && near(p.speed, base.speed * 1.35) && near(p.attackDelay, base.attackDelay * 0.5));
    for (int i = 0; i < 10; ++i) {
      selectPlayerCharacter(p, DEVELOPER);
      assert(near(p.maxHp, base.maxHp * 5));
      selectPlayerCharacter(p, 3);
      assert(sameStats(p, base) && p.color == 3);
      selectPlayerCharacter(p, DEVELOPER);
    }
  }

  // Aurora: stronger than the standard mages, below the Developer; any swap order lands on fresh stats.
  {
    const Player base = createPlayer("p", "Padrão", 0, &meta), dev = createPlayer("d", "Dev", DEVELOPER, &meta);
    Player p = createPlayer("p", "Aurora", AURORA, &meta);
    assert(near(p.maxHp, base.maxHp * 1.5) && p.maxHp < dev.maxHp);
    assert(p.damage > base.damage && p.damage < dev.damage && p.speed > base.speed && p.speed < dev.speed);
    assert(p.armor > base.armor && p.armor < dev.armor && p.projectiles == 1);
    assert(p.attackDelay < base.attackDelay && p.attackDelay > dev.attackDelay);
    for (int n = 0; n < 10; ++n)
      for (int color : {DEVELOPER, 3, AURORA, 0}) {
        selectPlayerCharacter(p, color);
        assert(sameStats(p, createPlayer("p", "", color, &meta)));
      }
    assert(createPlayer("x", "", 99).color == GOD && createPlayer("x", "", -3).color == 0);
  }

  // Código-fonte: three piercing bolts that slow and splash, never hitting the same enemy twice.
  {
    GameState s = fixture(DEVELOPER);
    Player& p = s.players.at("p");
    s.enemies.push_back(enemy(1, 300));
    s.enemies.push_back(enemy(2, 360));
    updateGame(s, 0.001, rng);
    assert(s.shots.size() == 3);
    for (const auto& shot : s.shots) assert(shot.color == DEVELOPER && shot.pierce == 6 && near(shot.damage, p.damage));
    p.attackCooldown = 999;
    bool slowed = false;
    for (int i = 0; i < 120; ++i) {
      updateGame(s, 1.0 / 60, rng);
      slowed |= s.enemies[0].slowFor > 0;
      for (const auto& shot : s.shots)
        for (std::size_t a = 0; a < shot.hitIds.size(); ++a)
          for (std::size_t b = a + 1; b < shot.hitIds.size(); ++b) assert(shot.hitIds[a] != shot.hitIds[b]);
    }
    assert(slowed && s.shots.empty());
    for (const auto& e : s.enemies) assert(e.hp < 10000);
  }

  // Reescrever realidade: 600-unit reach, erases shots, heals half, protects; works with full arrays.
  {
    GameState s = fixture(DEVELOPER, "dev");
    Player& p = s.players.at("dev");
    p.hp = 100; p.specialCharge = 100;
    s.enemies.push_back(enemy(1, 300));
    s.enemies.push_back(enemy(2, 800));
    s.enemyShots.push_back(EnemyShot{100, 0});
    s.enemyShots.push_back(EnemyShot{800, 0});
    while (s.shots.size() < cfg::MAX_SHOTS) s.shots.push_back(Shot{});
    while (s.zones.size() < cfg::MAX_ZONES) s.zones.push_back(Zone{});
    assert(activateSpecial(s, "dev", rng));
    assert(near(s.enemies[0].hp, 10000 - p.damage * 24) && s.enemies[1].hp == 10000);
    assert(s.enemyShots.size() == 1 && near(p.hp, 350) && near(p.invulnerableFor, 3) && p.specialCharge == 0);
    assert(near(p.stats.by.at("special"), p.damage * 24));
    p.specialCharge = 100;
    assert(!activateSpecial(s, "dev", rng));               // cooldown
    p.specialCooldown = 0; p.pendingPowers = {"arcane"};
    assert(!activateSpecial(s, "dev", rng));
    p.pendingPowers.clear(); s.phaseStatus = "transition";
    assert(!activateSpecial(s, "dev", rng));
  }

  // Restauração do sistema: heals and protects living allies nearby, never revives the fallen.
  {
    GameState s = fixture(DEVELOPER, "dev");
    Player& p = s.players.at("dev");
    p.specialVariant = 1; p.specialCharge = 100;
    struct Ally { const char* id; real x; bool alive; };
    for (const Ally a : {Ally{"near", 100, true}, Ally{"far", 800, true}, Ally{"down", 50, false}}) {
      Player ally = createPlayer(a.id, a.id);
      ally.x = a.x; ally.hp = a.alive ? 10 : 0; ally.alive = a.alive;
      s.players[a.id] = ally;
    }
    assert(activateSpecial(s, "dev", rng));
    assert(s.players.at("near").hp == 100 && near(s.players.at("near").invulnerableFor, 5));
    assert(s.players.at("far").hp == 10 && s.players.at("down").hp == 0);
  }

  // ...and removes every enemy on the map at that instant, with normal kills, damage and drops.
  {
    GameState s = fixture(DEVELOPER, "dev");
    Player& p = s.players.at("dev");
    p.specialVariant = 1; p.specialCharge = 100;
    s.enemies.push_back(enemy(1, 100));
    Enemy elite = enemy(2, 9000, 900000); elite.elite = true;
    s.enemies.push_back(elite);
    s.enemies.push_back(enemy(3, -12000));
    assert(activateSpecial(s, "dev", rng));
    for (std::size_t i = 0; i < 3; ++i) assert(s.enemies[i].hp <= 0);
    assert(p.stats.kills == 3 && near(p.stats.by.at("special"), 920000));
    bool chest = false, gem = false;
    for (const auto& d : s.gems) { chest |= d.type == "chest"; gem |= d.type == "gem"; }
    assert(chest && gem);
    // Slimes split on death: the slimelets spawned by the reset are not part of it.
    std::size_t slimelets = 0;
    for (const auto& e : s.enemies) slimelets += e.type == "slimelet" && e.hp > 0;
    assert(slimelets == 6);
    s.enemies.push_back(enemy(99, 0));
    p.attackCooldown = 999;
    updateGame(s, 0.016, rng);
    for (const auto& e : s.enemies) if (e.id == 99) assert(e.hp == 10000);
    assert(p.specialCharge < 100 && !activateSpecial(s, "dev", rng));
  }

  // ...bosses included, wherever they are: the phase still advances, and the last one wins the ritual.
  for (int phase : {0, 5}) {
    GameState s = fixture(DEVELOPER, "dev");
    Player& p = s.players.at("dev");
    s.phase = phase; s.phaseStatus = "boss";
    p.specialVariant = 1; p.specialCharge = 100;
    Enemy boss = enemy(1, 20000, 1e8); boss.type = phase == 0 ? "treant" : "umbra"; boss.boss = true;
    s.enemies.push_back(boss);
    assert(activateSpecial(s, "dev", rng) && s.enemies[0].hp == 0);
    updateGame(s, 0.016, rng);
    if (phase == 0) assert(s.phaseStatus == "transition" && !earnsAurora(s));
    else assert(s.over && s.victory && earnsAurora(s));
  }

  // The Developer's charge refills by itself (10/s), paused while choosing a power; the Aurora's does not.
  // (Steps of 0.05 s: updateGame caps a single step at 0.08 s.)
  {
    GameState s = fixture(DEVELOPER);
    Player& p = s.players.at("p");
    updateGame(s, 0.05, rng);
    assert(near(p.specialCharge, 0.5));
    p.pendingPowers = {"arcane"};
    updateGame(s, 0.05, rng);
    assert(near(p.specialCharge, 0.5));
    p.pendingPowers.clear(); p.specialCharge = 99.9;
    updateGame(s, 0.05, rng);
    assert(p.specialCharge == 100);
    GameState a = fixture(AURORA);
    updateGame(a, 0.05, rng);
    assert(a.players.at("p").specialCharge == 0 && a.players.at("p").maxHp == 150);
  }

  // Alvorada: finite reach and damage; Coroa da aurora needs Segundo feitiço and room for 12 spears.
  {
    GameState s = fixture(AURORA);
    Player& p = s.players.at("p");
    p.specialCharge = 100;
    s.enemies.push_back(enemy(1, 100));
    s.enemies.push_back(enemy(2, 400));
    s.enemyShots.push_back(EnemyShot{100, 0});
    s.enemyShots.push_back(EnemyShot{400, 0});
    assert(activateSpecial(s, "p", rng));
    assert(near(s.enemies[0].hp, 10000 - p.damage * 6) && s.enemies[1].hp == 10000);
    assert(s.enemyShots.size() == 1 && near(p.invulnerableFor, 1.5));
    const Loadout alternate{"", 1};
    MetaRanks none, second;
    second.rank["secondSpell"] = 1;
    assert(createPlayer("a", "", AURORA, &none, &alternate).specialVariant == 0);
    s.players["a"] = createPlayer("a", "", AURORA, &second, &alternate);
    Player& alt = s.players.at("a");
    alt.specialCharge = 100;
    while (s.shots.size() < cfg::MAX_SHOTS - 11) s.shots.push_back(Shot{});
    assert(!activateSpecial(s, "a", rng) && alt.specialCharge == 100);
    s.shots.clear();
    assert(activateSpecial(s, "a", rng) && s.shots.size() == 12);
    for (const auto& shot : s.shots) assert(shot.color == AURORA && shot.pierce == 2 && near(shot.damage, alt.damage * 3));
  }

  // Only a Classic victory over the sixth realm earns the Aurora; the unlock is recorded once.
  {
    const Player base = createPlayer("b", "Mago", 0);
    Player god = createPlayer("g", "The God", GOD);
    assert(near(god.maxHp, 500) && near(god.damage, base.damage * 1.5));
    assert(near(god.speed, base.speed * 1.2) && near(god.attackDelay, base.attackDelay * .85));
    assert(near(god.armor, 12) && god.projectiles == 1);
    selectPlayerCharacter(god, AURORA);
    selectPlayerCharacter(god, GOD);
    assert(sameStats(god, createPlayer("g", "The God", GOD)));

    GameState s = fixture(GOD);
    s.enemies.push_back(enemy(1, 66));
    updateGame(s, .05, rng);
    assert(s.enemies[0].hp < s.enemies[0].maxHp);
    assert(s.players.at("p").stats.by["orbit"] > 0);
    s.players.at("p").specialCharge = 100;
    assert(activateSpecial(s, "p", rng));
    const Loadout alt{"", 1}; MetaRanks second; second.rank["secondSpell"] = 1;
    s.players["a"] = createPlayer("a", "The God", GOD, &second, &alt);
    s.players["a"].specialCharge = 100;
    assert(activateSpecial(s, "a", rng));
    assert(s.shots.size() >= 12);
    for (const auto& shot : s.shots) if (shot.special && shot.color == GOD) assert(shot.pierce == 3);
  }

  {
    GameState s = fixture(AURORA);
    s.enemies.push_back(enemy(1, 100));
    s.enemies.push_back(enemy(2, 200));
    updateGame(s, .05, rng);
    assert(near(s.enemies[0].hp, 10000 - s.players.at("p").damage * 2.5));
    assert(near(s.enemies[1].hp, 10000));
    assert(!s.events.empty() && s.events.back().kind == "auroraRay");
    assert(s.events.back().points.size() == 2);
    updateGame(s, .05, rng);
    assert(near(s.enemies[0].hp, 10000 - s.players.at("p").damage * 2.5));
  }

  {
    GameState win = createGameState("classic");
    win.over = win.victory = true; win.phase = 5;
    assert(earnsAurora(win));
    for (int variant = 0; variant < 5; ++variant) {
      GameState s = win;
      if (variant == 0) s.over = false;
      if (variant == 1) s.victory = false;
      if (variant == 2) s.phase = 4;
      if (variant == 3) s.campaign = "quick";
      if (variant == 4) s.campaign = "endless";
      Profile p;
      assert(!earnsAurora(s) && !recordVictory(p, s) && !p.unlocks.aurora);
    }
    Profile p;
    assert(!characterAvailable(p.unlocks, AURORA) && !characterAvailable(p.unlocks, DEVELOPER) && characterAvailable(p.unlocks, 3));
    assert(recordVictory(p, win) && p.unlocks.aurora && !recordVictory(p, win));
    assert(characterAvailable(p.unlocks, AURORA) && !characterAvailable(p.unlocks, DEVELOPER));
    assert(!characterAvailable(p.unlocks, -1) && !characterAvailable(p.unlocks, CHARACTER_COUNT));
    // Both unlocks and the chosen characters survive a save round trip.
    p.unlocks.developer = true;
    p.coins = 60000;
    assert(!characterAvailable(p.unlocks, GOD));
    assert(buyGod(p) && p.unlocks.god && p.coins == 0 && !buyGod(p));
    assert(respec(p) == 0 && p.unlocks.god);
    p.prefs.characters = {AURORA, DEVELOPER, 2, 3};
    constexpr const char* path = "characters_test_profile.ini";
    assert(saveProfileAtomic(p, path));
    const Profile q = loadProfile(path);
    std::remove(path);
    assert(q.unlocks.developer && q.unlocks.aurora && q.unlocks.god && q.prefs.characters[0] == AURORA && q.prefs.characters[1] == DEVELOPER);
  }

  // The daily challenge keeps drawing only the four standard mages.
  for (int day = 1; day <= 28; ++day) {
    char key[16];
    std::snprintf(key, sizeof key, "2026-02-%02d", day);
    assert(dailyChallenge(key).character < STANDARD_CHARACTERS);
  }

  std::cout << "arcana_characters_tests: OK\n";
}
