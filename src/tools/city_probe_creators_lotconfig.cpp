// city_probe_creators_lotconfig - probe tooling, not part of the game: creates cSC4LotConfigurationManager
// for the probe's serializable factory. It is isolated in its own translation unit because
// LotConfigurationManager.h and BuildingDevelopmentSimulator.h both define the same constant name
// (kGZCLSID_cSC4BuildingDevelopmentSimulator), so they cannot be included in one file.
#include "sc4/LotConfigurationManager.h"
#include "tools/city_probe_plain.h"

cIGZSerializable* CreateCityProbeLotConfigurationManager() {
    return sc4::WrapPlain(new sc4::cSC4LotConfigurationManager());
}
