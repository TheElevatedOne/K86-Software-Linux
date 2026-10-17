// Macro storage format (256 bytes per macro slot, see docs/PROTOCOL.md).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace k86 {

constexpr size_t kMacroBytes = 256;
constexpr int kMaxMacros = 50;  // the original UI allocates macro slots 0..49

struct MacroEvent {
    enum class Type { KeyDown, KeyUp, MouseDown, MouseUp, MouseMove };
    Type type = Type::KeyDown;
    uint8_t code = 0;   // HID usage for keys, 240..244 for mouse buttons
    int8_t dx = 0, dy = 0;
    uint16_t delayMs = 0;  // delay after this event
};

struct Macro {
    uint16_t repeat = 1;
    std::vector<MacroEvent> events;
};

// Encodes into the 256-byte slot layout. Throws std::runtime_error if it does not fit.
std::vector<uint8_t> encodeMacro(const Macro& m);
// Decodes a slot read back from the keyboard (port of the original `buffToMacroEvents`).
Macro decodeMacro(const std::vector<uint8_t>& buf);

// Text form: tokens separated by spaces.
//   +A / -A            key down / key up       (HID key names, see keycodes)
//   +mouse:Left        mouse button down (Left, Right, Middle, Back, Forward)
//   move:DX,DY         relative mouse move (-128..127)
//   A                  shorthand for "+A -A"
// Every event may carry "/MS" for the delay after it (default 10 ms), e.g. "+LCtrl/5 C/20 -LCtrl".
std::string macroToString(const Macro& m);
Macro parseMacro(const std::string& text, uint16_t repeat = 1);

}  // namespace k86
