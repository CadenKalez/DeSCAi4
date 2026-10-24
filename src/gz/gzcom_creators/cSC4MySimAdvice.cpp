// GZCOM creator for sc4::cSC4MySimAdvice, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/MySimAdvice.h"

namespace gzcom {

void* GZCOM_CreatecSC4MySimAdvice() { return static_cast<void*>(new sc4::cSC4MySimAdvice()); }

}  // namespace gzcom
