// GZCOM creator for sc4::cSC4SignpostOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/SignpostOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4SignpostOccupant() { return static_cast<void*>(new sc4::cSC4SignpostOccupant()); }

}  // namespace gzcom
