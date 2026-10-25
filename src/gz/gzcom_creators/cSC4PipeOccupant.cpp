// GZCOM creator for sc4::cSC4PipeOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PipeOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4PipeOccupant() { return static_cast<void*>(new sc4::cSC4PipeOccupant()); }

}  // namespace gzcom
