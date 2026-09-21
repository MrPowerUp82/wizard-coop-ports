#pragma once

#include "arcana/game.hpp"
#include "arcana/native/fixed_step.hpp"
#include "arcana/native/render_queue.hpp"
#include "animator.hpp"
#include "batch_renderer.hpp"
#include "world_renderer.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace arcana::sdl {

// Platform-neutral input: each platform main fills one PadInput per local player every frame.
// The frontend never touches SDL/libnx input APIs, so PC, Switch and Vita share all game flow.
enum Action : std::uint32_t {
  ActSpecial = 1u << 0, ActDash = 1u << 1, ActPause = 1u << 2, ActConfirm = 1u << 3, ActCancel = 1u << 4,
  ActAlt = 1u << 5, ActUp = 1u << 6, ActDown = 1u << 7, ActLeft = 1u << 8, ActRight = 1u << 9, ActDebug = 1u << 10,
};

struct PadInput {
  bool connected{};
  float x{}, y{};        // movement, already dead-zoned, length <= 1, y down
  std::uint32_t held{};  // Action bits
};

struct InputFrame {
  std::array<PadInput, cfg::MAX_PLAYERS> pads{};
};

struct FrontendOptions {
  bool autoplay{};      // bots drive every joined slot (benchmarks, screenshots, soak tests)
  bool showPerf{};
  int autoplayPlayers{1};
  std::string campaign{"quick"};
  bool startImmediately{};
  bool debugCharge{};   // autoplay: specials always charged (effects soak test)
};

struct FrameTimings { double updateMs{}, buildMs{}, frameMs{}; int steps{}; };

class Frontend {
public:
  explicit Frontend(FrontendOptions options = {});

  // Advances menus/simulation. Returns false when the player asked to quit from the title screen.
  bool update(double frameSeconds, const InputFrame& input);
  void render(BatchRenderer& batch, float width, float height);

  // CPU time of the whole previous frame (update + render build), for the perf overlay.
  void setFrameMs(double ms) { timings_.frameMs = ms; }
  [[nodiscard]] const GameState& state() const { return state_; }
  [[nodiscard]] bool inGame() const { return screen_ == Screen::Playing || screen_ == Screen::Paused; }
  [[nodiscard]] double buildMs() const { return buildMs_; }

private:
  enum class Screen { Title, Playing, Paused, Over };

  struct PadEdges { std::uint32_t pressed{}, held{}; };

  void readEdges(const InputFrame& input);
  bool pressed(int slot, Action a) const { return (edges_[static_cast<std::size_t>(slot)].pressed & a) != 0; }
  bool anyPressed(Action a) const;

  void startRun();
  void updateTitle();
  void updatePlaying(double frameSeconds, const InputFrame& input);
  void updatePaused();
  void updateOver();
  void updateChooser(Player& chooser, int slot);
  void leashPlayers();
  void autoplayInput(InputFrame& input);

  Player* playerForSlot(int slot);
  Player* chooser(int* slotOut = nullptr);

  void renderWorld(BatchRenderer& batch, float width, float height);
  void renderFeedback(BatchRenderer& batch, float width, float height);
  void observeEvents();
  void announce(std::string text, std::uint32_t color, bool toast = false);
  void renderHud(BatchRenderer& batch, float width, float height);
  void renderChooser(BatchRenderer& batch, float width, float height);
  void renderTitle(BatchRenderer& batch, float width, float height);
  void renderPause(BatchRenderer& batch, float width, float height);
  void renderOver(BatchRenderer& batch, float width, float height);
  void renderPerf(BatchRenderer& batch, float width, float height);

  FrontendOptions options_;
  Screen screen_{Screen::Title};
  GameState state_{};
  DefaultRandom random_{};
  native::FixedStep clock_{60.0, 4};
  Animator anim_{};
  struct Banner { std::string text; std::uint32_t color{}; double age{99}, life{2.8}; };
  Banner announce_, toast_;
  std::uint64_t feedbackEventId_{};
  bool feedbackPrimed_{};
  double hurtFlash_{};
  std::array<double, cfg::MAX_PLAYERS> lastHp_{};
  native::Camera camera_{};
  std::array<PadEdges, cfg::MAX_PLAYERS> edges_{};
  std::array<bool, cfg::MAX_PLAYERS> joined_{true, false, false, false};
  int menuIndex_{0}, pauseIndex_{0}, choiceIndex_{0};
  std::string choiceKey_;
  double menuTime_{}, overTime_{};
  double buildMs_{};
  FrameTimings timings_{};
  BatchRenderer::Stats lastBatch_{};
  double fpsAccum_{}; int fpsFrames_{}; double fps_{};
  char scratch_[160]{};
};

} // namespace arcana::sdl
