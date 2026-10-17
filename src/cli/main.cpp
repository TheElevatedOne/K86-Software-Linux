// k86ctl - command line configuration tool for the Attack Shark K86 keyboard on Linux.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "discovery.hpp"
#include "keyboard.hpp"
#include "screen.hpp"
#include "sysinfo.hpp"

using namespace k86;

namespace {

const char* kUsage = R"(Usage: k86ctl [global options] <command> [arguments]

Global options:
  --device /dev/hidrawN   use this hidraw node (default: first K86 found)
  --force                 skip the firmware id check
  --dry-run[=v1|v2]       do not touch hardware; print the reports that would be sent
                          (v1 = YC3121 / PID 4015, v2 = YC3123 / PID 5002; default v2)
  --yes                   do not ask for confirmation (reset, screen erase)

Commands:
  list                                  list detected keyboards
  info                                  firmware id, versions and current settings
  profile [N]                           show / select the active profile (0-based)
  light [modes]                         show the current effect / list effects
  light set MODE [--brightness 0-4] [--speed 0-4] [--color RRGGBB] [--rainbow] [--option N]
  sidelight [modes]                     side light
  sidelight set MODE [--brightness 0-4] [--speed 0-3] [--color RRGGBB] [--rainbow]
  debounce [MS]                         show / set debounce time
  sleep [--sleep S] [--deep S]          show / set sleep timers in seconds (both radios)
        [--bt S] [--24g S] [--deep-bt S] [--deep-24g S]
  options [NAME=VALUE ...]              show / change keyboard option flags
  keys                                  list key slots of the keyboard
  actions                               list the key action syntax
  keymap show [--layer N] [--fn] [--os win|mac]
  keymap set KEY ACTION [--layer N] [--fn] [--os win|mac]
  keymap reset [--layer N] [--fn] [--os win|mac]   write the factory default layout
  keymap export FILE [--layer N] [--fn] [--os win|mac]
  keymap import FILE [--layer N] [--fn] [--os win|mac]
  macro show N                          show macro slot N (0-49)
  macro set N "EVENTS" [--repeat R]     e.g. macro set 0 "+LCtrl/5 C/20 -LCtrl"
  macro clear N
  perkey show [--layer N]               per-key colours of the "custom" effect
  perkey set KEY=RRGGBB ... [--layer N] change some keys (others are kept)
  perkey fill RRGGBB [--layer N]        set every key
  perkey anim FILE... [--delay N]       upload a per-key animation, one frame per file
                                        (each file: lines of KEY=RRGGBB)
  screen upload IMAGE [--slot 1-5] [--delay MS]   PNG/JPEG/BMP/GIF to the LCD
  screen clock                          set the keyboard clock to the local time
  screen language [0|1]                 send the screen language switch command
  screen erase                          erase the screen image flash (~55 s)
  sysinfo [--interval S] [--once]       stream CPU/RAM/disk/net stats to the screen
  reset                                 factory reset
  raw BYTES... [--checksum bit7|bit8|none] [--read]   send a raw command (hex bytes)
)";

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    bool has(const std::string& k) const { return opt.count(k) != 0; }
    std::string get(const std::string& k, const std::string& def = {}) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : it->second;
    }
};

const std::set<std::string> kValueOptions = {
    "device", "brightness", "speed", "color", "option", "layer", "os", "slot", "delay", "repeat",
    "interval", "checksum", "sleep", "deep", "bt", "24g", "deep-bt", "deep-24g"};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s.size() > 2 && s.rfind("--", 0) == 0) {
            std::string k = s.substr(2), v;
            auto eq = k.find('=');
            if (eq != std::string::npos) {
                v = k.substr(eq + 1);
                k = k.substr(0, eq);
            } else if (kValueOptions.count(k)) {
                if (i + 1 >= argc) throw std::runtime_error("--" + k + " needs a value");
                v = argv[++i];
            } else {
                v = "1";
            }
            a.opt[k] = v;
        } else if (s == "-h") {
            a.opt["help"] = "1";
        } else {
            a.pos.push_back(s);
        }
    }
    return a;
}

long toInt(const std::string& s, long lo, long hi, const std::string& what) {
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 0);
    if (s.empty() || *end || v < lo || v > hi)
        throw std::runtime_error(what + " must be a number in " + std::to_string(lo) + ".." + std::to_string(hi));
    return v;
}

Rgb toColor(std::string s) {
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw std::runtime_error("colour must be RRGGBB hex, got '" + s + "'");
    return Rgb(strtoul(s.c_str(), nullptr, 16));
}

std::string hexColor(Rgb c) {
    char b[8];
    snprintf(b, sizeof b, "%06x", c & 0xFFFFFF);
    return b;
}

std::string hexBytes(const uint8_t* d, size_t n) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        snprintf(b, sizeof b, "%02x", d[i]);
        s += (i ? " " : "") + std::string(b);
    }
    return s;
}

// Prints every report instead of talking to hardware. Reads return zeros except that byte 0
// echoes the last command and byte 1 is 1 (acknowledged), so that queries look answered.
class DryRunTransport : public Transport {
public:
    void sendFeature(const Report& r) override {
        last_ = r[0];
        std::cout << "SET_FEATURE " << hexBytes(r.data(), r.size()) << "\n";
    }
    Report getFeature() override {
        std::cout << "GET_FEATURE\n";
        Report r{};
        r[0] = last_;
        r[1] = 1;
        return r;
    }

private:
    uint8_t last_ = 0;
};

bool confirm(const Args& a, const std::string& question) {
    if (a.has("yes") || a.has("dry-run")) return true;
    std::cout << question << " [y/N] " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line == "y" || line == "Y" || line == "yes";
}

uint8_t layerArg(const Args& a, const DeviceModel& m) {
    return uint8_t(toInt(a.get("layer", "0"), 0, m.layers - 1, "--layer"));
}

uint8_t osArg(const Args& a) {
    std::string os = a.get("os", "win");
    if (os == "win" || os == "windows") return 0;
    if (os == "mac" || os == "macos") return 1;
    throw std::runtime_error("--os must be win or mac");
}

int slotArg(const KeyMatrix& defaults, const std::string& key) {
    auto s = findSlot(defaults, key);
    if (!s) throw std::runtime_error("unknown key '" + key + "' (see 'k86ctl keys'; '@N' selects slot N)");
    return *s;
}

void printLight(const LightSetting& l, const std::vector<LightModeInfo>& modes) {
    const LightModeInfo* m = findLightMode(modes, l.mode);
    std::string name = m ? m->name : l.mode == kModeUserAnimation ? "per-key animation" : "unknown";
    std::cout << "mode: " << name << " (" << int(l.mode) << ")\n"
              << "brightness: " << int(l.brightness) << "\n"
              << "speed: " << int(l.speed) << "\n"
              << "option: " << int(l.option) << "\n"
              << "colour: " << (l.rainbow ? "rainbow" : hexColor(l.color)) << "\n";
}

void listModes(const std::vector<LightModeInfo>& modes) {
    for (const auto& m : modes) {
        std::cout << "  " << m.name;
        for (size_t i = m.name.size(); i < 14; ++i) std::cout << ' ';
        std::cout << m.original;
        if (m.options) std::cout << "  options 0-" << m.options - 1;
        if (m.speed) std::cout << "  speed";
        if (m.color) std::cout << "  colour/rainbow";
        std::cout << "\n";
    }
}

LightSetting lightFromArgs(const Args& a, const std::vector<LightModeInfo>& modes, int maxSpeed) {
    if (a.pos.size() < 3) throw std::runtime_error("light set needs a MODE (see 'light modes')");
    const LightModeInfo* m = findLightMode(modes, a.pos[2]);
    if (!m) throw std::runtime_error("unknown mode '" + a.pos[2] + "'");
    LightSetting l;
    l.mode = m->code;
    l.brightness = uint8_t(toInt(a.get("brightness", "4"), 0, 4, "--brightness"));
    l.speed = uint8_t(toInt(a.get("speed", std::to_string(maxSpeed / 2)), 0, maxSpeed, "--speed"));
    l.option = uint8_t(toInt(a.get("option", "0"), 0, m->options ? m->options - 1 : 0, "--option"));
    l.color = toColor(a.get("color", "ff0000"));
    l.rainbow = a.has("rainbow");
    return l;
}

void printMatrix(const KeyMatrix& names, const KeyMatrix& defaults, const KeyMatrix& m) {
    for (int i = 0; i < kSlots; ++i) {
        std::string name = slotName(names, i);
        KeyAction a{m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]};
        if (name.empty() && a == KeyAction{0, 0, 0, 0}) continue;
        KeyAction d{defaults[i * 4], defaults[i * 4 + 1], defaults[i * 4 + 2], defaults[i * 4 + 3]};
        char line[96];
        snprintf(line, sizeof line, "@%-4d %-14s ", i, name.empty() ? "-" : name.c_str());
        std::cout << line << actionToString(a) << (a == d ? "" : "   (changed)") << "\n";
    }
}

void exportMatrix(const KeyMatrix& defaults, const KeyMatrix& m, const std::string& path) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "# k86ctl key map: <slot> <action>   (slot name in comment)\n";
    for (int i = 0; i < kSlots; ++i) {
        KeyAction a{m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]};
        std::string name = slotName(defaults, i);
        if (name.empty() && a == KeyAction{0, 0, 0, 0}) continue;
        f << "@" << i << " " << actionToString(a) << "   # " << (name.empty() ? "-" : name) << "\n";
    }
}

KeyMatrix importMatrix(const KeyMatrix& defaults, KeyMatrix m, const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::string line;
    int no = 0;
    while (std::getline(f, line)) {
        ++no;
        auto hash = line.find('#');
        // '#' starts a comment unless it is part of a "Key#N" slot name.
        while (hash != std::string::npos && hash > 0 && line[hash - 1] != ' ' && line[hash - 1] != '\t')
            hash = line.find('#', hash + 1);
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream in(line);
        std::string key, action;
        if (!(in >> key)) continue;
        if (!(in >> action)) throw std::runtime_error(path + ":" + std::to_string(no) + ": missing action");
        int slot = slotArg(defaults, key);
        std::string err;
        auto a = parseAction(action, &err);
        if (!a) throw std::runtime_error(path + ":" + std::to_string(no) + ": " + err);
        std::copy(a->begin(), a->end(), m.begin() + slot * 4);
    }
    return m;
}

std::array<Rgb, kSlots> readColorFile(const KeyMatrix& defaults, const std::string& path) {
    std::array<Rgb, kSlots> c{};
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::string tok;
    while (f >> tok) {
        auto eq = tok.find('=');
        if (eq == std::string::npos) throw std::runtime_error(path + ": expected KEY=RRGGBB, got '" + tok + "'");
        c[slotArg(defaults, tok.substr(0, eq))] = toColor(tok.substr(eq + 1));
    }
    return c;
}

int cmdList() {
    auto cands = findCandidates();
    if (cands.empty()) {
        std::cout << "No K86 configuration interface found.\n";
        return 1;
    }
    for (const auto& c : cands) {
        std::cout << c.info.path << "  " << c.model->displayName << "  [" << std::hex << c.info.vid << ":" << c.info.pid
                  << std::dec << "]  " << c.info.name;
        try {
            HidrawTransport t(c.info.path);
            auto kb = makeKeyboard(t, *c.model);
            if (auto id = kb->readDeviceId()) {
                const DeviceModel* exact = findModel(c.info.vid, c.info.pid, id);
                std::cout << "  firmware id " << *id << (exact ? "" : " (not a K86 id!)");
            }
        } catch (const std::exception& e) {
            std::cout << "  (" << e.what() << ")";
        }
        std::cout << "\n";
    }
    return 0;
}

int run(const Args& a) {
    if (a.pos.empty() || a.has("help")) {
        std::cout << kUsage;
        return a.pos.empty() && !a.has("help") ? 1 : 0;
    }
    const std::string cmd = a.pos[0];
    auto arg = [&](size_t i) -> std::string {
        if (i >= a.pos.size()) throw std::runtime_error("missing argument for '" + cmd + "'");
        return a.pos[i];
    };

    if (cmd == "list") return cmdList();
    if (cmd == "actions") {
        std::cout << actionSyntaxHelp();
        return 0;
    }

    // Open the keyboard (or a dry-run stand-in).
    std::unique_ptr<Transport> dryTransport;
    OpenKeyboard opened;
    Keyboard* kb = nullptr;
    if (a.has("dry-run")) {
        std::string v = a.get("dry-run");
        const auto& models = knownModels();
        const DeviceModel& m = (v == "v1") ? models[0] : models[1];
        dryTransport = std::make_unique<DryRunTransport>();
        opened.keyboard = makeKeyboard(*dryTransport, m, [](int) {});
        kb = opened.keyboard.get();
        std::cerr << "dry run: " << m.displayName << "\n";
    } else {
        opened = openKeyboard(a.get("device"), a.has("force"));
        kb = opened.keyboard.get();
    }
    const DeviceModel& model = kb->model();

    if (cmd == "keys") {
        for (int i = 0; i < kSlots; ++i) {
            std::string n = slotName(kb->defaultMatrix(), i);
            if (!n.empty()) std::cout << "@" << i << "\t" << n << "\n";
        }
        return 0;
    }

    if (cmd == "info") {
        std::cout << "model: " << model.displayName << " (" << model.firmwareName << ")\n";
        if (opened.transport) std::cout << "device: " << opened.info.path << "\n";
        if (opened.firmwareId) std::cout << "firmware id: " << *opened.firmwareId << "\n";
        Versions v = kb->readVersions();
        auto ver = [](const char* n, const std::optional<uint16_t>& x) {
            if (x) printf("%s version: 0x%04x\n", n, *x);
        };
        ver("usb", v.usb), ver("rf", v.rf), ver("mled", v.mled), ver("oled", v.oled), ver("flash", v.flash);
        if (auto p = kb->getProfile()) std::cout << "profile: " << int(*p) << "\n";
        if (auto d = kb->getDebounce()) std::cout << "debounce: " << int(*d) << "\n";
        if (auto l = kb->getLight()) {
            std::cout << "-- light\n";
            printLight(*l, model.lightModes);
        }
        if (!model.sideLightModes.empty())
            if (auto l = kb->getSideLight()) {
                std::cout << "-- side light\n";
                printLight(*l, model.sideLightModes);
            }
        return 0;
    }

    if (cmd == "profile") {
        if (a.pos.size() > 1) {
            kb->setProfile(uint8_t(toInt(a.pos[1], 0, model.layers - 1, "profile")));
        } else if (auto p = kb->getProfile()) {
            std::cout << int(*p) << "\n";
        }
        return 0;
    }

    if (cmd == "light" || cmd == "sidelight") {
        bool side = cmd == "sidelight";
        const auto& modes = side ? model.sideLightModes : model.lightModes;
        if (side && modes.empty()) throw std::runtime_error(model.displayName + " has no side light");
        if (a.pos.size() > 1 && a.pos[1] == "modes") {
            listModes(modes);
            return 0;
        }
        if (a.pos.size() > 1 && a.pos[1] == "set") {
            LightSetting l = lightFromArgs(a, modes, side ? model.maxSideSpeed : model.maxSpeed);
            side ? kb->setSideLight(l) : kb->setLight(l);
            return 0;
        }
        if (a.pos.size() > 1) throw std::runtime_error("usage: " + cmd + " [modes | set MODE ...]");
        auto l = side ? kb->getSideLight() : kb->getLight();
        if (!l) throw std::runtime_error("keyboard returned an unknown effect");
        printLight(*l, modes);
        return 0;
    }

    if (cmd == "debounce") {
        if (a.pos.size() > 1) kb->setDebounce(uint8_t(toInt(a.pos[1], 0, 255, "debounce")));
        else if (auto d = kb->getDebounce()) std::cout << int(*d) << "\n";
        return 0;
    }

    if (cmd == "sleep") {
        auto cur = kb->getSleep().value_or(SleepTimes{});
        bool change = false;
        auto setv = [&](const char* opt, uint16_t& field, int lo, int hi) {
            if (!a.has(opt)) return;
            field = uint16_t(toInt(a.get(opt), lo, hi, std::string("--") + opt));
            change = true;
        };
        if (a.has("sleep")) {
            uint16_t v = uint16_t(toInt(a.get("sleep"), model.sleepMin, model.sleepMax, "--sleep"));
            cur.bluetooth = cur.wireless24 = v;
            change = true;
        }
        if (a.has("deep")) {
            uint16_t v = uint16_t(toInt(a.get("deep"), model.deepSleepMin, model.deepSleepMax, "--deep"));
            cur.deepBluetooth = cur.deepWireless24 = v;
            change = true;
        }
        setv("bt", cur.bluetooth, model.sleepMin, model.sleepMax);
        setv("24g", cur.wireless24, model.sleepMin, model.sleepMax);
        setv("deep-bt", cur.deepBluetooth, model.deepSleepMin, model.deepSleepMax);
        setv("deep-24g", cur.deepWireless24, model.deepSleepMin, model.deepSleepMax);
        if (change) {
            kb->setSleep(cur);
        } else {
            std::cout << "sleep (bluetooth): " << cur.bluetooth << " s\n"
                      << "sleep (2.4G): " << cur.wireless24 << " s\n"
                      << "deep sleep (bluetooth): " << cur.deepBluetooth << " s\n"
                      << "deep sleep (2.4G): " << cur.deepWireless24 << " s\n";
        }
        return 0;
    }

    if (cmd == "options") {
        if (a.pos.size() == 1) {
            for (const auto& [k, v] : kb->getOptions()) std::cout << k << "=" << v << "\n";
            return 0;
        }
        std::map<std::string, int> changes;
        for (size_t i = 1; i < a.pos.size(); ++i) {
            auto eq = a.pos[i].find('=');
            if (eq == std::string::npos) throw std::runtime_error("expected NAME=VALUE, got '" + a.pos[i] + "'");
            changes[a.pos[i].substr(0, eq)] = int(toInt(a.pos[i].substr(eq + 1), 0, 255, a.pos[i].substr(0, eq)));
        }
        kb->setOptions(changes);
        return 0;
    }

    if (cmd == "keymap") {
        std::string sub = arg(1);
        uint8_t layer = layerArg(a, model);
        bool fn = a.has("fn");
        uint8_t os = osArg(a);
        const KeyMatrix& names = kb->defaultMatrix();
        const KeyMatrix& defaults = fn ? kb->defaultFnMatrix() : kb->defaultMatrix();
        auto read = [&] { return fn ? kb->getFnMatrix(layer, os) : kb->getKeyMatrix(layer); };
        auto write = [&](const KeyMatrix& m) { fn ? kb->setFnMatrix(layer, os, m) : kb->setKeyMatrix(layer, m); };
        if (sub == "show") {
            printMatrix(names, defaults, read());
        } else if (sub == "set") {
            int slot = slotArg(names, arg(2));
            std::string err;
            auto act = parseAction(arg(3), &err);
            if (!act) throw std::runtime_error(err);
            fn ? kb->setFnKey(layer, os, uint8_t(slot), *act) : kb->setKey(layer, uint8_t(slot), *act);
        } else if (sub == "reset") {
            write(defaults);
        } else if (sub == "export") {
            exportMatrix(names, read(), arg(2));
        } else if (sub == "import") {
            write(importMatrix(names, defaults, arg(2)));
        } else {
            throw std::runtime_error("unknown keymap command '" + sub + "'");
        }
        return 0;
    }

    if (cmd == "macro") {
        std::string sub = arg(1);
        uint8_t idx = uint8_t(toInt(arg(2), 0, kMaxMacros - 1, "macro slot"));
        if (sub == "show") {
            Macro m = kb->getMacro(idx);
            std::cout << "repeat: " << m.repeat << "\n" << macroToString(m) << "\n";
        } else if (sub == "set") {
            uint16_t repeat = uint16_t(toInt(a.get("repeat", "1"), 0, 65535, "--repeat"));
            kb->setMacro(idx, parseMacro(arg(3), repeat));
        } else if (sub == "clear") {
            kb->setMacro(idx, Macro{0, {}});
        } else {
            throw std::runtime_error("unknown macro command '" + sub + "'");
        }
        return 0;
    }

    if (cmd == "perkey") {
        std::string sub = arg(1);
        uint8_t layer = uint8_t(toInt(a.get("layer", "0"), 0, 255, "--layer"));
        const KeyMatrix& names = kb->defaultMatrix();
        if ((sub == "set" || sub == "fill") && kb->leaveUserAnimation())
            std::cerr << "note: stopped the per-key animation and switched to the custom effect\n";
        if (sub == "show") {
            auto c = kb->getUserPicture(layer);
            for (int i = 0; i < kSlots; ++i) {
                std::string n = slotName(names, i);
                if (!n.empty()) std::cout << n << "=" << hexColor(c[i]) << "\n";
            }
        } else if (sub == "set") {
            auto c = kb->getUserPicture(layer);
            for (size_t i = 2; i < a.pos.size(); ++i) {
                auto eq = a.pos[i].find('=');
                if (eq == std::string::npos) throw std::runtime_error("expected KEY=RRGGBB");
                c[slotArg(names, a.pos[i].substr(0, eq))] = toColor(a.pos[i].substr(eq + 1));
            }
            kb->setUserPicture(layer, c);
        } else if (sub == "fill") {
            std::array<Rgb, kSlots> c{};
            Rgb col = toColor(arg(2));
            for (int i = 0; i < kSlots; ++i)
                if (!slotName(names, i).empty()) c[i] = col;
            kb->setUserPicture(layer, c);
        } else if (sub == "anim") {
            std::vector<std::array<Rgb, kSlots>> frames;
            for (size_t i = 2; i < a.pos.size(); ++i) frames.push_back(readColorFile(names, a.pos[i]));
            if (frames.empty() || frames.size() > 255) throw std::runtime_error("perkey anim needs 1-255 frame files");
            uint16_t delay = uint16_t(toInt(a.get("delay", "50"), 0, 65535, "--delay"));
            kb->startUserAnimation();
            kb->session().sleep(500);
            for (size_t i = 0; i < frames.size(); ++i)
                kb->sendUserAnimationFrame(uint8_t(i), uint8_t(frames.size()), delay, frames[i]);
        } else {
            throw std::runtime_error("unknown perkey command '" + sub + "'");
        }
        return 0;
    }

    if (cmd == "screen") {
        std::string sub = arg(1);
        if (sub == "clock") {
            kb->syncClock();
        } else if (sub == "language") {
            if (!model.canSwitchLanguage) std::cerr << "warning: the original software does not offer this for this model\n";
            kb->switchScreenLanguage(a.pos.size() > 2 ? toInt(a.pos[2], 0, 1, "language") != 0 : true);
        } else if (sub == "erase") {
            if (!confirm(a, "Erase all images stored on the keyboard screen?")) return 1;
            if (!kb->eraseScreenFlash()) throw std::runtime_error("keyboard did not acknowledge the erase");
            std::cout << "Erase started; the original software waits about 55 seconds for it to finish.\n";
        } else if (sub == "upload") {
            LoadedImage img = loadImage(arg(2));
            const int W = model.screenWidth, H = model.screenHeight;
            auto progress = [](double p) {
                fprintf(stderr, "\r%3d%%", int(p * 100));
                if (p >= 1.0) fprintf(stderr, "\n");
            };
            if (img.frames.size() == 1) {
                uint8_t slot = uint8_t(toInt(a.get("slot", "1"), 1, model.screenSlots, "--slot") - 1);
                auto px = fitToScreen(img.frames[0], W, H);
                ScreenImage si = toScreenImage(px, W, contentRect(px, W, H));
                if (si.data.empty()) std::cerr << "warning: image is completely black; nothing to draw\n";
                if (!kb->screenBegin(slot, 1, 0, si)) throw std::runtime_error("keyboard rejected the image upload");
                kb->screenSendFrame(slot, 1, 0, si, progress);
            } else {
                if (img.frames.size() > 255) throw std::runtime_error("GIF has more than 255 frames");
                int delay = a.has("delay") ? int(toInt(a.get("delay"), 0, 255, "--delay"))
                                           : (img.delaysMs[0] > 0 ? img.delaysMs[0] : 50);
                if (delay > 255) {
                    std::cerr << "note: GIF frame delay " << delay << " ms clamped to 255 ms (device limit)\n";
                    delay = 255;
                }
                std::vector<std::vector<uint32_t>> px;
                std::vector<Rect> rects;
                for (const auto& f : img.frames) {
                    px.push_back(fitToScreen(f, W, H));
                    rects.push_back(contentRect(px.back(), W, H));
                }
                Rect u = unionRect(rects);
                uint8_t n = uint8_t(img.frames.size());
                std::vector<ScreenImage> frames;
                for (auto& p : px) frames.push_back(toScreenImage(p, W, u));
                if (!kb->screenBegin(0, n, uint8_t(delay), frames[0]))
                    throw std::runtime_error("keyboard rejected the animation upload");
                for (size_t i = 0; i < frames.size(); ++i) {
                    kb->screenSendFrame(uint8_t(i), n, uint8_t(delay), frames[i], [&](double p) {
                        fprintf(stderr, "\r%3d%%", int((i + p) * 100 / frames.size()));
                    });
                }
                fprintf(stderr, "\n");
            }
        } else {
            throw std::runtime_error("unknown screen command '" + sub + "'");
        }
        return 0;
    }

    if (cmd == "sysinfo") {
        if (!model.canSyncSystemInfo)
            std::cerr << "warning: the original software does not offer system info sync for this model\n";
        double interval = std::max(0.6, atof(a.get("interval", "1").c_str()));
        SystemInfoCollector col;
        col.sample();
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(int(interval * 1000)));
            SystemInfo s = col.sample();
            kb->sendSystemInfo(s);
            if (a.has("once")) break;
        } while (true);
        return 0;
    }

    if (cmd == "reset") {
        if (!confirm(a, "Reset the keyboard to factory settings?")) return 1;
        kb->factoryReset();
        return 0;
    }

    if (cmd == "raw") {
        std::vector<uint8_t> bytes;
        for (size_t i = 1; i < a.pos.size(); ++i) bytes.push_back(uint8_t(toInt("0x" + a.pos[i], 0, 255, "byte")));
        if (bytes.empty()) throw std::runtime_error("raw needs at least one byte");
        std::string cs = a.get("checksum", "bit7");
        Checksum c = cs == "bit8" ? Checksum::Bit8 : cs == "none" ? Checksum::None : Checksum::Bit7;
        if (cs != "bit7" && cs != "bit8" && cs != "none") throw std::runtime_error("--checksum must be bit7, bit8 or none");
        kb->session().send(bytes, c);
        if (a.has("read")) {
            Report r = kb->session().read();
            std::cout << hexBytes(r.data(), r.size()) << "\n";
        }
        return 0;
    }

    throw std::runtime_error("unknown command '" + cmd + "' (try --help)");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parseArgs(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
