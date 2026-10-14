#include "audio/HitManServices.h"

namespace Hit {
namespace {
cIHitManServices* g_services = nullptr;
}
cIHitManServices* GetServices() { return g_services; }
void SetServices(cIHitManServices* services) { g_services = services; }
}  // namespace Hit
