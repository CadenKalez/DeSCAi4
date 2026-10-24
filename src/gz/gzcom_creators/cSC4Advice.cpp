// GZCOM creator for sc4::cSC4Advice, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/Advice.h"

namespace gzcom {

void* GZCOM_CreatecSC4Advice() { return static_cast<void*>(new sc4::cSC4Advice()); }

}  // namespace gzcom
