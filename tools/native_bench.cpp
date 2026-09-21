#include "arcana/game.hpp"
#include <chrono>
#include <iostream>

using Clock = std::chrono::steady_clock;
struct R final : arcana::Random { double next() override { return 0.5; } };

template <class Setup>
double runScenario(Setup&& setup, int ticks = 600) {
  R rng;
  auto game = arcana::createGameState("classic");
  game.players.emplace("bench", arcana::createPlayer("bench", "Bench", 0));
  game.phaseStatus = "horde";
  game.spawn = 1e9;
  game.players.at("bench").pendingPowers = {"arcane"};
  setup(game);
  const auto start = Clock::now();
  for (int i = 0; i < ticks; ++i) arcana::updateGame(game, 1.0 / 60.0, rng);
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
  return static_cast<double>(us) / ticks;
}

int main() {
  const double crowded = runScenario([](arcana::GameState& s) {
    for (int i = 0; i < arcana::cfg::MAX_ENEMIES; ++i) {
      arcana::Enemy e;
      e.id = static_cast<std::uint64_t>(i + 1);
      e.type = "slime";
      e.x = (i % 15) * 10;
      e.y = (i / 15) * 10;
      e.hp = e.maxHp = 1e12;
      s.enemies.push_back(std::move(e));
    }
  });

  const double projectileWorstCase = runScenario([](arcana::GameState& s) {
    for (int i = 0; i < arcana::cfg::MAX_ENEMIES; ++i) {
      arcana::Enemy e;
      e.id = static_cast<std::uint64_t>(i + 1);
      e.type = "slime";
      e.x = (i % 15) * 40;
      e.y = (i / 15) * 40;
      e.hp = e.maxHp = 1e12;
      s.enemies.push_back(std::move(e));
    }
    for (int i = 0; i < arcana::cfg::MAX_SHOTS; ++i) {
      arcana::Shot shot;
      shot.x = 10000 + (i % 20) * 5;
      shot.y = 10000 + (i / 20) * 5;
      shot.ttl = 100;
      shot.damage = 1;
      shot.color = 0;
      shot.pierce = 1;
      shot.owner = "bench";
      s.shots.push_back(std::move(shot));
    }
  });

  std::cout << "native_bench (lower is better)\n"
            << "180-enemy crowded tick: " << crowded << " us\n"
            << "180 enemies + 320 distant shots: " << projectileWorstCase << " us\n";
}
