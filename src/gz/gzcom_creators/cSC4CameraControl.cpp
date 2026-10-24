// GZCOM creator for sc4::cSC4CameraControl, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/CameraControl.h"

namespace gzcom {

void* GZCOM_CreatecSC4CameraControl() { return static_cast<void*>(new sc4::cSC4CameraControl()); }

}  // namespace gzcom
