#pragma once

#include "arcana/native/render_queue.hpp"
#include "arcana/native/static_vector.hpp"

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

// Draws everything (sprites, shapes, text, floor) from ONE texture: the sprite atlas, a white
// silhouette copy of it (hit flashes), a soft radial glow, the floor tiles, a white texel for solid
// shapes and a baked glyph atlas. Draw order is submission order; a new SDL_RenderGeometry call is
// only issued when switching between normal and additive blending ("lighter" in the web canvas),
// so a busy frame is a handful of calls. Buffers are sized once; nothing allocates per frame.
//
// World drawing goes through a camera transform (setTransform); HUD code uses resetTransform().
class BatchRenderer {
public:
  struct Stats { int drawCalls{}, vertices{}; };

  // `atlas`: sprites in a grid of `cell` squares, `cols` per row, in SpriteId order.
  // `terrain`: optional strip of square floor tiles (one per phase), may be null.
  bool init(SDL_Renderer* renderer, SDL_Surface* atlas, int cell, int cols, SDL_Surface* terrain, TTF_Font* font, std::string& error);
  void shutdown();

  void begin();
  void flush();
  [[nodiscard]] Stats stats() const { return stats_; }

  void setTransform(float offsetX, float offsetY, float scale) { ox_ = offsetX; oy_ = offsetY; scale_ = scale; }
  void resetTransform() { ox_ = 0; oy_ = 0; scale_ = 1; }
  [[nodiscard]] float scale() const { return scale_; }
  void setAdditive(bool additive);

  // --- primitives (all in current transform space) ---
  void sprite(native::SpriteId id, float x, float y, float size, float rotation = 0, float alpha = 1,
              float sx = 1, float sy = 1, float flash = 0, std::uint32_t tint = 0xffffffffu);
  void sprite(const native::SpriteCommand& cmd);
  void icon(native::SpriteId id, float x, float y, float size, std::uint32_t color = 0xffffffffu);
  void glow(float x, float y, float radius, std::uint32_t color, float alpha = 1);
  void rect(float x, float y, float w, float h, std::uint32_t color);
  void rectGradient(float x, float y, float w, float h, std::uint32_t tl, std::uint32_t tr, std::uint32_t br, std::uint32_t bl);
  void frame(float x, float y, float w, float h, float border, std::uint32_t color);
  void line(float x1, float y1, float x2, float y2, float width, std::uint32_t color);
  // Colour fades from c1 at (x1,y1) to c2 at (x2,y2): shot trails, meteor tails.
  void lineGradient(float x1, float y1, float x2, float y2, float width, std::uint32_t c1, std::uint32_t c2);
  void quadGradient(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                    std::uint32_t c0, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3);
  void circle(float x, float y, float radius, std::uint32_t color, float thickness = 0);
  void ellipse(float x, float y, float rx, float ry, std::uint32_t color, float thickness = 0, float rotation = 0);
  // Arc from a0 to a1 (radians, clockwise on screen like canvas) on an ellipse; stroke only.
  void arc(float x, float y, float rx, float ry, float a0, float a1, std::uint32_t color, float thickness);
  void dashedCircle(float x, float y, float radius, float dash, float gap, float offset, std::uint32_t color, float thickness);
  void triangle(float x0, float y0, float x1, float y1, float x2, float y2, std::uint32_t color);
  void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, std::uint32_t color);

  // Scratch path (max 128 points): fill() fans from the first point (convex / star-shaped only).
  void beginPath() { path_.clear(); }
  void lineTo(float x, float y) { if (path_.size() < path_.capacity()) path_.push_back({x, y}); }
  void stroke(float width, std::uint32_t color, bool closed = false);
  void fill(std::uint32_t color);

  // Floor: covers the screen rect (0,0)-(screenW,screenH) with world-anchored 256-unit tiles of
  // `phase`, using the current world transform. Returns false when no terrain sheet was loaded.
  bool terrain(int phase, float screenW, float screenH);

  // --- text; `px` is the line height in current transform units. UTF-8, Latin-1 glyphs. ---
  float text(float x, float y, std::string_view utf8, float px, std::uint32_t color, Align align = Align::Left);
  float textOutlined(float x, float y, std::string_view utf8, float px, std::uint32_t color, std::uint32_t outline, Align align = Align::Left);
  float textWidth(std::string_view utf8, float px) const;
  float textWrapped(float x, float y, float maxWidth, std::string_view utf8, float px, std::uint32_t color, int maxLines = 4, Align align = Align::Left);

private:
  struct UvRect { float u0{}, v0{}, u1{}, v1{}; };
  struct Glyph { UvRect uv; float w{}, h{}, advance{}; bool valid{}; };

  void reserve(int vertices, int indices);
  void rawQuad(const SDL_FPoint (&p)[4], const UvRect& uv, SDL_Color color);
  SDL_FPoint tx(float x, float y) const { return {x * scale_ + ox_, y * scale_ + oy_}; }
  static SDL_Color unpack(std::uint32_t c, float alpha = 1);
  const Glyph& glyph(std::uint32_t codepoint) const;
  void ring(float x, float y, float rx, float ry, float a0, float a1, int segments, std::uint32_t color, float thickness, bool closed);

  SDL_Renderer* renderer_{};
  SDL_Texture* texture_{};
  std::array<UvRect, static_cast<std::size_t>(native::SpriteId::Count)> sprites_{};
  std::array<UvRect, static_cast<std::size_t>(native::SpriteId::Count)> silhouettes_{};
  std::array<UvRect, 8> terrain_{};
  int terrainCount_{};
  UvRect glow_{};
  std::array<Glyph, 256> glyphs_{};
  UvRect white_{};
  float fontHeight_{1};
  float ox_{}, oy_{}, scale_{1};
  bool additive_{};
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
  native::StaticVector<SDL_FPoint, 128> path_;
  Stats stats_{};
};

} // namespace arcana::sdl
