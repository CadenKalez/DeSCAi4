#include <cstdio>
#include "rz/PortMemoryStreams.h"
#include "sc4/BuildingDevelopmentSimulator.h"
#include "sc4/SCPropertyHelp.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Resources final : public IConstructionResources {
public:
    float total = 100, used = 0;
    float Total() const override { return total; }
    float Used() const override { return used; }
    void Add(float a) override { used += a; }
    void Update() override {}
};
class Host final : public IBuildingDevelopmentSimHost {
public:
    std::map<std::uint32_t, cSCPropertyHolder*> exemplars;
    int posted = 0;
    void ForEachExemplarKey(const std::function<void(const cGZPersistResourceKey&)>& fn) override {
        for (auto& kv : exemplars) {
            cGZPersistResourceKey key;
            key.instance = kv.first;
            fn(key);
        }
    }
    cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey& key) override {
        auto it = exemplars.find(key.instance);
        return it == exemplars.end() ? nullptr : it->second;
    }
    void ReleaseExemplar(cISCPropertyHolder*) override {}
    std::int64_t MaintenanceCost(cISCPropertyHolder*) override { return 25; }
    bool LoadLocalizedName(cISCPropertyHolder*, std::uint32_t, cRZString&) override { return false; }
    void CityCellSize(float& x, float& z) override { x = z = 16.0f; }
    void PostBuildingTypesUpdated() override { ++posted; }
    std::int32_t SimulatorDay() override { return 7; }
    void LotConfigurations(std::vector<LotConfigurationInfo>& out) override {
        LotConfigurationInfo c;
        c.sizeX = 2; c.sizeZ = 2;
        c.buildingIds = {0x500};              // a family
        c.objectX = 16; c.objectZ = 16;
        c.zoneCompatibility.assign(16, false);
        c.zoneCompatibility[1] = c.zoneCompatibility[2] = true;
        out.push_back(c);
    }
};
cSCPropertyHolder* MakeBuilding(std::uint8_t purpose, std::uint8_t wealth, std::uint16_t capacity, std::uint32_t family,
                                std::vector<float> size) {
    auto* h = new cSCPropertyHolder();
    h->AddRef();
    cSCPropertyHelp::SetPropertyValue(h, 0x10, static_cast<std::uint32_t>(kExemplarTypeBuilding));
    cRZString name;
    name.AssignFromCString("Test building");
    cSCPropertyHelp::SetPropertyValue(h, 0x20, name);
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropWealth, wealth);
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropPurpose, purpose);
    const std::uint32_t dev = cSC4BuildingDevelopmentSimulator::GetDeveloperType_Static(purpose, wealth);
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropCapacities, std::vector<std::uint32_t>{dev, capacity});
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropFamilies, std::vector<std::uint32_t>{family});
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropSize, size);
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropGroups, std::vector<std::uint32_t>{0x1000, 0x1301, 0x9999});
    cSCPropertyHelp::SetPropertyValue(h, kBuildingPropCost, static_cast<std::int64_t>(5000));
    return h;
}
}
int main() {
    // developer type / wealth tables
    using S = cSC4BuildingDevelopmentSimulator;
    CHECK(S::GetDeveloperType_Static(1, 1) == 0x1010 && S::GetDeveloperType_Static(1, 3) == 0x1030);
    CHECK(S::GetDeveloperType_Static(2, 2) == 0x3120 && S::GetDeveloperType_Static(3, 2) == 0x3320);
    CHECK(S::GetDeveloperType_Static(3, 1) == 0 && S::GetDeveloperType_Static(5, 0) == 0x4100);
    CHECK(S::GetDeveloperType_Static(8, 3) == 0x4400 && S::GetDeveloperType_Static(4, 1) == 0);
    CHECK(S::GetWealth_Static(0x1010) == 1 && S::GetWealth_Static(0x3320) == 2 && S::GetWealth_Static(0x4400) == 3 &&
          S::GetWealth_Static(0x1234) == 0);

    Host host;
    host.exemplars[0x1001] = MakeBuilding(1, 2, 40, 0x500, {16, 10, 16});     // medium-wealth residential, 40 people
    host.exemplars[0x1002] = MakeBuilding(2, 1, 12, 0x500, {80, 10, 80});     // too big for the 2x2 lot
    SetBuildingDevelopmentSimHost(&host);

    auto* sim = new cSC4BuildingDevelopmentSimulator();
    sim->AddRef();
    CHECK(sim->Init() && host.posted == 1);
    CHECK(sim->IsBuildingTypeValid(0x1001) && !sim->IsBuildingTypeValid(0x7777));
    CHECK(sim->IsBuildingFamilyTypeValid(0x500) && sim->IsBuildingInFamily(0x1002, 0x500));
    std::vector<std::uint32_t> members;
    CHECK(sim->GetBuildingTypesByFamily(members, 0x500) == 2);
    const BuildingProfile* profile = sim->GetBuildingProfilePtr(0x1001);
    CHECK(profile && profile->purpose == 1 && profile->wealth == 2 && profile->capacity == 40 &&
          profile->capacities.size() == 1 && profile->capacities[0].at(0x1020) == 40);
    CHECK(profile->growthStage == 30 && profile->size[0] == 16 && profile->size[2] == 16 && profile->cost == 5000 &&
          profile->maintenance == 25);
    CHECK((profile->categoryFlags & 1) && (profile->categoryFlags & (1u << 8)) && !(profile->categoryFlags & (1u << 7)));
    CHECK(std::string(sim->GetExemplarName(0x1001).ToChar()) == "Test building");
    CHECK(std::string(sim->GetBuildingName(0x1001).ToChar()) == "Building");          // no localized / custom name
    // zone capacity: the 40-capacity building on a 2x2 lot -> 40/4 + 0.5 = 10.5 -> 10; the big one does not fit
    CHECK(sim->GetMaxCapacity(1, 1, 2) == 10 && sim->GetMaxCapacity(2, 1, 2) == 10);
    CHECK(sim->GetMaxCapacity(1, 2, 1) == 0 && sim->GetMaxCapacity(3, 1, 2) == 0);

    // construction resources
    Resources resources;
    sim->SetConstructionResources(&resources);
    CHECK(sim->GetTotalConstructionResources() == 100 && sim->GetFreeConstructionResources() == 100);
    CHECK(sim->ClaimConstructionResources(60) && sim->GetFreeConstructionResources() == 40);
    CHECK(!sim->ClaimConstructionResources(41));
    sim->ReleaseConstructionResources(10);
    CHECK(sim->GetFreeConstructionResources() == 50);

    // growth marks and persisted state
    sim->MarkTract(0x1010, 4, 5);
    CHECK(!sim->IsTractUnmarked(0x1010, 4, 5) && sim->IsTractUnmarked(0x1010, 4, 6));
    sim->NoteSuccessfulBuild(0x1010, 33);
    sim->SetCheats(true, false, true);
    PortMemoryOStream os;
    CHECK(sim->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* back = new cSC4BuildingDevelopmentSimulator();
    back->AddRef();
    CHECK(back->Read(is));
    CHECK(!back->IsTractUnmarked(0x1010, 4, 5) && back->GetDayOfLastSuccessfulBuild(0x1010) == 33 &&
          back->CheatIgnorePlumbing() && !back->CheatIgnorePower() && back->CheatIgnoreCaps());
    back->Release();

    // reloading a changed exemplar replaces its profile and family membership
    host.exemplars[0x1001] = MakeBuilding(5, 0, 7, 0x600, {1, 1, 1});
    cISCPropertyHolder* changed = host.exemplars[0x1001];
    S::LoadBuildingProperties(*changed, 0x1001, false);
    CHECK(sim->GetBuildingProfilePtr(0x1001)->purpose == 5 && sim->IsBuildingInFamily(0x1001, 0x600) &&
          !sim->IsBuildingInFamily(0x1001, 0x500));

    // growth predicates (0x100150a4c / 0x100150a86), against the original's truth tables
    for (int zone = 0; zone < 12; ++zone) {
        for (int stage = 0; stage < 9; ++stage) {
            bool want = false;
            if (zone == 3 || (zone >= 6 && zone <= 9)) want = true;
            else if (zone == 1 || zone == 4) want = stage < 4;
            else if (zone == 2 || zone == 5) want = stage < 7;
            CHECK(S::GrowthStageFitsInZone(stage, static_cast<std::uint32_t>(zone)) == want);
        }
    }
    CHECK(!S::GrowthStageFitsInZone(0, 10) && !S::GrowthStageFitsInZone(-5, 0xffffffffu));
    for (std::uint32_t zone = 0; zone < 16; ++zone) {
        for (std::uint32_t purpose = 0; purpose < 16; ++purpose) {
            bool want = purpose == zone;
            if (zone == 2) want = want || purpose == 1;
            if (zone == 3) want = want || purpose == 1 || purpose == 2;
            if (zone == 5) want = want || purpose == 4;
            if (zone == 6) want = want || purpose == 4 || purpose == 5;
            if (zone == 9) want = want || purpose == 8;
            if (zone == 0xb || zone == 0xc || zone == 0xf) want = want || purpose == 0;
            CHECK(S::BuildingFitsInZone(purpose, zone) == want);
        }
    }

    // IsSuitableForConstruction (0x100150b0c): one failure code per branch
    struct Lot final : IConstructionLot { std::int32_t state = 1; std::int32_t ConstructionState() override { return state; } };
    struct City final : IConstructionSuitabilityHost {
        std::int8_t zone = 1; Lot* lot = nullptr; std::int32_t valueClass = 3; std::uint8_t value = 50; bool power = true;
        std::int8_t ZoneTypeAt(int, int) override { return zone; }
        IConstructionLot* LotAt(int, int) override { return lot; }
        std::int32_t LandValueClassAt(int, int) override { return valueClass; }
        std::uint8_t LandValueAt(int, int) override { return value; }
        bool HasPowerAt(int, int) override { return power; }
    };
    Lot lot; City city; city.lot = &lot;
    sim->SetSuitabilityHost(&city);
    sim->SetCheats(false, false, false);
    ConstructionProfile plan; plan.purpose = 1; plan.landValueClass = 3; plan.x = 4; plan.z = 5;
    ReasonForFailure reason = kReasonLotNotReady;
    CHECK(sim->IsSuitableForConstruction(plan, reason) && reason == kReasonLotNotReady);   // success leaves the reason alone
    city.zone = 4;   reason = kReasonLandValueRange;
    CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonNone); city.zone = 1;
    city.lot = nullptr;
    CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonNoLot); city.lot = &lot;
    city.power = false;
    CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonNoPower);
    sim->SetCheats(false, true, false);   // ignore power
    CHECK(sim->IsSuitableForConstruction(plan, reason));
    sim->SetCheats(false, false, false); city.power = true;
    city.valueClass = 2;
    CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonLandValueClass);
    plan.purpose = 8;   // purposes 7..9 skip the class comparison (and fit zone 1? no: zone 1 admits purpose 1 only)
    city.zone = 8;
    CHECK(sim->IsSuitableForConstruction(plan, reason));
    plan.purpose = 1; city.zone = 1; city.valueClass = 3;
    plan.minLandValue = 40; plan.maxLandValue = 60;
    CHECK(sim->IsSuitableForConstruction(plan, reason));
    city.value = 39; CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonLandValueRange);
    city.value = 61; CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonLandValueRange);
    city.value = 60; CHECK(sim->IsSuitableForConstruction(plan, reason));
    lot.state = 2;   CHECK(!sim->IsSuitableForConstruction(plan, reason) && reason == kReasonLotNotReady);
    sim->SetSuitabilityHost(nullptr);
    sim->Release();
    S::StaticShutdown();
    std::puts(failures ? "sc4_buildingdevelopment: FAILED" : "sc4_buildingdevelopment ok");
    return failures != 0;
}
