// Desktop (Windows/Linux/macOS) runtime for the native frontend. Used for development, for
// headless screenshots/benchmarks in CI (SDL_VIDEODRIVER=dummy), and as the reference for the
// console mains, which differ only in init, asset paths and input.
#include "batch_renderer.hpp"
#include "frontend.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace arcana;
using namespace arcana::sdl;

namespace {

struct Args {
  FrontendOptions frontend;
  std::string atlas, font, screenshot;
  int width{1280}, height{720};
  int frames{-1};         // headless: stop after N frames
  double benchSeconds{};  // headless benchmark with fixed 1/60 frames
};

Args parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&](const char* fallback) { return i + 1 < argc ? std::string(argv[++i]) : std::string(fallback); };
    if (k == "--autoplay") { a.frontend.autoplay = true; if (i + 1 < argc && argv[i + 1][0] != '-') a.frontend.autoplayPlayers = std::atoi(argv[++i]); }
    else if (k == "--perf") a.frontend.showPerf = true;
    else if (k == "--campaign") a.frontend.campaign = next("quick");
    else if (k == "--play") a.frontend.startImmediately = true;
    else if (k == "--atlas") a.atlas = next("");
    else if (k == "--font") a.font = next("");
    else if (k == "--screenshot") a.screenshot = next("screenshot.bmp");
    else if (k == "--frames") a.frames = std::atoi(next("600").c_str());
    else if (k == "--bench") a.benchSeconds = std::atof(next("60").c_str());
    else if (k == "--size") { a.width = std::atoi(next("1280").c_str()); a.height = std::atoi(next("720").c_str()); }
  }
  return a;
}

bool exists(const std::string& path) {
  if (path.empty()) return false;
  SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
  if (f) SDL_RWclose(f);
  return f != nullptr;
}

std::string findAsset(const std::string& explicitPath, const std::vector<std::string>& names) {
  if (exists(explicitPath)) return explicitPath;
  std::vector<std::string> roots = {"", "assets/", "../assets/", "../../assets/"};
  if (char* base = SDL_GetBasePath()) {
    roots.push_back(std::string(base) + "assets/");
    roots.push_back(std::string(base) + "../assets/");
    SDL_free(base);
  }
  for (const auto& name : names) {
    if (!name.empty() && (name[0] == '/' || name.find(':') != std::string::npos)) { if (exists(name)) return name; continue; }
    for (const auto& root : roots) if (exists(root + name)) return root + name;
  }
  return {};
}

float axis(SDL_GameController* pad, SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad, a) / 32767.0f; }

void deadzone(float& x, float& y) {
  const float len = std::sqrt(x * x + y * y);
  if (len < 0.2f) { x = y = 0; return; }
  const float scale = std::min(1.0f, (len - 0.2f) / 0.8f) / len;
  x *= scale; y *= scale;
}

void readInput(InputFrame& frame, const std::vector<SDL_GameController*>& pads) {
  frame = {};
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  auto& p0 = frame.pads[0];
  p0.connected = true;
  p0.x = static_cast<float>((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
  p0.y = static_cast<float>((k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) - (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]));
  if (p0.x != 0 && p0.y != 0) { p0.x *= 0.7071f; p0.y *= 0.7071f; }
  if (k[SDL_SCANCODE_SPACE]) p0.held |= ActSpecial;
  if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) p0.held |= ActDash;
  if (k[SDL_SCANCODE_ESCAPE]) p0.held |= ActPause;
  if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) p0.held |= ActConfirm;
  if (k[SDL_SCANCODE_BACKSPACE]) p0.held |= ActCancel;
  if (k[SDL_SCANCODE_R]) p0.held |= ActAlt;
  if (k[SDL_SCANCODE_F3]) p0.held |= ActDebug;
  if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) p0.held |= ActUp;
  if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) p0.held |= ActDown;
  if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) p0.held |= ActLeft;
  if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) p0.held |= ActRight;

  // First gamepad shares slot 0 with the keyboard; the others become players 2-4.
  for (std::size_t i = 0; i < pads.size() && i < frame.pads.size(); ++i) {
    SDL_GameController* pad = pads[i];
    auto& out = frame.pads[i];
    out.connected = true;
    float x = axis(pad, SDL_CONTROLLER_AXIS_LEFTX), y = axis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    deadzone(x, y);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) x = -1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) x = 1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) y = -1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) y = 1;
    if (x != 0 || y != 0) { out.x = x; out.y = y; const float l = std::sqrt(x * x + y * y); if (l > 1) { out.x /= l; out.y /= l; } }
    auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    const bool rt = axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f, lt = axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f;
    if (btn(SDL_CONTROLLER_BUTTON_A)) out.held |= ActConfirm | ActSpecial;
    if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || rt) out.held |= ActSpecial;
    if (btn(SDL_CONTROLLER_BUTTON_B)) out.held |= ActCancel | ActDash;
    if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || lt) out.held |= ActDash;
    if (btn(SDL_CONTROLLER_BUTTON_X) || btn(SDL_CONTROLLER_BUTTON_Y)) out.held |= ActAlt;
    if (btn(SDL_CONTROLLER_BUTTON_START)) out.held |= ActPause;
    if (btn(SDL_CONTROLLER_BUTTON_BACK)) out.held |= ActDebug;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) out.held |= ActUp;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) out.held |= ActDown;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) out.held |= ActLeft;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) out.held |= ActRight;
  }
}

} // namespace

int main(int argc, char** argv) {
  const Args args = parseArgs(argc, argv);
  const bool headless = !args.screenshot.empty() || args.benchSeconds > 0 || args.frames > 0;
  if (headless && !SDL_getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  if (TTF_Init() != 0) { std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 1; }
  IMG_Init(IMG_INIT_PNG);

  SDL_Window* window = SDL_CreateWindow("Arcana Survivors", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, args.width, args.height,
                                        headless ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
  if (!window) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, headless ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
  SDL_RendererInfo info{};
  SDL_GetRendererInfo(renderer, &info);

  const std::string atlasPath = findAsset(args.atlas, {"native_atlas_128.png", "native_atlas.png"});
  SDL_Surface* atlas = atlasPath.empty() ? nullptr : IMG_Load(atlasPath.c_str());
  if (!atlas) { std::fprintf(stderr, "atlas not found (%s): %s\n", atlasPath.c_str(), IMG_GetError()); return 1; }
  const std::string fontPath = findAsset(args.font, {"fonts/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                                                     "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"});
  TTF_Font* font = fontPath.empty() ? nullptr : TTF_OpenFont(fontPath.c_str(), kFontBakePx);
  if (!font) { std::fprintf(stderr, "font not found (%s): %s\n", fontPath.c_str(), TTF_GetError()); return 1; }

  BatchRenderer batch;
  std::string error;
  const int cell = atlas->w / 6;
  if (!batch.init(renderer, atlas, cell, 6, font, error)) { std::fprintf(stderr, "renderer init: %s\n", error.c_str()); return 1; }
  SDL_FreeSurface(atlas);
  TTF_CloseFont(font);
  std::printf("renderer=%s atlas=%s font=%s\n", info.name, atlasPath.c_str(), fontPath.c_str());

  FrontendOptions options = args.frontend;
  if (args.benchSeconds > 0) { options.autoplay = true; options.showPerf = true; }
  auto frontend = std::make_unique<Frontend>(options); // GameState is large: keep it off the stack

  std::vector<SDL_GameController*> pads;
  InputFrame input;
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  const int frameLimit = args.benchSeconds > 0 ? static_cast<int>(args.benchSeconds * 60) : args.frames;
  double sumFrame = 0, worstFrame = 0, sumBuild = 0;
  int frames = 0;
  bool running = true;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_CONTROLLERDEVICEADDED) if (SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which)) pads.push_back(c);
      if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        pads.erase(std::remove_if(pads.begin(), pads.end(), [&](SDL_GameController* c) {
          if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) != e.cdevice.which) return false;
          SDL_GameControllerClose(c); return true;
        }), pads.end());
      }
    }
    readInput(input, pads);

    const auto frameStart = Clock::now();
    double dt = std::chrono::duration<double>(frameStart - last).count();
    last = frameStart;
    if (headless) dt = 1.0 / 60.0; // deterministic pacing for screenshots/benchmarks

    if (!frontend->update(dt, input)) running = false;
    int w = args.width, h = args.height;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    frontend->render(batch, static_cast<float>(w), static_cast<float>(h));
    const double cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
    frontend->setFrameMs(cpuMs);

    ++frames;
    if (frames > 60) { // skip warm-up
      sumFrame += cpuMs; worstFrame = std::max(worstFrame, cpuMs); sumBuild += frontend->buildMs();
    }

    if (frameLimit > 0 && frames >= frameLimit) {
      if (!args.screenshot.empty()) {
        SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
        const bool png = args.screenshot.size() > 4 && args.screenshot.substr(args.screenshot.size() - 4) == ".png";
        if (png ? IMG_SavePNG(shot, args.screenshot.c_str()) : SDL_SaveBMP(shot, args.screenshot.c_str())) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
        SDL_FreeSurface(shot);
        std::printf("wrote %s\n", args.screenshot.c_str());
      }
      running = false;
    }
    SDL_RenderPresent(renderer);
  }

  if (frames > 60) {
    const auto& s = frontend->state();
    std::printf("frames=%d avg_cpu_frame=%.3fms worst=%.3fms avg_queue=%.3fms game_time=%.1fs phase=%d enemies=%d over=%d\n",
                frames, sumFrame / (frames - 60), worstFrame, sumBuild / (frames - 60), s.time, s.phase + 1,
                static_cast<int>(s.enemies.size()), s.over ? 1 : 0);
  }
  for (auto* c : pads) SDL_GameControllerClose(c);
  batch.shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  return 0;
}
