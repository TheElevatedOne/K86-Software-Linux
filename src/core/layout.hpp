// Physical key layout of the K86, for drawing the keyboard.
//
// Coordinates are the original driver's K86 drawing ("Keyboard_89_SG8967_KeyMappings", shared by
// the YC3121 and YC3123 versions): an 840 x 291 canvas with 40 x 40 standard keys. The knob is a
// circle (centre 814,26, radius 26) whose left, middle and right thirds stand for volume down,
// mute (press) and volume up.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "keyboard.hpp"

namespace k86 {

struct KeyGeometry {
    std::string id;      // DOM key code used by the original, e.g. "KeyA", "ArrowUp", "AudioVolumeMute"
    std::string legend;  // printed legend, e.g. "A", "Caps", "←"
    float x, y, w, h;
    KeyAction factory;   // the key's action in the base layer, used to find its matrix slot
    bool knob = false;   // one of the three knob zones
};

constexpr float kLayoutWidth = 840, kLayoutHeight = 291;
constexpr float kKnobCx = 814, kKnobCy = 26, kKnobR = 26;

const std::vector<KeyGeometry>& k86Layout();

// Matrix slot of a layout key for a model (looked up by the key's factory action in the model's
// default base layer), or nullopt if the model has no such key.
std::optional<int> layoutSlot(const DeviceModel& m, const KeyGeometry& k);

}  // namespace k86
