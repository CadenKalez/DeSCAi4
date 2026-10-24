// GZCOM creator for sc4::cSC4AdvisorBase, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdvisorConcrete.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdvisorBase() { return static_cast<void*>(new sc4::cSC4AdvisorBase()); }

}  // namespace gzcom
