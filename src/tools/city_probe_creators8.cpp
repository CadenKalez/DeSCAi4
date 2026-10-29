// The emergency vehicles a city file stores (patrol car, fire truck, ambulance). Own translation unit: sc4/AircraftManager.h and the
// vehicle headers both declare names from AutomataManager.h.
#include <cstdint>

#include "platform/Unwired.h"
#include "sc4/Ambulance.h"
#include "sc4/FireTruck.h"
#include "sc4/PatrolCar.h"
#include "tools/city_probe_plain.h"

namespace sc4 {
ITrafficAutomatonHost* NewStubITrafficAutomatonHost();   // app/hosts/IfaceITrafficAutomatonHost.cpp
IAutomatonHost* NewStubIAutomatonHost();                  // app/hosts/IfaceIAutomatonHost.cpp
IVehicleHost* NewStubIVehicleHost();                      // app/hosts/IfaceIVehicleHost.cpp
extern ITrafficAutomatonHost* g_loadTrafficHost;          // tools/city_probe_creators5.cpp, set by NativeTrafficHosts.cpp
extern IVehicleHost* g_loadVehicleHost;
}  // namespace sc4

// Nothing answers IEmergencyVehicleHost / IPatrolCarHost / IFireTruckHost in the running game yet (sirens, dispatch routes, crews, the
// police and fire simulators), so a loaded vehicle keeps its record but cannot dispatch.
template <class T, class... Extra>
static cIGZSerializable* NewEmergencyVehicle(Extra... extra) {
    auto* object = new T(sc4::g_loadTrafficHost ? sc4::g_loadTrafficHost : sc4::NewStubITrafficAutomatonHost(),
                         sc4::AutomatonHost() ? sc4::AutomatonHost() : sc4::NewStubIAutomatonHost(),
                         sc4::g_loadVehicleHost ? sc4::g_loadVehicleHost : sc4::NewStubIVehicleHost(), nullptr, extra...);
    object->AddRef();
    return object;
}

cIGZSerializable* CreateCityProbeSerializable8(std::uint32_t classId) {
    switch (classId) {
        case 0x4a222b98:
            SC4_UNWIRED("IPatrolCarHost / IEmergencyVehicleHost (loaded patrol car has no crime, police or siren services)");
            return NewEmergencyVehicle<sc4::cSC4PatrolCar>(static_cast<sc4::IPatrolCarHost*>(nullptr));
        case 0xaa222b83:
            SC4_UNWIRED("IFireTruckHost / IEmergencyVehicleHost (loaded fire truck has no fire, crew or siren services)");
            return NewEmergencyVehicle<sc4::cSC4FireTruck>(static_cast<sc4::IFireTruckHost*>(nullptr));
        case 0x6c1acde1:
            SC4_UNWIRED("IEmergencyVehicleHost (loaded ambulance has no siren or route services)");
            return NewEmergencyVehicle<sc4::cSC4Ambulance>();
        default: return nullptr;
    }
}
