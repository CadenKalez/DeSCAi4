// GZCOM creator for cSTESurfaceWater. The class needs the host services installed with gzcom::SetSurfaceWaterHost(); without a host the
// creator logs SC4_UNWIRED and answers null, as the unrecovered fallback in GZCOM.cpp did.
#include "gz/GZCOM.h"
#include "platform/Unwired.h"
#include "sc4/SurfaceWater.h"

namespace gzcom {

namespace {
sc4::ISurfaceWaterHost* g_surfaceWaterHost = nullptr;
}

void SetSurfaceWaterHost(sc4::ISurfaceWaterHost* host) { g_surfaceWaterHost = host; }

// The creators hand out the primary cIGZUnknown pointer.
void* GZCOM_CreatecSTESurfaceWater() {
    if (!g_surfaceWaterHost) {
        SC4_UNWIRED("ISurfaceWaterHost (GZCOM_CreatecSTESurfaceWater has no host)");
        return nullptr;
    }
    auto* water = new sc4::cSTESurfaceWater(g_surfaceWaterHost);
    return static_cast<void*>(static_cast<cIGZUnknown*>(static_cast<cIGZSerializable*>(water)));
}

}  // namespace gzcom
