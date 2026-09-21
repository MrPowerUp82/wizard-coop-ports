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
  return 0;
}
