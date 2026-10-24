// GZCOM creator for sc4::cSC4LotBaseTextureOccupant, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/LotBaseTextureOccupant.h"

namespace gzcom {

void* GZCOM_CreatecSC4LotBaseTextureOccupant() { return static_cast<void*>(new sc4::cSC4LotBaseTextureOccupant()); }

}  // namespace gzcom
