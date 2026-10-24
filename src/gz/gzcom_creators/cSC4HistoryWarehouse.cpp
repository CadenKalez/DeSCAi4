// GZCOM creator for sc4::cSC4HistoryWarehouse, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "sc4/HistoryWarehouse.h"

namespace gzcom {

void* GZCOM_CreatecSC4HistoryWarehouse() { return static_cast<void*>(new sc4::cSC4HistoryWarehouse()); }

}  // namespace gzcom
