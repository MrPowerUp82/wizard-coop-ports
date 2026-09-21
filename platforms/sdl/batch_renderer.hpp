#pragma once

#include "arcana/native/render_queue.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arcana::sdl {

enum class Align { Left, Center, Right };

// Point size platforms should open the TTF font at: glyphs are baked once at this size and scaled.
constexpr int kFontBakePx = 36;

// Draws everything (sprites, shapes, text) from ONE texture: the sprite atlas, a white texel for
// solid shapes and a baked glyph atlas share a single GPU texture. Draw order is submission order
// and a frame is normally one SDL_RenderGeometry call, instead of one call per sprite/rect/string.
// Buffers are sized once in init(); nothing allocates while a frame is being built.
class BatchRenderer {
public:
  struct Stats { int drawCalls{}, vertices{}; };

  // `atlas` holds sprites in a grid of `cell`-sized squares, `cols` per row, in SpriteId order.
  bool init(SDL_Renderer* renderer, SDL_Surface* atlas, int cell, int cols, TTF_Font* font, std::string& error);
  void shutdown();

  void begin();
  void flush();
  [[nodiscard]] Stats stats() const { return stats_; }

  void queue(const native::RenderQueue& q);
  void sprite(const native::SpriteCommand& cmd);
  void circle(float x, float y, float radius, std::uint32_t color, float thickness = 0);
  void rect(float x, float y, float w, float h, std::uint32_t color);
  void frame(float x, float y, float w, float h, float border, std::uint32_t color);
  void line(float x1, float y1, float x2, float y2, float width, std::uint32_t color);
  void icon(native::SpriteId id, float x, float y, float size, std::uint32_t color = 0xffffffffu);

  // `px` is the line height in pixels. Text is UTF-8; glyphs outside Latin-1 render as '?'.
  float text(float x, float y, std::string_view utf8, float px, std::uint32_t color, Align align = Align::Left);
  float textWidth(std::string_view utf8, float px) const;
  // Word-wraps into at most `maxLines` lines; returns the height used.
  float textWrapped(float x, float y, float maxWidth, std::string_view utf8, float px, std::uint32_t color, int maxLines = 4, Align align = Align::Left);

private:
  struct UvRect { float u0{}, v0{}, u1{}, v1{}; };
  struct Glyph { UvRect uv; float w{}, h{}, advance{}; bool valid{}; };

  void reserve(int vertices, int indices);
  void quad(const SDL_FPoint (&p)[4], const UvRect& uv, SDL_Color color);
  static SDL_Color unpack(std::uint32_t c) {
    return SDL_Color{static_cast<Uint8>(c & 0xff), static_cast<Uint8>((c >> 8) & 0xff), static_cast<Uint8>((c >> 16) & 0xff), static_cast<Uint8>(c >> 24)};
  }
  const Glyph& glyph(std::uint32_t codepoint) const;

  SDL_Renderer* renderer_{};
  SDL_Texture* texture_{};
  std::array<UvRect, static_cast<std::size_t>(native::SpriteId::Count)> sprites_{};
  std::array<Glyph, 256> glyphs_{};
  UvRect white_{};
  float fontHeight_{1};
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
  std::array<SDL_FPoint, 97> unitCircle_{}; // cos/sin table, 96 segments + wrap
  Stats stats_{};
};

} // namespace arcana::sdl
