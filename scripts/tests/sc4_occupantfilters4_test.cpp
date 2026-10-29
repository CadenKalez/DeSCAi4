// The stoplight, zoom/rotation and water occupant filters.
#include <cstdio>

#include "fake_occupant.h"
#include "sc4/OccupantFilters4.h"
#include "sc4/PlaceableObject.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct World final : IOccupantFilterWorld {
    bool network = false;
    std::uint32_t flags = 0;
    bool NetworkFlags(cISC4Occupant*, std::uint32_t& out) override { out = flags; return network; }
    bool NetworkIsPlaced(cISC4Occupant*) override { return true; }
    int OccupantViewLevel(cISC4Occupant*) override { return 0; }
};
struct Placeable final : cISC43DPlaceableObject {
    std::uint32_t zoom = 0, rotation = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    int GetModelCount() override { return 0; }
    std::uint32_t GetModels(cIS3DModelInstance**, std::uint32_t&) override { return 0; }
    bool AddModel(cIS3DModelInstance*) override { return false; }
    bool RemoveModel(cIS3DModelInstance*) override { return false; }
    bool AddOverlay(std::uint32_t) override { return false; }
    void SetVisibility(bool) override {}
    void SetOverlaysVisible(bool) override {}
    bool IsVisible() const override { return false; }
    void RemoveAll() override {}
    std::uint32_t GetZoomLevelsCreatedFor() const override { return zoom; }
    void SetZoomLevelsCreatedFor(std::uint32_t z) override { zoom = z; }
    std::uint32_t GetRotationsCreatedFor() const override { return rotation; }
    void SetRotationsCreatedFor(std::uint32_t r) override { rotation = r; }
};
}  // namespace

int main() {
    FakeOccupant o;

    cSC4StoplightPropOccupantFilter stoplight;
    o.type = kStoplightPropOccupantType;
    o.group = 0x5002;
    CHECK(stoplight.IsOccupantIncluded(&o));
    o.group = 0x5003;
    CHECK(stoplight.IsOccupantIncluded(&o));
    o.group = 0x5004;
    CHECK(!stoplight.IsOccupantIncluded(&o));
    o.group = 0x5002;
    o.type = 1;
    CHECK(!stoplight.IsOccupantIncluded(&o) && !stoplight.IsOccupantTypeIncluded(kStoplightPropOccupantType) && !stoplight.IsPropertyHolderIncluded(nullptr));

    // zoom 3 (bit 5) and rotation 2 (bit 9): the occupant is skipped when its models were already built for both
    cSC4ZoomRotationModelChangeOccupantFilter zoomRotation;
    zoomRotation.SetZoomAndRotation(3, 2);
    CHECK(zoomRotation.Mask() == ((1u << 9) | (1u << 5)));
    CHECK(!zoomRotation.IsOccupantIncluded(nullptr));
    o.placeable = nullptr;
    CHECK(zoomRotation.IsOccupantIncluded(&o));                       // nothing built yet
    Placeable placeable;
    o.placeable = &placeable;
    placeable.zoom = 0;                                               // neither built: included
    CHECK(zoomRotation.IsOccupantIncluded(&o));
    placeable.zoom = 1u << 3;                                         // zoom built, rotation not: included
    CHECK(zoomRotation.IsOccupantIncluded(&o));
    placeable.rotation = 1u << 2;                                     // both built: excluded
    CHECK(!zoomRotation.IsOccupantIncluded(&o));
    placeable.zoom = 0;                                               // rotation built, zoom not: included
    CHECK(zoomRotation.IsOccupantIncluded(&o));
    o.placeable = nullptr;

    // water filter: drivable network flags, the special interface, or the property holder's marks
    cSC4WaterOccupantFilter water;
    World world;
    SetOccupantFilterWorld(&world);
    world.network = true;
    world.flags = 0x10;                                               // not a drivable flag (mask 0x1f4f)
    cSCPropertyHolder holder;
    o.holder = &holder;
    CHECK(!water.IsOccupantIncluded(&o));                              // no marks, bool property unset
    world.flags = 0x40;
    CHECK(water.IsOccupantIncluded(&o));
    world.network = false;
    CHECK(!water.IsOccupantIncluded(&o));
    cSCPropertyHelp::SetPropertyValue(&holder, 0xcaa45672, true);
    CHECK(water.IsPropertyHolderIncluded(&holder) && water.IsOccupantIncluded(&o));
    cSCPropertyHolder marked;
    cSCPropertyHelp::SetPropertyValue(&marked, 0x49512a09, std::uint32_t{1});
    CHECK(water.IsPropertyHolderIncluded(&marked));
    o.iface = 0xea0a5acf;                                              // the special interface answers
    o.holder = nullptr;
    CHECK(water.IsOccupantIncluded(&o));
    SetOccupantFilterWorld(nullptr);

    {   // network OR filter: only network-type occupants, any flag of the mask
        cSC4NetworkORFilter orFilter(0x30);
        World w;
        SetOccupantFilterWorld(&w);
        w.network = true;
        o.type = kNetworkOccupantType;
        w.flags = 0x10;
        CHECK(orFilter.IsOccupantIncluded(&o));
        w.flags = 0x4;
        CHECK(!orFilter.IsOccupantIncluded(&o));
        w.flags = 0x30;
        o.type = 5;
        CHECK(!orFilter.IsOccupantIncluded(&o));
        CHECK(!cSC4NetworkORFilter().IsOccupantIncluded(&o));
        SetOccupantFilterWorld(nullptr);
    }

    std::puts(failures ? "sc4_occupantfilters4: FAILED" : "sc4_occupantfilters4 ok");
    return failures != 0;
}
