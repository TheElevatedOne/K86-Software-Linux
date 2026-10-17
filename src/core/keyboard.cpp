#include "keyboard.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <stdexcept>

namespace k86 {

namespace {

#include "k86_matrices.inc"

constexpr uint8_t kModeUserPicture = 13;
constexpr uint8_t kModeMusic3 = 20;
constexpr uint8_t kModeScreenColor = 21;
constexpr uint8_t kModeMusic2 = 22;

// Mode list of the original light layouts "Be" (YC3121 K86) and "I" (YC3123 K86).
std::vector<LightModeInfo> k86LightModes(int customPatterns) {
    return {
        {"off", "LightOff", 0, 0, false, false},
        {"static", "LightAlwaysOn", 1, 0, false, true},
        {"breath", "LightBreath", 2, 0, true, true},
        {"neon", "LightNeon", 3, 0, true, false},
        {"wave", "LightWave", 4, 4, true, true},
        {"ripple", "LightRipple", 5, 0, true, true},
        {"raindrop", "LightRaindrop", 6, 0, true, true},
        {"snake", "LightSnake", 7, 2, true, true},
        {"reactive", "LightPressAction", 8, 0, true, true},
        {"converge", "LightConverage", 9, 0, true, true},
        {"sinewave", "LightSineWave", 10, 0, true, true},
        {"kaleidoscope", "LightKaleidoscope", 11, 2, true, true},
        {"linewave", "LightLineWave", 12, 2, true, true},
        {"custom", "LightUserPicture", kModeUserPicture, customPatterns, false, false},
        {"laser", "LightLaser", 14, 0, true, true},
        {"circlewave", "LightCircleWave", 15, 2, true, true},
        {"dazzle", "LightDazzing", 16, 0, true, true},
        {"raindown", "LightRainDown", 17, 0, true, true},
        {"meteor", "LightMeteor", 18, 0, true, true},
        {"reactiveoff", "LightPressActionOff", 19, 0, true, true},
        {"music", "LightMusicFollow2", kModeMusic2, 3, false, true},
        {"screencolor", "LightScreenColor", kModeScreenColor, 0, false, false},
    };
}

// Side light layout "he" (YC3123 K86); codes are the side-light command's own mode numbers.
std::vector<LightModeInfo> k86SideModes() {
    return {
        {"off", "LightOff", 0, 0, false, false},
        {"static", "LightAlwaysOn", 1, 0, false, true},
        {"breath", "LightBreath", 2, 0, true, true},
        {"neon", "LightNeon", 3, 0, true, false},
        {"wave", "LightWave", 4, 0, true, true},
        {"snake", "LightSnake", 5, 0, true, true},
    };
}

std::vector<DeviceModel> buildModels() {
    std::vector<DeviceModel> v;
    DeviceModel a;
    a.displayName = "K86 (YC3121)";
    a.firmwareName = "yc3121_sg8967_3m_soc";
    a.vid = 0x3151;
    a.pid = 0x4015;
    a.firmwareId = 1168;
    a.chip = Chip::YC3121;
    a.layers = 3;
    a.defaultMatrix = kK86v1Matrix;
    a.defaultFnMatrix = kK86v1FnMatrix;
    a.lightModes = k86LightModes(3);
    a.maxBrightness = 4;
    a.maxSpeed = 4;
    a.sideLightModes = k86SideModes();  // not offered by the original for this chip
    a.maxSideSpeed = 3;
    a.screenWidth = 240;
    a.screenHeight = 135;
    a.screenSlots = 5;
    a.canSyncSystemInfo = false;
    a.canSwitchLanguage = false;
    a.sleepMin = 60;
    a.sleepMax = 3600;
    a.deepSleepMin = 600;
    a.deepSleepMax = 3600;
    a.vendorSleepMs = 500;
    a.waitForCommit = true;
    v.push_back(a);

    DeviceModel b = a;
    b.displayName = "K86 (YC3123)";
    b.firmwareName = "yc3123_sg_sg8967_3m_1k";
    b.pid = 0x5002;
    b.firmwareId = 2730;
    b.chip = Chip::YC3123;
    b.defaultMatrix = kK86v2Matrix;
    b.defaultFnMatrix = kK86v2FnMatrix;
    b.lightModes = k86LightModes(5);
    b.canSyncSystemInfo = true;
    b.canSwitchLanguage = true;
    b.sleepMin = 0;
    b.sleepMax = 18 * 3600;
    b.deepSleepMin = 10;
    b.deepSleepMax = 18 * 3600;
    b.vendorSleepMs = 100;
    b.waitForCommit = false;  // not verified on hardware
    v.push_back(b);
    return v;
}

uint16_t u16(const Report& r, int lo) { return uint16_t(r[lo] | (r[lo + 1] << 8)); }

std::vector<uint8_t> zeros(size_t n) { return std::vector<uint8_t>(n, 0); }

std::vector<uint8_t> rgbBytes(const std::array<Rgb, kSlots>& c) {
    std::vector<uint8_t> d(kSlots * 3);
    for (int i = 0; i < kSlots; ++i) {
        d[i * 3] = uint8_t(c[i] >> 16);
        d[i * 3 + 1] = uint8_t(c[i] >> 8);
        d[i * 3 + 2] = uint8_t(c[i]);
    }
    return d;
}

std::array<Rgb, kSlots> rgbFromBytes(const std::vector<uint8_t>& d) {
    std::array<Rgb, kSlots> c{};
    for (int i = 0; i < kSlots && size_t(i * 3 + 2) < d.size(); ++i)
        c[i] = (Rgb(d[i * 3]) << 16) | (Rgb(d[i * 3 + 1]) << 8) | d[i * 3 + 2];
    return c;
}

KeyMatrix toMatrix(const std::vector<uint8_t>& d) {
    KeyMatrix m{};
    std::copy_n(d.begin(), std::min(d.size(), m.size()), m.begin());
    return m;
}

// The original replaces pure white with this value on write and maps it back on read.
constexpr Rgb kWhite = 0xFFFFFF, kWhiteOnDevice = 16449530;  // 0xFAFFFA

// Colour presets encoded in the low nibble of the mode byte (COMMONCOLOR in the original).
Rgb presetColor(uint8_t idx, bool& ok) {
    static const Rgb table[] = {0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF};
    ok = idx < 7;
    return ok ? table[idx] : 0;
}

void putColor(std::vector<uint8_t>& e, Rgb c) {
    if (c == kWhite) c = kWhiteOnDevice;
    e[5] = uint8_t(c >> 16);
    e[6] = uint8_t(c >> 8);
    e[7] = uint8_t(c);
}

LightSetting decodeLight(const Report& e, int speed, bool musicMode, uint8_t dazzle, uint8_t normal) {
    LightSetting l;
    l.mode = e[1];
    l.speed = uint8_t(speed);
    l.brightness = e[3];
    l.option = e[4] >> 4;
    uint8_t c = e[4] & 15;
    Rgb rgb = (Rgb(e[5]) << 16) | (Rgb(e[6]) << 8) | e[7];
    if (c != dazzle && c != normal) {
        bool ok;
        Rgb p = presetColor(c, ok);
        if (ok) rgb = p;
    }
    if (rgb == kWhiteOnDevice) rgb = kWhite;
    l.color = rgb;
    l.rainbow = musicMode ? c == 0 : c == dazzle;
    return l;
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

int requireOption(const std::map<std::string, int>& m, const std::string& k) {
    auto it = m.find(k);
    if (it == m.end()) throw std::runtime_error("option '" + k + "' missing");
    return it->second;
}

void mergeOptions(std::map<std::string, int>& cur, const std::map<std::string, int>& changes,
                  const std::vector<std::string>& writable) {
    for (const auto& [k, v] : changes) {
        if (std::find(writable.begin(), writable.end(), k) == writable.end()) {
            std::string list;
            for (const auto& w : writable) list += " " + w;
            throw std::runtime_error("option '" + k + "' is not writable on this keyboard (writable:" + list + ")");
        }
        cur[k] = v;
    }
}

// ---------------------------------------------------------------------------------------
// YC3121 firmware ("CommonKbYc500" class family in the original driver).
class Yc3121Keyboard : public Keyboard {
public:
    using Keyboard::Keyboard;

    Versions readVersions() override {
        Versions v;
        Report r = s_.query({0x80}, Checksum::Bit7);
        v.usb = uint16_t(r[2] << 8 | r[1]);
        r = s_.query({0xAE}, Checksum::Bit7, 10, 100);
        if (r[0] == 0xAE) v.mled = uint16_t(r[2] << 8 | r[1]);
        r = s_.query({0xAD}, Checksum::Bit7, 10, 100);
        if (r[0] == 0xAD) {
            v.oled = u16(r, 1);
            v.flash = u16(r, 3);
        }
        return v;
    }

    void setProfile(uint8_t p) override {
        s_.send({0x05, p}, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<uint8_t> getProfile() override { return s_.query({0x85}, Checksum::Bit7)[1]; }

    void setDebounce(uint8_t ms) override {
        s_.send({0x11, 0, ms}, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<uint8_t> getDebounce() override { return s_.query({0x91}, Checksum::Bit7)[2]; }

    void setSleep(const SleepTimes& t) override {
        auto e = zeros(64);
        e[0] = 0x12;
        putSleep(e, t);
        s_.send(e, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<SleepTimes> getSleep() override {
        Report r = s_.query({0x92, 0, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        SleepTimes t;
        t.bluetooth = u16(r, 1);
        t.wireless24 = u16(r, 3);
        t.deepBluetooth = u16(r, 5);
        t.deepWireless24 = u16(r, 7);
        return t;
    }

    std::map<std::string, int> getOptions() override {
        auto e = zeros(64);
        e[0] = 0x86;
        Report r = s_.query(e, Checksum::Bit7);
        return {
            {"winLock", r[2] & 1},
            {"macMode", (r[2] >> 1) & 1},
            {"wasdSwap", (r[2] >> 3) & 1},
            {"ledOff", (r[2] >> 4) & 1},
            {"sideLedOff", (r[2] >> 5) & 1},
            {"keyboardMode", (r[2] >> 6) & 1},
            {"keyboardLock", (r[2] >> 7) & 1},
            {"fnMatrix", r[3] & 1},
            {"powerSave", r[4] ? 1 : 0},
        };
    }
    void setOptions(const std::map<std::string, int>& changes) override {
        auto cur = getOptions();
        mergeOptions(cur, changes,
                     {"winLock", "wasdSwap", "ledOff", "sideLedOff", "keyboardMode", "keyboardLock", "fnMatrix",
                      "powerSave"});
        auto b = [&](const char* k) { return requireOption(cur, k) ? 1 : 0; };
        auto n = zeros(64);
        n[0] = 0x06;
        n[1] = 0;
        // Bit 1 is always written as 0 and the "system" bit 2 ends up 0 in the original
        // (it shifts Number("win"/"mac"), which is NaN); reproduced as is.
        n[2] = uint8_t(b("winLock") | b("wasdSwap") << 3 | b("ledOff") << 4 | b("sideLedOff") << 5 |
                       b("keyboardMode") << 6 | b("keyboardLock") << 7);
        n[3] = uint8_t(b("fnMatrix"));
        n[4] = uint8_t(b("powerSave"));
        s_.send(n, Checksum::Bit7);
    }

    KeyMatrix getKeyMatrix(uint8_t layer) override {
        return toMatrix(readPaged([&](int p) { auto n = zeros(64); n[0] = 0x89; n[1] = layer; n[2] = uint8_t(p); return n; }, 8));
    }
    void setKeyMatrix(uint8_t layer, const KeyMatrix& m) override {
        sendPaged({m.begin(), m.end()}, 9, [&](int p, size_t) {
            return std::vector<uint8_t>{0x09, layer, 248, 1, uint8_t(p), 0, 0, 0};
        });
        vendorSleep();
    }
    void setKey(uint8_t layer, uint8_t slot, const KeyAction& a) override {
        if (slot > 127) throw std::runtime_error("slot out of range");
        auto n = zeros(64);
        n[0] = 0x13;
        n[1] = layer;
        n[2] = slot;
        std::copy(a.begin(), a.end(), n.begin() + 8);
        s_.send(n, Checksum::Bit7);
        vendorSleep();
    }
    KeyMatrix getFnMatrix(uint8_t layer, uint8_t) override {
        return toMatrix(readPaged([&](int p) { auto n = zeros(64); n[0] = 0x90; n[1] = layer; n[2] = uint8_t(p); return n; }, 8));
    }
    void setFnMatrix(uint8_t layer, uint8_t, const KeyMatrix& m) override {
        sendPaged({m.begin(), m.end()}, 9, [&](int p, size_t) {
            return std::vector<uint8_t>{0x10, layer, 248, 1, uint8_t(p), 0, 0, 0};
        });
        vendorSleep();
    }
    void setFnKey(uint8_t layer, uint8_t, uint8_t slot, const KeyAction& a) override {
        auto n = zeros(64);
        n[0] = 0x15;
        n[1] = layer;
        n[2] = slot;
        std::copy(a.begin(), a.end(), n.begin() + 8);
        s_.send(n, Checksum::Bit7);
        vendorSleep();
    }

    std::array<Rgb, kSlots> getUserPicture(uint8_t) override {
        return rgbFromBytes(readPaged([&](int p) { return std::vector<uint8_t>{0x8C, 0, uint8_t(p), 0, 0, 0, 0, 0}; }, 6));
    }
    void setUserPicture(uint8_t, const std::array<Rgb, kSlots>& colors) override {
        // Header bytes 2..3 carry 64 * floor(slots * 3 / 64) = 384.
        constexpr int total = 64 * (kSlots * 3 / 64);
        sendPaged(rgbBytes(colors), 7, [&](int p, size_t) {
            return std::vector<uint8_t>{0x0C, 0, uint8_t(total & 0xFF), uint8_t(total >> 8), uint8_t(p), 0, 0, 0};
        });
        vendorSleep();
    }
    void startUserAnimation() override { s_.send({0x18}, Checksum::Bit7); }
    void sendUserAnimationFrame(uint8_t frame, uint8_t count, uint16_t delay,
                                const std::array<Rgb, kSlots>& colors) override {
        sendPaged(rgbBytes(colors), 7, [&](int p, size_t) {
            return std::vector<uint8_t>{0x19, uint8_t(p), frame, count, uint8_t(delay), 0, 0, 0};
        }, 1);
    }

    void factoryReset() override {
        s_.send({0x02}, Checksum::Bit7);
        s_.sleep(4000);
    }

protected:
    uint8_t cmdSetMacro() const override { return 0x16; }
    uint8_t maxSpeed() const override { return 5; }

    static void putSleep(std::vector<uint8_t>& e, const SleepTimes& t) {
        const uint16_t v[] = {t.bluetooth, t.wireless24, t.deepBluetooth, t.deepWireless24};
        for (int i = 0; i < 4; ++i) {
            e[8 + i * 2] = uint8_t(v[i] & 0xFF);
            e[9 + i * 2] = uint8_t(v[i] >> 8);
        }
    }
};

// ---------------------------------------------------------------------------------------
// YC3123 firmware (common RongYuan keyboard class in the original driver).
class Yc3123Keyboard : public Yc3121Keyboard {
public:
    using Yc3121Keyboard::Yc3121Keyboard;

    Versions readVersions() override {
        Versions v;
        Report r = s_.query({0x8F}, Checksum::Bit7);
        v.usb = uint16_t(r[8] << 8 | r[7]);
        r = s_.query({0x80}, Checksum::Bit7);
        if (uint16_t rf = uint16_t(r[2] << 8 | r[1])) v.rf = rf;
        r = s_.query({0xAE}, Checksum::Bit7);
        if (r[0] == 0xAE)
            if (uint16_t m = uint16_t(r[2] << 8 | r[1])) v.mled = m;
        r = s_.query({0xAD}, Checksum::Bit7);
        if (r[0] == 0xAD) {
            v.oled = uint16_t(r[2] << 8 | r[1]);
            v.flash = uint16_t(r[4] << 8 | r[3]);
        }
        return v;
    }

    void setProfile(uint8_t p) override {
        s_.send({0x04, p, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<uint8_t> getProfile() override {
        auto e = zeros(64);
        e[0] = 0x84;
        return s_.query(e, Checksum::Bit7)[1];
    }

    void setDebounce(uint8_t ms) override {
        s_.send({0x06, ms, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<uint8_t> getDebounce() override {
        return s_.query({0x86, 0, 0, 0, 0, 0, 0, 0}, Checksum::Bit7)[1];
    }

    void setSleep(const SleepTimes& t) override {
        auto e = zeros(64);
        e[0] = 0x11;
        putSleep(e, t);
        s_.send(e, Checksum::Bit7);
        vendorSleep();
    }
    std::optional<SleepTimes> getSleep() override {
        Report r = s_.query({0x91, 0, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        SleepTimes t;
        t.bluetooth = u16(r, 8);
        t.wireless24 = u16(r, 10);
        t.deepBluetooth = u16(r, 12);
        t.deepWireless24 = u16(r, 14);
        return t;
    }

    std::map<std::string, int> getOptions() override {
        Report e = s_.query({0x89, 0, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        Report p = s_.query({0x85}, Checksum::Bit7);
        return {
            // 0 = Windows, 1 = macOS, 2 = iOS, 3 = Android (as decoded by the original)
            {"system", e[1] <= 3 ? e[1] : 0},
            {"fnLayer", e[2]},
            {"antiMistouch", e[3] ? 1 : 0},
            {"rtStab", e[4] > 5 ? 0 : e[4] * 25},
            {"wasdSwap", e[5] == 1 ? 1 : 0},
            {"lowPower", p[1] == 1 ? 1 : 0},
        };
    }
    void setOptions(const std::map<std::string, int>& changes) override {
        auto cur = getOptions();
        mergeOptions(cur, changes, {"fnLayer", "antiMistouch", "rtStab", "wasdSwap"});
        std::vector<uint8_t> e = {0x09, 0,
                                  uint8_t(requireOption(cur, "fnLayer")),
                                  uint8_t(requireOption(cur, "antiMistouch") ? 1 : 0),
                                  uint8_t(requireOption(cur, "rtStab") / 25),
                                  uint8_t(requireOption(cur, "wasdSwap") ? 1 : 0), 0, 0};
        s_.send(e, Checksum::Bit7);
        vendorSleep();
    }

    KeyMatrix getKeyMatrix(uint8_t layer) override {
        return toMatrix(readPaged([&](int p) {
            auto n = zeros(64);
            n[0] = 0x8A, n[1] = layer, n[2] = 255, n[3] = uint8_t(p), n[4] = 0;
            return n;
        }, 8));
    }
    void setKeyMatrix(uint8_t layer, const KeyMatrix& m) override {
        const int pages = int((m.size() + 55) / 56);
        sendPaged({m.begin(), m.end()}, pages, [&](int p, size_t len) {
            return std::vector<uint8_t>{0x0A, layer, 255, uint8_t(p), uint8_t(len), uint8_t(p == pages - 1), 0, 0};
        });
        vendorSleep();
    }
    void setKey(uint8_t layer, uint8_t slot, const KeyAction& a) override {
        if (slot > 127) throw std::runtime_error("slot out of range");
        auto n = zeros(64);
        n[0] = 0x0A, n[1] = layer, n[2] = slot, n[5] = 1, n[6] = 0;
        std::copy(a.begin(), a.end(), n.begin() + 8);
        s_.send(n, Checksum::Bit7);
        vendorSleep();
    }
    KeyMatrix getFnMatrix(uint8_t layer, uint8_t os) override {
        return toMatrix(readPaged([&](int p) {
            auto n = zeros(64);
            n[0] = 0x90, n[1] = os, n[2] = layer, n[3] = 255, n[4] = uint8_t(p);
            return n;
        }, 8));
    }
    void setFnMatrix(uint8_t layer, uint8_t os, const KeyMatrix& m) override {
        sendPaged({m.begin(), m.end()}, 10, [&](int p, size_t) {
            return std::vector<uint8_t>{0x10, os, layer, 255, uint8_t(p), uint8_t(p == 9 ? 0 : 56), uint8_t(p == 9), 0};
        });
        vendorSleep();
    }
    void setFnKey(uint8_t layer, uint8_t os, uint8_t slot, const KeyAction& a) override {
        auto n = zeros(64);
        n[0] = 0x10, n[1] = os, n[2] = layer, n[3] = slot;
        std::copy(a.begin(), a.end(), n.begin() + 8);
        s_.send(n, Checksum::Bit7);
        vendorSleep();
    }

    std::array<Rgb, kSlots> getUserPicture(uint8_t layer) override {
        return rgbFromBytes(readPaged([&](int p) {
            return std::vector<uint8_t>{0x8C, layer, 255, uint8_t(p), 0, 0, 0, 0};
        }, 6));
    }
    void setUserPicture(uint8_t layer, const std::array<Rgb, kSlots>& colors) override {
        sendPaged(rgbBytes(colors), 7, [&](int p, size_t) {
            // The original announces 378 bytes (126 keys) in total: 6 x 56 + 42.
            return std::vector<uint8_t>{0x0C, layer, 255, uint8_t(p), uint8_t(p == 6 ? 378 - 56 * 6 : 56),
                                        uint8_t(p == 6), 0, 0};
        });
        vendorSleep();
    }
    void startUserAnimation() override {
        s_.send({0x12, 0, 0, 0, 0, 0, 0, 0}, Checksum::Bit7);
        vendorSleep(300);
    }
    void sendUserAnimationFrame(uint8_t frame, uint8_t count, uint16_t delay,
                                const std::array<Rgb, kSlots>& colors) override {
        auto data = rgbBytes(colors);
        for (int p = 0; p < 7; ++p) {
            std::vector<uint8_t> msg = {0x12, frame, uint8_t(p), 1, count, uint8_t(delay & 0xFF), uint8_t(delay >> 8), 0};
            for (int i = 0; i < 56; ++i) {
                size_t idx = size_t(p) * 56 + i;
                msg.push_back(idx < data.size() ? data[idx] : 0);
            }
            s_.send(msg, Checksum::Bit7, 0);
            s_.sleep(5);
        }
    }

    void factoryReset() override {
        auto e = zeros(64);
        e[0] = 0x01;
        s_.send(e, Checksum::Bit7);
        s_.sleep(2000);
    }

protected:
    uint8_t cmdSetMacro() const override { return 0x0B; }
    uint8_t maxSpeed() const override { return 4; }
};

}  // namespace

const std::vector<DeviceModel>& knownModels() {
    static const std::vector<DeviceModel> models = buildModels();
    return models;
}

const DeviceModel* findModel(uint16_t vid, uint16_t pid, std::optional<uint32_t> firmwareId) {
    for (const auto& m : knownModels()) {
        if (m.vid != vid || m.pid != pid) continue;
        if (firmwareId && *firmwareId != m.firmwareId) continue;
        return &m;
    }
    return nullptr;
}

Keyboard::Keyboard(Transport& t, const DeviceModel& model, Session::SleepFn sleeper)
    : s_(t, std::move(sleeper)), model_(model) {
    std::copy_n(model.defaultMatrix, defaults_.size(), defaults_.begin());
    std::copy_n(model.defaultFnMatrix, fnDefaults_.size(), fnDefaults_.begin());
}

void Keyboard::vendorSleep(int ms) {
    s_.sleep(ms < 0 ? model_.vendorSleepMs : ms);
    if (waitForCommit_ && model_.waitForCommit) waitUntilIdle();
}

// While the firmware commits a write, every feature report transfer is stalled by ~100 ms;
// afterwards a device id query completes in a few ms. Wait for two fast queries in a row.
void Keyboard::waitUntilIdle() {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    int fast = 0;
    while (fast < 2 && Clock::now() < deadline) {
        auto start = Clock::now();
        try {
            s_.query({0x8F}, Checksum::Bit7);
        } catch (const std::runtime_error&) {  // a stalled transfer can time out
            fast = 0;
            continue;
        }
        fast = Clock::now() - start < std::chrono::milliseconds(60) ? fast + 1 : 0;
    }
}

std::optional<uint32_t> Keyboard::readDeviceId() {
    for (int i = 0; i < 3; ++i) {
        Report r = s_.query({0x8F}, Checksum::Bit7);
        if (r[0] == 0x8F) return uint32_t(r[1]) | uint32_t(r[2]) << 8 | uint32_t(r[3]) << 16 | uint32_t(r[4]) << 24;
    }
    return std::nullopt;
}

void Keyboard::sendPaged(const std::vector<uint8_t>& data, int pages,
                         const std::function<std::vector<uint8_t>(int, size_t)>& header, int preDelayMs) {
    for (int p = 0; p < pages; ++p) {
        size_t begin = size_t(p) * 56;
        size_t len = begin < data.size() ? std::min<size_t>(56, data.size() - begin) : 0;
        std::vector<uint8_t> msg = header(p, len);
        msg.resize(8, 0);
        for (size_t i = 0; i < 56; ++i) msg.push_back(i < len ? data[begin + i] : 0);
        s_.send(msg, Checksum::Bit7, preDelayMs);
    }
}

std::vector<uint8_t> Keyboard::readPaged(const std::function<std::vector<uint8_t>(int)>& cmd, int pages) {
    std::vector<uint8_t> out;
    for (int p = 0; p < pages; ++p) {
        Report r = s_.query(cmd(p), Checksum::Bit7);
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

void Keyboard::setLight(const LightSetting& l) {
    if (l.speed > maxSpeed()) throw std::runtime_error("speed out of range");
    auto e = zeros(64);
    e[0] = 0x07;
    uint8_t r = uint8_t(l.option << 4 | (l.rainbow ? kDazzle : kNormal));
    if (l.mode == kModeUserPicture) r = uint8_t(l.option << 4);
    if (l.mode == kModeMusic3 || l.mode == kModeMusic2) r = uint8_t(l.option << 4 | (l.rainbow ? 0 : 4));
    if (l.mode == kModeScreenColor) r = 0;
    e[1] = l.mode;
    e[2] = uint8_t(maxSpeed() - l.speed);
    e[3] = l.brightness;
    e[4] = r;
    putColor(e, l.color);
    if (l.mode == kModeUserPicture) e[5] = 0, e[6] = 200, e[7] = 200;
    s_.send(e, Checksum::Bit8);
    vendorSleep();
}

std::optional<LightSetting> Keyboard::getLight() {
    auto t = zeros(64);
    t[0] = 0x87;
    Report e = s_.query(t, Checksum::Bit7);
    if (e[1] > 24 && e[1] != kModeUserAnimation) return std::nullopt;
    return decodeLight(e, std::max(0, int(maxSpeed()) - int(e[2])), e[1] == kModeMusic3 || e[1] == kModeMusic2, kDazzle, kNormal);
}

bool Keyboard::leaveUserAnimation() {
    auto l = getLight();
    if (!l || l->mode != kModeUserAnimation) return false;
    LightSetting custom;
    custom.mode = kModeUserPicture;
    custom.brightness = uint8_t(model_.maxBrightness);
    setLight(custom);
    return true;
}

// Same packets on both chips; the original only uses them for the YC3123, but the YC3121 K86
// answers them too.
void Keyboard::setSideLight(const LightSetting& l) {
    if (model_.sideLightModes.empty()) throw std::runtime_error(model_.displayName + " has no side light");
    auto e = zeros(64);
    e[0] = 0x08;
    uint8_t a = uint8_t(l.option << 4 | (l.rainbow ? kDazzle : kNormal));
    if (l.mode == 3) a = 8;                                       // neon
    if (l.mode == 6) a = uint8_t(l.option << 4 | (l.rainbow ? 0 : 4));  // music
    e[1] = l.mode;
    e[2] = l.speed;
    e[3] = l.brightness;
    e[4] = a;
    putColor(e, l.color);
    s_.send(e, Checksum::Bit8);
    vendorSleep();
}
std::optional<LightSetting> Keyboard::getSideLight() {
    if (model_.sideLightModes.empty()) throw std::runtime_error(model_.displayName + " has no side light");
    auto t = zeros(64);
    t[0] = 0x88;
    Report e = s_.query(t, Checksum::Bit7);
    if (e[1] > 6) return std::nullopt;
    return decodeLight(e, e[2], e[1] == 6, kDazzle, kNormal);
}

Macro Keyboard::getMacro(uint8_t index) {
    std::vector<uint8_t> buf;
    for (int p = 0; p < 4; ++p) {
        auto n = zeros(64);
        n[0] = 0x8B, n[1] = index, n[2] = uint8_t(p);
        Report r = s_.query(n, Checksum::Bit7);
        buf.insert(buf.end(), r.begin(), r.end());
        bool end = false;
        for (size_t i = 0; i + 3 < r.size(); ++i)
            if (!r[i] && !r[i + 1] && !r[i + 2] && !r[i + 3]) end = true;
        if (end) break;
    }
    return decodeMacro(buf);
}

void Keyboard::setMacro(uint8_t index, const Macro& m) {
    auto data = encodeMacro(m);
    // Like the original, only the leading 56-byte chunks that contain data are sent.
    int count = 0;
    for (int c = 0; c < 5; ++c) {
        size_t b = size_t(c) * 56, e = std::min(data.size(), b + 56);
        if (std::any_of(data.begin() + b, data.begin() + e, [](uint8_t v) { return v != 0; })) ++count;
    }
    // An empty macro still needs its first chunk, otherwise clearing a slot sends nothing.
    count = std::max(count, 1);
    sendPaged(data, count, [&](int p, size_t) {
        return std::vector<uint8_t>{cmdSetMacro(), index, uint8_t(p), 56, uint8_t(p == count - 1), 0, 0, 0};
    });
    vendorSleep();
}

bool Keyboard::screenBegin(uint8_t frame, uint8_t frameCount, uint8_t delay, const ScreenImage& img) {
    auto i = zeros(64);
    uint32_t s = uint32_t(img.data.size());
    i[0] = 0xA5, i[1] = frame, i[2] = frameCount, i[3] = delay;
    i[4] = uint8_t(s & 0xFF), i[5] = uint8_t((s & 0xFFFF) >> 8), i[6] = 0;
    i[8] = uint8_t(img.left), i[9] = uint8_t(img.top), i[10] = uint8_t(img.right), i[11] = uint8_t(img.bottom);
    i[12] = uint8_t(img.left >> 8), i[13] = uint8_t(img.top >> 8), i[14] = uint8_t(img.right >> 8),
    i[15] = uint8_t(img.bottom >> 8);
    i[16] = uint8_t((s & 0xFFFFFF) >> 16), i[17] = uint8_t(s >> 24), i[18] = 0;
    Report r = s_.query(i, Checksum::Bit7, 10, 100);
    for (int tries = 0; r[1] == 0 && tries < 10; ++tries) {
        s_.sleep(100);
        r = s_.query(i, Checksum::Bit7, 10, 100);
    }
    s_.sleep(100);
    return r[1] == 1;
}

void Keyboard::screenSendFrame(uint8_t frame, uint8_t frameCount, uint8_t delay, const ScreenImage& img,
                               const Progress& progress) {
    const size_t n = (img.data.size() + 55) / 56;
    for (size_t p = 0; p < n; ++p) {
        size_t len = std::min<size_t>(56, img.data.size() - p * 56);
        std::vector<uint8_t> msg = {0x25, frame, frameCount, delay, uint8_t(p & 0xFF), uint8_t(p >> 8), uint8_t(len), 0};
        for (size_t i = 0; i < 56; ++i) msg.push_back(i < len ? img.data[p * 56 + i] : 0);
        s_.send(msg, Checksum::Bit7, 1);
        if (progress) progress(double(p) / double(n));
    }
    if (progress) progress(1.0);
}

void Keyboard::syncClock() {
    auto t = zeros(64);
    t[0] = 0x28;
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    int year = lt.tm_year + 1900;
    t[8] = uint8_t(year >> 8), t[9] = uint8_t(year & 0xFF), t[10] = uint8_t(lt.tm_mon + 1);
    t[11] = uint8_t(lt.tm_mday), t[12] = uint8_t(lt.tm_hour), t[13] = uint8_t(lt.tm_min), t[14] = uint8_t(lt.tm_sec);
    s_.send(t, Checksum::Bit7, 0);
}

void Keyboard::sendSystemInfo(const SystemInfo& in) {
    auto gb = [](double bytes) { return uint16_t(std::llround(bytes / 1024.0 / 1024.0 / 1024.0)); };
    auto clamp8 = [](int v) { return uint8_t(std::clamp(v, 0, 255)); };
    uint16_t e = gb(in.diskAvailableBytes), i = gb(in.diskTotalBytes), n = gb(in.memUsedBytes),
             s = gb(in.memTotalBytes), o = gb(in.netTotalUpBytes), f = gb(in.netTotalDownBytes);
    auto l = zeros(64);
    l[0] = 0x22;
    l[8] = uint8_t(e), l[9] = uint8_t(e >> 8), l[10] = uint8_t(i), l[11] = uint8_t(i >> 8);
    l[12] = uint8_t(n), l[13] = uint8_t(n >> 8), l[14] = uint8_t(s), l[15] = uint8_t(s >> 8);
    l[16] = clamp8(in.cpuUsagePercent), l[17] = clamp8(in.cpuTemperatureC);
    l[18] = uint8_t(o), l[19] = uint8_t(o >> 8), l[20] = uint8_t(f), l[21] = uint8_t(f >> 8);
    s_.send(l, Checksum::Bit7);
}

void Keyboard::switchScreenLanguage(bool value) {
    s_.send({0x27, uint8_t(value ? 1 : 0)}, Checksum::Bit7);
    vendorSleep();
}

bool Keyboard::eraseScreenFlash() {
    auto t = zeros(64);
    t[0] = 0xAC;
    Report e = s_.query(t, Checksum::Bit7);
    return e[0] == 0xAC && e[1] == 0xAA && e[2] == 0xAA && e[3] == 0x55 && e[4] == 0x55;
}

std::unique_ptr<Keyboard> makeKeyboard(Transport& t, const DeviceModel& model, Session::SleepFn sleeper) {
    if (model.chip == Chip::YC3121) return std::make_unique<Yc3121Keyboard>(t, model, std::move(sleeper));
    return std::make_unique<Yc3123Keyboard>(t, model, std::move(sleeper));
}

std::string slotName(const KeyMatrix& d, int slot) {
    KeyAction a{d[slot * 4], d[slot * 4 + 1], d[slot * 4 + 2], d[slot * 4 + 3]};
    if (a == KeyAction{0, 0, 0, 0}) return {};
    std::string n = actionToString(a);
    if (n.rfind("consumer:", 0) == 0) return n.substr(9);
    if (n.rfind("raw:", 0) == 0) return "Slot" + std::to_string(slot);
    return n;
}

std::optional<int> findSlot(const KeyMatrix& d, const std::string& nameIn) {
    std::string name = lower(nameIn);
    auto numeric = [](const std::string& s, int& v) {
        if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) return false;
        v = std::stoi(s);
        return true;
    };
    int v;
    if (name.size() > 1 && name[0] == '@' && numeric(name.substr(1), v) && v < kSlots) return v;
    int wanted = 0;
    auto hash = name.find('#');
    if (hash != std::string::npos) {
        if (!numeric(name.substr(hash + 1), wanted)) return std::nullopt;
        name = name.substr(0, hash);
    }
    int seen = 0;
    for (int i = 0; i < kSlots; ++i) {
        if (lower(slotName(d, i)) == name) {
            if (seen == wanted) return i;
            ++seen;
        }
    }
    // Accept key aliases (e.g. "Escape" for "Esc").
    if (auto u = hidUsageFromName(name)) {
        seen = 0;
        for (int i = 0; i < kSlots; ++i)
            if (d[i * 4] == 0 && d[i * 4 + 1] == 0 && d[i * 4 + 3] == 0 && d[i * 4 + 2] == *u) {
                if (seen == wanted) return i;
                ++seen;
            }
    }
    return std::nullopt;
}

const LightModeInfo* findLightMode(const std::vector<LightModeInfo>& modes, const std::string& name) {
    std::string n = lower(name);
    for (const auto& m : modes)
        if (m.name == n || lower(m.original) == n) return &m;
    return nullptr;
}

const LightModeInfo* findLightMode(const std::vector<LightModeInfo>& modes, uint8_t code) {
    for (const auto& m : modes)
        if (m.code == code) return &m;
    return nullptr;
}

}  // namespace k86
