#pragma once

#include "animator.hpp"
#include "batch_renderer.hpp"
#include "arcana/game.hpp"

namespace arcana::sdl {

struct WorldView {
  float width{}, height{};  // screen size in pixels
  float camX{}, camY{};     // world point at the centre of the screen
  float zoom{1};            // screen pixels per world unit
};

// Port of the web client's renderWorld() (src/render.js) + drawEffects/drawNumbers
// (src/animation.js): floor, atmosphere, zones, pickups, projectiles with trails, animated
// creatures, weapons, every effect, damage numbers, screen flash and off-screen arrows.
// Blending switches (normal <-> additive) are grouped into a few passes per frame.
void drawWorld(BatchRenderer& b, const GameState& game, const Animator& anim, const WorldView& view);

// Element colour of a character (the web client's SPELLS[i].tint).
std::uint32_t elementColor(int color, std::uint8_t alpha = 255);

} // namespace arcana::sdl
