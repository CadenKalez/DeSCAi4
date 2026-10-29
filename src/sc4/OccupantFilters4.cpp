#include "sc4/OccupantFilters4.h"

#include "sc4/SCPropertyHelp.h"

namespace sc4 {

bool cSC4StoplightPropOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (occupant->GetType() == kStoplightPropOccupantType)
        return occupant->IsOccupantGroup(kStoplightGroupA) || occupant->IsOccupantGroup(kStoplightGroupB);
    return false;
}

bool cSC4ZoomRotationModelChangeOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (!occupant) return false;
    cISC43DPlaceableObject* placeable = occupant->GetPlaceableObject();
    if (!placeable) return true;
    const std::uint32_t word = ((placeable->GetRotationsCreatedFor() << 7) | (placeable->GetZoomLevelsCreatedFor() << 2)) & mask_;
    if ((word & 0x780) != 0 && (word & 0x7c) != 0) return false;
    return true;
}

// 0x100193514: a network occupant with one of the drivable flags (0x1f4f) is included; one with interface 0xea0a5acf is too;
// everything else is decided by its property holder.
bool cSC4WaterOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (IOccupantFilterWorld* world = OccupantFilterWorld()) {
        std::uint32_t flags = 0;
        if (world->NetworkFlags(occupant, flags) && (flags & 0x1f4f) != 0) return true;
    }
    void* raw = nullptr;
    if (occupant->QueryInterface(0xea0a5acf, &raw)) {
        static_cast<cIGZUnknown*>(raw)->Release();
        return true;
    }
    return IsPropertyHolderIncluded(occupant->AsPropertyHolder());
}

// 0x1001935a4: included when the holder has property 0x88ed3303 or 0x49512a09, else by the bool property 0xcaa45672.
bool cSC4WaterOccupantFilter::IsPropertyHolderIncluded(cISCPropertyHolder* holder) {
    if (holder->HasProperty(0x88ed3303)) return true;
    if (holder->HasProperty(0x49512a09)) return true;
    bool value = false;
    cSCPropertyHelp::GetPropertyValue(holder, 0xcaa45672, value);
    return value;
}

bool cSC4NetworkORFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (occupant->GetType() != kNetworkOccupantType) return false;
    IOccupantFilterWorld* world = OccupantFilterWorld();
    std::uint32_t flags = 0;
    return world && world->NetworkFlags(occupant, flags) && (flags & mask_) != 0;
}

}  // namespace sc4
