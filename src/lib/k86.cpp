// libk86: the C API of include/k86/k86.h on top of the C++ core (the same code k86ctl and
// k86-gui use). Every entry point catches exceptions and turns them into k86_last_error().
#include "k86/k86.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "discovery.hpp"
#include "keyboard.hpp"
#include "screen.hpp"
#include "simulator.hpp"
#include "sysinfo.hpp"

using namespace k86;

struct k86_keyboard {
    std::unique_ptr<Transport> transport;  // hidraw device or simulator
    std::unique_ptr<Keyboard> kb;
    SystemInfoCollector sysinfo;
};

namespace {

thread_local std::string lastError;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Runs `f`, returning 0, or -1 with the error message stored.
template <typename F>
int guard(F&& f) {
    lastError.clear();
    try {
        f();
        return 0;
    } catch (const std::exception& e) {
        lastError = e.what();
    } catch (...) {
        lastError = "unknown error";
    }
    return -1;
}

Keyboard& need(k86_keyboard* kb) {
    if (!kb || !kb->kb) throw Error("no keyboard (NULL handle)");
    return *kb->kb;
}

template <typename T>
T& needOut(T* p, const char* what) {
    if (!p) throw Error(std::string(what) + " is NULL");
    return *p;
}

void copyString(const std::string& s, char* out, size_t size) {
    if (!out) throw Error("output buffer is NULL");
    if (s.size() + 1 > size)
        throw Error("buffer too small (" + std::to_string(s.size() + 1) + " bytes needed)");
    std::memcpy(out, s.c_str(), s.size() + 1);
}

void copyFixed(const std::string& s, char* out, size_t size) {
    std::strncpy(out, s.c_str(), size - 1);
    out[size - 1] = 0;
}

const char* str(const char* s, const char* what) {
    if (!s) throw Error(std::string(what) + " is NULL");
    return s;
}

uint8_t layerOf(Keyboard& k, int layer) {
    if (layer < 0 || layer >= k.model().layers)
        throw Error("layer must be 0-" + std::to_string(k.model().layers - 1));
    return uint8_t(layer);
}

uint8_t osOf(k86_os os) {
    if (os != K86_OS_WINDOWS && os != K86_OS_MACOS) throw Error("os must be K86_OS_WINDOWS or K86_OS_MACOS");
    return uint8_t(os);
}

uint8_t macroOf(int index) {
    if (index < 0 || index >= kMaxMacros) throw Error("macro index must be 0-" + std::to_string(kMaxMacros - 1));
    return uint8_t(index);
}

uint8_t patternOf(Keyboard& k, int pattern) {
    int n = k.model().chip == Chip::YC3123 ? 5 : 1;
    if (pattern < 0 || pattern >= n) throw Error("pattern must be 0-" + std::to_string(n - 1));
    return uint8_t(pattern);
}

int slotOf(Keyboard& k, const char* key) {
    auto s = findSlot(k.defaultMatrix(), str(key, "key"));
    if (!s) throw Error(std::string("unknown key '") + key + "' (see 'k86ctl keys'; '@N' selects slot N)");
    return *s;
}

KeyAction actionOf(const char* text) {
    std::string err;
    auto a = parseAction(str(text, "action"), &err);
    if (!a) throw Error(err.empty() ? std::string("invalid action '") + text + "'" : err);
    return *a;
}

KeyAction at(const KeyMatrix& m, int slot) {
    return {m[size_t(slot) * 4], m[size_t(slot) * 4 + 1], m[size_t(slot) * 4 + 2], m[size_t(slot) * 4 + 3]};
}

const std::vector<LightModeInfo>& modesOf(Keyboard& k, int side) {
    if (side && k.model().sideLightModes.empty()) throw Error(k.model().displayName + " has no side light");
    return side ? k.model().sideLightModes : k.model().lightModes;
}

LightSetting toSetting(Keyboard& k, const k86_light& l, bool side) {
    const auto& modes = modesOf(k, side);
    const LightModeInfo* m = findLightMode(modes, l.mode);
    if (!m) throw Error("unknown light mode " + std::to_string(l.mode));
    const int maxSpeed = side ? k.model().maxSideSpeed : k.model().maxSpeed;
    if (l.brightness > k.model().maxBrightness)
        throw Error("brightness must be 0-" + std::to_string(k.model().maxBrightness));
    if (l.speed > maxSpeed) throw Error("speed must be 0-" + std::to_string(maxSpeed));
    if (m->options ? l.option >= m->options : l.option != 0)
        throw Error("option must be " + (m->options ? "0-" + std::to_string(m->options - 1) : std::string("0")) +
                    " for " + m->name);
    if (l.color > 0xFFFFFF) throw Error("colour must be 0xRRGGBB");
    LightSetting s;
    s.mode = l.mode, s.option = l.option, s.brightness = l.brightness, s.speed = l.speed;
    s.color = l.color, s.rainbow = l.rainbow != 0;
    return s;
}

k86_light fromSetting(const LightSetting& s) {
    k86_light l{};
    l.mode = s.mode, l.option = s.option, l.brightness = s.brightness, l.speed = s.speed;
    l.color = s.color, l.rainbow = s.rainbow ? 1 : 0;
    return l;
}

}  // namespace

extern "C" {

// --------------------------------------------------------------------- library and connection
const char* k86_version(void) { return K86_VERSION_STRING; }

const char* k86_last_error(void) { return lastError.c_str(); }

int k86_list(k86_device* out, int max) {
    int count = -1;
    guard([&] {
        auto cands = findCandidates();
        for (size_t i = 0; i < cands.size() && out && int(i) < max; ++i) {
            k86_device& d = out[i];
            std::memset(&d, 0, sizeof d);
            copyFixed(cands[i].info.path, d.path, sizeof d.path);
            copyFixed(cands[i].info.name, d.name, sizeof d.name);
            d.vid = cands[i].info.vid;
            d.pid = cands[i].info.pid;
            copyFixed(cands[i].model ? cands[i].model->displayName : "", d.model, sizeof d.model);
        }
        count = int(cands.size());
    });
    return count;
}

k86_keyboard* k86_open(const char* path, int force) {
    std::unique_ptr<k86_keyboard> h;
    guard([&] {
        OpenKeyboard ok = openKeyboard(path ? path : "", force != 0);
        h = std::make_unique<k86_keyboard>();
        h->transport = std::move(ok.transport);
        h->kb = std::move(ok.keyboard);
    });
    return h.release();
}

k86_keyboard* k86_open_simulated(k86_chip chip) {
    std::unique_ptr<k86_keyboard> h;
    guard([&] {
        if (chip != K86_CHIP_YC3121 && chip != K86_CHIP_YC3123) throw Error("unknown chip");
        const DeviceModel& m = knownModels()[chip == K86_CHIP_YC3121 ? 0 : 1];
        h = std::make_unique<k86_keyboard>();
        h->transport = std::make_unique<SimulatedTransport>(m);
        h->kb = makeKeyboard(*h->transport, m, [](int) {});
    });
    return h.release();
}

void k86_close(k86_keyboard* kb) { delete kb; }

int k86_get_model(k86_keyboard* kb, k86_model* out) {
    return guard([&] {
        const DeviceModel& m = need(kb).model();
        k86_model& o = needOut(out, "out");
        std::memset(&o, 0, sizeof o);
        copyFixed(m.displayName, o.name, sizeof o.name);
        copyFixed(m.firmwareName, o.firmware, sizeof o.firmware);
        o.chip = m.chip == Chip::YC3121 ? K86_CHIP_YC3121 : K86_CHIP_YC3123;
        o.vid = m.vid, o.pid = m.pid, o.firmware_id = m.firmwareId, o.layers = m.layers;
        o.max_brightness = m.maxBrightness, o.max_speed = m.maxSpeed, o.max_side_speed = m.maxSideSpeed;
        o.has_side_light = m.sideLightModes.empty() ? 0 : 1;
        o.screen_width = m.screenWidth, o.screen_height = m.screenHeight, o.screen_slots = m.screenSlots;
        o.sleep_min = m.sleepMin, o.sleep_max = m.sleepMax;
        o.deep_sleep_min = m.deepSleepMin, o.deep_sleep_max = m.deepSleepMax;
    });
}

// ----------------------------------------------------------------------------------- settings
int k86_get_profile(k86_keyboard* kb, int* layer) {
    return guard([&] {
        auto p = need(kb).getProfile();
        if (!p) throw Error("the keyboard did not report its profile");
        needOut(layer, "layer") = *p;
    });
}

int k86_set_profile(k86_keyboard* kb, int layer) {
    return guard([&] { need(kb).setProfile(layerOf(need(kb), layer)); });
}

int k86_get_debounce(k86_keyboard* kb, int* ms) {
    return guard([&] {
        auto d = need(kb).getDebounce();
        if (!d) throw Error("the keyboard did not report its debounce time");
        needOut(ms, "ms") = *d;
    });
}

int k86_set_debounce(k86_keyboard* kb, int ms) {
    return guard([&] {
        if (ms < 0 || ms > 255) throw Error("debounce must be 0-255 ms");
        need(kb).setDebounce(uint8_t(ms));
    });
}

int k86_get_sleep(k86_keyboard* kb, k86_sleep* out) {
    return guard([&] {
        auto t = need(kb).getSleep();
        if (!t) throw Error("the keyboard did not report its sleep times");
        needOut(out, "out") = {t->bluetooth, t->wireless24, t->deepBluetooth, t->deepWireless24};
    });
}

int k86_set_sleep(k86_keyboard* kb, const k86_sleep* times) {
    return guard([&] {
        Keyboard& k = need(kb);
        const k86_sleep& t = needOut(times, "times");
        const DeviceModel& m = k.model();
        auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
        if (!in(t.bluetooth, m.sleepMin, m.sleepMax) || !in(t.wireless24, m.sleepMin, m.sleepMax) ||
            !in(t.deep_bluetooth, m.deepSleepMin, m.deepSleepMax) ||
            !in(t.deep_wireless24, m.deepSleepMin, m.deepSleepMax))
            throw Error("sleep must be " + std::to_string(m.sleepMin) + "-" + std::to_string(m.sleepMax) +
                        " s and deep sleep " + std::to_string(m.deepSleepMin) + "-" + std::to_string(m.deepSleepMax) +
                        " s");
        k.setSleep({t.bluetooth, t.wireless24, t.deep_bluetooth, t.deep_wireless24});
    });
}

int k86_get_options(k86_keyboard* kb, char* names, size_t size) {
    return guard([&] {
        std::string s;
        for (const auto& [k, v] : need(kb).getOptions()) s += k + "=" + std::to_string(v) + "\n";
        copyString(s, names, size);
    });
}

int k86_get_option(k86_keyboard* kb, const char* name, int* value) {
    return guard([&] {
        auto opts = need(kb).getOptions();
        auto it = opts.find(str(name, "name"));
        if (it == opts.end()) throw Error(std::string("unknown option '") + name + "'");
        needOut(value, "value") = it->second;
    });
}

int k86_set_option(k86_keyboard* kb, const char* name, int value) {
    return guard([&] { need(kb).setOptions({{str(name, "name"), value}}); });
}

// ----------------------------------------------------------------------------------- lighting
int k86_light_mode(k86_keyboard* kb, const char* name, int side) {
    int code = -1;
    guard([&] {
        const LightModeInfo* m = findLightMode(modesOf(need(kb), side), str(name, "name"));
        if (!m) throw Error(std::string("unknown light mode '") + name + "'");
        code = m->code;
    });
    return code;
}

const char* k86_light_mode_name(k86_keyboard* kb, int code, int side) {
    const char* name = nullptr;
    guard([&] {
        if (code < 0 || code > 255) throw Error("unknown light mode");
        const LightModeInfo* m = findLightMode(modesOf(need(kb), side), uint8_t(code));
        if (!m) throw Error("unknown light mode " + std::to_string(code));
        name = m->name.c_str();  // lives in the static model table
    });
    return name;
}

int k86_light_modes(k86_keyboard* kb, int side, char* names, size_t size) {
    return guard([&] {
        std::string s;
        for (const auto& m : modesOf(need(kb), side)) s += m.name + "\n";
        copyString(s, names, size);
    });
}

int k86_get_light(k86_keyboard* kb, k86_light* out) {
    return guard([&] {
        auto l = need(kb).getLight();
        if (!l) throw Error("the keyboard returned an unknown effect");
        needOut(out, "out") = fromSetting(*l);
    });
}

int k86_set_light(k86_keyboard* kb, const k86_light* light) {
    return guard([&] {
        Keyboard& k = need(kb);
        LightSetting s = toSetting(k, needOut(light, "light"), false);
        k.leaveUserAnimation();
        k.setLight(s);
    });
}

int k86_get_side_light(k86_keyboard* kb, k86_light* out) {
    return guard([&] {
        modesOf(need(kb), 1);
        auto l = need(kb).getSideLight();
        if (!l) throw Error("the keyboard returned an unknown side light effect");
        needOut(out, "out") = fromSetting(*l);
    });
}

int k86_set_side_light(k86_keyboard* kb, const k86_light* light) {
    return guard([&] { need(kb).setSideLight(toSetting(need(kb), needOut(light, "light"), true)); });
}

// ------------------------------------------------------------------------- keys and Fn layers
int k86_key_slot(k86_keyboard* kb, const char* key) {
    int slot = -1;
    guard([&] { slot = slotOf(need(kb), key); });
    return slot;
}

int k86_get_key(k86_keyboard* kb, int layer, const char* key, char* action, size_t size) {
    return guard([&] {
        Keyboard& k = need(kb);
        int slot = slotOf(k, key);
        copyString(actionToString(at(k.getKeyMatrix(layerOf(k, layer)), slot)), action, size);
    });
}

int k86_set_key(k86_keyboard* kb, int layer, const char* key, const char* action) {
    return guard([&] {
        Keyboard& k = need(kb);
        KeyAction a = actionOf(action);
        k.setKey(layerOf(k, layer), uint8_t(slotOf(k, key)), a);
    });
}

int k86_reset_keys(k86_keyboard* kb, int layer) {
    return guard([&] {
        Keyboard& k = need(kb);
        k.setKeyMatrix(layerOf(k, layer), k.defaultMatrix());
    });
}

int k86_get_fn_key(k86_keyboard* kb, int layer, k86_os os, const char* key, char* action, size_t size) {
    return guard([&] {
        Keyboard& k = need(kb);
        int slot = slotOf(k, key);
        copyString(actionToString(at(k.getFnMatrix(layerOf(k, layer), osOf(os)), slot)), action, size);
    });
}

int k86_set_fn_key(k86_keyboard* kb, int layer, k86_os os, const char* key, const char* action) {
    return guard([&] {
        Keyboard& k = need(kb);
        KeyAction a = actionOf(action);
        k.setFnKey(layerOf(k, layer), osOf(os), uint8_t(slotOf(k, key)), a);
    });
}

int k86_reset_fn_keys(k86_keyboard* kb, int layer, k86_os os) {
    return guard([&] {
        Keyboard& k = need(kb);
        k.setFnMatrix(layerOf(k, layer), osOf(os), k.defaultFnMatrix());
    });
}

int k86_action_label(const char* action, char* label, size_t size) {
    return guard([&] { copyString(actionLabel(actionOf(action)), label, size); });
}

// ------------------------------------------------------------------------------------- macros
int k86_get_macro(k86_keyboard* kb, int index, char* events, size_t size, int* repeat) {
    return guard([&] {
        Macro m = need(kb).getMacro(macroOf(index));
        copyString(macroToString(m), events, size);
        if (repeat) *repeat = m.repeat;
    });
}

int k86_set_macro(k86_keyboard* kb, int index, const char* events, int repeat) {
    return guard([&] {
        if (repeat < 1 || repeat > 65535) throw Error("repeat must be 1-65535");
        Macro m = parseMacro(str(events, "events"), uint16_t(repeat));
        encodeMacro(m);  // throws if it does not fit
        need(kb).setMacro(macroOf(index), m);
    });
}

int k86_clear_macro(k86_keyboard* kb, int index) {
    return guard([&] { need(kb).setMacro(macroOf(index), Macro{}); });
}

// ---------------------------------------------------------------------------- per-key colours
int k86_get_key_colors(k86_keyboard* kb, int pattern, k86_rgb colors[K86_SLOTS]) {
    return guard([&] {
        Keyboard& k = need(kb);
        auto c = k.getUserPicture(patternOf(k, pattern));
        if (!colors) throw Error("colors is NULL");
        std::copy(c.begin(), c.end(), colors);
    });
}

int k86_set_key_colors(k86_keyboard* kb, int pattern, const k86_rgb colors[K86_SLOTS]) {
    return guard([&] {
        Keyboard& k = need(kb);
        if (!colors) throw Error("colors is NULL");
        std::array<Rgb, kSlots> c{};
        for (int i = 0; i < kSlots; ++i) c[size_t(i)] = colors[i] & 0xFFFFFF;
        uint8_t p = patternOf(k, pattern);
        k.leaveUserAnimation();
        k.setUserPicture(p, c);
    });
}

int k86_set_key_color(k86_keyboard* kb, int pattern, const char* key, k86_rgb color) {
    return guard([&] {
        Keyboard& k = need(kb);
        uint8_t p = patternOf(k, pattern);
        int slot = slotOf(k, key);
        auto c = k.getUserPicture(p);
        c[size_t(slot)] = color & 0xFFFFFF;
        k.leaveUserAnimation();
        k.setUserPicture(p, c);
    });
}

// ------------------------------------------------------------------------------- screen (LCD)
int k86_screen_upload(k86_keyboard* kb, const char* image_path, int slot, int delay_ms, k86_progress_fn progress,
                      void* user) {
    return guard([&] {
        Keyboard& k = need(kb);
        const DeviceModel& m = k.model();
        LoadedImage img = loadImage(str(image_path, "image_path"));
        const int W = m.screenWidth, H = m.screenHeight;
        auto report = [&](double f) {
            if (progress) progress(f, user);
        };
        if (img.frames.size() == 1) {
            if (slot < 1 || slot > m.screenSlots) throw Error("slot must be 1-" + std::to_string(m.screenSlots));
            auto px = fitToScreen(img.frames[0], W, H);
            ScreenImage si = toScreenImage(px, W, contentRect(px, W, H));
            if (!k.screenBegin(uint8_t(slot - 1), 1, 0, si)) throw Error("the keyboard rejected the image");
            k.screenSendFrame(uint8_t(slot - 1), 1, 0, si, report);
            return;
        }
        if (img.frames.size() > 255) throw Error("the GIF has more than 255 frames");
        int delay = delay_ms > 0 ? delay_ms : (!img.delaysMs.empty() && img.delaysMs[0] > 0 ? img.delaysMs[0] : 50);
        delay = std::min(delay, 255);
        std::vector<std::vector<uint32_t>> px;
        std::vector<Rect> rects;
        for (const auto& f : img.frames) {
            px.push_back(fitToScreen(f, W, H));
            rects.push_back(contentRect(px.back(), W, H));
        }
        Rect u = unionRect(rects);
        const uint8_t n = uint8_t(img.frames.size());
        std::vector<ScreenImage> frames;
        for (auto& p : px) frames.push_back(toScreenImage(p, W, u));
        if (!k.screenBegin(0, n, uint8_t(delay), frames[0])) throw Error("the keyboard rejected the GIF");
        for (size_t i = 0; i < frames.size(); ++i)
            k.screenSendFrame(uint8_t(i), n, uint8_t(delay), frames[i],
                              [&](double f) { report((double(i) + f) / double(frames.size())); });
    });
}

int k86_screen_sync_clock(k86_keyboard* kb) {
    return guard([&] { need(kb).syncClock(); });
}

int k86_screen_send_system_info(k86_keyboard* kb) {
    return guard([&] {
        Keyboard& k = need(kb);
        k.sendSystemInfo(kb->sysinfo.sample());
    });
}

int k86_screen_erase(k86_keyboard* kb) {
    return guard([&] {
        if (!need(kb).eraseScreenFlash()) throw Error("the keyboard did not acknowledge the erase");
    });
}

int k86_factory_reset(k86_keyboard* kb) {
    return guard([&] { need(kb).factoryReset(); });
}

}  // extern "C"
