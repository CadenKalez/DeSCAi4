// GZCOM creator for sc4::cSC4AdviceNeghborDeal, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/AdviceNeighborDeal.h"

namespace gzcom {

void* GZCOM_CreatecSC4AdviceNeghborDeal() { return static_cast<void*>(new sc4::cSC4AdviceNeghborDeal()); }

}  // namespace gzcom
