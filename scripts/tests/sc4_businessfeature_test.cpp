#include <cstdio>

#include "sc4/BusinessFeature.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct Lot final : IBusinessLot {
    int refs = 1;
    cSC4BusinessUnit* unit = nullptr;
    int clears = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void SetBusinessUnit(cSC4BusinessUnit* u) override { unit = u; }
    void ClearOwnerLink() override { ++clears; }
};

struct Host final : IFeatureHost {
    bool LoadFeatureEntries(unsigned index, std::vector<std::pair<std::string, std::string>>& entries) override {
        if (index == 0) { entries = {{"10", "On"}, {"11", "on"}, {"12", "on"}}; return true; }
        if (index == 2) { entries = {{"11", "OFF"}, {"7", "1"}}; return true; }
        return false;
    }
};

int main() {
    auto* unit = new cSC4BusinessUnit();
    unit->AddRef();
    Lot a, b;
    unit->Add(&a);
    unit->Add(&a);                     // adding twice keeps a single member
    unit->Add(&b);
    CHECK(unit->GetMembers().size() == 2 && a.refs == 2 && a.unit == unit && b.unit == unit);
    unit->Remove(&a);
    CHECK(unit->GetMembers().size() == 1 && a.refs == 1 && a.unit == nullptr && a.clears == 1);
    CHECK(unit->GetGZCLSID() == 0x4a232da8);
    void* raw = nullptr;
    CHECK(unit->QueryInterface(0x6a233069, &raw) && unit->QueryInterface(0xe4fda3d4, &raw) && !unit->QueryInterface(3, &raw));
    unit->Release();
    unit->Release();
    unit->Release();
    CHECK(b.refs == 1);                // the destructor released the member reference

    Host host;
    SetFeatureHost(&host);
    cSC4FeatureManager* features = new cSC4FeatureManager();
    features->AddRef();
    CHECK(features->Init());
    CHECK(features->IsFeatureEnabled(10) && !features->IsFeatureEnabled(11) && features->IsFeatureEnabled(12) &&
          features->IsFeatureEnabled(7));
    CHECK(features->IsExpansionPackInstalled(1) && !features->IsExpansionPackInstalled(2) && features->IsExpansionPackInstalled(3));
    CHECK(features->GetCurrentExpansionPack() == 12);
    CHECK(features->QueryInterface(0xb562673, &raw) && !features->QueryInterface(4, &raw));
    features->Release();
    CHECK(cSC4FeatureManager::StaticShutdown());
    cSC4FeatureManager again;
    CHECK(!again.IsFeatureEnabled(10) && again.GetCurrentExpansionPack() == 0);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
