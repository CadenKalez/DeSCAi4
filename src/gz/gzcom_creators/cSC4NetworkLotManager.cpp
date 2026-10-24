// GZCOM creator for sc4::cSC4NetworkLotManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/NetworkLotManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4NetworkLotManager() { return static_cast<void*>(new sc4::cSC4NetworkLotManager()); }

}  // namespace gzcom
