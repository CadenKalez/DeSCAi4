// GZCOM creator for sc4::cSC4PedestrianManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PedestrianManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4PedestrianManager() { return static_cast<void*>(new sc4::cSC4PedestrianManager()); }

}  // namespace gzcom
