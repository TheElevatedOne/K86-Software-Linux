// Unit tests. The golden files in tests/golden/ were produced by running the original
// driver's JavaScript device classes against a recording transport (tests/js/), so these
// tests check that the C++ port emits byte-identical feature reports.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "keyboard.hpp"
#include "layout.hpp"
#include "macro.hpp"
#include "protocol.hpp"
#include "screen.hpp"
#include "simulator.hpp"

using namespace k86;

namespace {

int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; \
        }                                                                        \
    } while (0)

// Records sent reports; answers reads with a queue of canned responses (zeros when empty).
class RecordingTransport : public Transport {
public:
    std::vector<Report> sent;
    std::vector<Report> responses;
    size_t reads = 0;
    void sendFeature(const Report& r) override { sent.push_back(r); }
    Report getFeature() override {
        ++reads;
        if (responses.empty()) return Report{};
        Report r = responses.front();
        responses.erase(responses.begin());
        return r;
    }
};

std::string hex(const Report& r) {
    std::string s;
    char b[4];
    for (auto v : r) {
        snprintf(b, sizeof b, "%02x", v);
        s += b;
    }
    return s;
}

// golden file format: "# name" line followed by one hex report per line.
std::map<std::string, std::vector<std::string>> loadGolden(const std::string& path) {
    std::map<std::string, std::vector<std::string>> g;
    std::ifstream f(path);
    std::string line, cur;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') cur = line.substr(2);
        else g[cur].push_back(line);
    }
    return g;
}

void testFrame() {
    Report r = frame({0x8F}, Checksum::Bit7);
    CHECK(r[0] == 0x8F && r[7] == uint8_t(255 - 0x8F));
    Report l = frame({0x07, 4, 2, 4, 0x07, 0xFF, 0, 0}, Checksum::Bit8);
    unsigned sum = 0x07 + 4 + 2 + 4 + 0x07 + 0xFF;
    CHECK(l[8] == uint8_t(255 - (sum & 0xFF)));
    CHECK(frame({1, 2}, Checksum::None)[7] == 0);
}

void testActions() {
    for (const auto& m : knownModels()) {
        for (const uint8_t* mat : {m.defaultMatrix, m.defaultFnMatrix}) {
            for (int i = 0; i < kSlots; ++i) {
                KeyAction a{mat[i * 4], mat[i * 4 + 1], mat[i * 4 + 2], mat[i * 4 + 3]};
                std::string s = actionToString(a);
                auto back = parseAction(s);
                CHECK(back.has_value());
                if (back && *back != a) {
                    ++failures;
                    std::cerr << "action round trip failed for " << s << "\n";
                }
            }
        }
    }
    CHECK((*parseAction("LCtrl+C") == KeyAction{0, 0xE0, 0x06, 0}));
    CHECK((*parseAction("consumer:VolumeUp") == KeyAction{3, 0, 0xE9, 0}));
    CHECK((*parseAction("macro:3:hold") == KeyAction{9, 2, 3, 0}));
    CHECK((*parseAction("mouse:Back") == KeyAction{1, 0, 243, 0}));
    CHECK(!parseAction("NoSuchKey"));
}

void testSlots() {
    const DeviceModel& v1 = knownModels()[0];
    KeyMatrix d{};
    std::copy_n(v1.defaultMatrix, d.size(), d.begin());
    CHECK(findSlot(d, "Esc") == 0);
    CHECK(findSlot(d, "escape") == 0);
    CHECK(findSlot(d, "Fn") == 65);
    CHECK(findSlot(d, "@97") == 97);
    CHECK(findSlot(d, "VolumeUp") == 97);
}

void testMacro() {
    Macro m = parseMacro("+LCtrl/5 C/200 -LCtrl/0 +mouse:Left/1 -mouse:Left move:-5,7/300 move:1,1/20", 3);
    auto enc = encodeMacro(m);
    CHECK(enc.size() == 256);
    CHECK(enc[0] == 3 && enc[1] == 0);
    CHECK(enc[2] == 0xE0 && enc[3] == 0x80 + 5);  // short form key down
    Macro back = decodeMacro(enc);
    CHECK(back.repeat == 3);
    CHECK(back.events.size() == m.events.size());
    for (size_t i = 0; i < std::min(back.events.size(), m.events.size()); ++i) {
        CHECK(back.events[i].type == m.events[i].type);
        CHECK(back.events[i].code == m.events[i].code);
        CHECK(back.events[i].delayMs == m.events[i].delayMs);
        CHECK(back.events[i].dx == m.events[i].dx && back.events[i].dy == m.events[i].dy);
    }
}

void testMacroClear() {
    for (int mi = 0; mi < 2; ++mi) {
        RecordingTransport t;
        auto kb = makeKeyboard(t, knownModels()[mi], [](int) {});
        kb->setMacro(2, Macro{0, {}});
        CHECK(t.sent.size() == 1);  // one all-zero chunk marked as the last one
        CHECK(t.sent[0][1] == 2 && t.sent[0][2] == 0 && t.sent[0][4] == 1);
        CHECK(std::all_of(t.sent[0].begin() + 8, t.sent[0].end(), [](uint8_t v) { return v == 0; }));
    }
}

// With waitForCommit on, a YC3121 write is followed by device id queries until two answer fast.
void testWaitForCommit() {
    RecordingTransport t;
    auto kb = makeKeyboard(t, knownModels()[0], [](int) {});
    kb->setWaitForCommit(true);
    kb->setDebounce(5);
    CHECK(t.sent.size() == 3);
    CHECK(t.sent.size() == 3 && t.sent[1][0] == 0x8F && t.sent[2][0] == 0x8F);

    RecordingTransport t2;  // not enabled for the YC3123
    auto kb2 = makeKeyboard(t2, knownModels()[1], [](int) {});
    kb2->setWaitForCommit(true);
    kb2->setDebounce(5);
    CHECK(t2.sent.size() == 1);
}

// Every key of the drawing maps to its own matrix slot on both firmwares.
void testLayout() {
    for (const auto& m : knownModels()) {
        std::vector<int> seen;
        for (const auto& k : k86Layout()) {
            auto slot = layoutSlot(m, k);
            CHECK(slot.has_value());
            if (!slot) {
                std::cerr << m.displayName << ": no slot for " << k.id << "\n";
                continue;
            }
            CHECK(std::find(seen.begin(), seen.end(), *slot) == seen.end());
            seen.push_back(*slot);
        }
        CHECK(seen.size() == 90);
    }
}

// Named actions parse back to the same bytes, and the factory Fn layers have no unnamed entries.
void testActionNames() {
    for (const auto& c : actionCatalog()) {
        std::string token = actionToString(c.action);
        auto back = parseAction(token);
        CHECK(back && *back == c.action);
        CHECK(token.rfind("raw:", 0) != 0);
        CHECK(actionLabel(c.action).rfind("Unknown", 0) != 0);
    }
    for (const auto& m : knownModels()) {
        for (int i = 0; i < kSlots; ++i) {
            const uint8_t* d = m.defaultFnMatrix + i * 4;
            KeyAction a{d[0], d[1], d[2], d[3]};
            if (actionLabel(a).rfind("Unknown", 0) == 0) {
                ++failures;
                std::cerr << m.displayName << ": unnamed Fn action in slot " << i << "\n";
            }
        }
    }
    CHECK(actionLabel({9, 1, 2, 0}) == "Macro 3 (toggle)");
    CHECK(actionLabel({0, 0, 0xE3, 0x07}) == "Win + D");
    CHECK(actionShortLabel({14, 0, 0, 0}) == "BT1");
    CHECK(actionLabel({0, 0, 0x4B, 0}) == "Page Up");
    CHECK(*parseAction("conn:2.4G") == (KeyAction{14, 0, 5, 0}));
    CHECK(*parseAction("profile:2") == (KeyAction{8, 0, 4, 1}));
}

// Writes through the Keyboard API land in the simulated state and read back unchanged.
void testSimulator() {
    for (const auto& m : knownModels()) {
        SimulatedTransport sim(m);
        auto kb = makeKeyboard(sim, m, [](int) {});
        CHECK(kb->readDeviceId() == m.firmwareId);
        KeyMatrix base = kb->getKeyMatrix(1);
        CHECK(std::equal(base.begin(), base.end(), m.defaultMatrix));
        kb->setKey(1, 3, {0, 0, 0xE0, 0});
        KeyMatrix after = kb->getKeyMatrix(1);
        CHECK(after[12] == 0 && after[14] == 0xE0);
        kb->setFnKey(2, 1, 5, {14, 0, 1, 0});
        CHECK(kb->getFnMatrix(2, m.chip == Chip::YC3123 ? 1 : 0)[22] == 1);
        KeyMatrix full = base;
        full[0] = 9, full[1] = 2, full[2] = 7, full[3] = 0;
        kb->setKeyMatrix(0, full);
        CHECK(kb->getKeyMatrix(0) == full);
        std::array<Rgb, kSlots> pic{};
        pic[7] = 0x123456;
        kb->setUserPicture(0, pic);
        CHECK(kb->getUserPicture(0)[7] == 0x123456);
        Macro mac = parseMacro("+LCtrl/5 C/20 -LCtrl", 3);
        kb->setMacro(4, mac);
        CHECK(macroToString(kb->getMacro(4)) == macroToString(mac));
        kb->setMacro(4, Macro{0, {}});
        CHECK(kb->getMacro(4).events.empty());
        LightSetting l;
        l.mode = 2, l.speed = 1, l.brightness = 3, l.color = 0x00FF00;
        kb->setLight(l);
        auto back = kb->getLight();
        CHECK(back && back->mode == 2 && back->speed == 1 && back->brightness == 3 && back->color == 0x00FF00);
        kb->setProfile(2);
        CHECK(kb->getProfile() == 2);
        CHECK(!sim.writes().empty());

        ReadOnlyTransport ro(sim);
        auto rk = makeKeyboard(ro, m, [](int) {});
        CHECK(rk->getProfile() == 2);
        bool refused = false;
        try {
            rk->setProfile(0);
        } catch (const ReadOnlyError&) {
            refused = true;
        }
        CHECK(refused);
    }
}

void testScreen() {
    RgbaImage img{4, 2, std::vector<uint8_t>(4 * 2 * 4, 0)};
    for (int i = 0; i < 8; ++i) img.rgba[i * 4 + 0] = 255, img.rgba[i * 4 + 3] = 255;  // red
    auto px = fitToScreen(img, 240, 135);
    Rect r = contentRect(px, 240, 135);
    CHECK(r.left == 0 && r.right == 240);
    CHECK(r.top == 8 && r.bottom == 128);  // 240x120 centred in 135 (round(7.5) = 8)
    ScreenImage si = toScreenImage(px, 240, r);
    CHECK(si.data.size() == size_t(240 * 120 * 2));
    CHECK(si.data[0] == 0xF8 && si.data[1] == 0x00);
}

// Each scenario runs the same operation as the matching JS scenario in tests/js/run.mjs.
using Scenario = std::function<void(Keyboard&)>;

// Colours only on slots that hold a key (the original addresses keys, not raw slots).
std::array<Rgb, kSlots> testColors(const Keyboard& kb) {
    std::array<Rgb, kSlots> c{};
    const KeyMatrix& d = kb.defaultMatrix();
    for (int i = 0; i < kSlots; ++i)
        if (d[i * 4] || d[i * 4 + 1] || d[i * 4 + 2] || d[i * 4 + 3]) c[i] = Rgb((i * 0x010203 + 0x405060) & 0xFFFFFF);
    return c;
}

KeyMatrix testMatrix(const Keyboard& kb) {
    KeyMatrix m = kb.defaultMatrix();
    m[9 * 4 + 2] = 0x05;  // slot 9 -> B
    return m;
}

const std::vector<std::pair<std::string, Scenario>>& scenarios() {
    static const std::vector<std::pair<std::string, Scenario>> s = {
        {"deviceId", [](Keyboard& k) { k.readDeviceId(); }},
        {"versions", [](Keyboard& k) { k.readVersions(); }},
        {"setProfile", [](Keyboard& k) { k.setProfile(2); }},
        {"getProfile", [](Keyboard& k) { k.getProfile(); }},
        {"setDebounce", [](Keyboard& k) { k.setDebounce(10); }},
        {"getDebounce", [](Keyboard& k) { k.getDebounce(); }},
        {"setSleep", [](Keyboard& k) { k.setSleep({300, 600, 1800, 3600}); }},
        {"getSleep", [](Keyboard& k) { k.getSleep(); }},
        {"setLightWave", [](Keyboard& k) { k.setLight({4, 2, 3, 1, 0x12AB34, false}); }},
        {"setLightRainbow", [](Keyboard& k) { k.setLight({2, 0, 4, 3, 0xFFFFFF, true}); }},
        {"setLightCustom", [](Keyboard& k) { k.setLight({13, 1, 2, 0, 0x00FF00, false}); }},
        {"setLightMusic", [](Keyboard& k) { k.setLight({22, 2, 4, 0, 0x0000FF, false}); }},
        {"getLight", [](Keyboard& k) { k.getLight(); }},
        {"setKey", [](Keyboard& k) { k.setKey(1, 9, {0, 0xE0, 0x06, 0}); }},
        {"setKeyMatrix", [](Keyboard& k) { k.setKeyMatrix(1, testMatrix(k)); }},
        {"getKeyMatrix", [](Keyboard& k) { k.getKeyMatrix(2); }},
        {"setFnKey", [](Keyboard& k) { k.setFnKey(0, 1, 9, {3, 0, 0xE9, 0}); }},
        {"setFnMatrix", [](Keyboard& k) { k.setFnMatrix(0, 1, testMatrix(k)); }},
        {"getFnMatrix", [](Keyboard& k) { k.getFnMatrix(0, 1); }},
        {"setMacro", [](Keyboard& k) { k.setMacro(4, parseMacro("+LCtrl/5 C/200 -LCtrl/30 +mouse:Left/1 -mouse:Left/1 move:-5,7/300", 2)); }},
        {"setMacroLong", [](Keyboard& k) {
             std::string t;
             for (int i = 0; i < 40; ++i) t += "A/20 ";
             k.setMacro(7, parseMacro(t, 1));
         }},
        {"getMacro", [](Keyboard& k) { k.getMacro(4); }},
        {"setUserPicture", [](Keyboard& k) { k.setUserPicture(2, testColors(k)); }},
        {"getUserPicture", [](Keyboard& k) { k.getUserPicture(2); }},
        {"userAnimation", [](Keyboard& k) {
             k.startUserAnimation();
             k.sendUserAnimationFrame(1, 3, 300, testColors(k));
         }},
        {"screen", [](Keyboard& k) {
             ScreenImage img;
             for (int i = 0; i < 130; ++i) img.data.push_back(uint8_t(i * 7));
             img.left = 3, img.top = 4, img.right = 300, img.bottom = 120;
             k.screenBegin(2, 1, 0, img);
             k.screenSendFrame(2, 1, 0, img);
         }},
        {"sysinfo", [](Keyboard& k) {
             SystemInfo s;
             s.diskAvailableBytes = 123.4 * (1 << 30), s.diskTotalBytes = 512.0 * (1 << 30);
             s.memUsedBytes = 7.6 * (1 << 30), s.memTotalBytes = 32.0 * (1 << 30);
             s.cpuUsagePercent = 42, s.cpuTemperatureC = 57;
             s.netTotalUpBytes = 3.5 * (1 << 30), s.netTotalDownBytes = 300.2 * (1 << 30);
             k.sendSystemInfo(s);
         }},
        {"language", [](Keyboard& k) { k.switchScreenLanguage(true); }},
        {"erase", [](Keyboard& k) { k.eraseScreenFlash(); }},
        {"reset", [](Keyboard& k) { k.factoryReset(); }},
        {"getOptions", [](Keyboard& k) { k.getOptions(); }},
        {"setOptions", [](Keyboard& k) {
             if (k.model().chip == Chip::YC3123)
                 k.setOptions({{"fnLayer", 1}, {"antiMistouch", 1}, {"rtStab", 50}, {"wasdSwap", 1}});
             else
                 k.setOptions({{"winLock", 1}, {"ledOff", 1}});
         }},
        {"setSideLightWave", [](Keyboard& k) { k.setSideLight({4, 0, 3, 2, 0x00FFFF, false}); }},
        {"setSideLightNeon", [](Keyboard& k) { k.setSideLight({3, 0, 4, 1, 0xFF0000, true}); }},
        {"getSideLight", [](Keyboard& k) { k.getSideLight(); }},
    };
    return s;
}

void testGolden(const std::string& dir) {
    const char* tags[] = {"v1", "v2"};
    for (int mi = 0; mi < 2; ++mi) {
        std::string path = dir + "/" + tags[mi] + ".txt";
        auto golden = loadGolden(path);
        if (golden.empty()) {
            ++failures;
            std::cerr << "missing golden file " << path << "\n";
            continue;
        }
        for (const auto& [name, fn] : scenarios()) {
            if (mi == 0 && name.find("SideLight") != std::string::npos) continue;  // the original never sends these to the YC3121
            RecordingTransport t;
            auto kb = makeKeyboard(t, knownModels()[mi], [](int) {});
            fn(*kb);
            auto it = golden.find(name);
            if (it == golden.end()) {
                ++failures;
                std::cerr << tags[mi] << ": no golden data for " << name << "\n";
                continue;
            }
            std::vector<std::string> got;
            for (const auto& r : t.sent) got.push_back(hex(r));
            if (got != it->second) {
                ++failures;
                std::cerr << tags[mi] << ": " << name << " differs (" << got.size() << " vs " << it->second.size()
                          << " reports)\n";
                for (size_t i = 0; i < std::max(got.size(), it->second.size()); ++i) {
                    std::string g = i < got.size() ? got[i] : "-", e = i < it->second.size() ? it->second[i] : "-";
                    if (g != e) std::cerr << "  [" << i << "] c++ " << g << "\n      js  " << e << "\n";
                }
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    testFrame();
    testActions();
    testSlots();
    testMacro();
    testMacroClear();
    testWaitForCommit();
    testScreen();
    testLayout();
    testActionNames();
    testSimulator();
    std::string golden = argc > 1 ? argv[1] : K86_GOLDEN_DIR;
    testGolden(golden);
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
