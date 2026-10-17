#include "keycodes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace k86 {

namespace {

struct Named {
    uint16_t code;
    const char* name;
};

// USB HID Usage Tables, Keyboard/Keypad page (0x07).
const Named kHidUsages[] = {
    {0x04, "A"}, {0x05, "B"}, {0x06, "C"}, {0x07, "D"}, {0x08, "E"}, {0x09, "F"}, {0x0A, "G"},
    {0x0B, "H"}, {0x0C, "I"}, {0x0D, "J"}, {0x0E, "K"}, {0x0F, "L"}, {0x10, "M"}, {0x11, "N"},
    {0x12, "O"}, {0x13, "P"}, {0x14, "Q"}, {0x15, "R"}, {0x16, "S"}, {0x17, "T"}, {0x18, "U"},
    {0x19, "V"}, {0x1A, "W"}, {0x1B, "X"}, {0x1C, "Y"}, {0x1D, "Z"},
    {0x1E, "1"}, {0x1F, "2"}, {0x20, "3"}, {0x21, "4"}, {0x22, "5"}, {0x23, "6"}, {0x24, "7"},
    {0x25, "8"}, {0x26, "9"}, {0x27, "0"},
    {0x28, "Enter"}, {0x29, "Esc"}, {0x2A, "Backspace"}, {0x2B, "Tab"}, {0x2C, "Space"},
    {0x2D, "Minus"}, {0x2E, "Equal"}, {0x2F, "LBracket"}, {0x30, "RBracket"}, {0x31, "Backslash"},
    {0x32, "NonUSHash"}, {0x33, "Semicolon"}, {0x34, "Quote"}, {0x35, "Grave"}, {0x36, "Comma"},
    {0x37, "Period"}, {0x38, "Slash"}, {0x39, "CapsLock"},
    {0x3A, "F1"}, {0x3B, "F2"}, {0x3C, "F3"}, {0x3D, "F4"}, {0x3E, "F5"}, {0x3F, "F6"},
    {0x40, "F7"}, {0x41, "F8"}, {0x42, "F9"}, {0x43, "F10"}, {0x44, "F11"}, {0x45, "F12"},
    {0x46, "PrintScreen"}, {0x47, "ScrollLock"}, {0x48, "Pause"}, {0x49, "Insert"}, {0x4A, "Home"},
    {0x4B, "PageUp"}, {0x4C, "Delete"}, {0x4D, "End"}, {0x4E, "PageDown"}, {0x4F, "Right"},
    {0x50, "Left"}, {0x51, "Down"}, {0x52, "Up"}, {0x53, "NumLock"}, {0x54, "KP_Divide"},
    {0x55, "KP_Multiply"}, {0x56, "KP_Minus"}, {0x57, "KP_Plus"}, {0x58, "KP_Enter"},
    {0x59, "KP_1"}, {0x5A, "KP_2"}, {0x5B, "KP_3"}, {0x5C, "KP_4"}, {0x5D, "KP_5"}, {0x5E, "KP_6"},
    {0x5F, "KP_7"}, {0x60, "KP_8"}, {0x61, "KP_9"}, {0x62, "KP_0"}, {0x63, "KP_Period"},
    {0x64, "NonUSBackslash"}, {0x65, "Menu"}, {0x66, "Power"}, {0x67, "KP_Equal"},
    {0x68, "F13"}, {0x69, "F14"}, {0x6A, "F15"}, {0x6B, "F16"}, {0x6C, "F17"}, {0x6D, "F18"},
    {0x6E, "F19"}, {0x6F, "F20"}, {0x70, "F21"}, {0x71, "F22"}, {0x72, "F23"}, {0x73, "F24"},
    {0x74, "Execute"}, {0x75, "Help"}, {0x77, "Select"}, {0x78, "Stop"}, {0x79, "Again"},
    {0x7A, "Undo"}, {0x7B, "Cut"}, {0x7C, "Copy"}, {0x7D, "Paste"}, {0x7E, "Find"},
    {0x7F, "KbMute"}, {0x80, "KbVolumeUp"}, {0x81, "KbVolumeDown"}, {0x85, "KP_Comma"},
    {0x87, "Intl1"}, {0x88, "Intl2"}, {0x89, "Intl3"}, {0x8A, "Intl4"}, {0x8B, "Intl5"},
    {0x90, "Lang1"}, {0x91, "Lang2"}, {0x92, "Lang3"}, {0x93, "Lang4"},
    {0xE0, "LCtrl"}, {0xE1, "LShift"}, {0xE2, "LAlt"}, {0xE3, "LGui"},
    {0xE4, "RCtrl"}, {0xE5, "RShift"}, {0xE6, "RAlt"}, {0xE7, "RGui"},
};

const Named kHidAliases[] = {
    {0x29, "Escape"}, {0x28, "Return"}, {0x4C, "Del"}, {0x49, "Ins"}, {0x46, "PrtSc"},
    {0x65, "Application"}, {0x65, "ContextMenu"}, {0x35, "Backquote"}, {0x35, "Tilde"},
    {0xE0, "Ctrl"}, {0xE1, "Shift"}, {0xE2, "Alt"}, {0xE3, "Win"}, {0xE3, "Super"}, {0xE3, "LWin"},
    {0xE3, "Meta"}, {0xE7, "RWin"}, {0x4B, "PgUp"}, {0x4E, "PgDn"}, {0x39, "Caps"},
    {0x4F, "ArrowRight"}, {0x50, "ArrowLeft"}, {0x51, "ArrowDown"}, {0x52, "ArrowUp"},
};

// Consumer page usages that appear in the original software's function table.
const Named kConsumer[] = {
    {0x00B5, "NextTrack"}, {0x00B6, "PrevTrack"}, {0x00B7, "StopMedia"}, {0x00CD, "PlayPause"},
    {0x00E2, "Mute"}, {0x00E9, "VolumeUp"}, {0x00EA, "VolumeDown"}, {0x0183, "MediaPlayer"},
    {0x018A, "Mail"}, {0x0192, "Calculator"}, {0x0194, "MyComputer"}, {0x0221, "Search"},
    {0x0223, "Home"}, {0x0224, "Back"}, {0x0227, "Refresh"}, {0x006F, "BrightnessUp"},
    {0x0070, "BrightnessDown"},
};

const Named kSystem[] = {{0x81, "Power"}, {0x82, "Sleep"}, {0x83, "Wake"}};

struct NamedAction {
    KeyAction a;
    const char* name;
};

// Mouse actions ([1, 0, x, y]) from the original software's mouse map.
const NamedAction kMouse[] = {
    {{1, 0, 240, 0}, "mouse:Left"},     {{1, 0, 241, 0}, "mouse:Right"},
    {{1, 0, 242, 0}, "mouse:Middle"},   {{1, 0, 243, 0}, "mouse:Back"},
    {{1, 0, 244, 0}, "mouse:Forward"},  {{1, 0, 245, 1}, "mouse:WheelUp"},
    {{1, 0, 245, 255}, "mouse:WheelDown"},
};

// Vendor function actions whose meaning is stated by the original software.
const NamedAction kFunctions[] = {
    {{10, 1, 0, 0}, "Fn"},
    {{10, 1, 1, 0}, "RFn"},
    {{8, 0, 1, 0}, "profile:Next"},
    {{8, 0, 2, 0}, "profile:Prev"},
    {{8, 0, 3, 0}, "profile:Loop"},
    {{13, 2, 1, 0}, "led:BrightnessUp"},
    {{13, 2, 2, 0}, "led:BrightnessDown"},
    {{0, 0, 0, 0}, "Disabled"},
};

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ieq(const std::string& a, const char* b) { return lower(a) == lower(b); }

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool parseNumber(const std::string& s, long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = strtol(s.c_str(), &end, 0);
    return end && *end == '\0';
}

std::string hex2(uint8_t v) {
    char b[4];
    snprintf(b, sizeof b, "%02x", v);
    return b;
}

std::string rawString(const KeyAction& a) {
    return "raw:" + hex2(a[0]) + "," + hex2(a[1]) + "," + hex2(a[2]) + "," + hex2(a[3]);
}

std::string usageOrHex(uint8_t u) {
    std::string n = hidUsageName(u);
    if (!n.empty()) return n;
    return "0x" + hex2(u);
}

}  // namespace

std::string hidUsageName(uint8_t usage) {
    for (const auto& n : kHidUsages)
        if (n.code == usage) return n.name;
    return {};
}

std::optional<uint8_t> hidUsageFromName(const std::string& name) {
    for (const auto& n : kHidUsages)
        if (ieq(name, n.name)) return uint8_t(n.code);
    for (const auto& n : kHidAliases)
        if (ieq(name, n.name)) return uint8_t(n.code);
    long v;
    if ((name.rfind("0x", 0) == 0 || name.rfind("0X", 0) == 0) && parseNumber(name, v) && v >= 0 && v <= 255)
        return uint8_t(v);
    return std::nullopt;
}

std::vector<std::pair<uint8_t, std::string>> allHidUsageNames() {
    std::vector<std::pair<uint8_t, std::string>> out;
    for (const auto& n : kHidUsages) out.emplace_back(uint8_t(n.code), n.name);
    return out;
}

std::string actionToString(const KeyAction& a) {
    for (const auto& f : kFunctions)
        if (f.a == a) return f.name;
    for (const auto& m : kMouse)
        if (m.a == a) return m.name;
    switch (a[0]) {
        case 0: {
            std::vector<std::string> keys;
            for (int i = 1; i < 4; ++i)
                if (a[i]) keys.push_back(usageOrHex(a[i]));
            std::string s;
            for (size_t i = 0; i < keys.size(); ++i) s += (i ? "+" : "") + keys[i];
            // Plain single key: [0,0,usage,0]. Other layouts of a single key keep raw form
            // so that a round trip reproduces the exact bytes.
            if (keys.size() == 1 && !(a[1] == 0 && a[3] == 0)) return rawString(a);
            if (keys.size() == 2) {
                if (a[1] == 0) return "combo0:" + s;  // [0,0,k1,k2]
                if (a[3] != 0) return rawString(a);    // [0,k1,0,k2]
            }
            return s;
        }
        case 2:
            if (a[1] == 0 && a[3] == 0)
                for (const auto& n : kSystem)
                    if (n.code == a[2]) return std::string("system:") + n.name;
            break;
        case 3:
            if (a[1] == 0) {
                uint16_t u = uint16_t(a[2] | (a[3] << 8));
                for (const auto& n : kConsumer)
                    if (n.code == u) return std::string("consumer:") + n.name;
                char b[32];
                snprintf(b, sizeof b, "consumer:0x%04x", u);
                return b;
            }
            break;
        case 9:
            if (a[3] == 0 && a[1] <= 2) {
                static const char* types[] = {"repeat", "toggle", "hold"};
                return "macro:" + std::to_string(a[2]) + ":" + types[a[1]];
            }
            break;
        default:
            break;
    }
    return rawString(a);
}

std::optional<KeyAction> parseAction(const std::string& in, std::string* err) {
    auto fail = [&](const std::string& m) -> std::optional<KeyAction> {
        if (err) *err = m;
        return std::nullopt;
    };
    std::string s = in;
    if (s.empty()) return fail("empty action");

    for (const auto& f : kFunctions)
        if (ieq(s, f.name)) return f.a;
    if (ieq(s, "none") || ieq(s, "off")) return KeyAction{0, 0, 0, 0};
    for (const auto& m : kMouse)
        if (ieq(s, m.name)) return m.a;

    auto colon = s.find(':');
    std::string prefix = colon == std::string::npos ? "" : lower(s.substr(0, colon));
    std::string rest = colon == std::string::npos ? s : s.substr(colon + 1);

    if (prefix == "raw") {
        auto parts = split(rest, ',');
        if (parts.size() != 4) return fail("raw action needs 4 comma separated hex bytes");
        KeyAction a{};
        for (int i = 0; i < 4; ++i) {
            char* end = nullptr;
            long v = strtol(parts[i].c_str(), &end, 16);
            if (parts[i].empty() || *end != '\0' || v < 0 || v > 255) return fail("bad raw byte '" + parts[i] + "'");
            a[i] = uint8_t(v);
        }
        return a;
    }
    if (prefix == "consumer") {
        for (const auto& n : kConsumer)
            if (ieq(rest, n.name)) return KeyAction{3, 0, uint8_t(n.code & 0xFF), uint8_t(n.code >> 8)};
        long v;
        if (parseNumber(rest, v) && v >= 0 && v <= 0xFFFF) return KeyAction{3, 0, uint8_t(v & 0xFF), uint8_t(v >> 8)};
        return fail("unknown consumer usage '" + rest + "'");
    }
    if (prefix == "system") {
        for (const auto& n : kSystem)
            if (ieq(rest, n.name)) return KeyAction{2, 0, uint8_t(n.code), 0};
        return fail("unknown system action '" + rest + "' (Power, Sleep, Wake)");
    }
    if (prefix == "macro") {
        auto parts = split(rest, ':');
        long idx;
        if (parts.empty() || !parseNumber(parts[0], idx) || idx < 0 || idx > 255) return fail("bad macro index");
        uint8_t type = 0;
        if (parts.size() > 1) {
            std::string t = lower(parts[1]);
            if (t == "repeat") type = 0;
            else if (t == "toggle") type = 1;
            else if (t == "hold") type = 2;
            else return fail("macro type must be repeat, toggle or hold");
        }
        return KeyAction{9, type, uint8_t(idx), 0};
    }
    bool combo0 = prefix == "combo0";
    if (!prefix.empty() && !combo0) return fail("unknown action prefix '" + prefix + "'");

    // Keyboard usages joined with '+'.
    auto keys = split(rest, '+');
    if (keys.size() > 3) return fail("at most 3 keys can be combined");
    std::vector<uint8_t> codes;
    for (const auto& k : keys) {
        auto c = hidUsageFromName(k);
        if (!c) return fail("unknown key '" + k + "'");
        codes.push_back(*c);
    }
    if (codes.size() == 1) return KeyAction{0, 0, codes[0], 0};
    if (codes.size() == 2) {
        // The original firmware tables use both [0,0,k1,k2] (e.g. Win+D) and [0,k1,k2,0]
        // (e.g. Shift+[). "combo0:" selects the first layout explicitly.
        if (combo0) return KeyAction{0, 0, codes[0], codes[1]};
        return KeyAction{0, codes[0], codes[1], 0};
    }
    return KeyAction{0, codes[0], codes[1], codes[2]};
}

std::string actionSyntaxHelp() {
    std::ostringstream o;
    o << "Key action syntax:\n"
         "  A, F5, Esc, LCtrl ...        single key (HID keyboard usage name or 0xNN)\n"
         "  LCtrl+C, LCtrl+LShift+Esc    up to three keys pressed together\n"
         "  consumer:VolumeUp            consumer usage (name or number, e.g. consumer:0xe9)\n"
         "  system:Power|Sleep|Wake      system control\n"
         "  mouse:Left|Right|Middle|Back|Forward|WheelUp|WheelDown\n"
         "  macro:N[:repeat|toggle|hold] play macro slot N\n"
         "  Fn, profile:Next|Prev|Loop, led:BrightnessUp|BrightnessDown\n"
         "  Disabled                     key does nothing\n"
         "  raw:aa,bb,cc,dd              four raw bytes (hex)\n"
         "Consumer names:";
    for (const auto& n : kConsumer) o << ' ' << n.name;
    o << "\nKey names:";
    int col = 0;
    for (const auto& n : kHidUsages) {
        if (col++ % 12 == 0) o << "\n ";
        o << ' ' << n.name;
    }
    o << '\n';
    return o.str();
}

}  // namespace k86
