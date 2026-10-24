// GZCOM creator for sc4::cSC4AdvisorFinance, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdvisorConcrete.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdvisorFinance() { return static_cast<void*>(new sc4::cSC4AdvisorFinance()); }

}  // namespace gzcom
