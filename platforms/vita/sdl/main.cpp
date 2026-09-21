// PS Vita runtime: VitaSDK + SDL2 (GXM renderer) + the shared native frontend. Same game flow,
// HUD and batching as PC/Switch; only init, clocks and SceCtrl input live here.
#include "batch_renderer.hpp"
#include "frontend.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <memory>

// Main thread stack and newlib heap: the simulation state is heap-allocated, but SDL, FreeType and
// the glyph bake need headroom beyond the small defaults.
extern "C" {
unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;
unsigned int _newlib_heap_size_user = 96 * 1024 * 1024;
}

using namespace arcana;
using namespace arcana::sdl;

namespace {

constexpr int kWidth = 960, kHeight = 544;

float axis(unsigned char value) {
  return std::clamp((static_cast<int>(value) - 128) / 127.0f, -1.0f, 1.0f);
}

void readPad(PadInput& out) {
  SceCtrlData pad{};
  sceCtrlPeekBufferPositive(0, &pad, 1);
  out = {};
  out.connected = true;
  float x = axis(pad.lx), y = axis(pad.ly);
  const float len = std::sqrt(x * x + y * y);
  if (len < 0.2f) { x = y = 0; }
  else { const float scale = std::min(1.0f, (len - 0.2f) / 0.8f) / len; x *= scale; y *= scale; }
  const unsigned b = pad.buttons;
  std::uint32_t held = 0;
  if (b & (SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER)) held |= ActSpecial;
  if (b & (SCE_CTRL_CIRCLE | SCE_CTRL_LTRIGGER)) held |= ActDash;
  if (b & SCE_CTRL_START) held |= ActPause;
  if (b & SCE_CTRL_CROSS) held |= ActConfirm;
  if (b & SCE_CTRL_CIRCLE) held |= ActCancel;
  if (b & (SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE)) held |= ActAlt;
  if (b & SCE_CTRL_SELECT) held |= ActDebug;
  if (b & SCE_CTRL_UP) { held |= ActUp; y = -1; }
  if (b & SCE_CTRL_DOWN) { held |= ActDown; y = 1; }
  if (b & SCE_CTRL_LEFT) { held |= ActLeft; x = -1; }
  if (b & SCE_CTRL_RIGHT) { held |= ActRight; x = 1; }
  const float l = std::sqrt(x * x + y * y);
  if (l > 1) { x /= l; y /= l; }
  out.x = x; out.y = y; out.held = held;
}

} // namespace

int main(int, char**) {
  // Same clocks as the previous Vita runtimes.
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) { sceKernelExitProcess(1); return 1; }
  TTF_Init();
  IMG_Init(IMG_INIT_PNG);
  SDL_Window* window = SDL_CreateWindow("Arcana Survivors", 0, 0, kWidth, kHeight, SDL_WINDOW_SHOWN);
  // Native GXM renderer; the vitaGL/GLES2 fallback adds a translation layer we do not need.
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "VITA gxm");
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
  SDL_Surface* atlas = IMG_Load("app0:/assets/native_atlas_128.png");
  SDL_Surface* terrain = IMG_Load("app0:/assets/terrain_tiles.png"); // optional
  TTF_Font* font = TTF_OpenFont("app0:/assets/fonts/DejaVuSans-Bold.ttf", kFontBakePx);
  BatchRenderer batch;
  std::string error;
  if (!renderer || !atlas || !font || !batch.init(renderer, atlas, atlas->w / 6, 6, terrain, font, error)) {
    SDL_Log("Arcana init failed: %s %s", SDL_GetError(), error.c_str());
    sceKernelExitProcess(1);
    return 1;
  }
  SDL_FreeSurface(atlas);
  if (terrain) SDL_FreeSurface(terrain);
  TTF_CloseFont(font);

  auto frontend = std::make_unique<Frontend>();
  // ux0:data is the conventional homebrew save location; it survives reinstalling the .vpk.
  mkdir("ux0:data/arcana-survivors", 0777);
  frontend->setProfilePath("ux0:data/arcana-survivors/profile.ini");
  static Audio audio; // synth state lives for the whole process
  if (audio.init()) frontend->setAudio(&audio);
  InputFrame input;
  std::uint64_t lastUs = sceKernelGetProcessTimeWide();

  while (true) {
    readPad(input.pads[0]);
    const std::uint64_t nowUs = sceKernelGetProcessTimeWide();
    const double dt = static_cast<double>(nowUs - lastUs) / 1'000'000.0;
    lastUs = nowUs;

    if (!frontend->update(dt, input)) break;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    frontend->render(batch, kWidth, kHeight);
    frontend->setFrameMs(static_cast<double>(sceKernelGetProcessTimeWide() - nowUs) / 1000.0);
    SDL_RenderPresent(renderer);
  }

  audio.shutdown();
  batch.shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  sceKernelExitProcess(0);
  return 0;
}
