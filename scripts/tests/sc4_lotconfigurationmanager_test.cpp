#include <cstdio>
#include "rz/PortMemoryStreams.h"
#include "sc4/LotConfigurationManager.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Host final : public ILotConfigurationManagerHost {
public:
    std::uint8_t PreferencesDetail() override { return 1; }
    int enumerated = 0;
    void ForEachLotConfigurationKey(const std::function<void(const cGZPersistResourceKey&)>&) override { ++enumerated; }
    bool IsBuildingType(std::uint32_t id) override { return id >= 100 && id < 200; }
    bool BuildingTypeExists(std::uint32_t id) override { return IsBuildingType(id); }
    bool BuildingFootprint(std::uint32_t id, SC4Footprint& out) override {
        if (!IsBuildingType(id)) return false;
        out.x = static_cast<float>(id - 90);
        out.z = 20;
        return true;
    }
    bool BuildingsInFamily(std::uint32_t family, std::vector<std::uint32_t>& out) override {
        if (family != 7) return false;
        out.push_back(101);
        out.push_back(105);
        return true;
    }
    bool BuildingMatchesFamily(std::uint32_t type, std::uint32_t family) override { return family == 7 && (type == 101 || type == 105); }
    bool RetainingWallExists(std::uint32_t id) override { return id != 666; }
    bool BuildingInformation(std::uint32_t type, std::vector<std::uint32_t>& groups, bool& notRegular, std::uint16_t& capacity) override {
        groups = {0x10, 0x20};
        notRegular = false;
        capacity = static_cast<std::uint16_t>(type * 2);
        return true;
    }
    bool IsPropType(std::uint32_t) override { return true; }
    bool PropTypeExists(std::uint32_t id) override { return id != 9; }
    bool PropFootprint(std::uint32_t, SC4Footprint& f) override { f.x = 3; f.z = 3; return true; }
    bool PropsInFamily(std::uint32_t, std::vector<std::uint32_t>&) override { return false; }
    bool IsFloraType(std::uint32_t) override { return true; }
    bool FloraTypeExists(std::uint32_t) override { return true; }
    bool FloraFootprint(std::uint32_t, SC4Footprint& f) override { f.x = 4; f.z = 4; return true; }
    bool FloraInFamily(std::uint32_t, std::unordered_set<std::uint32_t>&) override { return false; }
    std::uint32_t NewGuid() override { return 0x1234; }
    void WriteSerializable(cIGZOStream& s, std::uint32_t) override { s.SetUint8(1); }
    bool ReadSerializable(cIGZIStream& s, std::uint32_t) override { std::uint8_t b; s.GetUint8(b); return true; }
};
class OnlySize3 final : public cISC4LotConfigurationFilter {
public:
    bool Matches(cSC4LotConfiguration* c) override { std::uint32_t x, z; c->GetSize(x, z); return x == 3; }
};
cSC4LotConfiguration* Make(std::uint32_t id, std::uint8_t sx, std::uint8_t sz, std::uint32_t family) {
    auto* c = new cSC4LotConfiguration();
    cSC4LotConfigurationObject o;
    o.type = 0;
    o.extra = {family};
    c->MakeGeneric(id, sx, sz, o);
    c->GetFamilyIDArray().push_back(family);
    return c;
}
}
int main() {
    Host host;
    SetLotConfigurationManagerHost(&host);
    auto* mgr = new cSC4LotConfigurationManager();
    mgr->AddRef();
    CHECK(mgr->Init());
    auto* a = Make(1, 3, 2, 7);
    auto* b = Make(2, 3, 3, 8);
    auto* c = Make(3, 1, 1, 7);
    CHECK(mgr->AddLotConfiguration(a, true) && mgr->AddLotConfiguration(b, true) && mgr->AddLotConfiguration(c, true));
    CHECK(mgr->GetLotConfiguration(2) == b && mgr->GetLotConfiguration(9) == nullptr);
    CHECK(cSC4LotConfigurationManager::GetLotConfigurationFamilyIDSet().count(8) == 1);

    cSC4LotConfigurationManager::ConfigurationSet set;
    CHECK(mgr->GetLotConfigurationsByFamily(set, 7) == 2 && set.count(a) && set.count(c));
    mgr->ClearLotConfigurationSet(set);
    CHECK(mgr->GetLotConfigurationsBySize(set, 3, 3) == 1 && set.count(b));
    mgr->ClearLotConfigurationSet(set);
    cSC4LotConfigurationManager::IdSet ids;
    CHECK(mgr->GetLotConfigurationIDsBySize(ids, 3, 2) == 1 && ids.count(1));

    OnlySize3 filter;
    mgr->RegisterLotConfigurationFilter(&filter);
    CHECK(mgr->GetLotConfigurationsByFilter(set, &filter) == 2);           // cached build
    mgr->ClearLotConfigurationSet(set);
    CHECK(mgr->GetLotConfigurationsByFilter(set, &filter) == 2);           // cache hit
    mgr->ClearLotConfigurationSet(set);
    mgr->UnregisterLotConfigurationFilter(&filter);

    // building information
    mgr->InitBuildingInformation(a);
    CHECK(a->GetMinBuildingCapacity() == 202 && a->GetMaxBuildingCapacity() == 210 && a->GetBuildingFlag() &&
          a->IsCompatibleWithStyle(0x20) && !a->IsCompatibleWithStyle(0x30));

    // footprints and object ids
    float rect[4];
    mgr->GetMaxFootprintForBuildingFamilies({7}, rect);
    CHECK(rect[0] == 15 && rect[1] == 20);
    std::vector<std::uint32_t> out;
    cSC4LotConfigurationObject* obj = nullptr;
    CHECK(mgr->LotConfigurationHasBuildingFamily(a, 7, &obj) && obj);
    mgr->GetObjectIDArray(obj, out);
    CHECK(out.size() == 2 && out[0] == 101);
    CHECK(mgr->LotConfigurationHasBuildingType(a, 105, &obj));

    // generic configuration
    cSC4LotConfiguration* generic = nullptr;
    CHECK(mgr->CreateGenericLotConfiguration(generic, 110) && generic);
    std::uint32_t sx, sz;
    generic->GetSize(sx, sz);
    CHECK(generic->GetID() == 0x1234 && sx == 2 && sz == 2 && generic->GetLotConfigurationObjectCount() == 1);
    cSC4LotConfiguration* none = nullptr;
    CHECK(!mgr->CreateGenericLotConfiguration(none, 5) && none == nullptr);
    // fix: drop an unknown prop id, empty objects removed
    cSC4LotConfigurationObject prop;
    prop.type = 1;
    prop.extra = {9};
    generic->GetLotConfigurationObjectArray().push_back(prop);
    CHECK(mgr->FixLotConfiguration(generic) == 1);       // first reason wins: invalid id (1), then removal (2)
    CHECK(generic->GetLotConfigurationObjectCount() == 1);
    generic->GetRetainingWallIDArray().push_back(666);
    CHECK(mgr->FixLotConfiguration(generic) == 6 && generic->GetRetainingWallIDArray().empty());
    generic->Release();

    // removal
    CHECK(mgr->RemoveLotConfiguration(2) && mgr->GetLotConfiguration(2) == nullptr);

    // stream
    PortMemoryOStream os;
    CHECK(mgr->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* m2 = new cSC4LotConfigurationManager();
    m2->AddRef();
    CHECK(m2->Read(is));
    m2->Release();
    mgr->Release();
    {
        const int before = host.enumerated;
        auto* loader = new cSC4LotConfigurationManager();
        loader->AddRef();
        loader->LoadAllStandardLotConfigurations();
        CHECK(host.enumerated == before + 1);
        loader->Release();
    }
    cSC4LotConfigurationManager::StaticShutdown();
    std::puts(failures ? "sc4_lotconfigurationmanager: FAILED" : "sc4_lotconfigurationmanager ok");
    return failures != 0;
}
