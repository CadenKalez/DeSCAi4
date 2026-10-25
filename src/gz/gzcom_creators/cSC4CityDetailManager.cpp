// GZCOM creator for sc4::cSC4CityDetailManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/CityDetailManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4CityDetailManager() { return static_cast<void*>(new sc4::cSC4CityDetailManager()); }

}  // namespace gzcom
