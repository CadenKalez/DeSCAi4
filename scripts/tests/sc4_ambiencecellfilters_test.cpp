#include <cstdio>

#include "sc4/AmbienceCellFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
class Lot final : public ISC4CellLot {
public:
    std::int32_t GetState() override { return state; }
    std::int32_t GetWealth() override { return wealth; }
    std::int32_t GetConstructionPhase() override { return 2; }
    bool GetBuildingZone(std::int32_t& z) override { z = zone; return true; }
    cISC4Occupant* GetBuildingOccupant() override { return nullptr; }
    bool GetGrowthStage(std::uint8_t& s) override { s = stage; return true; }
    std::int32_t GetZoneCode() override { return 7; }
    std::int32_t GetBuildingTypeId(int) override { return 9; }
    int state = 4, wealth = 2, zone = 7, stage = 5;
};
class World final : public IAmbienceCellWorld {
public:
    int ZoneTypeAt(int x, int) override { return x; }
    ISC4CellLot* LotAt(int, int) override { return &lot; }
    bool ZoneDeveloperHas(int, int, int mask) override { return mask == 1; }
    bool ZoneDeveloperIsWater(int, int) override { return false; }
    bool HasCrimeSim() override { return true; }
    std::uint8_t CrimeAt(int x, int) override { return std::uint8_t(x); }
    bool HasLandValueSim() override { return true; }
    std::uint8_t DesirabilityAt(int x, int) override { return std::uint8_t(x); }
    bool CellHasOccupant(int, int) override { return true; }
    bool BuildingZoneById(std::int32_t, std::int32_t& z) override { z = 5; return true; }
    Lot lot;
};
template <class F> bool Ask(int x, int z) { F* f = F::Create(); f->AddRef(); bool r = f->IsTrueCell(x, z); f->Release(); return r; }
}  // namespace

int main() {
    World w;
    SetAmbienceCellWorld(&w);
    CHECK(Ask<cAmbienceCellFilterIsResidential>(2, 0) && !Ask<cAmbienceCellFilterIsResidential>(4, 0));
    CHECK(Ask<cAmbienceCellFilterIsCommercial>(6, 0) && !Ask<cAmbienceCellFilterIsCommercial>(7, 0));
    CHECK(Ask<cAmbienceCellFilterIsIndustrial>(9, 0) && Ask<cAmbienceCellFilterIsLandfill>(0xe, 0) && Ask<cAmbienceCellFilterIsUnzoned>(0, 0));
    CHECK(Ask<cAmbienceCellFilterIsRoad>(0, 0) && !Ask<cAmbienceCellFilterIsRail>(0, 0));
    CHECK(Ask<cAmbienceCellFilterIsAbandoned>(0, 0) && Ask<cAmbienceCellFilterIsMediumWealth>(0, 0));
    CHECK(Ask<cAmbienceCellFilterIsLowCrime>(10, 0) && !Ask<cAmbienceCellFilterIsLowCrime>(130, 0));
    CHECK(Ask<cAmbienceCellFilterIsMediumCrime>(130, 0) && Ask<cAmbienceCellFilterIsHighCrime>(200, 0));
    CHECK(Ask<cAmbienceCellFilterIsMediumDesirability>(150, 0) && !Ask<cAmbienceCellFilterIsMediumDesirability>(200, 0));
    CHECK(Ask<cAmbienceCellFilterIsMediumGrowthStage>(0, 0) && Ask<cAmbienceCellFilterIsHighGrowthStage>(0, 0) && !Ask<cAmbienceCellFilterIsLowGrowthStage>(0, 0));
    CHECK(Ask<cAmbienceCellFilterIsAgriculture>(0, 0) && Ask<cAmbienceCellFilterIsIndustrialDirty>(0, 0));
    CHECK(!Ask<cAmbienceCellFilterIsEmpty>(0, 0) && !Ask<cAmbienceCellFilterIsZoo>(0, 0));
    std::puts(failures ? "sc4_ambiencecellfilters: FAILED" : "sc4_ambiencecellfilters: ok");
    return failures != 0;
}
