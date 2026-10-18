#pragma once

#include "simulator.hpp"

namespace k86gui {

struct App;
// Exercises the write paths of every page against the simulator; returns the exit code.
int runSelfTest(App* app, k86::SimulatedTransport* sim);

}  // namespace k86gui
