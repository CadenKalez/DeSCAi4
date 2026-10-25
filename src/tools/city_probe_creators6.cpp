#include <cstdint>

#include "sc4/AircraftManager.h"
#include "tools/city_probe_plain.h"

cIGZSerializable* CreateCityProbeSerializable6(std::uint32_t classId) {
    switch (classId) {
        case 0xc98f9d4a: { auto* object = new sc4::cSC4AircraftManager(); object->AddRef(); return object; }
        default: return nullptr;
    }
}
