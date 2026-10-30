// The native IZoneDeveloperHost and the lot developer's base-texture services (NativeZoneDeveloperHost.cpp).
#pragma once

namespace sc4 { class cSC4City; }

namespace native {

// Installs both on the loaded city and initialises its zone developer; true when it came up.
bool InstallZoneDeveloperHost(sc4::cSC4City* city);

}  // namespace native
