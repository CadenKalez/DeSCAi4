// GZCOM creator for sc4::cSC4MySim, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/MySim.h"

namespace gzcom {

void* GZCOM_CreatecSC4MySim() { return static_cast<void*>(new sc4::cSC4MySim()); }

}  // namespace gzcom
