// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fastmath.hpp"
#include "frontend.hpp"

#include <cassert>
#include <cmath>
#include <memory>

using namespace arcana;
using namespace arcana::sdl;

namespace {
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
// Straight to the title screen, as the tests below expect.
std::unique_ptr<Frontend> titleScreen() {
  FrontendOptions options;
  options.splash = false;
  return std::make_unique<Frontend>(options);
}
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
    auto f = titleScreen();
    tap(*f, 0, ActRight);
    tap(*f, 0, ActConfirm);
    assert(f->inGame());
    assert(colorOf(*f, "p1") == 1);
  }
  // Co-op: players never share a character, even when they try to.
  {
    auto f = titleScreen();
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
  // Splash: a button skips it without also confirming the title's first item.
  {
    auto f = std::make_unique<Frontend>();
    assert(!f->onTitle());
    tap(*f, 0, ActConfirm);
    assert(f->onTitle() && !f->inGame());
  }
  // Splash: ends on its own after a couple of seconds.
  {
    auto f = std::make_unique<Frontend>();
    for (int i = 0; i < 60 && !f->onTitle(); ++i) f->update(1.0 / 60, InputFrame{});
    assert(!f->onTitle());
    for (int i = 0; i < 240 && !f->onTitle(); ++i) f->update(1.0 / 60, InputFrame{});
    assert(f->onTitle());
  }
  // Credits: second-to-last title item; any button returns to the title.
  {
    auto f = titleScreen();
    tap(*f, 0, ActUp);                // Play -> Quit (wraps)
    tap(*f, 0, ActUp);                // Quit -> Credits
    tap(*f, 0, ActConfirm);
    assert(!f->onTitle() && !f->inGame());
    tap(*f, 0, ActCancel);
    assert(f->onTitle());
  }
  return 0;
}
