// GZCOM creator for sc4::cSC4AdvisorSystem, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdvisorSystem.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdvisorSystem() { return static_cast<void*>(new sc4::cSC4AdvisorSystem()); }

}  // namespace gzcom
