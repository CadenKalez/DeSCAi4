// GZCOM creator for sc4::cSC4TrafficNetworkMap, wired once the class gained its cIGZSerializable identity.
// The map has no SimHosts slot (it is only reachable through the city's components), so the creator hands it
// a null host; the class tolerates that until Init() is called with a real host installed by the city.
#include "gz/GZCOM.h"
#include "sc4/TrafficNetworkMap.h"

namespace gzcom {

void* GZCOM_CreatecSC4TrafficNetworkMap() { return static_cast<void*>(new sc4::cSC4TrafficNetworkMap(nullptr)); }

}  // namespace gzcom
