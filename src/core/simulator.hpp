// Test doubles for the HID transport.
//
//  * SimulatedTransport answers the K86 protocol from an in-memory keyboard state (key maps,
//    Fn layers, per-key colours, macros, lighting, settings), so the GUI's --dry-run and the
//    tests can exercise reads and writes end to end without hardware.
//  * ReadOnlyTransport wraps a real transport and refuses every command that is not a known
//    query, so nothing can be written to the keyboard by accident.
#pragma once

#include <array>
#include <functional>
#include <stdexcept>
#include <vector>

#include "keyboard.hpp"

namespace k86 {

class SimulatedTransport : public Transport {
public:
    explicit SimulatedTransport(const DeviceModel& model);

    void sendFeature(const Report& report) override;
    Report getFeature() override;

    // Every SET_FEATURE that changes state, in order (queries are not recorded).
    const std::vector<Report>& writes() const { return writes_; }
    // Called for every recorded write (e.g. to log it).
    std::function<void(const Report&)> onWrite;

    // Simulated keyboard state, public so tests can inspect and prepare it.
    struct State {
        std::array<KeyMatrix, 3> keymaps{};
        std::array<std::array<KeyMatrix, 2>, 3> fn{};  // [layer][os]
        std::array<std::array<uint8_t, 384>, 5> pictures{};
        std::array<std::array<uint8_t, 256>, 50> macros{};
        std::array<uint8_t, 8> light{}, side{};  // bytes 1..7 of the 07 / 08 packet
        uint8_t profile = 0, debounce = 5;
        std::array<uint16_t, 4> sleep{180, 180, 1620, 1620};
        std::array<uint8_t, 8> options{};  // YC3121: [2..4] of 06; YC3123: [1..5] of 09
    } state;

    void reset();  // factory state

private:
    const DeviceModel& m_;
    Report reply_{};
    std::vector<Report> writes_;
    bool v2() const { return m_.chip == Chip::YC3123; }
    void record(const Report& r);
};

struct ReadOnlyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ReadOnlyTransport : public Transport {
public:
    explicit ReadOnlyTransport(Transport& inner) : inner_(inner) {}
    void sendFeature(const Report& report) override;
    Report getFeature() override { return inner_.getFeature(); }
    static bool isQuery(uint8_t command);

private:
    Transport& inner_;
};

}  // namespace k86
