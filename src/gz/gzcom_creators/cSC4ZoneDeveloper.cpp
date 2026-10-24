// GZCOM creator for sc4::cSC4ZoneDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/ZoneDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4ZoneDeveloper() { return static_cast<void*>(new sc4::cSC4ZoneDeveloper()); }

}  // namespace gzcom
