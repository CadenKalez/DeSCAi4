// GZCOM creator for sc4::cSC4PipeConnectionOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PipeConnectionOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4PipeConnectionOccupant() { return static_cast<void*>(new sc4::cSC4PipeConnectionOccupant()); }

}  // namespace gzcom
