// GZCOM creator for sc4::cSC4NetworkManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/NetworkManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4NetworkManager() { return static_cast<void*>(new sc4::cSC4NetworkManager()); }

}  // namespace gzcom
