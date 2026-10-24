// GZCOM creator for sc4::cSC4AuraSimulator, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AuraSimulator.h"

namespace gzcom {

void* GZCOM_CreatecSC4AuraSimulator() { return static_cast<void*>(new sc4::cSC4AuraSimulator()); }

}  // namespace gzcom
