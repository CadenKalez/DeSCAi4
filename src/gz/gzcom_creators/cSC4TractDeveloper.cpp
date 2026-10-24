// GZCOM creator for sc4::cSC4TractDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/TractDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4TractDeveloper() { return static_cast<void*>(new sc4::cSC4TractDeveloper()); }

}  // namespace gzcom
