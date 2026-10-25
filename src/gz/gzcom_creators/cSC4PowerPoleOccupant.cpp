// GZCOM creator for sc4::cSC4PowerPoleOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PowerPoleOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4PowerPoleOccupant() { return static_cast<void*>(new sc4::cSC4PowerPoleOccupant()); }

}  // namespace gzcom
