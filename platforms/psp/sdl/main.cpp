// PSP runtime: PSPSDK + SDL2 (GU renderer) + the shared native frontend in compact mode.
// Differences from the Vita/Switch mains: a single 512x512 texture page (64 px atlas and floor),
// a UI laid out for 480x272 and one player, float simulation (ARCANA_REAL_FLOAT), audio synthesized
// at 22 kHz, and the save next to the EBOOT.
#include "batch_renderer.hpp"
#include "frontend.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <pspctrl.h>
#include <psppower.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

using namespace arcana;
using namespace arcana::sdl;

namespace {

constexpr int kWidth = 480, kHeight = 272;

void readPad(PadInput& out) {
  SceCtrlData pad{};
  sceCtrlPeekBufferPositive(&pad, 1);
  out = {};
  out.connected = true;
  float x = (static_cast<int>(pad.Lx) - 128) / 127.0f, y = (static_cast<int>(pad.Ly) - 128) / 127.0f;
  x = std::clamp(x, -1.0f, 1.0f); y = std::clamp(y, -1.0f, 1.0f);
  // The PSP nub rests far from centre on worn units: a generous dead zone.
  const float len = std::sqrt(x * x + y * y);
  if (len < 0.3f) { x = y = 0; }
  else { const float scale = std::min(1.0f, (len - 0.3f) / 0.7f) / len; x *= scale; y *= scale; }
  const unsigned b = pad.Buttons;
  std::uint32_t held = 0;
  if (b & (PSP_CTRL_CROSS | PSP_CTRL_RTRIGGER)) held |= ActSpecial;
  if (b & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) held |= ActDash;
  if (b & PSP_CTRL_START) held |= ActPause;
  if (b & PSP_CTRL_CROSS) held |= ActConfirm;
  if (b & PSP_CTRL_CIRCLE) held |= ActCancel;
  if (b & (PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE)) held |= ActAlt;
  if (b & PSP_CTRL_SELECT) held |= ActDebug;
  if (b & PSP_CTRL_UP) { held |= ActUp; y = -1; }
  if (b & PSP_CTRL_DOWN) { held |= ActDown; y = 1; }
  if (b & PSP_CTRL_LEFT) { held |= ActLeft; x = -1; }
  if (b & PSP_CTRL_RIGHT) { held |= ActRight; x = 1; }
  const float l = std::sqrt(x * x + y * y);
  if (l > 1) { x /= l; y /= l; }
  out.x = x; out.y = y; out.held = held;
}

} // namespace

// SDL2main (SDL_psp_main.c) provides the module info, the HOME-button exit callback and main().
int main(int argc, char* argv[]) {
  scePowerSetClockFrequency(333, 333, 166);
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  // Assets and the save live next to the EBOOT (ms0:/PSP/GAME/<folder>/).
  std::string dir = argc > 0 && argv[0] ? argv[0] : "";
  dir = dir.substr(0, dir.find_last_of('/') + 1);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
  TTF_Init();
  IMG_Init(IMG_INIT_PNG);
  SDL_Window* window = SDL_CreateWindow("Arcana Survivors", 0, 0, kWidth, kHeight, SDL_WINDOW_SHOWN);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
  SDL_Surface* atlas = IMG_Load((dir + "assets/native_atlas_64.png").c_str());
  SDL_Surface* terrain = IMG_Load((dir + "assets/terrain_tiles_64.png").c_str());
  // Baked small: at 480x272 text is drawn at 9-20 px, so 18 px glyphs keep the page tiny and sharp.
  TTF_Font* font = TTF_OpenFont((dir + "assets/fonts/DejaVuSans-Bold.ttf").c_str(), 18);
  BatchRenderer batch;
  std::string error;
  if (!renderer || !atlas || !font || !batch.init(renderer, atlas, atlas->w / 6, 6, terrain, font, error, true)) {
    SDL_Log("Arcana init failed: %s %s", SDL_GetError(), error.c_str());
    SDL_Quit();
    return 1;
  }
  SDL_FreeSurface(atlas);
  if (terrain) SDL_FreeSurface(terrain);
  TTF_CloseFont(font);

  FrontendOptions options;
  options.compact = true;
  // Test hook: an autoplay.txt next to the EBOOT starts a bot run with the performance overlay
  // ("charged" inside also keeps specials charged, to stress the effects).
  if (SDL_RWops* flag = SDL_RWFromFile((dir + "autoplay.txt").c_str(), "rb")) {
    char text[16] = {};
    SDL_RWread(flag, text, 1, sizeof text - 1);
    SDL_RWclose(flag);
    options.autoplay = true;
    options.showPerf = true;
    options.debugCharge = std::string(text).find("charged") != std::string::npos;
  }
  auto frontend = std::make_unique<Frontend>(options);
  frontend->setProfilePath(dir + "profile.ini");
  static Audio audio; // synth state lives for the whole process
  if (audio.init(22050)) frontend->setAudio(&audio);

  InputFrame input;
  Uint64 last = SDL_GetPerformanceCounter();
  const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = false;
    readPad(input.pads[0]);
    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt = static_cast<double>(now - last) / frequency;
    last = now;
    if (!frontend->update(dt, input)) break;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    frontend->render(batch, kWidth, kHeight);
    frontend->setFrameMs(static_cast<double>(SDL_GetPerformanceCounter() - now) * 1000.0 / frequency);
    SDL_RenderPresent(renderer);
  }

  audio.shutdown();
  batch.shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  return 0;
}
