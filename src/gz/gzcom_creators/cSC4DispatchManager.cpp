// GZCOM creator for sc4::cSC4DispatchManager, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/DispatchManager.h"

namespace gzcom {

void* GZCOM_CreatecSC4DispatchManager() { return static_cast<void*>(new sc4::cSC4DispatchManager()); }

}  // namespace gzcom
