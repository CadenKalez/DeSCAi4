// GZCOM creator for sc4::cSC4LotRetainingWallOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/LotRetainingWallOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4LotRetainingWallOccupant() { return static_cast<void*>(new sc4::cSC4LotRetainingWallOccupant()); }

}  // namespace gzcom
