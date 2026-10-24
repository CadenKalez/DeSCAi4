// GZCOM creator for sc4::cSC4LandfillDeveloper, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/LandfillDeveloper.h"

namespace gzcom {

void* GZCOM_CreatecSC4LandfillDeveloper() { return static_cast<void*>(new sc4::cSC4LandfillDeveloper()); }

}  // namespace gzcom
