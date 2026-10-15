#include <cstdio>
#include "rz/cRZString.h"
#include "sc4/LotConfiguration.h"
#include "sc4/SCPropertyHelp.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Host final : public ILotConfigurationHost {
public:
    cSCPropertyHolder* stored = nullptr;
    cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey&) override { return stored; }
    void ReleaseExemplar(cISCPropertyHolder*) override {}
    cISCPropertyHolder* CreateExemplar(const cGZPersistResourceKey&) override { stored = new cSCPropertyHolder(); stored->AddRef(); return stored; }
    std::uint32_t NextObjectId() override { return 1000; }
    bool StoreExemplar(cISCPropertyHolder*, const cGZPersistResourceKey&, void*) override { return true; }
    void* SegmentFor(const cGZPersistResourceKey&) override { return nullptr; }
};
}
int main() {
    Host host;
    SetLotConfigurationHost(&host);
    cSCPropertyHolder h;
    h.AddRef();
    cRZString name;
    name.AssignFromCString("Test lot");
    cSCPropertyHelp::SetPropertyValue(&h, 0x20, name);
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigVersion, static_cast<std::uint8_t>(2));
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigSize, std::vector<std::uint8_t>{3, 2});
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigFamilies, std::vector<std::uint32_t>{7, 9});
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigZoneTypes, std::vector<std::uint8_t>{1, 3, 200});
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigWealthTypes, std::vector<std::uint8_t>{2});
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigRequiredRoads, static_cast<std::uint8_t>(1));
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigFoundation, static_cast<std::uint32_t>(0x55));
    // one building object at x=1.0 z=2.0 (outside the 3x2 lot is clamped), 16.16 fixed point
    cSCPropertyHelp::SetPropertyValue(&h, kLotConfigObjectFirst, std::vector<std::uint32_t>{
        0, 0, 1, 65536, 0, 2 * 65536, 100, 200, 300, 400, 0x1234, 77, 5, 6});
    auto* cfgp = new cSC4LotConfiguration();
    cSC4LotConfiguration& cfg = *cfgp;
    cfg.AddRef();
    cGZPersistResourceKey key;
    key.instance = 0xabc;
    host.stored = &h;
    CHECK(cfg.Load(key, 0));
    std::uint32_t sx, sz;
    cfg.GetSize(sx, sz);
    CHECK(sx == 3 && sz == 2 && cfg.GetID() == 0xabc);
    CHECK(cfg.BelongsToFamily(9) && !cfg.BelongsToFamily(8));
    CHECK(cfg.IsCompatibleWithZoneType(1) && cfg.IsCompatibleWithZoneType(3) && !cfg.IsCompatibleWithZoneType(2));
    CHECK(cfg.IsCompatibleWithWealthType(2) && !cfg.IsCompatibleWithWealthType(1));
    std::vector<bool> roads;
    cfg.GetRequiredRoads(roads);
    CHECK(roads.size() == 4 && roads[0] && !roads[1] && !roads[2] && roads[3]);   // south edge forced on
    CHECK(cfg.GetLotConfigurationObjectCount() == 2);
    cSC4LotConfigurationObject* b = cfg.GetPrimaryBuildingConfigurationObject();
    CHECK(b && b->x == 1.0f && b->z == 2.0f && b->value == 0x1234 && b->id == 77 &&
          b->extra.size() == 2 && b->extra[0] == 5);
    CHECK(cfg.HasObjectsOfType(3) && cfg.GetPrimaryFoundationConfigurationObject()->value == 0x55);
    cGZPersistResourceKey k2;
    cRZString out;
    cfg.GetName(out);
    CHECK(std::string(out.ToChar()) == "Test lot");

    // save and reload round trip
    cSCPropertyHolder saved;
    saved.AddRef();
    CHECK(cfg.SaveToPropertyHolder(&saved));
    auto* againp = new cSC4LotConfiguration();
    cSC4LotConfiguration& again = *againp;
    again.AddRef();
    CHECK(again.LoadFromProperties(saved, 0xabc, 0));
    again.GetSize(sx, sz);
    CHECK(sx == 3 && sz == 2 && again.BelongsToFamily(7) && again.IsCompatibleWithZoneType(3) &&
          again.GetLotConfigurationObjectCount() == 2);
    b = again.GetPrimaryBuildingConfigurationObject();
    CHECK(b && b->x == 1.0f && b->id == 77 && b->extra.size() == 2);
    cfg.Release();
    again.Release();
    std::puts(failures ? "sc4_lotconfiguration: FAILED" : "sc4_lotconfiguration ok");
    return failures != 0;
}
