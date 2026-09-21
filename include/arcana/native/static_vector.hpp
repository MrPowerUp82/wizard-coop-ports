#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace arcana::native {

// Fixed-capacity vector used by the console runtime. It never allocates after
// construction and exposes contiguous iterators so STL algorithms keep working.
template <class T, std::size_t Capacity>
class StaticVector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr StaticVector() = default;

  StaticVector(std::initializer_list<T> init) { assign(init); }

  StaticVector& operator=(std::initializer_list<T> init) {
    assign(init);
    return *this;
  }

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }

  constexpr T* data() noexcept { return storage_.data(); }
  constexpr const T* data() const noexcept { return storage_.data(); }

  constexpr iterator begin() noexcept { return storage_.data(); }
  constexpr const_iterator begin() const noexcept { return storage_.data(); }
  constexpr const_iterator cbegin() const noexcept { return storage_.data(); }
  constexpr iterator end() noexcept { return storage_.data() + size_; }
  constexpr const_iterator end() const noexcept { return storage_.data() + size_; }
  constexpr const_iterator cend() const noexcept { return storage_.data() + size_; }

  constexpr T& operator[](size_type i) noexcept { return storage_[i]; }
  constexpr const T& operator[](size_type i) const noexcept { return storage_[i]; }
  T& at(size_type i) { if (i >= size_) throw std::out_of_range("StaticVector"); return storage_[i]; }
  const T& at(size_type i) const { if (i >= size_) throw std::out_of_range("StaticVector"); return storage_[i]; }
  constexpr T& front() noexcept { return storage_[0]; }
  constexpr const T& front() const noexcept { return storage_[0]; }
  constexpr T& back() noexcept { return storage_[size_ - 1]; }
  constexpr const T& back() const noexcept { return storage_[size_ - 1]; }

  // Compatibility with code that previously used std::vector::reserve().
  constexpr void reserve(size_type requested) {
    if (requested > Capacity) throw std::length_error("StaticVector capacity exceeded");
  }

  constexpr void clear() noexcept { size_ = 0; }

  bool push_back(const T& value) {
    if (full()) return false;
    storage_[size_++] = value;
    return true;
  }

  bool push_back(T&& value) {
    if (full()) return false;
    storage_[size_++] = std::move(value);
    return true;
  }

  template <class... Args>
  T* emplace_back(Args&&... args) {
    if (full()) return nullptr;
    storage_[size_] = T(std::forward<Args>(args)...);
    return &storage_[size_++];
  }

  void pop_back() noexcept { if (size_) --size_; }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    const auto firstIndex = static_cast<size_type>(first - cbegin());
    const auto lastIndex = static_cast<size_type>(last - cbegin());
    if (firstIndex > size_ || lastIndex > size_ || firstIndex > lastIndex) return end();
    const auto removed = lastIndex - firstIndex;
    for (size_type i = firstIndex; i + removed < size_; ++i)
      storage_[i] = std::move(storage_[i + removed]);
    size_ -= removed;
    return begin() + firstIndex;
  }

  void resize(size_type n) {
    if (n > Capacity) throw std::length_error("StaticVector capacity exceeded");
    if (n > size_) for (size_type i = size_; i < n; ++i) storage_[i] = T{};
    size_ = n;
  }

private:
  void assign(std::initializer_list<T> init) {
    if (init.size() > Capacity) throw std::length_error("StaticVector capacity exceeded");
    clear();
    for (const auto& item : init) push_back(item);
  }

  std::array<T, Capacity> storage_{};
  size_type size_{};
};

} // namespace arcana::native
