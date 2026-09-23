// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "frontend.hpp"

#include <cassert>
#include <cstdio>
#include <memory>

using namespace arcana;
using namespace arcana::sdl;

namespace {
void tap(Frontend& f, int slot, std::uint32_t action) {
  InputFrame in;
  for (auto& pad : in.pads) pad.connected = true;
  in.pads[static_cast<std::size_t>(slot)].held = action;
  f.update(1.0 / 60, in);
  in.pads[static_cast<std::size_t>(slot)].held = 0;
  f.update(1.0 / 60, in);
}
int rank(const Profile& p, const char* id) { const auto it = p.upgrades.rank.find(id); return it == p.upgrades.rank.end() ? 0 : it->second; }
constexpr const char* kPath = "meta_test_profile.ini";
std::unique_ptr<Frontend> titleScreen() {
  FrontendOptions options;
  options.splash = false;
  return std::make_unique<Frontend>(options);
}
} // namespace

int main() {
  std::remove(kPath);

  // Profile round trip, preferences included.
  {
    Profile p;
    p.coins = 1234; p.invested = 40; p.upgrades.rank["vigor"] = 1;
    p.prefs.characters = {3, 1, 0, 2}; p.prefs.campaign = "classic"; p.prefs.weapon = "aura"; p.prefs.special = 1; p.prefs.muted = true;
    assert(saveProfileAtomic(p, kPath));
    const Profile q = loadProfile(kPath);
    assert(q.coins == 1234 && q.invested == 40 && rank(q, "vigor") == 1);
    assert(q.prefs.characters[0] == 3 && q.prefs.characters[3] == 2);
    assert(q.prefs.campaign == "classic" && q.prefs.weapon == "aura" && q.prefs.special == 1 && q.prefs.muted);
    std::remove(kPath);
  }

  // Shop: buy Vigor with the coins in the save, and it persists.
  {
    Profile seed; seed.coins = 200; saveProfileAtomic(seed, kPath);
    auto f = titleScreen();
    f->setProfilePath(kPath);
    tap(*f, 0, ActDown); tap(*f, 0, ActDown); // Play, Ritual, [Grimório]
    tap(*f, 0, ActConfirm);                   // open the shop
    tap(*f, 0, ActConfirm);                   // buy Vigor (40)
    assert(f->profile().coins == 160 && rank(f->profile(), "vigor") == 1);
    assert(loadProfile(kPath).coins == 160);
    // Endless is not bought: cycling the ritual never lands on it.
    tap(*f, 0, ActCancel);                    // back to the title
    tap(*f, 0, ActUp);                        // Ritual
    for (int i = 0; i < 4; ++i) tap(*f, 0, ActConfirm);
    tap(*f, 0, ActUp);                        // Play
    tap(*f, 0, ActConfirm);
    assert(f->inGame());
    assert(f->state().campaign != "endless");
    // Vigor rank 1 = +6 max HP.
    assert(f->state().players.at("p1").maxHp == 106);
    // Leaving the run banks its coins.
    f->stateForTests().players.at("p1").coins = 37;
    tap(*f, 0, ActPause);
    tap(*f, 0, ActDown); tap(*f, 0, ActDown); tap(*f, 0, ActDown); // Menu principal
    tap(*f, 0, ActConfirm);
    assert(!f->inGame());
    assert(f->profile().coins == 197 && loadProfile(kPath).coins == 197);
    // Respec needs two presses and refunds everything.
    tap(*f, 0, ActDown); tap(*f, 0, ActDown); tap(*f, 0, ActConfirm); // Play -> Ritual -> Grimório
    for (int i = 0; i < 16; ++i) tap(*f, 0, ActDown); // last row: respec
    tap(*f, 0, ActConfirm);
    assert(rank(f->profile(), "vigor") == 1);    // armed only
    tap(*f, 0, ActConfirm);
    assert(rank(f->profile(), "vigor") == 0 && f->profile().coins == 237 && f->profile().invested == 0);
    assert(loadProfile(kPath).coins == 237);
  }

  // Preferences come back on the next launch.
  {
    auto f = titleScreen();
    f->setProfilePath(kPath);
    tap(*f, 0, ActConfirm); // Play
    assert(f->inGame());
    assert(f->state().players.at("p1").color == 0);
  }
  // Online preferences survive a save/load round trip.
  {
    Profile p;
    p.prefs.name = "Ana Lú";
    p.prefs.server = "wss://example.com/ws";
    assert(saveProfileAtomic(p, kPath));
    const Profile q = loadProfile(kPath);
    assert(q.prefs.name == "Ana Lú");
    assert(q.prefs.server == "wss://example.com/ws");
  }

  // The Developer: seven Alt presses on the title (src/menu.js #secretTitle), remembered in the save.
  {
    std::remove(kPath);
    auto f = titleScreen();
    f->setProfilePath(kPath);
    for (int i = 0; i < 6; ++i) tap(*f, 0, ActAlt);
    assert(!f->profile().unlocks.developer);
    assert(f->announceTextForTests().find("presença") != std::string::npos);
    tap(*f, 0, ActAlt);
    assert(f->profile().unlocks.developer && loadProfile(kPath).unlocks.developer);
    assert(f->announceTextForTests().find("Desenvolvedor") != std::string::npos);
    tap(*f, 0, ActConfirm);                   // Play, as the Developer
    assert(f->state().players.at("p1").color == DEVELOPER && f->state().players.at("p1").maxHp == 500);
  }
  // Next launch: the Developer is in the cycle; the Aurora Guardian is skipped while locked.
  {
    auto f = titleScreen();
    f->setProfilePath(kPath);                 // saved pick: the Developer
    tap(*f, 0, ActRight);                     // Developer -> (Aurora locked) -> Azul
    tap(*f, 0, ActLeft);                      // Azul -> (Aurora locked) -> Developer
    tap(*f, 0, ActLeft);                      // Developer -> Roxo
    tap(*f, 0, ActConfirm);
    assert(f->state().players.at("p1").color == 3);
  }
  // The Aurora Guardian: only a Classic victory unlocks it, and it joins the cycle right away.
  for (const char* campaign : {"quick", "classic"}) {
    Profile seed = loadProfile(kPath);
    seed.prefs.campaign = campaign;
    assert(saveProfileAtomic(seed, kPath));
    auto f = titleScreen();
    f->setProfilePath(kPath);
    tap(*f, 0, ActConfirm);                   // Play
    GameState& s = f->stateForTests();
    assert(s.campaign == campaign);
    s.phase = 5; s.phaseStatus = "boss"; s.spawn = 999;
    Enemy boss; boss.id = 1; boss.type = "umbra"; boss.boss = true; boss.hp = 0; boss.maxHp = 1;
    s.enemies.clear();
    s.enemies.push_back(boss);
    for (int i = 0; i < 120 && f->inGame(); ++i) tap(*f, 0, 0);
    assert(!f->inGame() && f->state().victory);   // results screen
    const bool classic = std::string(campaign) == "classic";
    assert(f->profile().unlocks.aurora == classic && loadProfile(kPath).unlocks.aurora == classic);
    if (!classic) continue;
    tap(*f, 0, ActCancel);                    // results -> title (P1 still on Roxo)
    tap(*f, 0, ActRight);                     // Roxo -> Developer
    tap(*f, 0, ActRight);                     // Developer -> Aurora Guardian
    tap(*f, 0, ActConfirm);
    assert(f->state().players.at("p1").color == AURORA && f->state().players.at("p1").maxHp == 150);
  }
  // A hand-edited save cannot pick locked characters: each slot falls back to its default mage.
  {
    Profile p;
    p.prefs.characters = {AURORA, DEVELOPER, 2, 3};
    assert(saveProfileAtomic(p, kPath));
    auto f = titleScreen();
    f->setProfilePath(kPath);
    tap(*f, 1, ActConfirm);                   // P2 joins
    tap(*f, 0, ActConfirm);
    assert(f->state().players.at("p1").color == 0 && f->state().players.at("p2").color == 1);
  }
  std::remove(kPath);
  return 0;
}
