// GZCOM creator for sc4::cSC4RewardAdvice, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/RewardAdvice.h"

namespace gzcom {

void* GZCOM_CreatecSC4RewardAdvice() { return static_cast<void*>(new sc4::cSC4RewardAdvice()); }

}  // namespace gzcom
