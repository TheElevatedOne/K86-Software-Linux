// Collects the values the keyboard screen can display from /proc and /sys.
#pragma once

#include "keyboard.hpp"

namespace k86 {

class SystemInfoCollector {
public:
    // CPU usage is computed from the /proc/stat delta since the previous call.
    SystemInfo sample();

private:
    unsigned long long prevIdle_ = 0, prevTotal_ = 0;
};

}  // namespace k86
