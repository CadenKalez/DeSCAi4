// GZCOM creator for sc4::cSC4Pedestrian, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/Pedestrian.h"

namespace gzcom {

void* GZCOM_CreatecSC4Pedestrian() { return static_cast<void*>(new sc4::cSC4Pedestrian()); }

}  // namespace gzcom
