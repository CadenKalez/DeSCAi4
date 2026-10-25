// GZCOM creator for sc4::cSC4MyStreetSim, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/MyStreetSim.h"

namespace gzcom {

void* GZCOM_CreatecSC4MyStreetSim() { return static_cast<void*>(new sc4::cSC4MyStreetSim()); }

}  // namespace gzcom
