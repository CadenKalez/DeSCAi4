#include <cstdio>

#include "fake_occupant.h"
#include "sc4/TrafficFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Network final : ITrafficNetworkOccupant {
    std::uint32_t flags = 0;
    std::uint32_t types = 0;   // bit per network type
    int released = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { ++released; return 1; }
    bool HasAnyNetworkFlag(std::uint32_t f) const override { return (flags & f) != 0; }
    bool IsOfType(std::uint32_t t) const override { return (types >> t) & 1; }
};
struct Occupant final : cISC4Occupant {
    FakeOccupant base;
    Network* network = nullptr;
    // forward everything to the scripted fake except the interface lookup
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC4NetworkOccupant && network) { *out = static_cast<ITrafficNetworkOccupant*>(network); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return base.type; }
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
    bool IsOccupantGroup(std::uint32_t) const override { return false; }
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
    Occupant o;
    Network net;
    cTrafficNetworkFilter_NetworkMap map;
    cTrafficNetworkFilter_PlayerDrive drive;
    cSC4TrafficQueryOccupantFilter query;

    o.base.type = 0x278128a0;   // a building: only the query filter takes it
    CHECK(!map.IsOccupantIncluded(&o) && !drive.IsOccupantIncluded(&o) && query.IsOccupantIncluded(&o));

    o.base.type = 0xc772bf98;   // a network occupant without the interface
    CHECK(!map.IsOccupantIncluded(&o) && !drive.IsOccupantIncluded(&o) && !query.IsOccupantIncluded(&o));

    o.network = &net;
    net.flags = 0x2000;         // outside 0x1fcf
    CHECK(!map.IsOccupantIncluded(&o) && !drive.IsOccupantIncluded(&o));
    net.flags = 0x0040;
    CHECK(map.IsOccupantIncluded(&o) && drive.IsOccupantIncluded(&o));
    CHECK(net.released == 4);   // every successful lookup was released

    net.released = 0;
    for (std::uint32_t t : {0u, 1u, 2u, 3u, 6u, 7u, 8u, 9u, 10u, 11u, 12u}) { net.types = 1u << t; CHECK(query.IsOccupantIncluded(&o)); }
    net.types = (1u << 4) | (1u << 5) | (1u << 13);
    CHECK(!query.IsOccupantIncluded(&o));
    CHECK(net.released == 12);
    CHECK(!query.IsPropertyHolderIncluded(nullptr));

    o.base.type = 0x12345;
    CHECK(!map.IsOccupantIncluded(&o) && !query.IsOccupantIncluded(&o));
    if (failures) { std::printf("sc4_trafficfilters: %d failure(s)\n", failures); return 1; }
    std::printf("sc4_trafficfilters OK\n");
    return 0;
}
