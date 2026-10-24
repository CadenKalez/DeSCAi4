// GZCOM creator for sc4::cSC4PropOccupantVisualEffect, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PropOccupantVisualEffect.h"

namespace gzcom {

void* GZCOM_CreatecSC4PropOccupantVisualEffect() { return static_cast<void*>(new sc4::cSC4PropOccupantVisualEffect()); }

}  // namespace gzcom
