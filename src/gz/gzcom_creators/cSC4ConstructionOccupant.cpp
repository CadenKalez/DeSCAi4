// GZCOM creator for sc4::cSC4ConstructionOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/ConstructionOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4ConstructionOccupant() { return static_cast<void*>(new sc4::cSC4ConstructionOccupant()); }

}  // namespace gzcom
