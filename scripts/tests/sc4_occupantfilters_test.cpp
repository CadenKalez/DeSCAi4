#include <cstdio>

#include "sc4/OccupantFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
// Only the calls the filters make are implemented; everything else is never reached.
struct FakeOccupant final : cISC4Occupant {
    std::uint32_t type = 0;
    std::uint32_t group = 0;
    std::uint32_t iface = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid == iface) { *out = this; return true; } return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return type; }
    void GetPosition(float*) override {}
    void SetPosition(const float*) override {}
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect&) override {}
    void SetRemovalFlags(int) override {}
    void UnsetRemovalFlags(int) override {}
    bool CanRemove(int) override { return true; }
    void PostOccupantMessage(std::uint32_t, std::uint32_t) override {}
    std::uint32_t GetHighlight() const override { return 0; }
    void SetHighlight(int, bool) override {}
    void SetVisibility(bool, bool) override {}
    cISC43DPlaceableObject* GetPlaceableObject() const override { return nullptr; }
    cISC43DPlaceableObject* GetOrCreatePlaceableObject() override { return nullptr; }
    void SetPlaceableObject(cISC43DPlaceableObject*) override {}
    bool IsOccupantGroup(std::uint32_t g) const override { return g == group; }
    void AddOccupantGroup(std::uint32_t) override {}
    bool GetOccupantGroups(std::set<std::uint32_t>&) const override { return false; }
    bool GetOccupantManagerBBox(std::uint8_t*) const override { return false; }
    void SetOccupantManagerBBox(const std::uint8_t*) override {}
    bool GetLotTag(std::uint32_t&) const override { return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t) override {}
    void SetAllFlags(std::uint32_t) override {}
    void ClearFlag(std::uint32_t) override {}
    bool IsFlagSet(std::uint32_t) override { return false; }
    std::uint32_t GetFlags() override { return 0; }
};
}  // namespace

int main() {
    FakeOccupant o;
    cSC4BlockingLotOccupantFilter blocking;
    o.type = 0x74758926;
    CHECK(blocking.IsOccupantIncluded(&o));
    o.type = 0x278128a0;
    CHECK(!blocking.IsOccupantIncluded(&o));
    cLCEBlockingLotOccupantFilter lce;
    CHECK(lce.IsOccupantIncluded(&o));
    o.type = 0x1234;
    CHECK(!lce.IsOccupantIncluded(&o));
    cAutomataOccupantFilter automata;
    o.type = 0x897f7ec2; CHECK(automata.IsOccupantIncluded(&o));
    o.type = 0x278128a0; CHECK(!automata.IsOccupantIncluded(&o));
    cSC4AutomatonOccupantFilter automaton;
    o.type = 0xc7cc75f1; CHECK(automaton.IsOccupantIncluded(&o) && !automaton.IsPropertyHolderIncluded(nullptr));
    cSC4ObliterateOccupantFilter obliterate;
    o.type = 0x74758926; CHECK(!obliterate.IsOccupantIncluded(&o));
    o.type = 0x5; CHECK(obliterate.IsOccupantIncluded(&o));
    tCrimeSimOccupantFilter crime;
    o.type = 0x496e5783; o.group = 0x4102; CHECK(crime.IsOccupantIncluded(&o));
    o.group = 0; CHECK(!crime.IsOccupantIncluded(&o));
    cStoplightPropFilter stoplight;
    o.type = 0xa823821e; o.group = 0x5003; CHECK(stoplight.IsOccupantIncluded(&o));
    o.group = 0x5004; CHECK(!stoplight.IsOccupantIncluded(&o));
    cSC4ActiveToxicOccupantFilter toxic;
    o.type = 0xabf98cb4; CHECK(toxic.IsOccupantIncluded(&o));
    cSC4DecalFilter decal;
    o.type = 0xa823821e; o.iface = 0x2b3b7d86; CHECK(decal.IsOccupantIncluded(&o));
    o.iface = 0; CHECK(!decal.IsOccupantIncluded(&o));
    cSC4DemolishByNetworksOccupantFilter byNet;
    o.iface = 0xa9764c8c; CHECK(!byNet.IsOccupantIncluded(&o));
    o.iface = 0; CHECK(byNet.IsOccupantIncluded(&o));
    cSC4HoverQueriableOccupantFilter hover;
    o.type = 0xc772bf98; CHECK(hover.IsOccupantIncluded(&o));
    o.type = 0x6a11bd2b; CHECK(hover.IsOccupantIncluded(&o));
    o.type = 0x9999; CHECK(!hover.IsOccupantIncluded(&o));
    std::printf(failures ? "FAILED\n" : "sc4_occupantfilters ok\n");
    return failures ? 1 : 0;
}
