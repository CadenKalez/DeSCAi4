#include <cstdint>

#include "sc4/WatercraftManager.h"
#include "tools/city_probe_plain.h"

namespace sc4 { void ConnectNeutralWatercraftServices(cSC4WatercraftManager& manager); }   // tools/watercraft_neutral.cpp

cIGZSerializable* CreateCityProbeSerializable3(std::uint32_t classId) {
    switch (classId) {
        case 0xa98f9d28: {
            auto* manager = new sc4::cSC4WatercraftManager();
            sc4::ConnectNeutralWatercraftServices(*manager);   // the inherited automata half and a neutral host, so the record can be read
            return sc4::WrapPlain(manager);
        }
        default: return nullptr;
    }
}
