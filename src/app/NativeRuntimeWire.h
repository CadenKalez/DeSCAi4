// Runs the engine lifecycle checks over the real, already booted sub-systems and prints the wiring map.
#pragma once
#include "render/gd/IGZGDriver.h"

namespace native {

// Binds the game's sub-systems (resource manager, message servers, graphics driver, audio, input queue, the network stub) into
// runtime::EngineLifecycle and logs "[Runtime Wire]" lines. Returns false when a required dependency is missing; the caller halts.
bool VerifyRuntimeWiring(gd::cIGZGDriver* driver, bool audioRunning);

// Prints which city-phase SL:: services are bound after the city view is up.
void ReportCityWiring();

}  // namespace native
