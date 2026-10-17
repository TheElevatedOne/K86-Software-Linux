// Finds K86 keyboards and opens their configuration interface.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "hidraw.hpp"
#include "keyboard.hpp"

namespace k86 {

// The original driver selects the HID collection with usage page 0xFFFF, usage 2.
constexpr uint16_t kConfigUsagePage = 0xFFFF;
constexpr uint16_t kConfigUsage = 0x0002;

struct Candidate {
    HidrawInfo info;
    const DeviceModel* model;  // by VID/PID; confirmed by firmware id when opened
};

std::vector<Candidate> findCandidates();

struct OpenKeyboard {
    std::unique_ptr<HidrawTransport> transport;
    std::unique_ptr<Keyboard> keyboard;
    HidrawInfo info;
    std::optional<uint32_t> firmwareId;
};

// Opens `path` (or the first candidate when empty). Unless `force` is set, the firmware id
// must match a known K86 model, because other keyboards share the same VID/PID.
OpenKeyboard openKeyboard(const std::string& path = {}, bool force = false);

}  // namespace k86
