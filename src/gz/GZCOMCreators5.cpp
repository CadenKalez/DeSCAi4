// GZCOM creators of the hand-driven disaster managers. The original returns the cISC4DisasterManager view (the object
// plus 0x10); fire hands out its own cISC4DisasterManagerFire view (the object plus 0x78).
#include "gz/GZCOM.h"
#include "sc4/DisasterManagers.h"
#include "sc4/PipeBurstInstance.h"

namespace gzcom {

void* GZCOM_CreatecSC4DisasterManagerRiot() { return static_cast<sc4::cISC4DisasterManager*>(new sc4::cSC4DisasterManagerRiot()); }
void* GZCOM_CreatecSC4DisasterManagerFire() { return static_cast<sc4::cISC4DisasterManagerFire*>(new sc4::cSC4DisasterManagerFire()); }
void* GZCOM_CreatecSC4DisasterManagerEarthquake() { return static_cast<sc4::cISC4DisasterManager*>(new sc4::cSC4DisasterManagerEarthquake()); }
void* GZCOM_CreatecSC4DisasterManagerPipeBurst() { return static_cast<sc4::cISC4DisasterManager*>(new sc4::cSC4DisasterManagerPipeBurst()); }
void* GZCOM_CreatecSC4DisasterManagerVolcano() { return static_cast<sc4::cISC4DisasterManager*>(new sc4::cSC4DisasterManagerVolcano()); }
void* GZCOM_CreatecSC4DisasterInstancePipeBurst() { return static_cast<sc4::cISC4DisasterInstance*>(new sc4::cSC4DisasterInstancePipeBurst()); }

}  // namespace gzcom
