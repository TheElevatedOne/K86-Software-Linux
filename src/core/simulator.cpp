#include "simulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "layout.hpp"

namespace k86 {

namespace {

// Commands that only read state (identical numbers on both chips).
constexpr uint8_t kQueries[] = {0x80, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
                                0x8B, 0x8C, 0x8F, 0x90, 0x91, 0x92, 0xAD, 0xAE};

template <size_t N>
void copyPage(Report& out, const std::array<uint8_t, N>& src, size_t offset) {
    for (size_t i = 0; i < kReportSize; ++i) out[i] = offset + i < N ? src[offset + i] : 0;
}

template <size_t N>
void writeChunk(std::array<uint8_t, N>& dst, const Report& r, size_t offset) {
    for (size_t i = 0; i < 56 && offset + i < N; ++i) dst[offset + i] = r[8 + i];
}

void putU16(Report& r, int at, uint16_t v) {
    r[size_t(at)] = uint8_t(v & 0xFF);
    r[size_t(at) + 1] = uint8_t(v >> 8);
}

uint16_t getU16(const Report& r, int at) { return uint16_t(r[size_t(at)] | r[size_t(at) + 1] << 8); }

// A rainbow across the keyboard, so per-key colours look like something in a dry run.
std::array<uint8_t, 384> rainbowPicture(const DeviceModel& m, double phase) {
    std::array<uint8_t, 384> p{};
    for (const auto& k : k86Layout()) {
        auto slot = layoutSlot(m, k);
        if (!slot) continue;
        double h = std::fmod((k.x + k.w / 2) / kLayoutWidth + phase, 1.0) * 6.0;
        double x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
        double rgb[3] = {0, 0, 0};
        int seg = int(h);
        const int map[6][3] = {{0, 1, 2}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}, {1, 2, 0}, {0, 2, 1}};
        rgb[map[seg][0]] = 1, rgb[map[seg][1]] = x, rgb[map[seg][2]] = 0;
        for (int c = 0; c < 3; ++c) p[size_t(*slot) * 3 + size_t(c)] = uint8_t(std::lround(rgb[c] * 255));
    }
    return p;
}

}  // namespace

bool ReadOnlyTransport::isQuery(uint8_t command) {
    return std::find(std::begin(kQueries), std::end(kQueries), command) != std::end(kQueries);
}

void ReadOnlyTransport::sendFeature(const Report& report) {
    if (!isQuery(report[0])) {
        char b[96];
        snprintf(b, sizeof b, "read-only mode: refused to send command 0x%02x to the keyboard", report[0]);
        throw ReadOnlyError(b);
    }
    inner_.sendFeature(report);
}

SimulatedTransport::SimulatedTransport(const DeviceModel& model) : m_(model) { reset(); }

void SimulatedTransport::reset() {
    State s;
    for (auto& km : s.keymaps) std::copy_n(m_.defaultMatrix, km.size(), km.begin());
    for (auto& layer : s.fn)
        for (auto& km : layer) std::copy_n(m_.defaultFnMatrix, km.size(), km.begin());
    for (size_t i = 0; i < s.pictures.size(); ++i) s.pictures[i] = rainbowPicture(m_, double(i) / 5.0);
    // A small example macro in slot 0: Shift+H, I.
    Macro demo = parseMacro("+LShift/5 H/5 -LShift/5 I/5", 1);
    auto bytes = encodeMacro(demo);
    std::copy(bytes.begin(), bytes.end(), s.macros[0].begin());
    // Wave, rainbow, brightness 4, speed 2 (the speed byte is inverted on the wire).
    const int maxSpeed = v2() ? 4 : 5;
    s.light = {4, uint8_t(maxSpeed - 2), 4, 8, 0xFF, 0, 0, 0};
    s.side = {0, 2, 4, 7, 0, 0xFF, 0xFF, 0};
    state = s;
}

void SimulatedTransport::record(const Report& r) {
    writes_.push_back(r);
    if (onWrite) onWrite(r);
}

Report SimulatedTransport::getFeature() { return reply_; }

void SimulatedTransport::sendFeature(const Report& r) {
    const uint8_t cmd = r[0];
    reply_ = Report{};
    reply_[0] = cmd;
    if (!ReadOnlyTransport::isQuery(cmd)) record(r);
    State& s = state;
    auto layer = [](uint8_t l) { return size_t(std::min<uint8_t>(l, 2)); };
    auto os = [](uint8_t o) { return size_t(std::min<uint8_t>(o, 1)); };

    // Commands with the same meaning on both chips.
    switch (cmd) {
        case 0x8F: {
            uint32_t id = m_.firmwareId;
            for (int i = 0; i < 4; ++i) reply_[size_t(1 + i)] = uint8_t(id >> (8 * i));
            if (v2()) putU16(reply_, 7, 0x0115);
            return;
        }
        case 0x80: putU16(reply_, 1, v2() ? 0 : 0x0114); return;
        case 0xAD: putU16(reply_, 1, 0x0106); return;
        case 0xAE: return;
        case 0x87: std::copy_n(s.light.begin(), 7, reply_.begin() + 1); return;
        case 0x07: std::copy_n(r.begin() + 1, 7, s.light.begin()); return;
        case 0x88: std::copy_n(s.side.begin(), 7, reply_.begin() + 1); return;
        case 0x08: std::copy_n(r.begin() + 1, 7, s.side.begin()); return;
        case 0x8B: copyPage(reply_, s.macros[std::min<size_t>(r[1], 49)], size_t(r[2]) * 64); return;
        case 0xA5: reply_[1] = 1; return;
        case 0xAC: reply_ = Report{0xAC, 0xAA, 0xAA, 0x55, 0x55}; return;
        case 0x25: case 0x28: case 0x22: case 0x27: return;
        default: break;
    }
    if (cmd == (v2() ? 0x0B : 0x16)) {  // macro chunk
        auto& mac = s.macros[std::min<size_t>(r[1], 49)];
        if (r[2] == 0) mac.fill(0);
        writeChunk(mac, r, size_t(r[2]) * 56);
        return;
    }

    if (!v2()) {
        switch (cmd) {
            case 0x05: s.profile = r[1]; return;
            case 0x85: reply_[1] = s.profile; return;
            case 0x11: s.debounce = r[2]; return;
            case 0x91: reply_[2] = s.debounce; return;
            case 0x12: for (int i = 0; i < 4; ++i) s.sleep[size_t(i)] = getU16(r, 8 + i * 2); return;
            case 0x92: for (int i = 0; i < 4; ++i) putU16(reply_, 1 + i * 2, s.sleep[size_t(i)]); return;
            case 0x06: std::copy_n(r.begin() + 2, 3, s.options.begin() + 2); return;
            case 0x86: std::copy_n(s.options.begin() + 2, 3, reply_.begin() + 2); return;
            case 0x89: copyPage(reply_, s.keymaps[layer(r[1])], size_t(r[2]) * 64); return;
            case 0x09: writeChunk(s.keymaps[layer(r[1])], r, size_t(r[4]) * 56); return;
            case 0x13: std::copy_n(r.begin() + 8, 4, s.keymaps[layer(r[1])].begin() + r[2] * 4); return;
            case 0x90: copyPage(reply_, s.fn[layer(r[1])][0], size_t(r[2]) * 64); return;
            case 0x10: writeChunk(s.fn[layer(r[1])][0], r, size_t(r[4]) * 56); return;
            case 0x15: std::copy_n(r.begin() + 8, 4, s.fn[layer(r[1])][0].begin() + r[2] * 4); return;
            case 0x8C: copyPage(reply_, s.pictures[0], size_t(r[2]) * 64); return;
            case 0x0C: writeChunk(s.pictures[0], r, size_t(r[4]) * 56); return;
            case 0x02: reset(); return;
            default: return;  // 0x18 / 0x19 animation and unknown commands are accepted silently
        }
    }
    switch (cmd) {
        case 0x04: s.profile = r[1]; return;
        case 0x84: reply_[1] = s.profile; return;
        case 0x06: s.debounce = r[1]; return;
        case 0x86: reply_[1] = s.debounce; return;
        case 0x11: for (int i = 0; i < 4; ++i) s.sleep[size_t(i)] = getU16(r, 8 + i * 2); return;
        case 0x91: for (int i = 0; i < 4; ++i) putU16(reply_, 8 + i * 2, s.sleep[size_t(i)]); return;
        case 0x09: std::copy_n(r.begin() + 2, 4, s.options.begin() + 2); return;
        case 0x89: std::copy_n(s.options.begin() + 1, 5, reply_.begin() + 1); return;
        case 0x85: return;
        case 0x8A: copyPage(reply_, s.keymaps[layer(r[1])], size_t(r[3]) * 64); return;
        case 0x0A:
            if (r[2] == 255) writeChunk(s.keymaps[layer(r[1])], r, size_t(r[3]) * 56);
            else std::copy_n(r.begin() + 8, 4, s.keymaps[layer(r[1])].begin() + r[2] * 4);
            return;
        case 0x90: copyPage(reply_, s.fn[layer(r[2])][os(r[1])], size_t(r[4]) * 64); return;
        case 0x10:
            if (r[3] == 255) writeChunk(s.fn[layer(r[2])][os(r[1])], r, size_t(r[4]) * 56);
            else std::copy_n(r.begin() + 8, 4, s.fn[layer(r[2])][os(r[1])].begin() + r[3] * 4);
            return;
        case 0x8C: copyPage(reply_, s.pictures[std::min<size_t>(r[1], 4)], size_t(r[3]) * 64); return;
        case 0x0C: writeChunk(s.pictures[std::min<size_t>(r[1], 4)], r, size_t(r[3]) * 56); return;
        case 0x01: reset(); return;
        default: return;
    }
}

}  // namespace k86
