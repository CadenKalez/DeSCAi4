// GZCOM creator for sc4::cSC4MySimBalloonAdvice, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/MySimBalloonAdvice.h"

namespace gzcom {

void* GZCOM_CreatecSC4MySimBalloonAdvice() { return static_cast<void*>(new sc4::cSC4MySimBalloonAdvice()); }

}  // namespace gzcom
