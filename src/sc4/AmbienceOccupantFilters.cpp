#include "sc4/AmbienceOccupantFilters.h"

#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
IAmbienceWorld* g_world = nullptr;
}
IAmbienceWorld* AmbienceWorld() { return g_world; }
void SetAmbienceWorld(IAmbienceWorld* world) { g_world = world; }

bool cAmbienceOccupantFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIAmbienceOccupantFilter || iid == kGZIID_cIAmbienceOccupantFilterAlt) {
        *out = static_cast<cIAmbienceOccupantFilter*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cAmbienceOccupantFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cAmbienceOccupantFilterIsTraffic::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    return Remember(occupant, occupant->IsOccupantGroup(0x4002) && !occupant->IsOccupantGroup(0x4005));
}

bool cAmbienceOccupantFilterIsLandfill::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    return Remember(occupant, occupant->GetType() == 0x6a11bd2b);
}

bool cAmbienceOccupantFilterIsBuilding::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    void* raw = nullptr;
    const bool isBuilding = occupant->QueryInterface(kGZIID_cISC4BuildingOccupant, &raw);
    if (isBuilding) static_cast<cIGZUnknown*>(raw)->Release();
    return Remember(occupant, isBuilding);
}

bool cAmbienceOccupantFilterIsNature::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    void* raw = nullptr;
    const bool isNature = occupant->QueryInterface(kGZIID_cISC4NatureOccupant, &raw);
    if (isNature) static_cast<cIGZUnknown*>(raw)->Release();
    return Remember(occupant, isNature);
}

bool cAmbienceOccupantFilterIsOnFire::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    bool onFire = cached_;     // (sic) the stale cached value is the default when the property is absent
    cSCPropertyHelp::GetPropertyValue(occupant->AsPropertyHolder(), 0x69dbebe1, onFire);
    return Remember(occupant, onFire);
}

namespace {
// zone type of a building occupant; -1 when it is not one
bool BuildingZone(cISC4Occupant* occupant, std::int32_t& zone) {
    void* raw = nullptr;
    if (!occupant->QueryInterface(kGZIID_cISC4BuildingOccupant, &raw)) return false;
    auto* building = static_cast<cISC4BuildingOccupant*>(raw);
    zone = building->GetBuildingProfile().purpose;
    building->Release();
    return true;
}
}  // namespace

template <int Zone>
bool cAmbienceZoneFilter<Zone>::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (this->Remembered(occupant, answer)) return answer;
    std::int32_t zone = 0;
    const bool hit = BuildingZone(occupant, zone) && zone == Zone;
    return this->Remember(occupant, hit);
}
template class cAmbienceZoneFilter<1>;
template class cAmbienceZoneFilter<2>;
template class cAmbienceZoneFilter<5>;
template class cAmbienceZoneFilter<8>;

bool cAmbienceOccupantFilterIsIndustrialDirty::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    std::int32_t zone = 0;
    const bool hit = BuildingZone(occupant, zone) && (static_cast<std::uint32_t>(zone) & 0xfffffffe) == 6;
    return Remember(occupant, hit);
}

bool cAmbienceOccupantFilterIsAbandoned::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    SC4CellRect cells;
    occupant->GetBoundingCityCells(cells);
    bool abandoned = false;
    if (IAmbienceWorld* world = AmbienceWorld())
        if (ISC4LotInfo* lot = world->LotAtCell(cells.x0, cells.z0)) abandoned = lot->GetState() == 4;
    return Remember(occupant, abandoned);
}

bool cAmbienceOccupantFilterIsDistressed::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    SC4CellRect cells;
    occupant->GetBoundingCityCells(cells);
    bool cachedValue = false, returned = false;
    IAmbienceWorld* world = AmbienceWorld();
    if (world && cells.x0 >= 0 && cells.x0 < world->CityCellsX() && cells.z0 >= 0 && cells.z0 < world->CityCellsZ()) {
        if (ISC4LotInfo* lot = world->LotAtCell(cells.x0, cells.z0)) {
            const std::uint32_t state = static_cast<std::uint32_t>(lot->GetState());
            if (state - 2u < 5u) {
                cachedValue = (0x101000101ull >> (((state - 2u) & 7) << 3)) & 1;
                returned = (0x1bu >> ((state - 2u) & 0x1f)) & 1;
            }
        }
    }
    lastOccupant_ = occupant;
    cached_ = cachedValue;
    return returned;
}

bool cAmbienceOccupantFilterIsPowered::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (Remembered(occupant, answer)) return answer;
    IAmbienceWorld* world = AmbienceWorld();
    ISC4LotInfo* lot = world ? world->LotOfOccupant(occupant) : nullptr;
    if (!lot) return Remember(occupant, false);
    const bool powered = lot->GetState() != 1;
    Remember(occupant, powered);
    lot->Release();
    return powered;
}

template <int Wealth>
bool cAmbienceWealthFilter<Wealth>::IsTrueOccupant(cISC4Occupant* occupant) {
    bool answer;
    if (this->Remembered(occupant, answer)) return answer;
    IAmbienceWorld* world = AmbienceWorld();
    ISC4LotInfo* lot = world ? world->LotOfOccupant(occupant) : nullptr;
    if (!lot) return this->Remember(occupant, false);
    const bool match = lot->GetWealth() == Wealth;
    this->Remember(occupant, match);
    lot->Release();
    return match;
}
template class cAmbienceWealthFilter<1>;
template class cAmbienceWealthFilter<2>;
template class cAmbienceWealthFilter<3>;

}  // namespace sc4
