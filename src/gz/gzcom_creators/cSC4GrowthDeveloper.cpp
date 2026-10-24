// GZCOM creator for sc4::cSC4GrowthDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/GrowthDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4GrowthDeveloper() { return static_cast<void*>(new sc4::cSC4GrowthDeveloper()); }

}  // namespace gzcom
