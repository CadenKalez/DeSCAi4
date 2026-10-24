// GZCOM creator for sc4::cSC4Simulator, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/Simulator.h"

namespace gzcom {

void* GZCOM_CreatecSC4Simulator() { return static_cast<void*>(new sc4::cSC4Simulator()); }

}  // namespace gzcom
