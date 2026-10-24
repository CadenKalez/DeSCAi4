// GZCOM creator for sc4::cSC4ModelMaker, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/ModelMaker.h"

namespace gzcom {

void* GZCOM_CreatecSC4ModelMaker() { return static_cast<void*>(new sc4::cSC4ModelMaker()); }

}  // namespace gzcom
