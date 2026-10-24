// GZCOM creator for sc4::cSC43DRender, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/Render3D.h"

namespace gzcom {

void* GZCOM_CreatecSC43DRender() { return static_cast<void*>(new sc4::cSC43DRender()); }

}  // namespace gzcom
