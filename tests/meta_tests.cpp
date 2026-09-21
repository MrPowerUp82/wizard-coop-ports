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
    auto f = std::make_unique<Frontend>();
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
    for (int i = 0; i < 15; ++i) tap(*f, 0, ActDown); // last row: respec
    tap(*f, 0, ActConfirm);
    assert(rank(f->profile(), "vigor") == 1);    // armed only
    tap(*f, 0, ActConfirm);
    assert(rank(f->profile(), "vigor") == 0 && f->profile().coins == 237 && f->profile().invested == 0);
    assert(loadProfile(kPath).coins == 237);
  }

  // Preferences come back on the next launch.
  {
    auto f = std::make_unique<Frontend>();
    f->setProfilePath(kPath);
    tap(*f, 0, ActConfirm); // Play
    assert(f->inGame());
    assert(f->state().players.at("p1").color == 0);
  }
  std::remove(kPath);
  return 0;
}
