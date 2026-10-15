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
    sim->Release();
    S::StaticShutdown();
    std::puts(failures ? "sc4_buildingdevelopment: FAILED" : "sc4_buildingdevelopment ok");
    return failures != 0;
}
