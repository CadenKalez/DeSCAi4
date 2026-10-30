// GZCOM creator for sc4::cSC4AnimationTickManager (0x100364a80), wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AnimationTickManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4AnimationTickManager() { return static_cast<void*>(static_cast<sc4::cISC4AnimationTickManager*>(new sc4::cSC4AnimationTickManager())); }

}  // namespace gzcom
