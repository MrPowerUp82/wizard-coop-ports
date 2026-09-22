// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fastmath.hpp"
#include "frontend.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace arcana;
using namespace arcana::sdl;

namespace {
struct FakeOnline final : OnlinePort {
  std::unique_ptr<GameState> view = std::make_unique<GameState>();
  std::string id{"me"};
  bool isClosed{};
  MenuResult next{MenuResult::Stay};
  std::vector<std::string> calls;
  Vec2 lastInput{};
  void openMenu(const Profile&) override { calls.push_back("open"); }
  MenuResult updateMenu(std::uint32_t, std::uint32_t, const TextInput&, Profile&) override { return std::exchange(next, MenuResult::Stay); }
  void renderMenu(BatchRenderer&, float, float, float) override {}
  bool wantsText() const override { return false; }
  bool frame(double, Vec2 input, GameState& out) override { lastInput = input; out = *view; return true; }
  const std::string& localId() const override { return id; }
  bool reconnecting() const override { return false; }
  bool closed() const override { return isClosed; }
  std::optional<std::string> takeNotice() override { return std::nullopt; }
  void choosePower(const std::string& p) override { calls.push_back("choose:" + p); }
  void reroll() override { calls.push_back("reroll"); }
  void special() override { calls.push_back("special"); }
  void dash(Vec2) override { calls.push_back("dash"); }
  void signal(const char* kind, std::optional<Vec2>) override { calls.push_back(std::string("signal:") + kind); }
  void leave() override { calls.push_back("leave"); }
  [[nodiscard]] int count(const std::string& call) const { return static_cast<int>(std::count(calls.begin(), calls.end(), call)); }
};
// One frame with `held` on player 1 (edge-triggered like tap(), but with any combination held).
void hold(Frontend& f, std::uint32_t held, float x = 0, float y = 0) {
  InputFrame in;
  for (auto& pad : in.pads) pad.connected = true;
  in.pads[0].held = held; in.pads[0].x = x; in.pads[0].y = y;
  f.update(1.0 / 60, in);
}
// One frame with `action` held on `slot`, then one frame released (edge-triggered input).
void tap(Frontend& f, int slot, std::uint32_t action) {
  InputFrame in;
  for (auto& pad : in.pads) pad.connected = true;
  in.pads[static_cast<std::size_t>(slot)].held = action;
  f.update(1.0 / 60, in);
  in.pads[static_cast<std::size_t>(slot)].held = 0;
  f.update(1.0 / 60, in);
}
int colorOf(const Frontend& f, const char* id) { return f.state().players.at(id).color; }
} // namespace

int main() {
  // Visual trig: within 2e-3 of libm over several turns (renderer only, never gameplay).
  for (float a = -20.0f; a < 20.0f; a += 0.0137f) {
    float fs, fc;
    fastSinCos(a, fs, fc);
    assert(std::fabs(fs - std::sin(a)) < 2e-3f && std::fabs(fc - std::cos(a)) < 2e-3f);
  }
  // Solo: P1 picks Vermelho (1) with one press to the right.
  {
    auto f = std::make_unique<Frontend>();
    tap(*f, 0, ActRight);
    tap(*f, 0, ActConfirm);
    assert(f->inGame());
    assert(colorOf(*f, "p1") == 1);
  }
  // Co-op: players never share a character, even when they try to.
  {
    auto f = std::make_unique<Frontend>();
    tap(*f, 1, ActConfirm);           // P2 joins with its default (Vermelho)
    tap(*f, 0, ActRight);             // P1 Azul -> skips Vermelho (taken) -> Verde
    tap(*f, 2, ActConfirm);           // P3 default Verde is taken -> next free: Roxo
    tap(*f, 1, ActLeft);              // P2 Vermelho -> Azul (free since P1 left it)
    tap(*f, 0, ActConfirm);           // start
    assert(f->inGame());
    assert(colorOf(*f, "p1") == 2);
    assert(colorOf(*f, "p2") == 0);
    assert(colorOf(*f, "p3") == 3);
    assert(f->state().players.size() == 3);
  }
  // Online: the title gets "Jogar online" right after "Jogar"; the match runs on the server's view.
  {
    auto f = std::make_unique<Frontend>();
    auto owned = std::make_unique<FakeOnline>();
    FakeOnline& online = *owned;
    f->setOnline(std::move(owned));
    Player me = createPlayer("me", "Ana", 1), ally = createPlayer("ally", "Beto", 2);
    online.view->players["me"] = me;
    online.view->players["ally"] = ally;
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    assert(online.count("open") == 1 && !f->inGame());
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);
    assert(f->inGame());
    hold(*f, 0, 1, 0);
    assert(f->state().players.size() == 2 && online.lastInput.x == 1);
    tap(*f, 0, ActSpecial);
    assert(online.count("special") == 1);
    tap(*f, 0, ActSignalHelp);
    assert(online.count("signal:help") == 1);
    hold(*f, ActAlt);                                  // gamepad: hold X/Y, then a direction
    hold(*f, ActAlt | ActUp);
    assert(online.count("signal:here") == 1);
    hold(*f, 0);
    // Power choice goes to the server once per offer.
    online.view->players["me"].pendingPowers = {"arcane", "haste"};
    hold(*f, 0);
    tap(*f, 0, ActConfirm);
    tap(*f, 0, ActConfirm);
    assert(online.count("choose:arcane") == 1);
    online.view->players["me"].pendingPowers.clear();
    // Esc opens the online menu over the running match; "Sair da sala" leaves.
    tap(*f, 0, ActPause);
    assert(f->inGame());
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    assert(!f->inGame() && online.count("leave") == 1);
    // A dropped session ends the match too.
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);
    assert(f->inGame());
    online.isClosed = true;
    hold(*f, 0);
    assert(!f->inGame() && online.count("leave") == 2);
  }
  // Regression: resetRunView() must reset feedbackEventId_, not just feedbackPrimed_. Otherwise
  // the watermark from one online match (event ids can be large) survives into the next one
  // (which restarts its own ids from a low number), and observeEvents() silently skips every
  // announcement/sound of the new match because `e.id <= feedbackEventId_`.
  {
    auto f = std::make_unique<Frontend>();
    auto owned = std::make_unique<FakeOnline>();
    FakeOnline& online = *owned;
    f->setOnline(std::move(owned));
    Player me = createPlayer("me", "Ana", 1);
    online.view->players["me"] = me;
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);                                          // transition frame: Online -> Playing
    assert(f->inGame());
    hold(*f, 0);                                          // first Playing frame: primes the watermark on an empty event set
    // First match: a high-id event is announced normally.
    Event chest; chest.id = 800; chest.kind = "chest";
    online.view->events.push_back(chest);
    hold(*f, 0);
    assert(f->toastTextForTests().find("Baú compartilhado") != std::string::npos);
    online.view->events.clear();
    // Leave the match (Esc, down to "Sair da sala", confirm).
    tap(*f, 0, ActPause);
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    assert(!f->inGame());
    // Second match: its view starts a fresh event stream at a low id, distinct from "chest".
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);                                          // transition frame: Online -> Playing (resetRunView() runs here)
    assert(f->inGame());
    hold(*f, 0);                                          // priming frame: feedbackEventId_ must be back at 0 here, not stuck at 800
    Event boss; boss.id = 1; boss.kind = "boss";
    online.view->events.push_back(boss);
    hold(*f, 0);
    assert(f->announceTextForTests().find("despertou") != std::string::npos);
  }
  // Without an online port the title is unchanged (first item still starts a local run).
  {
    auto f = std::make_unique<Frontend>();
    tap(*f, 0, ActConfirm);
    assert(f->inGame());
  }
  return 0;
}
