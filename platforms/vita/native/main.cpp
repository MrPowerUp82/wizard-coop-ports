#include "arcana/game.hpp"
#include "arcana/native/fixed_step.hpp"
#include "arcana/native/render_queue.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <vita2d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kWidth = 960.0f;
constexpr float kHeight = 544.0f;
constexpr float kAtlasCell = 256.0f;
constexpr int kAtlasCols = arcana::native::kAtlasColumns;

float axis(std::uint8_t value) {
  float v = (static_cast<int>(value) - 128) / 127.0f;
  if (std::abs(v) < 0.16f) return 0.0f;
  return std::clamp(v, -1.0f, 1.0f);
}

void normalize(float& x, float& y) {
  const float len = std::sqrt(x * x + y * y);
  if (len > 1.0f) { x /= len; y /= len; }
}

unsigned int vitaColor(std::uint32_t rgba) { return rgba; }

void drawSprite(vita2d_texture* atlas, const arcana::native::SpriteCommand& cmd) {
  using arcana::native::SpriteId;
  if (cmd.sprite == SpriteId::Unknown || cmd.size <= 0) return;
  const int index = static_cast<int>(cmd.sprite);
  const float tx = static_cast<float>((index % kAtlasCols) * static_cast<int>(kAtlasCell));
  const float ty = static_cast<float>((index / kAtlasCols) * static_cast<int>(kAtlasCell));
  const float scale = cmd.size / kAtlasCell;
  vita2d_draw_texture_part_tint_scale_rotate(
      atlas,
      cmd.x - cmd.size * 0.5f,
      cmd.y - cmd.size * 0.5f,
      tx, ty, kAtlasCell, kAtlasCell,
      scale, scale, cmd.rotation,
      vitaColor(cmd.rgba));
}

void drawHud(const arcana::Player& p, const arcana::GameState& game) {
  constexpr float x = 18, y = 18, w = 230, h = 12;
  vita2d_draw_rectangle(x - 2, y - 2, w + 4, h + 4, RGBA8(8, 16, 22, 220));
  const float hpRatio = static_cast<float>(std::clamp(p.hp / p.maxHp, 0.0, 1.0));
  vita2d_draw_rectangle(x, y, w * hpRatio, h, RGBA8(225, 70, 92, 255));
  vita2d_draw_rectangle(x, y + 18, w, 5, RGBA8(20, 36, 42, 220));
  const float special = static_cast<float>(std::clamp(p.specialCharge / 100.0, 0.0, 1.0));
  vita2d_draw_rectangle(x, y + 18, w * special, 5, RGBA8(90, 215, 180, 255));

  // Tiny native performance HUD: entity counts become bars, avoiding font work in hot frames.
  const float enemyRatio = std::min(1.0f, static_cast<float>(game.enemies.size()) / 180.0f);
  vita2d_draw_rectangle(18, 50, 180 * enemyRatio, 3, RGBA8(255, 176, 80, 210));
}
} // namespace

int main() {
  // Same clocks already used by the old Vita runtime, now without QuickJS/Canvas overhead.
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  vita2d_init_advanced(2 * 1024 * 1024);
  vita2d_set_vblank_wait(1);
  vita2d_set_clear_color(RGBA8(7, 11, 20, 255));

  vita2d_texture* atlas = vita2d_load_PNG_file("app0:/assets/native_atlas.png");
  if (!atlas) {
    vita2d_fini();
    sceKernelExitProcess(1);
    return 1;
  }
  vita2d_texture_set_filters(atlas, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);

  arcana::SeededRandom rng(0xA7CA4A11u);
  // Keep the large fixed-capacity state out of the small main-thread stack.
  static arcana::GameState game = arcana::createGameState("classic");
  game.players.emplace("vita", arcana::createPlayer("vita", "Vita", 0));
  auto& player = game.players.at("vita");

  arcana::native::FixedStep fixed(60.0, 4);
  static arcana::native::RenderQueue queue;
  arcana::native::Camera camera;
  camera.width = kWidth;
  camera.height = kHeight;

  std::uint64_t lastUs = sceKernelGetProcessTimeWide();
  std::uint32_t previousButtons = 0;

  while (true) {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const std::uint32_t down = pad.buttons & ~previousButtons;
    previousButtons = pad.buttons;
    if (down & SCE_CTRL_START) break;

    float ix = axis(pad.lx);
    float iy = axis(pad.ly);
    if (pad.buttons & SCE_CTRL_LEFT) ix = -1;
    if (pad.buttons & SCE_CTRL_RIGHT) ix = 1;
    if (pad.buttons & SCE_CTRL_UP) iy = -1;
    if (pad.buttons & SCE_CTRL_DOWN) iy = 1;
    normalize(ix, iy);
    player.input.x = ix;
    player.input.y = iy;

    if (!player.pendingPowers.empty()) {
      int choice = -1;
      if (down & SCE_CTRL_CROSS) choice = 0;
      else if (down & SCE_CTRL_SQUARE) choice = 1;
      else if (down & SCE_CTRL_TRIANGLE) choice = 2;
      if (choice >= 0 && choice < static_cast<int>(player.pendingPowers.size()))
        arcana::applyPower(player, player.pendingPowers[choice]);
    } else {
      if (down & SCE_CTRL_CIRCLE) arcana::activateDash(game, player.id, {ix, iy});
      if (down & SCE_CTRL_CROSS) arcana::activateSpecial(game, player.id, rng);
    }

    const std::uint64_t nowUs = sceKernelGetProcessTimeWide();
    const double frameSeconds = static_cast<double>(nowUs - lastUs) / 1'000'000.0;
    lastUs = nowUs;
    fixed.advance(frameSeconds, [&](double dt) { arcana::updateGame(game, dt, rng); });

    camera.x = static_cast<float>(player.x);
    camera.y = static_cast<float>(player.y);
    arcana::native::buildRenderQueue(game, camera, queue);

    vita2d_start_drawing();
    vita2d_clear_screen();

    // Ground: intentionally cheap for the first native milestone. Tile batching comes next.
    vita2d_draw_rectangle(0, 0, kWidth, kHeight, RGBA8(9, 24, 30, 255));
    for (const auto& circle : queue.circles)
      vita2d_draw_fill_circle(circle.x, circle.y, circle.radius, vitaColor(circle.rgba));
    for (const auto& sprite : queue.sprites) drawSprite(atlas, sprite);
    drawHud(player, game);

    vita2d_end_drawing();
    vita2d_swap_buffers();
  }

  vita2d_free_texture(atlas);
  vita2d_fini();
  sceKernelExitProcess(0);
  return 0;
}
