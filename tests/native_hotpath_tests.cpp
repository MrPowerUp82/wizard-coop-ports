#include "arcana/game.hpp"
#include "arcana/native/render_queue.hpp"
#include "arcana/native/spatial_grid.hpp"
#include "arcana/native/static_vector.hpp"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>

namespace {
std::atomic<unsigned long long> allocations{0};
}

void* operator new(std::size_t n) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

struct DeterministicRandom final : arcana::Random {
  double next() override { return 0.41; }
};

int main() {
  using namespace arcana;

  native::StaticVector<int, 4> ints{1, 2, 3};
  assert(ints.capacity() == 4 && ints.size() == 3);
  assert(ints.push_back(4));
  assert(!ints.push_back(5));

  native::StaticVector<Enemy, cfg::MAX_ENEMIES> enemies;
  for (int i = 0; i < 20; ++i) {
    Enemy e;
    e.id = static_cast<std::uint64_t>(i + 1);
    e.x = i * 10.0;
    e.y = 0;
    enemies.push_back(std::move(e));
  }
  native::SpatialGrid<Enemy, cfg::MAX_ENEMIES> grid(64.0f);
  grid.rebuild(enemies);
  int near = 0;
  grid.query(0, 0, 31, [&](Enemy&, float) { ++near; return false; });
  assert(near == 4);

  DeterministicRandom rng;
  static GameState game = createGameState("classic");
  game.players.emplace("hotpath", createPlayer("hotpath", "HotPath", 0));
  auto& p = game.players.at("hotpath");
  // Keep the player in a power-choice pause so the test exercises spawning,
  // enemy movement/separation and rendering without level-up UI allocations.
  p.pendingPowers = {"arcane"};

  static native::RenderQueue queue;
  native::Camera camera{};
  for (int i = 0; i < 600; ++i) {
    updateGame(game, 1.0 / 60.0, rng);
    camera.x = static_cast<float>(p.x);
    camera.y = static_cast<float>(p.y);
    native::buildRenderQueue(game, camera, queue);
  }

  const auto before = allocations.load(std::memory_order_relaxed);
  for (int i = 0; i < 600; ++i) {
    updateGame(game, 1.0 / 60.0, rng);
    native::buildRenderQueue(game, camera, queue);
  }
  const auto after = allocations.load(std::memory_order_relaxed);
  assert(after == before && "native hot path performed a heap allocation after warm-up");
  return 0;
}
