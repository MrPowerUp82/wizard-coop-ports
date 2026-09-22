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
#include <memory>
#include <optional>
#include <string>

namespace arcana::sdl {

// Platform-neutral input: each platform main fills one PadInput per local player every frame.
// The frontend never touches SDL/libnx input APIs, so PC, Switch and Vita share all game flow.
enum Action : std::uint32_t {
  ActSpecial = 1u << 0, ActDash = 1u << 1, ActPause = 1u << 2, ActConfirm = 1u << 3, ActCancel = 1u << 4,
  ActAlt = 1u << 5, ActUp = 1u << 6, ActDown = 1u << 7, ActLeft = 1u << 8, ActRight = 1u << 9, ActDebug = 1u << 10,
  // Online signals to allies (PC keyboard Q/E/X/C; gamepads use Alt + direction instead).
  ActSignalHere = 1u << 11, ActSignalHelp = 1u << 12, ActSignalDanger = 1u << 13, ActSignalLook = 1u << 14,
};

struct PadInput {
  bool connected{};
  float x{}, y{};        // movement, already dead-zoned, length <= 1, y down
  std::uint32_t held{};  // Action bits
};

// Text typed this frame (PC keyboard, only while the frontend asks for text). Consoles leave it empty.
struct TextInput {
  std::string typed;  // UTF-8
  int backspaces{};
  bool submit{};      // Enter
};

struct InputFrame {
  std::array<PadInput, cfg::MAX_PLAYERS> pads{};
  TextInput text;
};

// Online co-op, implemented on PC by platforms/online (IXWebSocket). The frontend only talks to this
// interface, so the console builds compile without any networking code.
class OnlinePort {
public:
  enum class MenuResult { Stay, Title, Play };
  virtual ~OnlinePort() = default;
  // Online pages (room list, create, name/code entry, lobby) before a match.
  virtual void openMenu(const Profile& profile) = 0;
  // `pressed`/`held`: player 1's Action bits (sticks already folded into directions).
  virtual MenuResult updateMenu(std::uint32_t pressed, std::uint32_t held, const TextInput& text, Profile& profile) = 0;
  virtual void renderMenu(BatchRenderer& b, float width, float height, float scale) = 0;
  [[nodiscard]] virtual bool wantsText() const = 0;
  // In a match: pumps the connection and writes the view to draw. False until the first snapshot.
  virtual bool frame(double dt, Vec2 input, GameState& out) = 0;
  [[nodiscard]] virtual const std::string& localId() const = 0;
  [[nodiscard]] virtual bool reconnecting() const = 0;
  // The connection is gone for good (the online pages show why).
  [[nodiscard]] virtual bool closed() const = 0;
  virtual std::optional<std::string> takeNotice() = 0;
  virtual void choosePower(const std::string& id) = 0;
  virtual void reroll() = 0;
  virtual void special() = 0;
  virtual void dash(Vec2 direction) = 0;
  virtual void signal(const char* kind, std::optional<Vec2> at) = 0;
  // Back to the online pages (after a match, or when the player leaves the room).
  virtual void leave() = 0;
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
  bool startOnline{};   // open the online pages right away (--online-bot)
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
  [[nodiscard]] const Profile& profile() const { return profile_; }
  [[nodiscard]] const GameState& state() const { return state_; }
  GameState& stateForTests() { return state_; }
  [[nodiscard]] bool inGame() const { return screen_ == Screen::Playing || screen_ == Screen::Paused; }
  [[nodiscard]] double buildMs() const { return buildMs_; }

  // PC only: enables "Jogar online" on the title screen.
  void setOnline(std::unique_ptr<OnlinePort> online);
  // The platform should deliver keyboard text (SDL_StartTextInput) while this is true.
  [[nodiscard]] bool wantsTextInput() const;

private:
  enum class Screen { Title, Playing, Paused, Over, Shop, Online };
  enum class TitleItem : std::uint8_t { Play, Online, Campaign, Weapon, Special, Shop, Quit };

  struct PadEdges { std::uint32_t pressed{}, held{}; };

  void readEdges(const InputFrame& input);
  bool pressed(int slot, Action a) const { return (edges_[static_cast<std::size_t>(slot)].pressed & a) != 0; }
  bool anyPressed(Action a) const;

  void startRun();
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
  void renderTitle(BatchRenderer& batch, float width, float height);
  void renderPause(BatchRenderer& batch, float width, float height);
  void renderOver(BatchRenderer& batch, float width, float height);
  void renderPerf(BatchRenderer& batch, float width, float height);

  void resetRunView();
  void enterOnline();
  void updateOnlineMenu(const InputFrame& input);
  void startOnlineRun();
  void updateOnlinePlaying(double frameSeconds, const InputFrame& input);
  void updateOnlineChooser(const Player& me);
  void sendSignals(const Player& me);
  void leaveOnlineMatch();
  void renderOnlineOverlay(BatchRenderer& batch, float width, float height);
  native::StaticVector<const Player*, cfg::MAX_PLAYERS> hudPlayers() const;

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
  double menuTime_{}, overTime_{};
  double buildMs_{};
  FrameTimings timings_{};
  BatchRenderer::Stats lastBatch_{};
  double fpsAccum_{}; int fpsFrames_{}; double fps_{};
  char scratch_[160]{};

  std::unique_ptr<OnlinePort> online_;
  bool onlineMatch_{}, onlineMenuOpen_{};
  std::string sentChoiceKey_;
  // Player id behind each local slot: "p1".."p4" offline; online only slot 0 (the server's id).
  std::array<std::string, cfg::MAX_PLAYERS> localIds_{"p1", "p2", "p3", "p4"};
};

} // namespace arcana::sdl

