#pragma once

#include <string>
#include <string_view>

namespace arcana::online {

enum class TextKind { RoomCode, Name };

// Text typed on a keyboard (UTF-8 from SDL_TEXTINPUT) or picked on an on-screen grid with a gamepad
// (Steam Deck). Room codes use only the server's alphabet; names take any printable text.
class TextEntry {
public:
  static constexpr int kColumns = 10;
  static constexpr std::string_view kRoomAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static constexpr std::string_view kNameAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";

  explicit TextEntry(TextKind kind = TextKind::Name, std::string initial = {});
  void type(std::string_view utf8);
  void backspace();
  // Grid: dx wraps inside the row, dy moves between rows (clamped).
  void move(int dx, int dy);
  // Grid: types the key under the cursor, or DEL / OK.
  void press();
  // True once after OK was pressed on a valid value.
  bool takeSubmit();
  [[nodiscard]] const std::string& value() const { return value_; }
  [[nodiscard]] bool valid() const;
  [[nodiscard]] int cursor() const { return cursor_; }
  [[nodiscard]] int keyCount() const { return static_cast<int>(alphabet().size()) + 2; }
  [[nodiscard]] std::string keyLabel(int index) const;
  [[nodiscard]] TextKind kind() const { return kind_; }
  [[nodiscard]] std::size_t maxLength() const { return kind_ == TextKind::RoomCode ? 6 : 16; }

private:
  [[nodiscard]] std::string_view alphabet() const { return kind_ == TextKind::RoomCode ? kRoomAlphabet : kNameAlphabet; }
  [[nodiscard]] std::size_t length() const;
  TextKind kind_;
  std::string value_;
  int cursor_{};
  bool submit_{};
};

} // namespace arcana::online
