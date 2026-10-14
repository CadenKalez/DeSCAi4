// cSC4AudioListener, cRZMessage2Standard, SC4GeometryUtilities
#include <cassert>
#include <cmath>
#include <cstdio>

#include "rz/cRZMessage2Standard.h"
#include "sc4/SC4GeometryUtilities.h"
#include "sc4audio/EaseSound.h"
#include "sc4audio/Listener.h"
#include "sc4audio_fakes.h"

using namespace fakes;
using namespace SC4Audio;

namespace {

struct View final : cIViewport {
    float eye[3] = {100, 200, 300}, target[3] = {100, 0, 100}, up[3] = {0, 1, 0};
    int zoom = 3;
    void ProjectPointToViewportCoordinates2D(float[3]) override {}
    std::int32_t ViewportWidth() const override { return 1; }
    std::int32_t ViewportHeight() const override { return 1; }
    void GetCameraEye(float o[3]) const override { for (int i = 0; i < 3; ++i) o[i] = eye[i]; }
    void GetCameraTarget(float o[3]) const override { for (int i = 0; i < 3; ++i) o[i] = target[i]; }
    std::int32_t GetZoomLevel() const override { return zoom; }
    void GetCameraUp(float o[3]) const override { for (int i = 0; i < 3; ++i) o[i] = up[i]; }
};

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

void TestMessage() {
    cRZMessage2Standard* m = new cRZMessage2Standard();
    assert(!m->GetHasData1() && m->GetType() == 0);
    m->SetType(0x1234);
    m->SetData1(7); m->SetData3(9);
    assert(m->GetHasData1() && !m->GetHasData2() && m->GetHasData3() && m->GetData1() == 7 && m->GetData3() == 9);
    int x = 0;
    m->SetVoid2(&x);
    assert(m->GetVoid2() == &x && m->GetHasData2());
    m->SetType(0);
    assert(m->GetType() == 0);
    m->SetType(5);
    // unknown reference counting
    struct Obj final : cIGZUnknown {
        int refs = 1;
        bool QueryInterface(std::uint32_t, void**) override { return false; }
        std::uint32_t AddRef() override { return ++refs; }
        std::uint32_t Release() override { return --refs; }
    } obj;
    m->SetIGZUnknown(&obj);
    assert(obj.refs == 2 && m->GetHasIGZUnknown() && m->GetIGZUnknown() == &obj);
    m->SetIGZUnknown(nullptr);
    assert(obj.refs == 1 && !m->GetHasIGZUnknown());
    // QI / clone / compare / lifetime
    void* p = nullptr;
    assert(m->QueryInterface(0x65297976, &p) && p == static_cast<cIGZMessage2Standard*>(m));
    assert(m->QueryInterface(0xa52294b4, &p) && m->QueryInterface(1, &p) && !m->QueryInterface(2, &p));
    void* clone = nullptr;
    assert(m->Create(0x65297976, &clone));
    auto* c = static_cast<cIGZMessage2Standard*>(clone);
    assert(c->GetType() == 5 && !c->GetHasData1() && m->IsEqual(*c) && !m->IsLess(*c));
    assert(c->Release() == 0);          // refcount 1 (from QI) -> destroyed
    assert(m->Release() == 2 && m->Release() == 1 && m->Release() == 0);   // 3 QI references; destroyed at 1
}

void TestGeometry() {
    const float origin[3] = {0, 10, 0}, down[3] = {0, -1, 0}, plane[4] = {0, 1, 0, -2};
    float out[3];
    assert(SC4GeometryUtilities::RayIntersectsPlane(origin, down, plane, out) && Near(out[1], 2) && Near(out[0], 0));
    const float parallel[3] = {1, 0, 0};
    assert(!SC4GeometryUtilities::RayIntersectsPlane(origin, parallel, plane, out) && out[0] == 0 && out[1] == 0);
    const float onPlane[3] = {5, 2, 6};
    assert(SC4GeometryUtilities::RayIntersectsPlane(onPlane, parallel, plane, out) && out[0] == 5 && out[2] == 6);
    assert(SC4GeometryUtilities::RayIntersectsPlane(origin, down, plane, nullptr));
    assert(SC4GeometryUtilities::LengthOfLineSquared(origin, onPlane) == 25 + 64 + 36);
}

struct Rig {
    FakeSndSys sys;
    FakeServer server;
    FakeGame game;
    FakeExemplars exemplars;
    View view;
    cSC4AudioListener* listener;
    Rig() {
        Services& s = GetServices();
        s = Services{};
        s.soundSystem = &sys; s.messageServer = &server; s.game = &game; s.exemplars = &exemplars;
        SetViewUtilities(&view);
        for (int i = 0; i < 5; ++i) ZoomDistances()[i] = 0;
        listener = cSC4AudioListener::Create();
        listener->AddRef();
    }
    ~Rig() {
        listener->Release();
        SetViewUtilities(nullptr);
        GetServices() = Services{};
    }
    void CityInit() {
        FakeMessage m; m.type = kMessageCityInit;
        assert(listener->DoMessage(&m));
    }
};

void TestLifecycle() {
    Rig r;
    assert(r.listener->Init() && r.listener->initialised());
    assert(r.server.added.size() == 3 && r.server.added[0].second == 0x26d31ec1 && r.server.added[2].second == 0xc53d10aa);
    assert(r.listener->Init());          // idempotent
    assert(r.server.added.size() == 3);
    r.exemplars.exists = true;
    r.exemplars.floats = {{0x2a36e3cb, 2.5f}, {0x2a35a25f, 2.0f}};
    r.CityInit();
    assert(r.server.added.size() == 5 && r.server.added[3].second == 0xa6b79602 && r.server.added[4].second == 0xa6b79612);
    assert(r.sys.listener.rolloff == 2.5f);
    // zoom distances: default table * multipliers (1) * scale (2)
    assert(Near(ZoomDistances()[0], 2672) && Near(ZoomDistances()[4], 146));
    FakeMessage m; m.type = kMessageCityShutdown;
    assert(r.listener->DoMessage(&m));
    assert(r.server.removed.size() == 2 && r.server.removed[0].second == 0xa6b79602);
    assert(r.listener->Shutdown() && !r.listener->initialised());
    assert(r.server.removed.size() == 5);
    // unknown message is accepted; null is not
    m.type = 0x1;
    assert(r.listener->DoMessage(&m) && !r.listener->DoMessage(nullptr));
    // QI
    void* p = nullptr;
    assert(r.listener->QueryInterface(0xca4a66b9, &p) && r.listener->QueryInterface(0x452294aa, &p) && r.listener->QueryInterface(1, &p));
    assert(!r.listener->QueryInterface(5, &p));
    r.listener->Release(); r.listener->Release(); r.listener->Release();
}

void TestUpdate() {
    Rig r;
    r.listener->Init();
    r.exemplars.exists = true;
    r.exemplars.arrays = {{0xca359f17, {1000, 500, 250, 100, 50}}, {0x6a359f22, {1, 1, 1, 1, 1}}, {0x2aa40ccc, {10, 5, 2, 1, 0.5f}}};
    r.CityInit();      // reads tuning and positions the listener
    assert(Near(ZoomDistances()[3], 100));
    // zoom 3: target stays (100,0,100); listener 100 above it, looking straight down
    assert(Near(r.listener->eye()[0], 100) && Near(r.listener->eye()[1], 100) && Near(r.listener->eye()[2], 100));
    assert(Near(r.listener->target()[1], 0));
    assert(r.sys.listener.positionSets >= 1);
    assert(Near(r.sys.listener.position[1], 100));
    assert(Near(r.sys.listener.orientation[0], 0) && Near(r.sys.listener.orientation[1], -1) && Near(r.sys.listener.orientation[2], 0));
    // up vector is unit and perpendicular to front
    const float* o = r.sys.listener.orientation;
    assert(Near(o[3] * o[3] + o[4] * o[4] + o[5] * o[5], 1) && Near(o[3] * o[0] + o[4] * o[1] + o[5] * o[2], 0));
    // far zoom: target is the city centre
    r.view.zoom = 0;
    r.game.cityX = 4000; r.game.cityZ = 2000;
    assert(r.listener->UpdateListener());
    assert(Near(r.listener->target()[0], 2000) && Near(r.listener->target()[2], 1000));
    assert(Near(r.listener->eye()[1], 0 + 1000));
    // target below sea level: replaced by the intersection with the sea-level plane
    r.view.zoom = 3;
    r.view.target[1] = -50;
    r.game.seaLevel = 10;
    r.view.eye[0] = 0; r.view.eye[1] = 110; r.view.eye[2] = 0;
    r.view.target[0] = 0; r.view.target[2] = 100;   // looking down 45 degrees towards +z
    assert(r.listener->UpdateListener());
    // ray from (0,110,0) towards (0,-160,100) normalised hits y=10 at t where 110 + t*dy = 10
    const float len = std::sqrt(160.0f * 160.0f + 100.0f * 100.0f);
    const float t = 100.0f / (160.0f / len);
    assert(Near(r.listener->target()[1], 10, 0.05f) && Near(r.listener->target()[2], t * 100.0f / len, 0.1f));
    // missing prerequisites
    r.game.hasOccupants = false;
    assert(!r.listener->UpdateListener());
    r.game.hasOccupants = true;
    SetViewUtilities(nullptr);
    assert(r.listener->UpdateListener());
}

void TestMovementNotification() {
    Rig r;
    r.listener->Init();
    r.exemplars.exists = true;
    r.exemplars.arrays = {{0xca359f17, {1000, 500, 250, 100, 50}}, {0x6a359f22, {1, 1, 1, 1, 1}}, {0x2aa40ccc, {1, 1, 1, 1, 1}}};
    r.CityInit();          // threshold per level = (1*1*16*16) = 256 squared metres
    r.server.posted.clear();
    r.view.eye[0] = 500;   // far from the position recorded at city init -> posts once
    r.listener->UpdateListener();
    assert(r.server.posted.size() == 1 && r.server.posted[0] == 0x8a4a69c7);
    assert(r.server.postedUnknown[0] == static_cast<cIGZMessageTarget2*>(r.listener));
    assert(r.server.postedData1[0] == r.listener->eye());
    assert(r.listener->lastNotifiedCameraPosition()[0] == 500);
    // tiny move: below threshold, no message
    r.view.eye[0] = 501;
    r.listener->UpdateListener();
    assert(r.server.posted.size() == 1);
    // larger than sqrt(256) = 16 units
    r.view.eye[0] = 540;
    r.listener->UpdateListener();
    assert(r.server.posted.size() == 2);
}

void TestTuningReload() {
    Rig r;
    r.listener->Init();
    r.exemplars.exists = true;
    r.CityInit();
    FakeMessage m; m.type = 0xc53d10aa;
    r.sys.posted.clear();
    r.exemplars.arrays = {{0xca359f17, {10, 20, 30, 40, 50}}, {0x6a359f22, {1, 1, 1, 1, 2}}};
    assert(r.listener->DoMessage(&m));
    assert(r.sys.posted.size() == 1 && r.sys.posted[0] == 0xaa36d9b6);
    assert(Near(ZoomDistances()[4], 100) && Near(ZoomDistances()[0], 10));
    // short tables are padded with zeros
    r.exemplars.arrays = {{0xca359f17, {10, 20}}};
    r.listener->DoMessage(&m);
    assert(Near(ZoomDistances()[0], 10) && Near(ZoomDistances()[3], 0));
    // cheat code message: accepted
    FakeMessage cheat; cheat.type = kMessageCheatCode;
    assert(r.listener->DoMessage(&cheat));
}

void TestEaseSound() {
    cEaseSound e;
    const float a[3] = {10, 0, 0};
    e.SetPosition(a, false);          // before the first update the current position follows
    float p[3];
    e.GetPosition(p);
    assert(p[0] == 10);
    e.SetDelta(1000.0f, 2000.0f);     // stored per millisecond: 1 unit and 2 volume per Update(1)
    e.Update(0.0f);                   // marks the sound as updated
    const float b[3] = {20, 0, 0};
    e.SetPosition(b, false);          // now eased
    e.GetPosition(p);
    assert(p[0] == 10);
    e.Update(4.0f);
    e.GetPosition(p);
    assert(Near(p[0], 14));
    e.Update(100.0f);
    e.GetPosition(p);
    assert(Near(p[0], 20));           // never overshoots
    e.SetVolume(200, false);
    assert(e.GetVolume() == 0);
    e.Update(30.0f);                  // 60 volume
    assert(e.GetVolume() == 60);
    e.Update(100.0f);
    assert(e.GetVolume() == 200);
    e.SetVolume(10, true);
    assert(e.GetVolume() == 10);
    e.SetPosition(a, true);
    e.GetPosition(p);
    assert(p[0] == 10);
}

}  // namespace

int main() {
    TestEaseSound();
    TestMessage();
    TestGeometry();
    TestLifecycle();
    TestUpdate();
    TestMovementNotification();
    TestTuningReload();
    std::puts("sc4 listener ok");
}
