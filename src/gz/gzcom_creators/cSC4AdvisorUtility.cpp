// GZCOM creator for sc4::cSC4AdvisorUtility, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdvisorConcrete.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdvisorUtility() { return static_cast<void*>(new sc4::cSC4AdvisorUtility()); }

}  // namespace gzcom
