// GZCOM creator for cSC4RegionalCity, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/RegionalCity.h"

namespace gzcom {

void* GZCOM_CreatecSC4RegionalCity() { return static_cast<void*>(new cSC4RegionalCity()); }

}  // namespace gzcom
