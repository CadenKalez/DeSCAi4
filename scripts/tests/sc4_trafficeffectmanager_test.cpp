// cSC4TrafficEffectManager: effect creation, rectangles, aging, vehicle following, populate flags, streams.
#include <cstdio>
#include <set>

#include "rz/PortMemoryStreams.h"
#include "sc4/TrafficEffectManager.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Host : ITrafficEffectHost {
    std::uint32_t now = 0;
    int attached = 0, detached = 0;
    bool attachResult = true;
    std::uint32_t ElapsedMilliseconds() override { return now; }
    void PositionToCell(float x, float z, int& cx, int& cz) override { cx = int(x / 16.0f); cz = int(z / 16.0f); }
    bool AttachAccidentController(cISC4Occupant*, const float*) override { ++attached; return attachResult; }
    void DetachController(cISC4Occupant*) override { ++detached; }
};
struct Vehicle final : cISC4Occupant {
    int refs = 1;
    float position[3] = {0, 0, 0};
    SC4CellRect cells{4, 6, 5, 7};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return 0; }
    void GetPosition(float* xyz) override { for (int i = 0; i < 3; ++i) xyz[i] = position[i]; }
    void SetPosition(const float*) override {}
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect& r) override { r = cells; }
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
    Host host;
    Vehicle car;
    cSC4TrafficEffectManager manager(&host);
    int x = 0, z = 0;
    CHECK(!manager.GetMostRecentAccidentLocation(x, z));
    CHECK(manager.GetPopulateFlags(0, 0) == 0);

    // an accident on the car: centre = middle of its cell rectangle (integer division), radius 3
    car.position[0] = 4.5f * 16; car.position[2] = 6.5f * 16;
    TrafficEffect* accident = manager.CreateAccident(&car, 3, 2000);
    CHECK(accident->x == 4 && accident->z == 6 && accident->radius == 3 && accident->timeLeft == 2000);
    CHECK(accident->nearRect.x0 == 2 && accident->nearRect.z1 == 8 && accident->farRect.x0 == 1 && accident->farRect.x1 == 7 && accident->farRect.z1 == 9);
    CHECK(accident->controllerAttached && host.attached == 1 && car.refs == 2);
    CHECK(manager.GetMostRecentAccidentLocation(x, z) && x == 4 && z == 6);
    CHECK(manager.GetPopulateFlags(4, 6) == 3);   // inside both
    CHECK(manager.GetPopulateFlags(1, 6) == 2);   // only the far rectangle
    CHECK(manager.GetPopulateFlags(0, 6) == 0);
    CHECK(manager.GetPopulateFlags(4, 10) == 0);

    // a fixed effect: radius 0 makes the near rect a single cell, lasts forever
    TrafficEffect* fixed = manager.CreateEffect(20, 20, 0, nullptr);
    CHECK(fixed->timeLeft == 0xffffffffu && fixed->nearRect.x0 == 20 && fixed->nearRect.x1 == 20 && !fixed->controllerAttached);
    CHECK(manager.GetPopulateFlags(20, 20) == 3);
    CHECK(manager.GetMostRecentAccidentLocation(x, z) && x == 20);
    TrafficEffect* held = manager.CreateEffect(30, 30, 1, &car);
    CHECK(held->occupant == &car && car.refs == 3);

    // updates only every 500 ms; the car moves one cell east and the rectangles follow
    host.now = 400;
    car.position[0] = 5.5f * 16;
    manager.Update();
    CHECK(manager.NextUpdate() == 900 && accident->x == 5 && accident->timeLeft == 1600 && accident->nearRect.x0 == 3);
    CHECK(fixed->timeLeft == 0xffffffffu - 400);
    host.now = 500;
    manager.Update();                       // too early
    CHECK(accident->timeLeft == 1600);
    host.now = 1000;
    manager.Update();
    CHECK(accident->timeLeft == 1000 && manager.NextUpdate() == 1500);
    // expiry: more than timeLeft elapsed -> shut down and removed
    host.now = 3000;
    manager.Update();
    CHECK(manager.Effects().size() == 2 && host.detached == 1 && car.refs == 2);
    CHECK(manager.GetPopulateFlags(5, 6) == 3 && manager.GetPopulateFlags(30, 30) == 0);   // the held effect followed the car to (5,6)

    // streams: one effect without the (nested) vehicle object round-trips
    {
        PortMemoryOStream os;
        CHECK(manager.Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        cSC4TrafficEffectManager loaded(&host);
        CHECK(loaded.Read(is));
        CHECK(loaded.Effects().size() == 2);
        auto it = loaded.Effects().begin();
        CHECK(it->x == 20 && it->z == 20 && it->nearRect.x0 == 20 && it->timeLeft == fixed->timeLeft);
        PortMemoryIStream bad(os.bytes().data(), 1);
        cSC4TrafficEffectManager failed(&host);
        CHECK(!failed.Read(bad));
        const std::uint8_t wrongVersion[2] = {2, 0};
        PortMemoryIStream wrong(wrongVersion, 2);
        CHECK(!failed.Read(wrong));
    }

    // stopping and shutdown
    CHECK(manager.StopEffect(held));
    CHECK(manager.Effects().size() == 1 && car.refs == 1);
    TrafficEffect stranger;
    CHECK(manager.StopEffect(&stranger) && manager.Effects().size() == 1);   // unknown effect: still true
    manager.Shutdown();
    CHECK(manager.Effects().empty());
    host.attachResult = false;
    CHECK(!manager.CreateAccident(&car, 1, 10)->controllerAttached);
    manager.Shutdown();
    CHECK(car.refs == 1);
    if (failures) { std::printf("sc4_trafficeffectmanager: %d failure(s)\n", failures); return 1; }
    std::printf("sc4_trafficeffectmanager OK\n");
    return 0;
}
