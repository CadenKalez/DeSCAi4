// GZCOM creator for sc4::cSC4AirportDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AirportDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4AirportDeveloper() { return static_cast<void*>(new sc4::cSC4AirportDeveloper()); }

}  // namespace gzcom
