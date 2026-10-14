#include "sc4/AmbienceCellFilters.h"

namespace sc4 {

namespace {
IAmbienceCellWorld* g_world = nullptr;
}
IAmbienceCellWorld* AmbienceCellWorld() { return g_world; }
void SetAmbienceCellWorld(IAmbienceCellWorld* world) { g_world = world; }

bool cAmbienceCellFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIAmbienceCellFilter || iid == kGZIID_cIAmbienceOccupantFilterAlt) {
        *out = static_cast<cIAmbienceCellFilter*>(this);
        AddRef();
        return true;
    }
    return false;
}
std::uint32_t cAmbienceCellFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

#define WORLD_OR(false_value) \
    IAmbienceCellWorld* world = AmbienceCellWorld(); \
    if (!world) return Remember(x, z, false_value);

template <int Lo, int Count>
bool cAmbienceZoneTypeCellFilter<Lo, Count>::IsTrueCell(int x, int z) {
    bool answer;
    if (this->Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    const int zone = world ? world->ZoneTypeAt(x, z) : -100;
    return this->Remember(x, z, static_cast<unsigned>(zone - Lo) < static_cast<unsigned>(Count));
}
template class cAmbienceZoneTypeCellFilter<1, 3>;
template class cAmbienceZoneTypeCellFilter<4, 3>;
template class cAmbienceZoneTypeCellFilter<7, 3>;
template class cAmbienceZoneTypeCellFilter<0xb, 1>;
template class cAmbienceZoneTypeCellFilter<0xc, 1>;
template class cAmbienceZoneTypeCellFilter<0xd, 1>;
template class cAmbienceZoneTypeCellFilter<0xe, 1>;
template class cAmbienceZoneTypeCellFilter<0, 1>;

template <int Mask>
bool cAmbienceDeveloperCellFilter<Mask>::IsTrueCell(int x, int z) {
    bool answer;
    if (this->Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return this->Remember(x, z, world && world->ZoneDeveloperHas(x, z, Mask));
}
template class cAmbienceDeveloperCellFilter<1>;
template class cAmbienceDeveloperCellFilter<2>;
template class cAmbienceDeveloperCellFilter<4>;
template class cAmbienceDeveloperCellFilter<0x10>;
template class cAmbienceDeveloperCellFilter<0x48>;
template class cAmbienceDeveloperCellFilter<0x80>;

bool cAmbienceCellFilterIsWater::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->ZoneDeveloperIsWater(x, z));
}

template <int Wealth>
bool cAmbienceCellWealthFilter<Wealth>::IsTrueCell(int x, int z) {
    bool answer;
    if (this->Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    return this->Remember(x, z, lot && lot->GetWealth() == Wealth);
}
template class cAmbienceCellWealthFilter<1>;
template class cAmbienceCellWealthFilter<2>;
template class cAmbienceCellWealthFilter<3>;

bool cAmbienceCellFilterIsAbandoned::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    return Remember(x, z, lot && lot->GetState() == 4);
}

bool cAmbienceCellFilterIsDistressed::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    bool returned = false, cachedValue = false;
    if (lot) {
        const std::uint32_t state = static_cast<std::uint32_t>(lot->GetState());
        if (state - 2u < 5u) {      // (sic) the cached value and the returned value differ for some states
            cachedValue = (0x101000101ull >> (((state - 2u) & 7) << 3)) & 1;
            returned = (0x1bu >> ((state - 2u) & 0x1f)) & 1;
        }
    }
    x_ = x;
    z_ = z;
    cached_ = cachedValue;
    return returned;
}

bool cAmbienceCellFilterIsContruction::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    return Remember(x, z, lot && lot->GetConstructionPhase() == 2);
}

bool cAmbienceCellFilterIsBuilding::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    return Remember(x, z, lot && lot->GetBuildingOccupant() != nullptr);
}

bool cAmbienceCellFilterIsEmpty::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, !(world && world->CellHasOccupant(x, z)));
}

bool cAmbienceCellFilterIsZoo::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    cISC4Occupant* building = lot ? lot->GetBuildingOccupant() : nullptr;
    const bool zoo = building && building->IsOccupantGroup(0x1702);
    return Remember(x, z, zoo);
}

bool cAmbienceCellFilterIsAgriculture::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::int32_t zone = 0;
    bool hit = false;
    if (lot) {
        const std::int32_t id = lot->GetBuildingTypeId(1);
        hit = id != 0 && world->BuildingZoneById(id, zone) && zone == 5;
    }
    return Remember(x, z, hit);
}

bool cAmbienceCellFilterIsIndustrialClean::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::int32_t zone = 0;
    return Remember(x, z, lot && lot->GetBuildingZone(zone) && zone == 8);
}

bool cAmbienceCellFilterIsIndustrialDirty::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::int32_t zone = 0;
    return Remember(x, z, lot && lot->GetBuildingZone(zone) && (static_cast<std::uint32_t>(zone) & 0xfffffffe) == 6);
}

bool cAmbienceCellFilterIsLowGrowthStage::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::uint8_t stage = 0;
    if (lot && lot->GetGrowthStage(stage)) {
        const std::int32_t zoneCode = lot->GetZoneCode();
        if (stage < 3 || (stage == 0 && static_cast<std::uint32_t>(zoneCode) - 7u < 3u)) return Remember(x, z, true);
    }
    return Remember(x, z, false);
}

bool cAmbienceCellFilterIsMediumGrowthStage::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::uint8_t stage = 0;
    if (lot && lot->GetGrowthStage(stage)) {
        const std::int32_t zoneCode = lot->GetZoneCode();
        if (static_cast<std::uint8_t>(stage - 3u) < 4 || (stage == 1 && static_cast<std::uint32_t>(zoneCode) - 7u < 3u))
            return Remember(x, z, true);
    }
    return Remember(x, z, false);
}

bool cAmbienceCellFilterIsHighGrowthStage::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    ISC4CellLot* lot = world ? world->LotAt(x, z) : nullptr;
    std::uint8_t stage = 0;
    if (lot && lot->GetGrowthStage(stage)) {
        const std::int32_t zoneCode = lot->GetZoneCode();
        if (stage > 6 || (stage > 1 && static_cast<std::uint32_t>(zoneCode) - 7u < 3u)) return Remember(x, z, true);
    }
    return Remember(x, z, false);
}

bool cAmbienceCellFilterIsLowCrime::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasCrimeSim() && world->CrimeAt(x, z) < 0x78);
}
bool cAmbienceCellFilterIsMediumCrime::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasCrimeSim() &&
                              static_cast<std::uint8_t>(world->CrimeAt(x, z) + 0x88u) < 0x50);
}
bool cAmbienceCellFilterIsHighCrime::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasCrimeSim() && world->CrimeAt(x, z) >= 200);
}
bool cAmbienceCellFilterIsLowDesirability::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasLandValueSim() && world->DesirabilityAt(x, z) < 0x8c);
}
bool cAmbienceCellFilterIsMediumDesirability::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasLandValueSim() &&
                              static_cast<std::uint8_t>(world->DesirabilityAt(x, z) + 0x74u) < 0x3c);
}
bool cAmbienceCellFilterIsHighDesirability::IsTrueCell(int x, int z) {
    bool answer;
    if (Remembered(x, z, answer)) return answer;
    IAmbienceCellWorld* world = AmbienceCellWorld();
    return Remember(x, z, world && world->HasLandValueSim() && world->DesirabilityAt(x, z) >= 200);
}

}  // namespace sc4
