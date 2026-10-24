// GZCOM creator for sc4::cSC4AdvisorSafety, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdvisorConcrete.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdvisorSafety() { return static_cast<void*>(new sc4::cSC4AdvisorSafety()); }

}  // namespace gzcom
