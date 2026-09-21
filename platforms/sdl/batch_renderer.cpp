#include "batch_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::sdl {
namespace {

constexpr int kMaxVertices = 32768;
constexpr int kMaxIndices = kMaxVertices * 3 / 2;
constexpr int kWhiteSize = 4;
constexpr int kGlyphPadding = 2;

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

// Shelf packer over up to two free rectangles (right of and below the sprite atlas).
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

bool BatchRenderer::init(SDL_Renderer* renderer, SDL_Surface* atlas, int cell, int cols, TTF_Font* font, std::string& error) {
  renderer_ = renderer;
  const int size = nextPow2(std::max(atlas->w + 256, atlas->h));
  SDL_Surface* sheet = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ABGR8888);
  if (!sheet) { error = SDL_GetError(); return false; }
  SDL_FillRect(sheet, nullptr, 0);
  SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
  SDL_BlitSurface(atlas, nullptr, sheet, nullptr);

  const float inv = 1.0f / static_cast<float>(size);
  // Half-texel inset keeps linear filtering from sampling the neighbouring cell.
  for (int i = 0; i < static_cast<int>(native::SpriteId::Unknown); ++i) {
    const float x = static_cast<float>((i % cols) * cell), y = static_cast<float>((i / cols) * cell);
    sprites_[static_cast<std::size_t>(i)] = {(x + 0.5f) * inv, (y + 0.5f) * inv, (x + cell - 0.5f) * inv, (y + cell - 0.5f) * inv};
  }

  Packer packer;
  packer.areas[0] = {atlas->w, 0, size - atlas->w, size};
  packer.areas[1] = {0, atlas->h, atlas->w, size - atlas->h};
  SDL_Rect whiteRect{};
  if (!packer.place(kWhiteSize, kWhiteSize, whiteRect)) { error = "no room for white texel"; SDL_FreeSurface(sheet); return false; }
  SDL_FillRect(sheet, &whiteRect, SDL_MapRGBA(sheet->format, 255, 255, 255, 255));
  const float wc = (static_cast<float>(whiteRect.x) + kWhiteSize * 0.5f) * inv, wr = (static_cast<float>(whiteRect.y) + kWhiteSize * 0.5f) * inv;
  white_ = {wc, wr, wc, wr};

  fontHeight_ = static_cast<float>(TTF_FontHeight(font));
  const SDL_Color white{255, 255, 255, 255};
  for (std::uint32_t cp = 32; cp < 256; ++cp) {
    if (cp >= 127 && cp < 160) continue;
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
      g.uv = {dst.x * inv, dst.y * inv, (dst.x + dst.w) * inv, (dst.y + dst.h) * inv};
      g.w = static_cast<float>(dst.w); g.h = static_cast<float>(dst.h);
      g.valid = true;
    }
    SDL_FreeSurface(bitmap);
  }
  if (!glyphs_['?'].valid) { error = "font has no '?' glyph"; SDL_FreeSurface(sheet); return false; }

  texture_ = SDL_CreateTextureFromSurface(renderer, sheet);
  SDL_FreeSurface(sheet);
  if (!texture_) { error = SDL_GetError(); return false; }
  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);

  for (std::size_t i = 0; i < unitCircle_.size(); ++i) {
    const double a = static_cast<double>(i) * 2 * PI / 96.0;
    unitCircle_[i] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
  }
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
}

void BatchRenderer::flush() {
  if (indices_.empty()) return;
  SDL_RenderGeometry(renderer_, texture_, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(), static_cast<int>(indices_.size()));
  ++stats_.drawCalls;
  stats_.vertices += static_cast<int>(vertices_.size());
  vertices_.clear();
  indices_.clear();
}

void BatchRenderer::reserve(int vertices, int indices) {
  if (static_cast<int>(vertices_.size()) + vertices > kMaxVertices || static_cast<int>(indices_.size()) + indices > kMaxIndices) flush();
}

void BatchRenderer::quad(const SDL_FPoint (&p)[4], const UvRect& uv, SDL_Color color) {
  reserve(4, 6);
  const int base = static_cast<int>(vertices_.size());
  vertices_.push_back({p[0], color, {uv.u0, uv.v0}});
  vertices_.push_back({p[1], color, {uv.u1, uv.v0}});
  vertices_.push_back({p[2], color, {uv.u1, uv.v1}});
  vertices_.push_back({p[3], color, {uv.u0, uv.v1}});
  const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
  indices_.insert(indices_.end(), idx, idx + 6);
}

void BatchRenderer::sprite(const native::SpriteCommand& cmd) {
  if (cmd.sprite >= native::SpriteId::Unknown || cmd.size <= 0) return;
  UvRect uv = sprites_[static_cast<std::size_t>(cmd.sprite)];
  if (cmd.flip) std::swap(uv.u0, uv.u1);
  const float h = cmd.size * 0.5f;
  SDL_FPoint p[4];
  if (cmd.rotation == 0.0f) {
    p[0] = {cmd.x - h, cmd.y - h}; p[1] = {cmd.x + h, cmd.y - h}; p[2] = {cmd.x + h, cmd.y + h}; p[3] = {cmd.x - h, cmd.y + h};
  } else {
    const float c = std::cos(cmd.rotation) * h, s = std::sin(cmd.rotation) * h;
    p[0] = {cmd.x - c + s, cmd.y - s - c}; p[1] = {cmd.x + c + s, cmd.y + s - c};
    p[2] = {cmd.x + c - s, cmd.y + s + c}; p[3] = {cmd.x - c - s, cmd.y - s + c};
  }
  quad(p, uv, unpack(cmd.rgba));
}

void BatchRenderer::icon(native::SpriteId id, float x, float y, float size, std::uint32_t color) {
  sprite({id, x, y, size, 0, color, 0, false});
}

void BatchRenderer::rect(float x, float y, float w, float h, std::uint32_t color) {
  if (w <= 0 || h <= 0) return;
  const SDL_FPoint p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
  quad(p, white_, unpack(color));
}

void BatchRenderer::frame(float x, float y, float w, float h, float border, std::uint32_t color) {
  rect(x, y, w, border, color);
  rect(x, y + h - border, w, border, color);
  rect(x, y + border, border, h - border * 2, color);
  rect(x + w - border, y + border, border, h - border * 2, color);
}

void BatchRenderer::line(float x1, float y1, float x2, float y2, float width, std::uint32_t color) {
  const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.01f) return;
  const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
  const SDL_FPoint p[4] = {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}};
  quad(p, white_, unpack(color));
}

void BatchRenderer::circle(float x, float y, float radius, std::uint32_t color, float thickness) {
  if (radius <= 0.5f) return;
  const int segments = radius < 12 ? 12 : radius < 30 ? 16 : radius < 80 ? 24 : radius < 200 ? 32 : 48;
  const int step = 96 / segments;
  const SDL_Color c = unpack(color);
  const SDL_FPoint uv{white_.u0, white_.v0};
  if (thickness <= 0) {
    reserve(segments + 1, segments * 3);
    const int center = static_cast<int>(vertices_.size());
    vertices_.push_back({{x, y}, c, uv});
    for (int i = 0; i < segments; ++i) {
      const auto& d = unitCircle_[static_cast<std::size_t>(i * step)];
      vertices_.push_back({{x + d.x * radius, y + d.y * radius}, c, uv});
    }
    for (int i = 0; i < segments; ++i) {
      const int a = center + 1 + i, b = center + 1 + (i + 1) % segments;
      const int idx[3] = {center, a, b};
      indices_.insert(indices_.end(), idx, idx + 3);
    }
    return;
  }
  const float inner = std::max(0.0f, radius - thickness);
  reserve(segments * 2, segments * 6);
  const int base = static_cast<int>(vertices_.size());
  for (int i = 0; i < segments; ++i) {
    const auto& d = unitCircle_[static_cast<std::size_t>(i * step)];
    vertices_.push_back({{x + d.x * radius, y + d.y * radius}, c, uv});
    vertices_.push_back({{x + d.x * inner, y + d.y * inner}, c, uv});
  }
  for (int i = 0; i < segments; ++i) {
    const int o0 = base + i * 2, i0 = o0 + 1, o1 = base + ((i + 1) % segments) * 2, i1 = o1 + 1;
    const int idx[6] = {o0, o1, i1, o0, i1, i0};
    indices_.insert(indices_.end(), idx, idx + 6);
  }
}

void BatchRenderer::queue(const native::RenderQueue& q) {
  for (const auto& c : q.circles) if (c.layer < native::kSpriteLayerMin) circle(c.x, c.y, c.radius, c.rgba, c.thickness);
  for (const auto& s : q.sprites) sprite(s);
  for (const auto& c : q.circles) if (c.layer >= native::kSpriteLayerMin) circle(c.x, c.y, c.radius, c.rgba, c.thickness);
  for (const auto& l : q.lines) line(l.x1, l.y1, l.x2, l.y2, l.width, l.rgba);
  for (const auto& b : q.bars) rect(b.x, b.y, b.w, b.h, b.rgba);
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
  const float scale = px / fontHeight_;
  const float width = textWidth(s, px);
  if (align == Align::Center) x -= width * 0.5f;
  else if (align == Align::Right) x -= width;
  const SDL_Color c = unpack(color);
  for (std::size_t i = 0; i < s.size();) {
    const Glyph& g = glyph(decodeUtf8(s, i));
    if (g.w > 0) {
      const SDL_FPoint p[4] = {{x, y}, {x + g.w * scale, y}, {x + g.w * scale, y + g.h * scale}, {x, y + g.h * scale}};
      quad(p, g.uv, c);
    }
    x += g.advance * scale;
  }
  return width;
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
