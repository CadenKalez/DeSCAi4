// GZCOM creator for sc4::cSC4TextDecalOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/TextDecalOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4TextDecalOccupant() { return static_cast<void*>(new sc4::cSC4TextDecalOccupant()); }

}  // namespace gzcom
