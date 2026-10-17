#include "macro.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "keycodes.hpp"

namespace k86 {

namespace {

const struct {
    uint8_t code;
    const char* name;
} kMouseButtons[] = {{240, "Left"}, {241, "Right"}, {242, "Middle"}, {243, "Back"}, {244, "Forward"}};

const char* mouseName(uint8_t code) {
    for (const auto& b : kMouseButtons)
        if (b.code == code) return b.name;
    return nullptr;
}

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::vector<uint8_t> encodeMacro(const Macro& m) {
    std::vector<uint8_t> out;
    out.push_back(uint8_t(m.repeat & 0xFF));
    out.push_back(uint8_t(m.repeat >> 8));
    for (const auto& e : m.events) {
        uint16_t d = e.delayMs;
        if (e.type == MacroEvent::Type::MouseMove) {
            out.push_back(249);
            out.push_back(d <= 127 && d != 0 ? uint8_t(d) : 0);
            out.push_back(uint8_t(e.dx));
            out.push_back(uint8_t(e.dy));
            if (d > 127 || d == 0) {
                out.push_back(uint8_t(d & 0xFF));
                out.push_back(uint8_t(d >> 8));
            }
            continue;
        }
        bool down = e.type == MacroEvent::Type::KeyDown || e.type == MacroEvent::Type::MouseDown;
        out.push_back(e.code);
        // A short delay of 0 would be read back as a long-form event, so 0 uses the long form.
        if (d <= 127 && d != 0) {
            out.push_back(uint8_t(down ? d + 128 : d));
        } else {
            out.push_back(down ? 128 : 0);
            out.push_back(uint8_t(d & 0xFF));
            out.push_back(uint8_t(d >> 8));
        }
    }
    if (out.size() > kMacroBytes)
        throw std::runtime_error("macro too long: " + std::to_string(out.size()) + " bytes (max 256)");
    out.resize(kMacroBytes, 0);
    return out;
}

Macro decodeMacro(const std::vector<uint8_t>& t) {
    Macro m;
    if (t.size() < 6) return m;
    auto at = [&](size_t i) -> uint8_t { return i < t.size() ? t[i] : 0; };
    m.repeat = uint16_t(at(0) | (at(1) << 8));
    size_t i = 2;
    uint8_t s[4] = {at(2), at(3), at(4), at(5)};
    i += 4;
    while (i < t.size()) {
        MacroEvent e;
        if (s[0] == 249) {
            e.type = MacroEvent::Type::MouseMove;
            e.dx = int8_t(s[2]);
            e.dy = int8_t(s[3]);
            if (s[1] != 0) {
                // The original decoder uses s[1] >> 1 here, which does not match its own
                // encoder (it stores the delay unshifted); the stored value is used as-is.
                e.delayMs = s[1];
            } else {
                e.delayMs = uint16_t(at(i) | (at(i + 1) << 8));
                i += 2;
            }
            m.events.push_back(e);
        } else {
            bool down = s[1] > 127;
            bool known = true;
            if (s[0] >= 4 && s[0] <= 239) {
                e.type = down ? MacroEvent::Type::KeyDown : MacroEvent::Type::KeyUp;
            } else if (s[0] >= 240 && s[0] <= 244) {
                e.type = down ? MacroEvent::Type::MouseDown : MacroEvent::Type::MouseUp;
            } else {
                known = false;
            }
            e.code = s[0];
            if (s[1] & 127) {
                e.delayMs = s[1] & 127;
                i -= 2;
            } else {
                e.delayMs = uint16_t(s[2] | (s[3] << 8));
            }
            if (known) m.events.push_back(e);
        }
        s[0] = at(i), s[1] = at(i + 1), s[2] = at(i + 2), s[3] = at(i + 3);
        if (!s[0] && !s[1] && !s[2] && !s[3]) break;
        i += 4;
    }
    return m;
}

std::string macroToString(const Macro& m) {
    std::ostringstream o;
    bool first = true;
    for (const auto& e : m.events) {
        if (!first) o << ' ';
        first = false;
        switch (e.type) {
            case MacroEvent::Type::KeyDown:
            case MacroEvent::Type::KeyUp: {
                std::string n = hidUsageName(e.code);
                if (n.empty()) {
                    char b[8];
                    snprintf(b, sizeof b, "0x%02x", e.code);
                    n = b;
                }
                o << (e.type == MacroEvent::Type::KeyDown ? '+' : '-') << n;
                break;
            }
            case MacroEvent::Type::MouseDown:
            case MacroEvent::Type::MouseUp: {
                const char* n = mouseName(e.code);
                o << (e.type == MacroEvent::Type::MouseDown ? '+' : '-') << "mouse:" << (n ? n : "?");
                break;
            }
            case MacroEvent::Type::MouseMove:
                o << "move:" << int(e.dx) << ',' << int(e.dy);
                break;
        }
        o << '/' << e.delayMs;
    }
    return o.str();
}

Macro parseMacro(const std::string& text, uint16_t repeat) {
    Macro m;
    m.repeat = repeat;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        uint16_t delay = 10;
        auto slash = tok.rfind('/');
        if (slash != std::string::npos) {
            std::string d = tok.substr(slash + 1);
            char* end = nullptr;
            long v = strtol(d.c_str(), &end, 10);
            if (d.empty() || *end || v < 0 || v > 65535) throw std::runtime_error("bad delay in '" + tok + "'");
            delay = uint16_t(v);
            tok = tok.substr(0, slash);
        }
        if (tok.empty()) throw std::runtime_error("empty macro token");
        if (lower(tok).rfind("move:", 0) == 0) {
            int dx = 0, dy = 0;
            if (sscanf(tok.c_str() + 5, "%d,%d", &dx, &dy) != 2 || dx < -128 || dx > 127 || dy < -128 || dy > 127)
                throw std::runtime_error("bad mouse move '" + tok + "' (move:DX,DY in -128..127)");
            MacroEvent e;
            e.type = MacroEvent::Type::MouseMove;
            e.dx = int8_t(dx);
            e.dy = int8_t(dy);
            e.delayMs = delay;
            m.events.push_back(e);
            continue;
        }
        char sign = 0;
        if (tok[0] == '+' || tok[0] == '-') {
            sign = tok[0];
            tok = tok.substr(1);
        }
        bool mouse = false;
        uint8_t code = 0;
        if (lower(tok).rfind("mouse:", 0) == 0) {
            std::string n = lower(tok.substr(6));
            for (const auto& b : kMouseButtons)
                if (lower(b.name) == n) code = b.code;
            if (!code) throw std::runtime_error("unknown mouse button '" + tok + "'");
            mouse = true;
        } else {
            auto c = hidUsageFromName(tok);
            if (!c || *c < 4 || *c > 239) throw std::runtime_error("unknown key '" + tok + "' in macro");
            code = *c;
        }
        auto add = [&](bool down) {
            MacroEvent e;
            e.code = code;
            e.delayMs = delay;
            if (mouse) e.type = down ? MacroEvent::Type::MouseDown : MacroEvent::Type::MouseUp;
            else e.type = down ? MacroEvent::Type::KeyDown : MacroEvent::Type::KeyUp;
            m.events.push_back(e);
        };
        if (sign == '+') add(true);
        else if (sign == '-') add(false);
        else {
            add(true);
            add(false);
        }
    }
    return m;
}

}  // namespace k86
