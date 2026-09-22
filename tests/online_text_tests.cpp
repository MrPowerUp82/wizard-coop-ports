// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "text_entry.hpp"

#include <cassert>
#include <cstdio>

using namespace arcana::online;

int main() {
  // Room codes: upper-cased, only the server's alphabet (no I, O, 0, 1), exactly 6 characters.
  {
    TextEntry code(TextKind::RoomCode);
    code.type("ab1c-io2345xyz");
    assert(code.value() == "ABC234");
    assert(code.valid());
    code.backspace();
    assert(code.value() == "ABC23" && !code.valid());
    assert(code.keyCount() == 34 && code.keyLabel(0) == "A" && code.keyLabel(32) == "DEL" && code.keyLabel(33) == "OK");
  }
  // Gamepad grid: move, press a key, DEL and OK.
  {
    TextEntry code(TextKind::RoomCode);
    code.press();                                      // cursor starts on "A"
    code.move(1, 0); code.press();                     // "B"
    assert(code.value() == "AB");
    code.move(-2, 0);                                  // wraps inside the row: last key of row 0
    assert(code.cursor() == TextEntry::kColumns - 1);
    code.move(0, 5);                                   // clamps to the last row
    assert(code.cursor() >= 30);
    while (code.keyLabel(code.cursor()) != "DEL") code.move(1, 0);
    code.press();
    assert(code.value() == "A");
    code.type("BC234");
    while (code.keyLabel(code.cursor()) != "OK") code.move(1, 0);
    code.press();
    assert(code.takeSubmit() && !code.takeSubmit());
  }
  // Names: any printable text, 16 codepoints, trimmed for validity.
  {
    TextEntry name(TextKind::Name, "Ana");
    assert(name.valid());
    name.type(" L\xC3\xBA\x07");
    assert(name.value() == "Ana L\xC3\xBA");
    name.backspace();
    assert(name.value() == "Ana L");
    name.type("12345678901234567890");
    assert(name.value() == "Ana L12345678901");
    TextEntry blank(TextKind::Name, "   ");
    assert(!blank.valid());
    while (blank.keyLabel(blank.cursor()) != "OK") blank.move(1, 0); // OK on an invalid value does not submit
    blank.press();
    assert(!blank.takeSubmit());
    assert(TextEntry(TextKind::Name).keyLabel(62) == "ESP");
  }
  std::puts("online_text: ok");
  return 0;
}
