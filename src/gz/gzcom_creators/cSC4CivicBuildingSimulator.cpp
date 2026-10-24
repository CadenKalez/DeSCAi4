// GZCOM creator for sc4::cSC4CivicBuildingSimulator, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/CivicBuildingSimulator.h"

namespace gzcom {

void* GZCOM_CreatecSC4CivicBuildingSimulator() { return static_cast<void*>(new sc4::cSC4CivicBuildingSimulator()); }

}  // namespace gzcom
