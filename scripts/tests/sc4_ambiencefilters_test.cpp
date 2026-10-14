#include <cstdio>

#include "sc4/AmbienceOccupantFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
class Lot final : public ISC4LotInfo {
public:
    Lot(int s, int w) : state(s), wealth(w) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::int32_t GetState() override { return state; }
    std::int32_t GetWealth() override { return wealth; }
    int state, wealth, refs = 1;
};
class World final : public IAmbienceWorld {
public:
    std::int32_t CityCellsX() override { return 256; }
    std::int32_t CityCellsZ() override { return 256; }
    ISC4LotInfo* LotAtCell(int, int) override { return &lot; }
    ISC4LotInfo* LotOfOccupant(cISC4Occupant*) override { lot.AddRef(); return &lot; }
    Lot lot{4, 3};
};
class Bld final : public cSC4Occupant, public cISC4BuildingOccupant {
public:
    explicit Bld(int zone) : zone_(zone) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC4BuildingOccupant) { *out = static_cast<cISC4BuildingOccupant*>(this); AddRef(); return true; }
        return cSC4Occupant::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cSC4Occupant::AddRef(); }
    std::uint32_t Release() override { return cSC4Occupant::Release(); }
    std::int32_t GetZoneType() override { return zone_; }
    int zone_;
};
}  // namespace

int main() {
    World world;
    SetAmbienceWorld(&world);
    auto* o = new cSC4Occupant();
    o->AddRef();
    o->AddOccupantGroup(0x4200);
    auto* bus = cAmbienceOccupantFilterIsBus::Create(); bus->AddRef();
    auto* taxi = cAmbienceOccupantFilterIsTaxi::Create(); taxi->AddRef();
    CHECK(bus->IsTrueOccupant(o) && !taxi->IsTrueOccupant(o));
    auto* traffic = cAmbienceOccupantFilterIsTraffic::Create(); traffic->AddRef();
    CHECK(!traffic->IsTrueOccupant(o));
    o->AddOccupantGroup(0x4002);
    CHECK(!traffic->IsTrueOccupant(o));                       // cached for this occupant (sic)
    auto* fresh = cAmbienceOccupantFilterIsTraffic::Create(); fresh->AddRef();
    CHECK(fresh->IsTrueOccupant(o));
    auto* aband = cAmbienceOccupantFilterIsAbandoned::Create(); aband->AddRef();
    CHECK(aband->IsTrueOccupant(o));
    auto* high = cAmbienceOccupantFilterIsHighWealth::Create(); high->AddRef();
    auto* low = cAmbienceOccupantFilterIsLowWealth::Create(); low->AddRef();
    CHECK(high->IsTrueOccupant(o) && !low->IsTrueOccupant(o));
    auto* powered = cAmbienceOccupantFilterIsPowered::Create(); powered->AddRef();
    CHECK(powered->IsTrueOccupant(o) && world.lot.refs == 1);
    auto* distressed = cAmbienceOccupantFilterIsDistressed::Create(); distressed->AddRef();
    world.lot.state = 5;
    CHECK(distressed->IsTrueOccupant(o));
    auto* b = new Bld(2); b->AddRef();
    auto* comm = cAmbienceOccupantFilterIsCommercial::Create(); comm->AddRef();
    auto* res = cAmbienceOccupantFilterIsResidential::Create(); res->AddRef();
    auto* building = cAmbienceOccupantFilterIsBuilding::Create(); building->AddRef();
    CHECK(comm->IsTrueOccupant(b) && !res->IsTrueOccupant(b) && building->IsTrueOccupant(b));
    void* raw = nullptr;
    CHECK(bus->QueryInterface(kGZIID_cIAmbienceOccupantFilter, &raw));
    static_cast<cIAmbienceOccupantFilter*>(raw)->Release();
    CHECK(!bus->QueryInterface(0x1234, &raw));
    std::puts(failures ? "sc4_ambiencefilters: FAILED" : "sc4_ambiencefilters: ok");
    return failures != 0;
}
