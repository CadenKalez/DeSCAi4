// The native IZoneDeveloperHost and the lot developer's base-texture services (NativeZoneDeveloperHost.cpp).
#pragma once
#include <cstdint>

namespace sc4 { class cSC4City; }

namespace native {

// Installs both on the loaded city and initialises its zone developer; true when it came up.
bool InstallZoneDeveloperHost(sc4::cSC4City* city);

// The network types (bit n = network type n) of the first network occupant on a city cell; 0 when the cell has none.
std::uint32_t NativeNetworkTypesAt(int x, int z);
// cSC4LotDeveloper::GetAreaSlope over an inclusive cell rect {x0, z0, x1, z1} (0 without the lot developer).
float NativeAreaSlope(const std::int32_t rect[4], bool includeWater);

}  // namespace native
