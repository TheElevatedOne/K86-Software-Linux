// Key action encoding used in the keyboard's key matrix (4 bytes per key slot).
//
//   [0, k1, k2, k3]   keyboard usages pressed together (plain key = [0,0,usage,0])
//   [1, 0, b, x]      mouse (b: 240 left, 241 right, 242 middle, 243 back, 244 forward, 245.. wheel)
//   [2, 0, u, 0]      system control usage (0x81 power, 0x82 sleep, 0x83 wake)
//   [3, 0, lo, hi]    consumer control usage (volume, media, ...)
//   [8, 0, op, n]     profile switching
//   [9, type, idx, 0] macro (type 0 = repeat N times, 1 = toggle on/off, 2 = repeat while held)
//   [10, 1, 0, 0]     Fn key
//   [0, 0, 0, 0]      disabled
// Other values are vendor functions that the original software only exposes by
// default position; they are shown and accepted as raw bytes.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace k86 {

using KeyAction = std::array<uint8_t, 4>;

// Short name of a HID keyboard usage (e.g. 0x04 -> "A"), empty if unknown.
std::string hidUsageName(uint8_t usage);
// Inverse of hidUsageName (case-insensitive, accepts a few aliases), or nullopt.
std::optional<uint8_t> hidUsageFromName(const std::string& name);

// Human readable form, e.g. "A", "LCtrl+C", "consumer:VolumeUp", "macro:3:repeat", "raw:0d,08,01,00".
std::string actionToString(const KeyAction& a);
// Parses the format produced by actionToString. Returns nullopt and sets `err` on failure.
std::optional<KeyAction> parseAction(const std::string& s, std::string* err = nullptr);

// Help text listing the accepted action syntax.
std::string actionSyntaxHelp();

// All named keyboard usages (for help output).
std::vector<std::pair<uint8_t, std::string>> allHidUsageNames();

}  // namespace k86
