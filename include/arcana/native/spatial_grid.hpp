#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arcana::native {

// Allocation-free uniform hash grid for short-range entity queries.
// Each item is stored exactly once; buckets are collision-resolved with open
// addressing and each bucket keeps a linked list of item indices.
template <class T, std::size_t MaxItems, std::size_t BucketCount = 512>
class SpatialGrid {
  static_assert((BucketCount & (BucketCount - 1)) == 0, "BucketCount must be a power of two");
  static_assert(MaxItems <= static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()));

public:
  explicit constexpr SpatialGrid(float cellSize = 96.0f) noexcept : cellSize_(cellSize) { clear(); }

  void clear() noexcept {
    for (auto& slot : slots_) slot.head = kUnused;
    data_ = nullptr;
    count_ = 0;
  }

  template <class Container, class Pred>
  void rebuild(Container& items, Pred&& include) noexcept {
    clear();
    data_ = items.data();
    count_ = items.size() < MaxItems ? items.size() : MaxItems;
    for (std::size_t i = 0; i < count_; ++i) {
      next_[i] = kEnd;
      if (!include(data_[i])) continue;
      insert(i, static_cast<float>(data_[i].x), static_cast<float>(data_[i].y));
    }
  }

  template <class Container>
  void rebuild(Container& items) noexcept {
    rebuild(items, [](const T&) { return true; });
  }

  template <class Fn>
  void query(float x, float y, float radius, Fn&& visit) const noexcept(noexcept(visit(*data_, 0.0f))) {
    if (!data_ || count_ == 0 || radius < 0.0f) return;
    const float r2 = radius * radius;
    const int x0 = cellOf(x - radius), x1 = cellOf(x + radius);
    const int y0 = cellOf(y - radius), y1 = cellOf(y + radius);
    for (int cy = y0; cy <= y1; ++cy) {
      for (int cx = x0; cx <= x1; ++cx) {
        const Slot* slot = find(cx, cy);
        if (!slot) continue;
        for (std::int16_t idx = slot->head; idx != kEnd; idx = next_[static_cast<std::size_t>(idx)]) {
          T& item = data_[static_cast<std::size_t>(idx)];
          const float dx = static_cast<float>(item.x) - x;
          const float dy = static_cast<float>(item.y) - y;
          const float d2 = dx * dx + dy * dy;
          if (d2 <= r2 && visit(item, d2)) return;
        }
      }
    }
  }

  [[nodiscard]] float cellSize() const noexcept { return cellSize_; }

private:
  static constexpr std::int16_t kUnused = -2;
  static constexpr std::int16_t kEnd = -1;

  struct Slot {
    int cx{};
    int cy{};
    std::int16_t head{kUnused};
  };

  [[nodiscard]] int cellOf(float v) const noexcept {
    return static_cast<int>(std::floor(v / cellSize_));
  }

  [[nodiscard]] static std::size_t hashCell(int x, int y) noexcept {
    std::uint32_t hx = static_cast<std::uint32_t>(x) * 0x8da6b343u;
    std::uint32_t hy = static_cast<std::uint32_t>(y) * 0xd8163841u;
    std::uint32_t h = hx ^ hy;
    h ^= h >> 16;
    return static_cast<std::size_t>(h) & (BucketCount - 1);
  }

  Slot* findOrCreate(int cx, int cy) noexcept {
    std::size_t pos = hashCell(cx, cy);
    for (std::size_t n = 0; n < BucketCount; ++n) {
      Slot& slot = slots_[pos];
      if (slot.head == kUnused) {
        slot.cx = cx;
        slot.cy = cy;
        slot.head = kEnd;
        return &slot;
      }
      if (slot.cx == cx && slot.cy == cy) return &slot;
      pos = (pos + 1) & (BucketCount - 1);
    }
    return nullptr;
  }

  [[nodiscard]] const Slot* find(int cx, int cy) const noexcept {
    std::size_t pos = hashCell(cx, cy);
    for (std::size_t n = 0; n < BucketCount; ++n) {
      const Slot& slot = slots_[pos];
      if (slot.head == kUnused) return nullptr;
      if (slot.cx == cx && slot.cy == cy) return &slot;
      pos = (pos + 1) & (BucketCount - 1);
    }
    return nullptr;
  }

  void insert(std::size_t index, float x, float y) noexcept {
    Slot* slot = findOrCreate(cellOf(x), cellOf(y));
    if (!slot) return;
    next_[index] = slot->head;
    slot->head = static_cast<std::int16_t>(index);
  }

  float cellSize_{96.0f};
  T* data_{};
  std::size_t count_{};
  std::array<Slot, BucketCount> slots_{};
  std::array<std::int16_t, MaxItems> next_{};
};

} // namespace arcana::native
