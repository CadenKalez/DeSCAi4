// GZCOM creator for sc4::cSC4VehicleManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/VehicleManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4VehicleManager() { return static_cast<void*>(new sc4::cSC4VehicleManager()); }

}  // namespace gzcom
