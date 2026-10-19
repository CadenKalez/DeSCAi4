#include <cstdio>

#include "fake_occupant.h"
#include "sc4/OccupantFilters3.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    FakeOccupant o;
    // ambience filter: the listed types only, own interface, own counting
    auto* ambience = new cSC4AudioAmbienceManagerOccupantFilter;
    ambience->AddRef();
    for (std::uint32_t t : {0x897f7ec2u, 0xa823821eu, 0xc772bf98u, 0xc7cc75f1u, 0x278128a0u, 0x480e6151u, 0x74758926u}) CHECK(ambience->IsOccupantTypeIncluded(t));
    CHECK(!ambience->IsOccupantTypeIncluded(0x6a11bd2b) && !ambience->IsOccupantTypeIncluded(0));
    o.type = 0x278128a0;
    CHECK(ambience->IsOccupantIncluded(&o) && ambience->IsPropertyHolderIncluded(nullptr));
    void* iface = nullptr;
    CHECK(ambience->QueryInterface(0x46a80026, &iface) && !ambience->QueryInterface(0x1234, &iface));
    ambience->Release();                      // 2 -> 1
    ambience->Release();                      // deletes

    // lots may be placed over everything but two types
    cSC4PlaceLotOccupantFilter place;
    CHECK(!place.IsOccupantTypeIncluded(0x0990b2ea) && !place.IsOccupantTypeIncluded(0x2890d4de) && place.IsOccupantTypeIncluded(0x278128a0));
    o.type = 0x2890d4de;
    CHECK(!place.IsOccupantIncluded(&o) && place.IsPropertyHolderIncluded(nullptr));

    // flora is blocked by props, buildings, landfill and other flora, never by networks
    cSC4FloraBlockingOccupantFilter flora;
    for (std::uint32_t t : {0xa823821eu, 0x278128a0u, 0x6a11bd2bu, 0x74758926u}) { o.type = t; CHECK(flora.IsOccupantIncluded(&o)); }
    for (std::uint32_t t : {0xc772bf98u, 0x1u}) { o.type = t; CHECK(!flora.IsOccupantIncluded(&o)); }
    // an occupant of another type is no active fire
    cSC4ActiveFireOccupantFilter fire;
    o.type = 0x278128a0;
    CHECK(!fire.IsOccupantIncluded(&o));
    // the animatable filters need the model-class test
    cSC4AnimatableOccupantFilter animatable;
    SetModelClassTest([](cISC4Occupant*, const std::uint32_t* ids, int n) { return n == 2 && ids[0] == 0xea123be1 && ids[1] == 0xaa123bf9 && false; });
    CHECK(!animatable.IsOccupantIncluded(&o));
    if (failures == 0) std::puts("sc4_occupantfilters3: all passed");
    return failures ? 1 : 0;
}
