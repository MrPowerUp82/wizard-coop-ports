#pragma once

#include "arcana/game.hpp"
#include "arcana/profile.hpp"
#include "arcana/native/fixed_step.hpp"
#include "arcana/native/render_queue.hpp"
#include "animator.hpp"
#include "audio.hpp"
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

// Button names in the on-screen hints. Each platform main sets its own; the desktop switches
// between keyboard and gamepad names as the player changes device.
struct ButtonLabels {
  const char* confirm{"A"};
  const char* cancel{"B"};
  const char* alt{"X/Y"};
  const char* join{"A"};   // how players 2-4 join and leave: gamepad buttons even when P1 is on the keyboard
  const char* leave{"B"};
};

struct FrontendOptions {
  bool autoplay{};      // bots drive every joined slot (benchmarks, screenshots, soak tests)
  bool showPerf{};
  int autoplayPlayers{1};
  std::string campaign{"quick"};
  bool startImmediately{};
  bool compact{};      // small single-player screen (PSP 480x272): bigger UI, reflowed layouts
  bool openShop{};     // dev/screenshots: start on the Grimório
  bool debugCharge{};   // autoplay: specials always charged (effects soak test)
  bool splash{true};    // developer seal before the title (never with autoplay/startImmediately/openShop)
  bool openCredits{};   // dev/screenshots: start on the credits screen
  ButtonLabels buttons{};
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
  // Optional: without it the game is silent.
  void setAudio(Audio* audio);
  // Where the Grimório (coins, upgrades) and menu choices persist. Without it nothing is saved.
  void setProfilePath(std::string path);
  void setButtons(const ButtonLabels& buttons) { options_.buttons = buttons; }
  [[nodiscard]] const Profile& profile() const { return profile_; }
  [[nodiscard]] const GameState& state() const { return state_; }
  GameState& stateForTests() { return state_; }
  [[nodiscard]] bool inGame() const { return screen_ == Screen::Playing || screen_ == Screen::Paused; }
  [[nodiscard]] bool onTitle() const { return screen_ == Screen::Title; }
  [[nodiscard]] double buildMs() const { return buildMs_; }

private:
  enum class Screen { Splash, Title, Credits, Playing, Paused, Over, Shop };
  enum class TitleItem : std::uint8_t { Play, Campaign, Weapon, Special, Shop, Credits, Quit };

  struct PadEdges { std::uint32_t pressed{}, held{}; };

  void readEdges(const InputFrame& input);
  bool pressed(int slot, Action a) const { return (edges_[static_cast<std::size_t>(slot)].pressed & a) != 0; }
  bool anyPressed(Action a) const;

  void startRun();
  void updateSplash(double frameSeconds);
  void updateTitle();
  float uiScale(float height) const { return height / 720.0f * (options_.compact ? 1.7f : 1.0f); }
  void updateShop();
  void renderShop(BatchRenderer& batch, float width, float height);
  native::StaticVector<TitleItem, 7> titleItems() const;
  bool unlocked(const char* id) const;
  void depositRun();
  void saveProfileNow();
  void cycleCampaign();
  bool characterTaken(int slot, int character) const;
  void cycleCharacter(int slot, int direction, bool includeCurrent = false);
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
  void updateMusic();
  void playEventSound(const Event& e);
  void sfx(Sound sound) { if (audio_) audio_->play(sound); }
  void announce(std::string text, std::uint32_t color, bool toast = false);
  void renderHud(BatchRenderer& batch, float width, float height);
  void renderChooser(BatchRenderer& batch, float width, float height);
  void renderSplash(BatchRenderer& batch, float width, float height);
  void renderTitle(BatchRenderer& batch, float width, float height);
  void renderCredits(BatchRenderer& batch, float width, float height);
  void renderPause(BatchRenderer& batch, float width, float height);
  void renderOver(BatchRenderer& batch, float width, float height);
  void renderPerf(BatchRenderer& batch, float width, float height);

  FrontendOptions options_;
  Screen screen_{Screen::Title};
  GameState state_{};
  DefaultRandom random_{};
  native::FixedStep clock_{60.0, 4};
  Animator anim_{};
  Audio* audio_{};
  struct Sampled { double hp{-1}, xp{}, charge{}; int level{}, coins{}, cast{}; };
  std::array<Sampled, cfg::MAX_PLAYERS> sampled_{};
  std::size_t hazardCount_{};
  bool overSoundPlayed_{};
  struct Banner { std::string text; std::uint32_t color{}; double age{99}, life{2.8}; };
  Banner announce_, toast_;
  std::uint64_t feedbackEventId_{};
  bool feedbackPrimed_{};
  double hurtFlash_{};
  std::array<double, cfg::MAX_PLAYERS> lastHp_{};
  native::Camera camera_{};
  std::array<PadEdges, cfg::MAX_PLAYERS> edges_{};
  std::array<bool, cfg::MAX_PLAYERS> joined_{true, false, false, false};
  // Character (spell element / sprite) chosen by each slot; joined slots never share one.
  std::array<int, cfg::MAX_PLAYERS> character_{0, 1, 2, 3};
  Profile profile_{};
  std::string profilePath_;
  int campaign_{0};         // index into kCampaigns
  std::string weapon_;       // Arsenal loadout (P1), empty = normal draw
  int special_{};            // Segundo feitiço loadout (P1)
  bool deposited_{};
  int earned_{};
  int shopIndex_{};
  bool respecArmed_{};
  bool quitRequested_{};
  int menuIndex_{0}, pauseIndex_{0}, choiceIndex_{0};
  std::string choiceKey_;
  double menuTime_{}, overTime_{}, splashTime_{};
  double buildMs_{};
  FrameTimings timings_{};
  BatchRenderer::Stats lastBatch_{};
  double fpsAccum_{}; int fpsFrames_{}; double fps_{};
  char scratch_[160]{};
};

} // namespace arcana::sdl
