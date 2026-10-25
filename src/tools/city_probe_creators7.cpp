#include <cstdint>

#include "sc4/Pedestrian.h"
#include "tools/city_probe_plain.h"

namespace sc4 {
IPedestrianHost* NewStubIPedestrianHost();   // app/hosts/IfaceIPedestrianHost.cpp
}  // namespace sc4

cIGZSerializable* CreateCityProbeSerializable7(std::uint32_t classId) {
    switch (classId) {
        case 0x896e75af: {
            auto* object = new sc4::cSC4Pedestrian(sc4::NewStubIPedestrianHost());
            object->AddRef();
            return object;
        }
        default: return nullptr;
    }
}
