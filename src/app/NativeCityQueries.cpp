// Small city queries kept in their own translation unit (the automata and renderer headers clash with the controller manager's).
#include "app/NativeCity.h"
#include "app/NativeCityView.h"
#include "sc4/PedestrianManager.h"
#include "sc4/VehicleManager.h"
#include "sc4/cSC4City.h"

namespace native {

// The vehicle or the pedestrian layer is active: city vslots 0x158 / 0x160, cSC4AutomataManager::IsActive (vslot 0x30).
bool CityAutomataLayersActive() {
    sc4::cSC4City* city = HostCity();
    if (!city) return false;
    auto* vehicles = dynamic_cast<sc4::cSC4VehicleManager*>(city->GetComponent(sc4::CityComponent::VehicleManager));
    auto* pedestrians = dynamic_cast<sc4::cSC4PedestrianManager*>(city->GetComponent(sc4::CityComponent::PedestrianManager));
    return (vehicles && vehicles->IsActive()) || (pedestrians && pedestrians->IsActive());
}

}  // namespace native
