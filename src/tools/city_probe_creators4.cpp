#include <cstdint>

#include "sc4/AutomataAttractor.h"
#include "tools/city_probe_plain.h"

cIGZSerializable* CreateCityProbeSerializable4(std::uint32_t classId) {
    switch (classId) {
        case 0x6a1628ba: { auto* object = new sc4::cSC4AutomataAttractor(); object->AddRef(); return object; }
        default: return nullptr;
    }
}
