#include <cstdint>

#include "sc4/WatercraftManager.h"
#include "tools/city_probe_plain.h"

cIGZSerializable* CreateCityProbeSerializable3(std::uint32_t classId) {
    switch (classId) {
        case 0xa98f9d28: return sc4::WrapPlain(new sc4::cSC4WatercraftManager());
        default: return nullptr;
    }
}
