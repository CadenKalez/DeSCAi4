// GZCOM creator for sc4::cSC4PylonOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PylonOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4PylonOccupant() { return static_cast<void*>(new sc4::cSC4PylonOccupant()); }

}  // namespace gzcom
