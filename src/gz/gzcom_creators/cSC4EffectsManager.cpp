// GZCOM creator for sc4::cSC4EffectsManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/EffectsManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4EffectsManager() { return static_cast<void*>(new sc4::cSC4EffectsManager()); }

}  // namespace gzcom
