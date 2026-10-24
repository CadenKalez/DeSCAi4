// GZCOM creator for sc4::cSC4FireOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/FireOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4FireOccupant() { return static_cast<void*>(new sc4::cSC4FireOccupant()); }

}  // namespace gzcom
