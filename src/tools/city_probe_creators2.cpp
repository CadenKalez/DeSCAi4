// city_probe_creators2 - the recovered city components whose class ids are not in the GZCOM table (the city creates them
// by id). Each class is its own translation unit's include set because the headers redefine each other's constants.
#include <cstdint>

#include "sc4/CitySituationManager.h"
#include "tools/city_probe_plain.h"

cIGZSerializable* CreateCityProbeSerializable2(std::uint32_t classId) {
    switch (classId) {
        case 0x0bb14381: return sc4::WrapPlain(new sc4::cSC4CitySituationManager());
        default: return nullptr;
    }
}
