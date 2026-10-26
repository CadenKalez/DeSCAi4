// The region screen's cloud shadows and scrolling (see NativeRegionExtras.cpp).
#pragma once
#include <cstdint>

namespace gd { class cIGZGDriver; }

namespace native {

// `preferences` answers the application's preference blob (the edge-scroll and cloud switches live in it).
void InstallRegionExtras(gd::cIGZGDriver* driver, std::uint8_t* (*preferences)());

}  // namespace native
