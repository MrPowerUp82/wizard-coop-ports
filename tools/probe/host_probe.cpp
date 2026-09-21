// Host reference for the PSP CPU probe: same scenarios as platforms/psp/probe.
#include "probe_scenarios.hpp"

#include <chrono>
#include <cstdio>

int main() {
  const auto start = std::chrono::steady_clock::now();
  auto now = [start] {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  };
  std::printf("Arcana CPU probe (host)\nsizeof(GameState)=%zu\n", sizeof(arcana::GameState));
  arcana::probe::runAll(now, [](const arcana::probe::Result& r) {
    char line[160];
    arcana::probe::format(line, sizeof line, r);
    std::printf("%s\n", line);
  });
}
