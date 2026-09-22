#include "text_entry.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace arcana::online {
namespace {
std::size_t sequenceLength(unsigned char lead) {
  return lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
}
} // namespace

TextEntry::TextEntry(TextKind kind, std::string initial) : kind_(kind) { type(initial); }

std::size_t TextEntry::length() const {
  std::size_t n = 0;
  for (std::size_t i = 0; i < value_.size(); i += sequenceLength(static_cast<unsigned char>(value_[i]))) ++n;
  return n;
}

void TextEntry::type(std::string_view in) {
  for (std::size_t i = 0; i < in.size();) {
    const auto lead = static_cast<unsigned char>(in[i]);
    const std::size_t len = sequenceLength(lead);
    if (i + len > in.size()) break;
    const std::string_view ch = in.substr(i, len);
    i += len;
    if (length() >= maxLength()) break;
    if (kind_ == TextKind::RoomCode) {
      const char c = static_cast<char>(std::toupper(lead));
      if (len == 1 && kRoomAlphabet.find(c) != std::string_view::npos) value_ += c;
    } else if (!(len == 1 && (lead < 0x20 || lead == 0x7F))) {
      value_.append(ch);
    }
  }
}

void TextEntry::backspace() {
  if (value_.empty()) return;
  std::size_t at = value_.size() - 1;
  while (at > 0 && (static_cast<unsigned char>(value_[at]) & 0xC0) == 0x80) --at; // start of the last character
  value_.erase(at);
}

void TextEntry::move(int dx, int dy) {
  const int count = keyCount();
  const int rows = (count + kColumns - 1) / kColumns;
  int row = cursor_ / kColumns, col = cursor_ % kColumns;
  row = std::clamp(row + dy, 0, rows - 1);
  const int inRow = std::min(kColumns, count - row * kColumns);
  col = ((col + dx) % inRow + inRow) % inRow;
  cursor_ = std::min(row * kColumns + col, count - 1);
}

void TextEntry::press() {
  const int letters = static_cast<int>(alphabet().size());
  if (cursor_ < letters) type(alphabet().substr(static_cast<std::size_t>(cursor_), 1));
  else if (cursor_ == letters) backspace();
  else if (valid()) submit_ = true;
}

bool TextEntry::takeSubmit() { return std::exchange(submit_, false); }

bool TextEntry::valid() const {
  if (kind_ == TextKind::RoomCode) return length() == 6;
  return value_.find_first_not_of(' ') != std::string::npos;
}

std::string TextEntry::keyLabel(int index) const {
  const int letters = static_cast<int>(alphabet().size());
  if (index == letters) return "DEL";
  if (index == letters + 1) return "OK";
  const char c = alphabet()[static_cast<std::size_t>(index)];
  return c == ' ' ? "ESP" : std::string(1, c);
}

} // namespace arcana::online
