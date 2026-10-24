// GZCOM creator for sc4::cSC4DemandSimulator, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/DemandSimulator.h"

namespace gzcom {

void* GZCOM_CreatecSC4DemandSimulator() { return static_cast<void*>(new sc4::cSC4DemandSimulator()); }

}  // namespace gzcom
