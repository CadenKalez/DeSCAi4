// GZCOM creator for sc4::cSC4ParkManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/ParkManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4ParkManager() { return static_cast<void*>(new sc4::cSC4ParkManager()); }

}  // namespace gzcom
