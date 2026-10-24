// GZCOM creator for sc4::cSC4NetworkOccupantWithPreBuiltModel, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/NetworkOccupantWithPreBuiltModel.h"

namespace gzcom {

void* GZCOM_CreatecSC4NetworkOccupantWithPreBuiltModel() { return static_cast<void*>(new sc4::cSC4NetworkOccupantWithPreBuiltModel()); }

}  // namespace gzcom
