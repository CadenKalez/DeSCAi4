// GZCOM creator for sc4::cSC4TutorialSystem, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/TutorialSystem.h"

namespace gzcom {

void* GZCOM_CreatecSC4TutorialSystem() { return static_cast<void*>(new sc4::cSC4TutorialSystem()); }

}  // namespace gzcom
