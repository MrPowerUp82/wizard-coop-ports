#include "arcana/game.hpp"
#include "arcana/native/fixed_step.hpp"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float axis(int value) {
  const float v = (static_cast<float>(value) / 32767.0f);
  return std::abs(v) < 0.16f ? 0.0f : std::clamp(v, -1.0f, 1.0f);
}
void normalize(double& x, double& y) {
  const double len = std::sqrt(x*x + y*y);
  if (len > 1.0) { x /= len; y /= len; }
}
}

int main(int, char**) {
  consoleInit(nullptr);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad{};
  padInitializeDefault(&pad);

  // Large fixed-capacity state is static so it does not consume the main thread stack.
  static arcana::GameState game = arcana::createGameState("classic");
  game.players.emplace("switch", arcana::createPlayer("switch", "Switch", 0));
  auto& player = game.players.at("switch");
  arcana::SeededRandom rng(0xA7CA4A11u);
  arcana::native::FixedStep fixed(60.0, 4);

  u64 lastTick = armGetSystemTick();
  u64 profileStart = lastTick;
  u64 updateNs = 0;
  unsigned profileFrames = 0;
  unsigned simSteps = 0;

  while (appletMainLoop()) {
    padUpdate(&pad);
    const u64 down = padGetButtonsDown(&pad);
    if (down & HidNpadButton_Plus) break;

    const HidAnalogStickState stick = padGetStickPos(&pad, 0);
    player.input.x = axis(stick.x);
    player.input.y = -axis(stick.y);
    normalize(player.input.x, player.input.y);

    if (!player.pendingPowers.empty()) {
      int choice = -1;
      if (down & HidNpadButton_A) choice = 0;
      else if (down & HidNpadButton_X) choice = 1;
      else if (down & HidNpadButton_Y) choice = 2;
      if (choice >= 0 && choice < static_cast<int>(player.pendingPowers.size()))
        arcana::applyPower(player, player.pendingPowers[static_cast<std::size_t>(choice)]);
    } else {
      if (down & HidNpadButton_B) arcana::activateDash(game, player.id, {player.input.x, player.input.y});
      if (down & HidNpadButton_A) arcana::activateSpecial(game, player.id, rng);
    }

    const u64 now = armGetSystemTick();
    const double elapsed = static_cast<double>(armTicksToNs(now - lastTick)) / 1'000'000'000.0;
    lastTick = now;

    const u64 updateStart = armGetSystemTick();
    simSteps += static_cast<unsigned>(fixed.advance(elapsed, [&](double dt) { arcana::updateGame(game, dt, rng); }));
    updateNs += armTicksToNs(armGetSystemTick() - updateStart);
    ++profileFrames;

    if (armTicksToNs(now - profileStart) >= 1'000'000'000ULL) {
      const double avgUpdateUs = profileFrames ? static_cast<double>(updateNs) / profileFrames / 1000.0 : 0.0;
      std::printf("\x1b[2J\x1b[H");
      std::printf("Arcana Survivors - native C++ Switch probe\n\n");
      std::printf("Simulation: 60 Hz fixed step\n");
      std::printf("Avg core update: %.2f us/frame\n", avgUpdateUs);
      std::printf("Render frames: %u  sim steps: %u\n", profileFrames, simSteps);
      std::printf("Enemies: %zu / %d\n", game.enemies.size(), arcana::cfg::MAX_ENEMIES);
      std::printf("Shots: %zu / %d\n", game.shots.size(), arcana::cfg::MAX_SHOTS);
      std::printf("Enemy shots: %zu / %d\n", game.enemyShots.size(), arcana::cfg::MAX_ENEMY_SHOTS);
      std::printf("Drops: %zu / %d\n", game.gems.size(), arcana::cfg::MAX_DROPS);
      std::printf("Game time: %.1f s  phase: %d\n", game.time, game.phase + 1);
      std::printf("\nStick: move | B: dash | A: special | +: exit\n");
      std::printf("Power choice: A / X / Y\n");
      consoleUpdate(nullptr);
      profileStart = now;
      updateNs = 0;
      profileFrames = 0;
      simSteps = 0;
    }
  }

  consoleExit(nullptr);
  return 0;
}
