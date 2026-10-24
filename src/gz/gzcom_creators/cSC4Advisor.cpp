// GZCOM creator for sc4::cSC4Advisor, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/Advisor.h"

namespace gzcom {

void* GZCOM_CreatecSC4Advisor() { return static_cast<void*>(new sc4::cSC4Advisor()); }

}  // namespace gzcom
