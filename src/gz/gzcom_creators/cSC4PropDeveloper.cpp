// GZCOM creator for sc4::cSC4PropDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/PropDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4PropDeveloper() { return static_cast<void*>(new sc4::cSC4PropDeveloper()); }

}  // namespace gzcom
