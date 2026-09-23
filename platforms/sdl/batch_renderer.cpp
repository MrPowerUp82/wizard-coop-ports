#include "batch_renderer.hpp"
#include "fastmath.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace arcana::sdl {
namespace {

constexpr int kMaxVertices = 32768;
constexpr int kMaxIndices = kMaxVertices * 3 / 2;
constexpr int kWhiteSize = 4;
constexpr int kGlowSize = 64;
constexpr int kGlyphPadding = 2;
constexpr float kTerrainTile = 256.0f;

int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

// Decodes one UTF-8 codepoint and advances `i`. Invalid bytes become '?'.
std::uint32_t decodeUtf8(std::string_view s, std::size_t& i) {
  const auto c = static_cast<unsigned char>(s[i++]);
  if (c < 0x80) return c;
  int extra = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
  if (extra < 0) return '?';
  std::uint32_t cp = c & (0x3f >> extra);
  while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3f);
  return cp;
}

// Shelf packer over up to two free rectangles.
struct Packer {
  SDL_Rect areas[2]{};
  int area{}, x{}, y{}, shelf{};
  bool place(int w, int h, SDL_Rect& out) {
    while (area < 2) {
      const SDL_Rect& a = areas[area];
      if (x + w > a.w) { x = 0; y += shelf + kGlyphPadding; shelf = 0; }
      if (w <= a.w && y + h <= a.h) {
        out = {a.x + x, a.y + y, w, h};
        x += w + kGlyphPadding; shelf = std::max(shelf, h);
        return true;
      }
      ++area; x = y = shelf = 0;
    }
    return false;
  }
};

} // namespace

SDL_Color BatchRenderer::unpack(std::uint32_t c, float alpha) {
  const float a = static_cast<float>(c >> 24) * std::clamp(alpha, 0.0f, 1.0f);
  return SDL_Color{static_cast<Uint8>(c & 0xff), static_cast<Uint8>((c >> 8) & 0xff), static_cast<Uint8>((c >> 16) & 0xff), static_cast<Uint8>(a)};
}

bool BatchRenderer::init(SDL_Renderer* renderer, SDL_Surface* atlas, int cell, int cols, SDL_Surface* terrain, TTF_Font* font, std::string& error,
                         bool compact) {
  renderer_ = renderer;
  hasSilhouettes_ = !compact;
  const int terrainH = terrain ? terrain->h : 0;
  const int spriteColumns = compact ? atlas->w : atlas->w * 2; // atlas (+ silhouettes)
  const int width = compact ? 512 : nextPow2(atlas->w * 2 + 256), height = compact ? 512 : nextPow2(atlas->h + terrainH);
  // Compact: the free space left of the 512x512 page must hold the white texel, the glow and every
  // glyph; the glyph loop below reports a shortfall instead of dropping letters silently.
  if (compact && (atlas->w > width || atlas->h + terrainH > height)) { error = "atlas too large for a 512x512 page"; return false; }
  SDL_Surface* sheet = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
  if (!sheet) { error = SDL_GetError(); return false; }
  SDL_FillRect(sheet, nullptr, 0);
  SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
  SDL_BlitSurface(atlas, nullptr, sheet, nullptr);
  // Hit-flash silhouettes: same alpha, pure white. Replaces the web client's baked flash canvases.
  if (hasSilhouettes_) {
    SDL_Rect silhouetteAt{atlas->w, 0, atlas->w, atlas->h};
    SDL_BlitSurface(atlas, nullptr, sheet, &silhouetteAt);
    for (int y = 0; y < atlas->h; ++y) {
      auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(sheet->pixels) + y * sheet->pitch) + atlas->w;
      for (int x = 0; x < atlas->w; ++x) row[x] = (row[x] & 0xff000000u) | 0x00ffffffu;
    }
  }

  const float iw = 1.0f / static_cast<float>(width), ih = 1.0f / static_cast<float>(height);
  auto uv = [&](float x, float y, float w, float h, float inset) {
    return UvRect{(x + inset) * iw, (y + inset) * ih, (x + w - inset) * iw, (y + h - inset) * ih};
  };
  // Half-texel inset keeps linear filtering from sampling the neighbouring cell.
  for (int i = 0; i < static_cast<int>(native::SpriteId::Unknown); ++i) {
    const float x = static_cast<float>((i % cols) * cell), y = static_cast<float>((i / cols) * cell);
    sprites_[static_cast<std::size_t>(i)] = uv(x, y, static_cast<float>(cell), static_cast<float>(cell), 0.5f);
    silhouettes_[static_cast<std::size_t>(i)] = uv(x + static_cast<float>(atlas->w), y, static_cast<float>(cell), static_cast<float>(cell), 0.5f);
  }

  if (terrain && terrain->w <= spriteColumns) {
    SDL_Rect at{0, atlas->h, terrain->w, terrain->h};
    SDL_SetSurfaceBlendMode(terrain, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(terrain, nullptr, sheet, &at);
    terrainCount_ = std::min(static_cast<int>(terrain_.size()), terrain->w / terrain->h);
    for (int i = 0; i < terrainCount_; ++i)
      terrain_[static_cast<std::size_t>(i)] = uv(static_cast<float>(i * terrain->h), static_cast<float>(atlas->h),
                                                 static_cast<float>(terrain->h), static_cast<float>(terrain->h), compact ? 1.5f : 1.0f);
  }

  Packer packer;
  packer.areas[0] = {spriteColumns, 0, width - spriteColumns, height};
  packer.areas[1] = {0, atlas->h + terrainH, spriteColumns, height - atlas->h - terrainH};

  SDL_Rect whiteRect{};
  if (!packer.place(kWhiteSize, kWhiteSize, whiteRect)) { error = "no room for white texel"; SDL_FreeSurface(sheet); return false; }
  SDL_FillRect(sheet, &whiteRect, SDL_MapRGBA(sheet->format, 255, 255, 255, 255));
  const float wc = (static_cast<float>(whiteRect.x) + kWhiteSize * 0.5f) * iw, wr = (static_cast<float>(whiteRect.y) + kWhiteSize * 0.5f) * ih;
  white_ = {wc, wr, wc, wr};

  // Soft radial glow (white centre fading linearly to transparent), tinted per draw: this is the
  // web client's drawSoftGlow / cachedGlow.
  SDL_Rect glowRect{};
  if (!packer.place(kGlowSize, kGlowSize, glowRect)) { error = "no room for glow"; SDL_FreeSurface(sheet); return false; }
  for (int y = 0; y < kGlowSize; ++y) {
    auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(sheet->pixels) + (glowRect.y + y) * sheet->pitch) + glowRect.x;
    for (int x = 0; x < kGlowSize; ++x) {
      const float dx = (x + 0.5f) / (kGlowSize * 0.5f) - 1, dy = (y + 0.5f) / (kGlowSize * 0.5f) - 1;
      const float a = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
      row[x] = SDL_MapRGBA(sheet->format, 255, 255, 255, static_cast<Uint8>(a * 255));
    }
  }
  glow_ = uv(static_cast<float>(glowRect.x), static_cast<float>(glowRect.y), kGlowSize, kGlowSize, 0.5f);

  fontHeight_ = static_cast<float>(TTF_FontHeight(font));
  const SDL_Color white{255, 255, 255, 255};
  // The compact page only has room for what the Portuguese UI writes (PC also shows typed names).
  static constexpr char kCompactLatin1[] = "\xB7\xA9\xD7\xC1\xC0\xC2\xC3\xC7\xC9\xCA\xCD\xD3\xD4\xD5\xDA\xDC\xE1\xE0\xE2\xE3\xE7\xE9\xEA\xED\xF3\xF4\xF5\xFA\xFC";
  for (std::uint32_t cp = 32; cp < 256; ++cp) {
    if (cp >= 127 && cp < 160) continue;
    if (compact && cp >= 160 && !std::strchr(kCompactLatin1, static_cast<char>(cp))) continue;
    if (!TTF_GlyphIsProvided(font, static_cast<Uint16>(cp))) continue;
    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics(font, static_cast<Uint16>(cp), &minx, &maxx, &miny, &maxy, &advance) != 0) continue;
    Glyph& g = glyphs_[cp];
    g.advance = static_cast<float>(advance);
    if (cp == ' ') { g.valid = true; continue; }
    SDL_Surface* bitmap = TTF_RenderGlyph_Blended(font, static_cast<Uint16>(cp), white);
    if (!bitmap) continue;
    SDL_Rect dst;
    if (packer.place(bitmap->w, bitmap->h, dst)) {
      SDL_SetSurfaceBlendMode(bitmap, SDL_BLENDMODE_NONE);
      SDL_BlitSurface(bitmap, nullptr, sheet, &dst);
      g.uv = uv(static_cast<float>(dst.x), static_cast<float>(dst.y), static_cast<float>(dst.w), static_cast<float>(dst.h), 0);
      g.w = static_cast<float>(dst.w); g.h = static_cast<float>(dst.h);
      g.valid = true;
    } else if (compact) {
      error = "no room for glyph " + std::to_string(cp);
      SDL_FreeSurface(bitmap); SDL_FreeSurface(sheet);
      return false;
    }
    SDL_FreeSurface(bitmap);
  }
  if (!glyphs_['?'].valid) { error = "font has no '?' glyph"; SDL_FreeSurface(sheet); return false; }

  texture_ = SDL_CreateTextureFromSurface(renderer, sheet);
  SDL_FreeSurface(sheet);
  if (!texture_) { error = SDL_GetError(); return false; }
  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);

  vertices_.reserve(kMaxVertices);
  indices_.reserve(kMaxIndices);
  return true;
}

void BatchRenderer::shutdown() {
  if (texture_) SDL_DestroyTexture(texture_);
  texture_ = nullptr;
}

void BatchRenderer::begin() {
  vertices_.clear();
  indices_.clear();
  stats_ = {};
  resetTransform();
  additive_ = false;
  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
#if defined(__PSP__) || defined(PSP)
  // SDL2's PSP backend (2.32) activates the texture for textured RenderGeometry but never enables
  // GU_TEXTURE_2D, so textured triangles come out as flat colour whenever texturing was left off.
  // Its untextured geometry path ends with sceGuEnable(GU_TEXTURE_2D): one invisible degenerate
  // triangle per frame puts the GU in the state the textured batches need.
  const SDL_Vertex prime[3] = {{{0, 0}, {0, 0, 0, 0}, {0, 0}}, {{0, 0}, {0, 0, 0, 0}, {0, 0}}, {{0, 0}, {0, 0, 0, 0}, {0, 0}}};
  SDL_RenderGeometry(renderer_, nullptr, prime, 3, nullptr, 0);
#endif
}

void BatchRenderer::flush() {
  if (indices_.empty()) return;
  SDL_RenderGeometry(renderer_, texture_, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(), static_cast<int>(indices_.size()));
  ++stats_.drawCalls;
  stats_.vertices += static_cast<int>(vertices_.size());
  vertices_.clear();
  indices_.clear();
}

void BatchRenderer::setAdditive(bool additive) {
  if (additive == additive_) return;
  flush(); // SDL captures the texture blend mode when the geometry command is queued
  additive_ = additive;
  SDL_SetTextureBlendMode(texture_, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
}

void BatchRenderer::reserve(int vertices, int indices) {
  if (static_cast<int>(vertices_.size()) + vertices > kMaxVertices || static_cast<int>(indices_.size()) + indices > kMaxIndices) flush();
}

void BatchRenderer::rawQuad(const SDL_FPoint (&p)[4], const UvRect& uv, SDL_Color color) {
  if (color.a == 0) return;
  reserve(4, 6);
  const int base = static_cast<int>(vertices_.size());
  vertices_.push_back({p[0], color, {uv.u0, uv.v0}});
  vertices_.push_back({p[1], color, {uv.u1, uv.v0}});
  vertices_.push_back({p[2], color, {uv.u1, uv.v1}});
  vertices_.push_back({p[3], color, {uv.u0, uv.v1}});
  const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
  indices_.insert(indices_.end(), idx, idx + 6);
}

void BatchRenderer::sprite(native::SpriteId id, float x, float y, float size, float rotation, float alpha, float sx, float sy, float flash, std::uint32_t tint) {
  if (id >= native::SpriteId::Unknown || size <= 0 || alpha <= 0) return;
  const float hx = size * 0.5f * sx, hy = size * 0.5f * sy;
  SDL_FPoint p[4];
  if (rotation == 0.0f) {
    p[0] = tx(x - hx, y - hy); p[1] = tx(x + hx, y - hy); p[2] = tx(x + hx, y + hy); p[3] = tx(x - hx, y + hy);
  } else {
    float s, c;
    fastSinCos(rotation, s, c);
    const float cx[4] = {-hx, hx, hx, -hx}, cy[4] = {-hy, -hy, hy, hy};
    for (int i = 0; i < 4; ++i) p[i] = tx(x + cx[i] * c - cy[i] * s, y + cx[i] * s + cy[i] * c);
  }
  const auto index = static_cast<std::size_t>(id);
  if (flash > 0 && !hasSilhouettes_) {
    // No room for silhouettes on a 512x512 page: hits tint the sprite red instead of flashing white.
    const float k = 1 - std::min(1.0f, flash) * 0.55f;
    const std::uint32_t g = static_cast<std::uint32_t>(((tint >> 8) & 0xff) * k), bch = static_cast<std::uint32_t>(((tint >> 16) & 0xff) * k);
    tint = (tint & 0xff0000ffu) | (g << 8) | (bch << 16);
  }
  rawQuad(p, sprites_[index], unpack(tint, alpha));
  if (flash > 0 && hasSilhouettes_) rawQuad(p, silhouettes_[index], unpack(0xffffffffu, alpha * std::min(1.0f, flash) * 0.6f));
}

void BatchRenderer::sprite(const native::SpriteCommand& cmd) {
  sprite(cmd.sprite, cmd.x, cmd.y, cmd.size, cmd.rotation, 1, cmd.flip ? -1.0f : 1.0f, 1, 0, cmd.rgba);
}

void BatchRenderer::icon(native::SpriteId id, float x, float y, float size, std::uint32_t color) {
  sprite(id, x, y, size, 0, 1, 1, 1, 0, color);
}

void BatchRenderer::glow(float x, float y, float radius, std::uint32_t color, float alpha) {
  if (radius <= 0 || alpha <= 0) return;
  const SDL_FPoint p[4] = {tx(x - radius, y - radius), tx(x + radius, y - radius), tx(x + radius, y + radius), tx(x - radius, y + radius)};
  rawQuad(p, glow_, unpack(color, alpha));
}

void BatchRenderer::rect(float x, float y, float w, float h, std::uint32_t color) {
  if (w <= 0 || h <= 0) return;
  const SDL_FPoint p[4] = {tx(x, y), tx(x + w, y), tx(x + w, y + h), tx(x, y + h)};
  rawQuad(p, white_, unpack(color));
}

void BatchRenderer::rectGradient(float x, float y, float w, float h, std::uint32_t tl, std::uint32_t tr, std::uint32_t br, std::uint32_t bl) {
  if (w <= 0 || h <= 0) return;
  reserve(4, 6);
  const int base = static_cast<int>(vertices_.size());
  const SDL_FPoint uv{white_.u0, white_.v0};
  vertices_.push_back({tx(x, y), unpack(tl), uv});
  vertices_.push_back({tx(x + w, y), unpack(tr), uv});
  vertices_.push_back({tx(x + w, y + h), unpack(br), uv});
  vertices_.push_back({tx(x, y + h), unpack(bl), uv});
  const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
  indices_.insert(indices_.end(), idx, idx + 6);
}

void BatchRenderer::frame(float x, float y, float w, float h, float border, std::uint32_t color) {
  rect(x, y, w, border, color);
  rect(x, y + h - border, w, border, color);
  rect(x, y + border, border, h - border * 2, color);
  rect(x + w - border, y + border, border, h - border * 2, color);
}

void BatchRenderer::line(float x1, float y1, float x2, float y2, float width, std::uint32_t color) {
  const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.01f || width <= 0) return;
  const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
  const SDL_FPoint p[4] = {tx(x1 + nx, y1 + ny), tx(x2 + nx, y2 + ny), tx(x2 - nx, y2 - ny), tx(x1 - nx, y1 - ny)};
  rawQuad(p, white_, unpack(color));
}

void BatchRenderer::quadGradient(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                                 std::uint32_t c0, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3) {
  reserve(4, 6);
  const int base = static_cast<int>(vertices_.size());
  const SDL_FPoint uv{white_.u0, white_.v0};
  vertices_.push_back({tx(x0, y0), unpack(c0), uv});
  vertices_.push_back({tx(x1, y1), unpack(c1), uv});
  vertices_.push_back({tx(x2, y2), unpack(c2), uv});
  vertices_.push_back({tx(x3, y3), unpack(c3), uv});
  const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
  indices_.insert(indices_.end(), idx, idx + 6);
}

void BatchRenderer::lineGradient(float x1, float y1, float x2, float y2, float width, std::uint32_t c1, std::uint32_t c2) {
  const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.01f || width <= 0) return;
  const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
  quadGradient(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, c1, c2, c2, c1);
}

void BatchRenderer::triangle(float x0, float y0, float x1, float y1, float x2, float y2, std::uint32_t color) {
  const SDL_Color c = unpack(color);
  if (c.a == 0) return;
  reserve(3, 3);
  const int base = static_cast<int>(vertices_.size());
  const SDL_FPoint uv{white_.u0, white_.v0};
  vertices_.push_back({tx(x0, y0), c, uv});
  vertices_.push_back({tx(x1, y1), c, uv});
  vertices_.push_back({tx(x2, y2), c, uv});
  const int idx[3] = {base, base + 1, base + 2};
  indices_.insert(indices_.end(), idx, idx + 3);
}

void BatchRenderer::quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, std::uint32_t color) {
  const SDL_FPoint p[4] = {tx(x0, y0), tx(x1, y1), tx(x2, y2), tx(x3, y3)};
  rawQuad(p, white_, unpack(color));
}

void BatchRenderer::ring(float x, float y, float rx, float ry, float a0, float a1, int segments, std::uint32_t color, float thickness, bool closed) {
  const SDL_Color c = unpack(color);
  if (c.a == 0 || rx <= 0.1f) return;
  const SDL_FPoint uv{white_.u0, white_.v0};
  const float step = (a1 - a0) / static_cast<float>(segments);
  const int points = closed ? segments : segments + 1;
  // Points come from rotating a unit vector by `step`: 4 trig calls per ring instead of 2 per
  // point (sin/cos are software routines on the PSP). Drift over <= 49 steps is negligible.
  float ca, sa, cs, ss;
  fastSinCos(a0, sa, ca);
  fastSinCos(step, ss, cs);
  auto advance = [&] { const float n = ca * cs - sa * ss; sa = sa * cs + ca * ss; ca = n; };
  if (thickness <= 0) { // filled disc / ellipse / pie
    reserve(points + 1, segments * 3);
    const int center = static_cast<int>(vertices_.size());
    vertices_.push_back({tx(x, y), c, uv});
    for (int i = 0; i < points; ++i, advance()) vertices_.push_back({tx(x + ca * rx, y + sa * ry), c, uv});
    for (int i = 0; i < segments; ++i) {
      const int idx[3] = {center, center + 1 + i, center + 1 + (i + 1) % points};
      indices_.insert(indices_.end(), idx, idx + 3);
    }
    return;
  }
  const float half = thickness * 0.5f;
  reserve(points * 2, segments * 6);
  const int base = static_cast<int>(vertices_.size());
  for (int i = 0; i < points; ++i, advance()) {
    vertices_.push_back({tx(x + ca * (rx + half), y + sa * (ry + half)), c, uv});
    vertices_.push_back({tx(x + ca * std::max(0.0f, rx - half), y + sa * std::max(0.0f, ry - half)), c, uv});
  }
  for (int i = 0; i < segments; ++i) {
    const int o0 = base + i * 2, i0 = o0 + 1, o1 = base + ((i + 1) % points) * 2, i1 = o1 + 1;
    const int idx[6] = {o0, o1, i1, o0, i1, i0};
    indices_.insert(indices_.end(), idx, idx + 6);
  }
}

namespace {
int segmentsFor(float screenRadius) {
  return screenRadius < 8 ? 10 : screenRadius < 20 ? 14 : screenRadius < 60 ? 22 : screenRadius < 160 ? 32 : 48;
}
} // namespace

void BatchRenderer::circle(float x, float y, float radius, std::uint32_t color, float thickness) {
  if (radius <= 0.3f) return;
  ring(x, y, radius, radius, 0, static_cast<float>(2 * PI), segmentsFor(radius * scale_), color, thickness, true);
}

void BatchRenderer::ellipse(float x, float y, float rx, float ry, std::uint32_t color, float thickness, float rotation) {
  if (rx <= 0.3f || ry <= 0.3f) return;
  if (rotation == 0.0f) { ring(x, y, rx, ry, 0, static_cast<float>(2 * PI), segmentsFor(std::max(rx, ry) * scale_), color, thickness, true); return; }
  // Rotated ellipses are rare (leaves, petals): build them as a path.
  const int n = 16;
  float s, c;
  fastSinCos(rotation, s, c);
  beginPath();
  for (int i = 0; i < n; ++i) {
    float sa, ca;
    fastSinCos(static_cast<float>(i) * static_cast<float>(2 * PI) / n, sa, ca);
    const float ex = ca * rx, ey = sa * ry;
    lineTo(x + ex * c - ey * s, y + ex * s + ey * c);
  }
  if (thickness > 0) stroke(thickness, color, true); else fill(color);
}

void BatchRenderer::arc(float x, float y, float rx, float ry, float a0, float a1, std::uint32_t color, float thickness) {
  const float span = std::abs(a1 - a0);
  if (span <= 0.001f) return;
  const int segments = std::max(3, static_cast<int>(segmentsFor(std::max(rx, ry) * scale_) * span / static_cast<float>(2 * PI)) + 1);
  ring(x, y, rx, ry, a0, a1, segments, color, thickness, false);
}

void BatchRenderer::dashedCircle(float x, float y, float radius, float dash, float gap, float offset, std::uint32_t color, float thickness) {
  if (radius <= 1) return;
  const float period = dash + gap;
  const float circumference = static_cast<float>(2 * PI) * radius;
  float start = std::fmod(offset, period);
  if (start > 0) start -= period;
  for (float d = start; d < circumference; d += period) {
    const float from = std::max(0.0f, d), to = std::min(circumference, d + dash);
    if (to <= from) continue;
    ring(x, y, radius, radius, from / radius, to / radius, 3, color, thickness, false);
  }
}

void BatchRenderer::stroke(float width, std::uint32_t color, bool closed) {
  const std::size_t n = path_.size();
  if (n < 2) return;
  const std::size_t segments = closed ? n : n - 1;
  for (std::size_t i = 0; i < segments; ++i) {
    const SDL_FPoint a = path_[i], b = path_[(i + 1) % n];
    // Square caps (extend by half the width) hide the gaps at polyline joints.
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) continue;
    const float ex = dx / len * width * 0.5f, ey = dy / len * width * 0.5f;
    line(a.x - ex, a.y - ey, b.x + ex, b.y + ey, width, color);
  }
}

void BatchRenderer::fill(std::uint32_t color) {
  const std::size_t n = path_.size();
  if (n < 3) return;
  const SDL_Color c = unpack(color);
  if (c.a == 0) return;
  float cx = 0, cy = 0;
  for (const auto& p : path_) { cx += p.x; cy += p.y; }
  cx /= static_cast<float>(n); cy /= static_cast<float>(n);
  reserve(static_cast<int>(n) + 1, static_cast<int>(n) * 3);
  const SDL_FPoint uv{white_.u0, white_.v0};
  const int center = static_cast<int>(vertices_.size());
  vertices_.push_back({tx(cx, cy), c, uv});
  for (const auto& p : path_) vertices_.push_back({tx(p.x, p.y), c, uv});
  for (std::size_t i = 0; i < n; ++i) {
    const int idx[3] = {center, center + 1 + static_cast<int>(i), center + 1 + static_cast<int>((i + 1) % n)};
    indices_.insert(indices_.end(), idx, idx + 3);
  }
}

bool BatchRenderer::terrain(int phase, float screenW, float screenH) {
  if (terrainCount_ == 0) return false;
  const UvRect& uv = terrain_[static_cast<std::size_t>(std::clamp(phase, 0, terrainCount_ - 1))];
  const float x0 = (0 - ox_) / scale_, y0 = (0 - oy_) / scale_, x1 = (screenW - ox_) / scale_, y1 = (screenH - oy_) / scale_;
  const SDL_Color white{255, 255, 255, 255};
  for (float y = std::floor(y0 / kTerrainTile) * kTerrainTile; y < y1; y += kTerrainTile) {
    for (float x = std::floor(x0 / kTerrainTile) * kTerrainTile; x < x1; x += kTerrainTile) {
      // One extra unit of overlap hides rasterizer cracks between tiles at fractional zoom.
      const float e = kTerrainTile + 1;
      const SDL_FPoint p[4] = {tx(x, y), tx(x + e, y), tx(x + e, y + e), tx(x, y + e)};
      rawQuad(p, uv, white);
    }
  }
  return true;
}

const BatchRenderer::Glyph& BatchRenderer::glyph(std::uint32_t cp) const {
  if (cp < glyphs_.size() && glyphs_[cp].valid) return glyphs_[cp];
  return glyphs_['?'];
}

float BatchRenderer::textWidth(std::string_view s, float px) const {
  const float scale = px / fontHeight_;
  float w = 0;
  for (std::size_t i = 0; i < s.size();) w += glyph(decodeUtf8(s, i)).advance;
  return w * scale;
}

float BatchRenderer::text(float x, float y, std::string_view s, float px, std::uint32_t color, Align align) {
  const float width = textWidth(s, px);
  if (align == Align::Center) x -= width * 0.5f;
  else if (align == Align::Right) x -= width;
  const SDL_Color c = unpack(color);
  if (c.a == 0) return width;
  const float scale = px / fontHeight_;
  for (std::size_t i = 0; i < s.size();) {
    const Glyph& g = glyph(decodeUtf8(s, i));
    if (g.w > 0) {
      const float w = g.w * scale, h = g.h * scale;
      const SDL_FPoint p[4] = {tx(x, y), tx(x + w, y), tx(x + w, y + h), tx(x, y + h)};
      rawQuad(p, g.uv, c);
    }
    x += g.advance * scale;
  }
  return width;
}

float BatchRenderer::textOutlined(float x, float y, std::string_view s, float px, std::uint32_t color, std::uint32_t outline, Align align) {
  const float o = std::max(1.0f / scale_, px * 0.07f);
  static constexpr float offsets[8][2] = {{-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  const int passes = hasSilhouettes_ ? 8 : 4; // compact (PSP) pages: diagonals only, half the glyph quads
  for (int i = 0; i < passes; ++i) text(x + offsets[i][0] * o, y + offsets[i][1] * o, s, px, outline, align);
  return text(x, y, s, px, color, align);
}

float BatchRenderer::textWrapped(float x, float y, float maxWidth, std::string_view s, float px, std::uint32_t color, int maxLines, Align align) {
  const float lineHeight = px * 1.15f;
  int lines = 0;
  std::size_t start = 0;
  while (start < s.size() && lines < maxLines) {
    std::size_t end = start, lastFit = start;
    while (end <= s.size()) {
      const std::size_t next = s.find(' ', end);
      const std::size_t wordEnd = next == std::string_view::npos ? s.size() : next;
      if (textWidth(s.substr(start, wordEnd - start), px) > maxWidth && lastFit > start) break;
      lastFit = wordEnd;
      if (wordEnd == s.size()) break;
      end = wordEnd + 1;
    }
    const float lx = align == Align::Center ? x + maxWidth * 0.5f : x;
    text(lx, y + lines * lineHeight, s.substr(start, lastFit - start), px, color, align);
    ++lines;
    start = lastFit + 1;
  }
  return lines * lineHeight;
}

} // namespace arcana::sdl
