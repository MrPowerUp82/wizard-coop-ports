// Nintendo Switch runtime: libnx + SDL2 (GPU through mesa/nouveau) + the shared native frontend.
// The whole frame is batched into ~1 SDL_RenderGeometry call from a single atlas texture.
// Input is read straight from libnx HID so every Joy-Con mode (handheld, pair, single sideways,
// Pro) works, following the mapping validated in the nx.js port (platforms/switch/src/input).
#include "batch_renderer.hpp"
#include "frontend.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace arcana;
using namespace arcana::sdl;

namespace {

constexpr int kWidth = 1280, kHeight = 720;
constexpr float kDeadzone = 0.2f;

// Sideways Joy-Con sticks arrive in the device's own frame (the hold type is left at its default,
// like nx.js did) and are rotated here. Flip to false if hardware shows the HID already rotates them.
constexpr bool kRotateSingleJoyCon = true;

enum class Kind { Standard, JoyLeft, JoyRight };

Kind classify(const PadState& pad) {
  const u32 style = padGetStyleSet(&pad);
  if (style & (HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)) return Kind::Standard;
  if (style & HidNpadStyleTag_NpadJoyLeft) return Kind::JoyLeft;
  if (style & HidNpadStyleTag_NpadJoyRight) return Kind::JoyRight;
  return Kind::Standard;
}

void deadzone(float& x, float& y) {
  const float len = std::sqrt(x * x + y * y);
  if (len < kDeadzone) { x = y = 0; return; }
  const float scale = std::min(1.0f, (len - kDeadzone) / (1 - kDeadzone)) / len;
  x *= scale; y *= scale;
}

void readPad(const PadState& pad, PadInput& out) {
  out = {};
  if (!padIsConnected(&pad)) return;
  out.connected = true;
  const Kind kind = classify(pad);
  const u64 b = padGetButtons(&pad);
  auto has = [&](u64 mask) { return (b & mask) != 0; };

  // libnx sticks: -32767..32767 with +Y up. The game wants +Y down.
  const HidAnalogStickState stick = padGetStickPos(&pad, kind == Kind::JoyRight ? 1 : 0);
  float x = stick.x / 32767.0f, y = -stick.y / 32767.0f;
  if (kRotateSingleJoyCon && kind == Kind::JoyLeft) { const float t = x; x = y; y = -t; }
  if (kRotateSingleJoyCon && kind == Kind::JoyRight) { const float t = x; x = -y; y = t; }
  deadzone(x, y);

  std::uint32_t held = 0;
  if (kind == Kind::Standard) {
    if (has(HidNpadButton_A | HidNpadButton_R | HidNpadButton_ZR)) held |= ActSpecial;
    if (has(HidNpadButton_B | HidNpadButton_L | HidNpadButton_ZL)) held |= ActDash;
    if (has(HidNpadButton_Plus | HidNpadButton_Minus)) held |= ActPause;
    if (has(HidNpadButton_A)) held |= ActConfirm;
    if (has(HidNpadButton_B)) held |= ActCancel;
    if (has(HidNpadButton_X | HidNpadButton_Y)) held |= ActAlt;
    if (has(HidNpadButton_StickL | HidNpadButton_StickR)) held |= ActDebug;
    if (has(HidNpadButton_Up)) { held |= ActUp; y = -1; }
    if (has(HidNpadButton_Down)) { held |= ActDown; y = 1; }
    if (has(HidNpadButton_Left)) { held |= ActLeft; x = -1; }
    if (has(HidNpadButton_Right)) { held |= ActRight; x = 1; }
  } else if (kind == Kind::JoyLeft) {
    // Turned counter-clockwise: D-pad Down is on the player's right (east), Left at the bottom.
    const u64 east = HidNpadButton_Down, south = HidNpadButton_Left, west = HidNpadButton_Up, north = HidNpadButton_Right;
    if (has(HidNpadButton_LeftSL | east)) held |= ActSpecial;
    if (has(HidNpadButton_LeftSR | south)) held |= ActDash;
    if (has(HidNpadButton_Minus)) held |= ActPause;
    if (has(east)) held |= ActConfirm;
    if (has(south)) held |= ActCancel;
    if (has(north | west)) held |= ActAlt;
    if (has(HidNpadButton_StickL)) held |= ActDebug;
  } else {
    // Turned clockwise: X on the player's right (east), A at the bottom.
    const u64 east = HidNpadButton_X, south = HidNpadButton_A, west = HidNpadButton_B, north = HidNpadButton_Y;
    if (has(HidNpadButton_RightSL | east)) held |= ActSpecial;
    if (has(HidNpadButton_RightSR | south)) held |= ActDash;
    if (has(HidNpadButton_Plus)) held |= ActPause;
    if (has(east)) held |= ActConfirm;
    if (has(south)) held |= ActCancel;
    if (has(north | west)) held |= ActAlt;
    if (has(HidNpadButton_StickR)) held |= ActDebug;
  }
  const float len = std::sqrt(x * x + y * y);
  if (len > 1) { x /= len; y /= len; }
  out.x = x; out.y = y; out.held = held;
}

void fatal(const char* what, const char* detail) {
  // Show the error on the console screen instead of silently returning to hbmenu.
  consoleInit(nullptr);
  std::printf("Arcana Survivors: %s\n%s\n\nPressione + para sair.\n", what, detail ? detail : "");
  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(nullptr);
  }
  consoleExit(nullptr);
}

} // namespace

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  romfsInit();
  plInitialize(PlServiceType_User);
  // CPU boost while decoding the atlas/baking glyphs; gameplay then runs at stock clocks.
  appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);

  padConfigureInput(cfg::MAX_PLAYERS, HidNpadStyleSet_NpadStandard);
  PadState pads[cfg::MAX_PLAYERS];
  padInitialize(&pads[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
  for (int i = 1; i < cfg::MAX_PLAYERS; ++i) padInitialize(&pads[i], static_cast<HidNpadIdType>(HidNpadIdType_No1 + i));

  if (SDL_Init(SDL_INIT_VIDEO) != 0) { fatal("SDL_Init falhou", SDL_GetError()); return 1; }
  TTF_Init();
  IMG_Init(IMG_INIT_PNG);
  SDL_Window* window = SDL_CreateWindow("Arcana Survivors", 0, 0, kWidth, kHeight, SDL_WINDOW_SHOWN);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
  if (!renderer) { fatal("Nao foi possivel criar o renderer", SDL_GetError()); return 1; }

  SDL_Surface* atlas = IMG_Load("romfs:/native_atlas_128.png");
  if (!atlas) { fatal("Atlas ausente no romfs", IMG_GetError()); return 1; }

  SDL_Surface* terrain = IMG_Load("romfs:/terrain_tiles.png"); // optional

  // The console's shared system font: no TTF shipped in the .nro.
  PlFontData fontData{};
  TTF_Font* font = nullptr;
  if (R_SUCCEEDED(plGetSharedFontByType(&fontData, PlSharedFontType_Standard)))
    font = TTF_OpenFontRW(SDL_RWFromConstMem(fontData.address, static_cast<int>(fontData.size)), 1, kFontBakePx);
  if (!font) { fatal("Fonte do sistema indisponivel", TTF_GetError()); return 1; }

  BatchRenderer batch;
  std::string error;
  if (!batch.init(renderer, atlas, atlas->w / 6, 6, terrain, font, error)) { fatal("Falha ao preparar texturas", error.c_str()); return 1; }
  SDL_FreeSurface(atlas);
  if (terrain) SDL_FreeSurface(terrain);
  TTF_CloseFont(font);
  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);

  auto frontend = std::make_unique<Frontend>(); // GameState is large: keep it off the main thread stack
  static Audio audio; // synth state lives for the whole process
  if (audio.init()) frontend->setAudio(&audio);
  InputFrame input;
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();

  while (appletMainLoop()) {
    for (int i = 0; i < cfg::MAX_PLAYERS; ++i) {
      padUpdate(&pads[i]);
      readPad(pads[i], input.pads[static_cast<std::size_t>(i)]);
    }
    const auto frameStart = Clock::now();
    const double dt = std::chrono::duration<double>(frameStart - last).count();
    last = frameStart;

    if (!frontend->update(dt, input)) break;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    frontend->render(batch, kWidth, kHeight);
    frontend->setFrameMs(std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
    SDL_RenderPresent(renderer);
  }

  audio.shutdown();
  batch.shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  plExit();
  romfsExit();
  return 0;
}
