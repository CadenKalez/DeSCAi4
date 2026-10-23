// cSC4AudioScapeManager — unit test.
//
// Covers: the three COM views and their ids, AddRef/Release, the service accessors, Init/Shutdown (framework
// registration, the gpSndSys mirror), CreateLayer/DestroyLayer/ClearLayer/ClearAllLayers with the layer flags,
// RegisterLayerSound/UnregisterLayerSound and the min/max instance window of tGroup::HandleSound, InsertInstance
// (implicit "nameless" layer, duplicate rejection, group merging), RemoveInstance, UpdateInstance (the ignored
// layer id, the group move), GetGroupPositions / GetDistributedGroupPositionsAvg / ...AvgIntersected (clipping and
// binning), the OnTick volume fade and every quirk listed in the header.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "gz/GZCOMDirector.h"
#include "rz/cRZSysServPtr.h"
#include "rz/cRZString.h"
#include "sc4audio/AudioScapeManager.h"
#include "sc4audio_fakes.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (false)

constexpr std::uint32_t kSndSysService = 0x23cecba0, kSndSysIid = 0x106077d2;
constexpr std::uint32_t kSndMgrService = 0x1af85bfb, kSndMgrIid = 0x3af85bb7;

// The sound system the class posts to; records every PostEventIds call with its four payloads.
struct RecordingSndSys final : fakes::FakeSndSys {
    struct Event {
        std::uint32_t id = 0, data1 = 0, data2 = 0, data3 = 0, data4 = 0;
    };
    std::vector<Event> postedIds;

    bool PostEventIds(std::uint32_t id, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                      std::uint32_t data4) override {
        postedIds.push_back(Event{id, data1, data2, data3, data4});
        return true;
    }
    void Clear() { postedIds.clear(); }
    std::size_t Count(std::uint32_t id) const {
        std::size_t n = 0;
        for (const Event& e : postedIds) n += (e.id == id) ? 1 : 0;
        return n;
    }
    const Event* Last(std::uint32_t id) const {
        for (auto it = postedIds.rbegin(); it != postedIds.rend(); ++it) {
            if (it->id == id) return &*it;
        }
        return nullptr;
    }
};

// The framework: records the tick/system-service registrations the original performs in Init/Shutdown.
struct FakeFramework final : cIGZFrameWork {
    std::uint32_t refs = 1;
    std::vector<cIGZSystemService*> services, tickList;
    int addTickCalls = 0, removeTickCalls = 0;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return refs ? --refs : 0; }
    bool AddSystemService(cIGZSystemService* service) override {
        services.push_back(service);
        return true;
    }
    bool RemoveSystemService(cIGZSystemService* service) override {
        for (auto it = services.begin(); it != services.end(); ++it) {
            if (*it == service) {
                services.erase(it);
                return true;
            }
        }
        return false;
    }
    bool GetSystemService(std::uint32_t, std::uint32_t, void**) override { return false; }
    bool EnumSystemServices(cIGZUnknownEnumerator&, cIGZUnknown*, std::uint32_t) override { return false; }
    bool AddHook(cIGZFrameWorkHooks*) override { return false; }
    bool RemoveHook(cIGZFrameWorkHooks*) override { return false; }
    bool AddToTick(cIGZSystemService* service) override {
        ++addTickCalls;
        tickList.push_back(service);
        return true;
    }
    bool RemoveFromTick(cIGZSystemService* service) override {
        ++removeTickCalls;
        for (auto it = tickList.begin(); it != tickList.end(); ++it) {
            if (*it == service) {
                tickList.erase(it);
                return true;
            }
        }
        return false;
    }
    bool AddToOnIdle(cIGZSystemService* service) override {
        idleList.push_back(service);
        return true;
    }
    bool RemoveFromOnIdle(cIGZSystemService* service) override {
        for (auto it = idleList.begin(); it != idleList.end(); ++it) {
            if (*it == service) {
                idleList.erase(it);
                return true;
            }
        }
        return false;
    }
    int GetOnIdleInterval() const override { return 1; }
    void SetOnIdleInterval(int) override {}
    void OnTick(std::uint32_t tick) override {
        for (cIGZSystemService* service : tickList) service->OnTick(tick);
    }
    void OnIdle() override {
        for (cIGZSystemService* service : idleList) service->OnIdle(0);
    }
    bool IsTickEnabled() const override { return true; }
    void ToggleTick(bool) override {}
    void Quit(int) override {}
    void AbortiveQuit(int) override {}
    cIGZCmdLine* CommandLine() override { return nullptr; }
    bool IsInstall() const override { return false; }
    cIGZCOM* GetCOMObject() override { return nullptr; }
    int GetState() const override { return 0; }
    cIGZDebugStream* GetDebugStream() override { return nullptr; }
    cIGZDebugStream* DefaultDebugStream() override { return nullptr; }
    cIGZDebugStream* DebugStream() override { return nullptr; }
    void SetDebugStream(cIGZDebugStream*) override {}
    void SetDebugLevel(int) override {}
    int GetDebugLevel() const override { return 0; }
    cIGZOStream* StdOut() override { return nullptr; }
    cIGZOStream* StdErr() override { return nullptr; }
    cIGZIStream* StdIn() override { return nullptr; }
    bool GetStream(int, std::uint32_t, void**) override { return false; }
    bool SetStream(int, cIGZUnknown*) override { return false; }
    void SetApplication(cIGZApp*) override {}
    cIGZApp* Application() const override { return nullptr; }
    void ReportException(const char*) override {}
    cIGZUnknown* ExceptionNotificationObj() const override { return nullptr; }

    std::vector<cIGZSystemService*> idleList;
};

FakeFramework g_framework;
RecordingSndSys g_sndSys;
fakes::FakeSndMgr g_sndMgr;
bool g_failManagerLookup = false;

bool ResolveService(std::uint32_t serviceId, std::uint32_t iid, void** out) {
    if (serviceId == kSndSysService && iid == kSndSysIid) {
        g_sndSys.AddRef();
        *out = static_cast<cIGZSndSys*>(&g_sndSys);
        return true;
    }
    if (serviceId == kSndMgrService && iid == kSndMgrIid) {
        if (g_failManagerLookup) return false;
        g_sndMgr.AddRef();
        *out = static_cast<cIGZSndMgr*>(&g_sndMgr);
        return true;
    }
    return false;
}

void InstallFakes() {
    SetRZFramework(&g_framework);
    SetRZServiceResolver(&ResolveService);
}

// A manager taken through QueryInterface on the Create() result; |AddRef| once (count 1), so one Release deletes.
cSC4AudioScapeManager* NewManager() {
    cIGZSystemService* service = cSC4AudioScapeManager::Create();
    CHECK(service != nullptr);
    void* raw = nullptr;
    CHECK(service->QueryInterface(kGZIID_cISC4AudioScapeManager, &raw));   // count 0 -> 1
    return static_cast<cSC4AudioScapeManager*>(static_cast<cISC4AudioScapeManager*>(raw));
}

bool Near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }
bool NearVec(const cS3DVector3& a, const cS3DVector3& b, float epsilon = 1e-4f) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

// ---------------------------------------------------------------------------------- COM / layout
void TestComAndLayout() {
    // Create() returns the cIGZSystemService view at +8 of the object.
    {
        cIGZSystemService* service = cSC4AudioScapeManager::Create();
        CHECK(service != nullptr);
        void* raw = nullptr;
        CHECK(service->QueryInterface(kGZIID_cISC4AudioScapeManager, &raw));   // count 0 -> 1
        auto* primary = static_cast<cSC4AudioScapeManager*>(static_cast<cISC4AudioScapeManager*>(raw));
        CHECK(reinterpret_cast<char*>(service) == reinterpret_cast<char*>(primary) + 8);
        auto* messageTarget = static_cast<cIGZMessageTarget2*>(primary);
        CHECK(reinterpret_cast<char*>(messageTarget) == reinterpret_cast<char*>(primary) + 0x10);
        CHECK(primary->Release() == 0);   // count 1: destroys
    }

    // QueryInterface ids, AddRef/Release bookkeeping and the service accessors.
    auto* manager = new cSC4AudioScapeManager();   // count 0
    void* raw = nullptr;
    CHECK(manager->QueryInterface(kGZIID_cISC4AudioScapeManager, &raw));   // 0 -> 1
    CHECK(raw == static_cast<cISC4AudioScapeManager*>(manager));
    CHECK(manager->QueryInterface(kGZIID_cIGZUnknown, &raw));             // 1 -> 2
    CHECK(raw == static_cast<cISC4AudioScapeManager*>(manager));
    CHECK(manager->QueryInterface(kGZIID_cIGZSystemService, &raw));       // 2 -> 3
    CHECK(raw == static_cast<cIGZSystemService*>(manager));
    CHECK(!manager->QueryInterface(0x12345678, &raw));                    // not answered, no reference taken
    CHECK(!manager->QueryInterface(kGZIID_cISC4AudioScapeManager, nullptr));
    CHECK(manager->AddRef() == 4);
    CHECK(manager->Release() == 3);

    CHECK(manager->GetServiceID() == kGZSID_cISC4AudioScapeManager);
    manager->SetServiceID(0x11223344);
    CHECK(manager->GetServiceID() == 0x11223344);
    manager->SetServiceID(kGZSID_cISC4AudioScapeManager);
    CHECK(manager->GetServicePriority() == std::numeric_limits<std::int32_t>::min());
    CHECK(!manager->IsServiceRunning());
    manager->SetServiceRunning(true);
    CHECK(manager->IsServiceRunning());
    manager->SetServiceRunning(false);
    CHECK(!manager->OnIdle(16));

    // DoMessage claims every non-null message, including a type it never registered for.
    fakes::FakeMessage message;
    message.type = 0xdeadbeef;
    CHECK(!manager->DoMessage(nullptr));
    CHECK(manager->DoMessage(&message));

    CHECK(manager->Release() == 2);
    CHECK(manager->Release() == 1);
    CHECK(manager->Release() == 0);   // destroys
}

// ---------------------------------------------------------------------------------- Init / Shutdown
void TestInitShutdown() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    CHECK(g_framework.tickList.size() == 1);
    CHECK(g_framework.tickList[0] == static_cast<cIGZSystemService*>(manager));
    CHECK(g_framework.services.size() == 1);
    CHECK(manager->Init());   // second Init returns early
    CHECK(g_framework.tickList.size() == 1);

    // gpSndSys is set: a layer sound inserted through the 4-argument call has a group that starts it in OnTick.
    const cRZString layerName("city");
    CHECK(manager->CreateLayer(layerName, 1, 0, 20.0f));
    CHECK(manager->RegisterLayerSound(1, 0x500, false, 0, 1, 0x7fffffff));
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(1, 7, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);
    const RecordingSndSys::Event* start = g_sndSys.Last(kAudioScapeEventStartSound);
    CHECK(start != nullptr && start->data1 == 0x500);
    CHECK(g_sndSys.Count(kAudioScapeEventSetVolume) == 1);   // _PostStart also sets the volume

    CHECK(manager->Shutdown());
    CHECK(g_framework.tickList.empty());
    CHECK(g_framework.services.empty());
    CHECK(manager->Shutdown());   // already shut down
    // gpSndSys is cleared by Shutdown: the same insert now posts nothing.
    g_sndSys.Clear();
    CHECK(manager->CreateLayer(layerName, 2, 0, 20.0f));
    CHECK(manager->RegisterLayerSound(2, 0x501, false, 0, 1, 0x7fffffff));
    CHECK(manager->InsertInstance(2, 8, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.postedIds.empty());
    CHECK(manager->DestroyLayer(2));
    manager->Release();

    // A missing sound-manager service makes Init fail after the sound system has been stored (and mirrored).
    g_failManagerLookup = true;
    cSC4AudioScapeManager* failing = NewManager();
    CHECK(!failing->Init());
    g_failManagerLookup = false;
    g_sndSys.Clear();
    CHECK(failing->CreateLayer(layerName, 3, 0, 20.0f));
    CHECK(failing->RegisterLayerSound(3, 0x502, false, 0, 1, 0x7fffffff));
    CHECK(failing->InsertInstance(3, 9, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);   // gpSndSys was kept
    // Shutdown without a successful Init is a no-op (quirk): gpSndSys stays set and the layers are not freed.
    CHECK(failing->Shutdown());
    g_sndSys.Clear();
    CHECK(failing->CreateLayer(layerName, 4, 0, 20.0f));
    CHECK(failing->RegisterLayerSound(4, 0x503, false, 0, 1, 0x7fffffff));
    CHECK(failing->InsertInstance(4, 10, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);
    CHECK(failing->DestroyLayer(3));
    CHECK(failing->DestroyLayer(4));
    failing->Release();
    // Clear the mirror through a fully initialised manager's Shutdown so the static destructor has no live pointer.
    cSC4AudioScapeManager* cleanup = NewManager();
    CHECK(cleanup->Init());
    CHECK(cleanup->Shutdown());
    cleanup->Release();
    g_sndSys.Clear();
}

// ---------------------------------------------------------------------------------- layers
void TestLayers() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    const cRZString city("city"), own("own"), persistent("persistent");

    CHECK(manager->CreateLayer(city, 1, 0, 10.0f));
    CHECK(!manager->CreateLayer(city, 1, 0, 10.0f));   // duplicate id
    CHECK(manager->CreateLayer(own, 2, kAudioScapeLayerFlagOwnSound, 30.0f));
    // Flag 0 -> no auto sound; flag 1 -> auto sound with id == layer id and range 1..INT_MAX.
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(2, 1, cS3DVector3{0, 0, 0}, 1.0f));
    const RecordingSndSys::Event* start = g_sndSys.Last(kAudioScapeEventStartSound);
    CHECK(start != nullptr && start->data1 == 2);
    CHECK(manager->InsertInstance(1, 2, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);   // layer 1 has no sound

    // DestroyLayer clears the flags of every layer it inspects (quirk). A persistent layer keeps groups with
    // instances through Clear only while flag 0x10 is set.
    CHECK(manager->CreateLayer(persistent, 3, kAudioScapeLayerFlagPersistent, 10.0f));
    CHECK(manager->InsertInstance(3, 3, cS3DVector3{1, 0, 1}, 1.0f));
    int count = 10, total = 0;
    cS3DVector3 positions[8];
    CHECK(manager->GetGroupPositions(3, positions, count, total));
    CHECK(count == 1 && total == 1);
    CHECK(!manager->DestroyLayer(999));   // clears the flags of layers 1, 2 and 3
    CHECK(manager->ClearLayer(3));        // flags are 0 now: the group is freed, not kept
    count = 10;
    CHECK(manager->GetGroupPositions(3, positions, count, total));
    CHECK(count == 0 && total == 0);

    CHECK(!manager->ClearLayer(999));
    CHECK(manager->DestroyLayer(1));
    CHECK(!manager->DestroyLayer(1));
    CHECK(manager->DestroyLayer(2));
    CHECK(manager->DestroyLayer(3));

    // ClearAllLayers with a flag mask: only layers carrying all mask bits are cleared.
    CHECK(manager->CreateLayer(city, 10, kAudioScapeLayerFlagPersistent, 10.0f));
    CHECK(manager->CreateLayer(own, 11, 0, 10.0f));
    CHECK(manager->InsertInstance(10, 1, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(manager->InsertInstance(11, 1, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(manager->ClearAllLayers(kAudioScapeLayerFlagPersistent, true));
    count = 10;
    CHECK(manager->GetGroupPositions(10, positions, count, total));
    CHECK(count == 1);                              // group kept, instances removed
    CHECK(total == 1);   // quirk: ClearAllLayers leaves the layer's instance count stale
    count = 10;
    CHECK(manager->GetGroupPositions(11, positions, count, total));
    CHECK(count == 1 && total == 1);   // not selected by the mask
    // Clearing without keeping deletes the groups of the selected layer only.
    CHECK(manager->ClearAllLayers(kAudioScapeLayerFlagPersistent, false));
    count = 10;
    CHECK(manager->GetGroupPositions(10, positions, count, total));
    CHECK(count == 0 && total == 1);   // the group is gone, the stale instance count stays
    count = 10;
    CHECK(manager->GetGroupPositions(11, positions, count, total));
    CHECK(count == 1 && total == 1);

    CHECK(manager->DestroyLayer(10));
    CHECK(manager->DestroyLayer(11));
    CHECK(manager->Shutdown());
    manager->Release();
}

// ---------------------------------------------------------------------------------- sounds
void TestSounds() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    const cRZString name("sounds");

    CHECK(manager->CreateLayer(name, 1, 0, 10.0f));
    CHECK(manager->RegisterLayerSound(1, 0x600, false, 7, 2, 3));   // min 2, max 3 instances
    CHECK(!manager->RegisterLayerSound(1, 0x600, false, 0, 0, 0));  // duplicate sound id
    CHECK(!manager->RegisterLayerSound(999, 0x601, false, 0, 0, 0));
    CHECK(manager->RegisterLayerSound(1, 0x601, true, 0, 0, 0x7fffffff));   // min 0

    g_sndSys.Clear();
    // One instance: 0x600 needs at least 2 -> stop (no start event for it); 0x601 starts (min 0).
    CHECK(manager->InsertInstance(1, 100, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);
    CHECK(g_sndSys.Last(kAudioScapeEventStartSound)->data1 == 0x601);
    // Second instance: 0x600 enters its window and starts too.
    CHECK(manager->InsertInstance(1, 101, cS3DVector3{0.5f, 0, 0}, 1.0f));
    CHECK(g_sndSys.Last(kAudioScapeEventStartSound)->data1 == 0x600);
    // A third instance keeps both playing (0x600 max 3).
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(1, 102, cS3DVector3{1, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 0);
    // Removing down to one instance stops 0x600 again (its _Stop posts the stop id).
    g_sndSys.Clear();
    CHECK(manager->RemoveInstance(1, 102));
    CHECK(manager->RemoveInstance(1, 101));
    CHECK(g_sndSys.Count(kAudioScapeEventStopSound) >= 1);

    CHECK(manager->UnregisterLayerSound(1, 0x601));
    CHECK(!manager->UnregisterLayerSound(1, 0x601));
    CHECK(!manager->UnregisterLayerSound(999, 0x600));
    CHECK(manager->UnregisterLayerSound(1, 0x600));

    CHECK(manager->DestroyLayer(1));
    CHECK(manager->Shutdown());
    manager->Release();
}

// ---------------------------------------------------------------------------------- instances
void TestInstances() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    const cRZString name("instances");
    CHECK(manager->CreateLayer(name, 1, 0, 10.0f));

    int count = 0, total = 0;
    cS3DVector3 out[8];

    // Two instances inside the radius share a group; their centre is the average.
    CHECK(manager->InsertInstance(1, 1, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(manager->InsertInstance(1, 2, cS3DVector3{10, 0, 0}, 1.0f));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 1 && total == 2);
    CHECK(NearVec(out[0], cS3DVector3{5, 0, 0}));
    // Duplicate instance id.
    CHECK(!manager->InsertInstance(1, 2, cS3DVector3{50, 0, 50}, 1.0f));
    // Far away: a second group with the layer radius.
    CHECK(manager->InsertInstance(1, 3, cS3DVector3{100, 0, 0}, 1.0f));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 2 && total == 3);

    // The capacity check: a buffer smaller than the group count fails without touching count.
    int small = 1;
    CHECK(!manager->GetGroupPositions(1, out, small, total));
    CHECK(small == 1);
    // Unknown layer / instance.
    CHECK(!manager->GetGroupPositions(999, out, count, total));
    CHECK(!manager->RemoveInstance(999, 1));
    CHECK(!manager->RemoveInstance(1, 4242));

    // UpdateInstance ignores the layer id (quirk): pass a bogus one and the instance still moves.
    g_sndSys.Clear();
    CHECK(manager->UpdateInstance(999, 1, cS3DVector3{2, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventSetPosition) == 0);   // no sound playing in this layer
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 2);
    // The first group of (1,2) is now centred on (6,0,0); the far group is still at 100.
    bool sawSix = false, sawHundred = false;
    for (int i = 0; i < count; ++i) {
        sawSix = sawSix || NearVec(out[i], cS3DVector3{6, 0, 0});
        sawHundred = sawHundred || NearVec(out[i], cS3DVector3{100, 0, 0});
    }
    CHECK(sawSix && sawHundred);

    // Moving the instance out of its group's radius detaches it. The old group's averaging centre is *not*
    // recomputed on detach (quirk): it keeps (5+95)/2 = 52.5 while holding only instance 2 at 10. The moved
    // instance joins the group at 100, whose centre becomes (100+95)/2 = 97.5.
    CHECK(manager->UpdateInstance(1, 1, cS3DVector3{95, 0, 0}, 1.0f));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 2 && total == 3);
    bool sawStale = false, sawNinetySeven = false;
    for (int i = 0; i < count; ++i) {
        sawStale = sawStale || NearVec(out[i], cS3DVector3{52.5f, 0, 0});
        sawNinetySeven = sawNinetySeven || NearVec(out[i], cS3DVector3{97.5f, 0, 0});
    }
    CHECK(sawStale && sawNinetySeven);

    // Removing one instance of the 97.5 group (the original 100 one) recomputes it to (95).
    CHECK(manager->RemoveInstance(1, 3));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 2 && total == 2);
    bool sawNinetyFive = false;
    for (int i = 0; i < count; ++i) {
        sawNinetyFive = sawNinetyFive || NearVec(out[i], cS3DVector3{95, 0, 0});
    }
    CHECK(sawNinetyFive);
    // Removing the last instance of a group deletes the group.
    CHECK(manager->RemoveInstance(1, 1));
    CHECK(manager->RemoveInstance(1, 2));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 0 && total == 0);

    // The implicit "nameless" layer: an unknown layer id is created with flag 1 and radius 50, so the instance
    // joins it and the layer's own sound (id == layer id) starts.
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(55, 9, cS3DVector3{0, 0, 0}, 1.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 1);
    CHECK(g_sndSys.Last(kAudioScapeEventStartSound)->data1 == 55);
    count = 8;
    CHECK(manager->GetGroupPositions(55, out, count, total));
    CHECK(count == 1 && total == 1);
    // 50-unit radius: a second instance 40 units away joins the same group.
    CHECK(manager->InsertInstance(55, 10, cS3DVector3{40, 0, 0}, 1.0f));
    count = 8;
    CHECK(manager->GetGroupPositions(55, out, count, total));
    CHECK(count == 1 && total == 2);

    // The 6-argument overload: a one-off sound (min 1, max INT_MAX) plays in the target group.
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(1, 0x700, false, 11, cS3DVector3{0, 0, 0}, 2.0f));
    const RecordingSndSys::Event* start = g_sndSys.Last(kAudioScapeEventStartSound);
    CHECK(start != nullptr && start->data1 == 0x700);
    CHECK(start->data2 != 0);   // the instance key is the low 32 bits of the definition copy
    // Sound id 0 = no one-off definition; the instance is ordinary.
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(1, 0, false, 12, cS3DVector3{1, 0, 0}, 2.0f));
    CHECK(g_sndSys.Count(kAudioScapeEventStartSound) == 0);

    // UpdatePositions posts the group centre truncated to ints (cvttss2si).
    const cRZString fractional("fractional");
    CHECK(manager->CreateLayer(fractional, 3, 0, 10.0f));
    CHECK(manager->RegisterLayerSound(3, 0x900, false, 0, 1, 0x7fffffff));
    g_sndSys.Clear();
    CHECK(manager->InsertInstance(3, 1, cS3DVector3{1.9f, 2.9f, 3.9f}, 1.0f));
    const RecordingSndSys::Event* position = g_sndSys.Last(kAudioScapeEventSetPosition);
    CHECK(position != nullptr && position->data2 == 1 && position->data3 == 2 && position->data4 == 3);

    CHECK(manager->DestroyLayer(1));
    CHECK(manager->DestroyLayer(3));
    CHECK(manager->DestroyLayer(55));
    CHECK(manager->Shutdown());
    manager->Release();
}

// ---------------------------------------------------------------------------------- OnTick fade
void TestFade() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    const cRZString name("fade");
    CHECK(manager->CreateLayer(name, 1, 0, 10.0f));
    CHECK(manager->RegisterLayerSound(1, 0x800, false, 0, 1, 0x7fffffff));
    CHECK(manager->InsertInstance(1, 1, cS3DVector3{0, 0, 0}, 1.0f));
    g_sndSys.Clear();

    // Volume channels start at 0, targets at 1024; each tick moves by difference/7, whole for |difference| < 7.
    std::vector<std::uint32_t> volumes;
    for (int tick = 0; tick < 40; ++tick) {
        CHECK(manager->OnTick(16));
        const RecordingSndSys::Event* event = g_sndSys.Last(kAudioScapeEventSetVolume);
        if (event) volumes.push_back(event->data2);
    }
    CHECK(!volumes.empty());
    CHECK(volumes.front() == 146);   // 1024 / 7
    CHECK(volumes.back() == 1024);
    for (std::size_t i = 1; i < volumes.size(); ++i) CHECK(volumes[i] >= volumes[i - 1]);
    // Two channels per tick until both settle: 2 events per tick while fading.
    CHECK(g_sndSys.Count(kAudioScapeEventSetVolume) >= 2);

    // A second layer with no instances posts nothing.
    g_sndSys.Clear();
    CHECK(manager->DestroyLayer(1));
    CHECK(manager->CreateLayer(name, 2, 0, 10.0f));
    g_sndSys.Clear();
    CHECK(manager->OnTick(16));
    CHECK(g_sndSys.postedIds.empty());

    CHECK(manager->DestroyLayer(2));
    CHECK(manager->Shutdown());
    manager->Release();
}

// ---------------------------------------------------------------------------------- positions / binning
void TestPositions() {
    cSC4AudioScapeManager* manager = NewManager();
    CHECK(manager->Init());
    const cRZString a("a"), b("b");
    CHECK(manager->CreateLayer(a, 1, 0, 1.0f));    // radius 1: every instance gets its own group
    CHECK(manager->CreateLayer(b, 2, 0, 10.0f));
    for (std::uint32_t i = 0; i < 4; ++i) {
        const float x = (i & 1) ? 90.0f : 10.0f;
        const float z = (i & 2) ? 90.0f : 10.0f;
        CHECK(manager->InsertInstance(1, i + 1, cS3DVector3{x, 0, z}, 1.0f));
    }
    CHECK(manager->InsertInstance(1, 5, cS3DVector3{50, 0, 50}, 1.0f));   // a fifth group in the middle
    int count = 8, total = 0;
    cS3DVector3 out[16];
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 5 && total == 5);

    SC4RectFloat rect{0, 0, 100, 100};
    // capacity >= groups: the plain centres come back.
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, false));
    CHECK(count == 5 && total == 5);
    // capacity 4 (< 5 groups): side = 2, four 50x50 bins. (50,50) shares bin 3 with (90,90).
    count = 4;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, false));
    CHECK(count == 4 && total == 5);
    bool sawCorner = false, sawMiddle = false, sawBin3 = false;
    for (int i = 0; i < count; ++i) {
        sawCorner = sawCorner || NearVec(out[i], cS3DVector3{10, 0, 10}) || NearVec(out[i], cS3DVector3{90, 0, 10}) ||
                    NearVec(out[i], cS3DVector3{10, 0, 90});
        sawMiddle = sawMiddle || NearVec(out[i], cS3DVector3{50, 0, 50});
        sawBin3 = sawBin3 || NearVec(out[i], cS3DVector3{70, 0, 70});
    }
    CHECK(sawCorner && sawBin3);
    CHECK(!sawMiddle);   // the middle group was averaged into the (90,90) bin
    // capacity 1: one bin, so the answer is the average of all five centres.
    count = 1;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, false));
    CHECK(count == 1 && total == 5);
    CHECK(NearVec(out[0], cS3DVector3{50, 0, 50}));
    // Clipping: only (10,10) is inside this rectangle (the inclusive edges leave (50,50) out).
    rect = SC4RectFloat{0, 0, 49, 49};
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, true));
    CHECK(count == 1 && NearVec(out[0], cS3DVector3{10, 0, 10}));
    count = 4;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, true));
    CHECK(count == 1 && NearVec(out[0], cS3DVector3{10, 0, 10}));
    // No clip keeps everything; a far-away clipping rectangle drops everything.
    rect = SC4RectFloat{500, 500, 600, 600};
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, false));
    CHECK(count == 5);
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, true));
    CHECK(count == 0);
    // Safety deviation: groups left of / above the rectangle would get a negative bin index in the original, which
    // writes outside the bin vector. The port skips them instead.
    count = 4;
    CHECK(manager->GetDistributedGroupPositionsAvg(1, rect, out, count, total, false));
    CHECK(count == 0);
    // A zero group layer reports false and zeroes both counters; an unknown layer reports false with count 0.
    CHECK(manager->CreateLayer(cRZString("empty"), 3, 0, 10.0f));
    rect = SC4RectFloat{0, 0, 100, 100};
    count = 8; total = 8;
    CHECK(!manager->GetDistributedGroupPositionsAvg(3, rect, out, count, total, false));
    CHECK(count == 0 && total == 0);
    count = 8;
    CHECK(!manager->GetDistributedGroupPositionsAvg(999, rect, out, count, total, false));
    CHECK(count == 0);

    // Intersected: pairs whose centre distance is below the sum of the two layer radii; the temporary group's
    // position is the *second* layer's group centre.
    CHECK(manager->InsertInstance(2, 1, cS3DVector3{12, 0, 12}, 1.0f));
    CHECK(manager->InsertInstance(2, 2, cS3DVector3{95, 0, 95}, 1.0f));
    CHECK(manager->InsertInstance(2, 3, cS3DVector3{500, 0, 500}, 1.0f));
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvgIntersected(1, 2, rect, out, count, total, false));
    CHECK(count == 2);   // (10,10) near (12,12), (90,90) near (95,95); (10,90)/(90,10) find nothing
    bool sawFirst = false, sawSecond = false;
    for (int i = 0; i < count; ++i) {
        sawFirst = sawFirst || NearVec(out[i], cS3DVector3{12, 0, 12});
        sawSecond = sawSecond || NearVec(out[i], cS3DVector3{95, 0, 95});
    }
    CHECK(sawFirst && sawSecond);
    // Missing layers: count zeroed, false.
    count = 8;
    CHECK(!manager->GetDistributedGroupPositionsAvgIntersected(999, 2, rect, out, count, total, false));
    CHECK(count == 0);
    count = 8;
    CHECK(!manager->GetDistributedGroupPositionsAvgIntersected(1, 999, rect, out, count, total, false));
    CHECK(count == 0);

    // The marks the intersected call leaves on layer 1's groups are reset on the next call.
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvgIntersected(1, 2, rect, out, count, total, false));
    CHECK(count == 2);
    // A group of layer 1 is used at most once per call even when several layer-2 groups are near it.
    CHECK(manager->DestroyLayer(2));
    CHECK(manager->CreateLayer(b, 4, 0, 20.0f));
    CHECK(manager->InsertInstance(4, 20, cS3DVector3{11, 0, 11}, 1.0f));
    CHECK(manager->InsertInstance(4, 21, cS3DVector3{12, 0, 12}, 1.0f));   // joins the (11,11) group
    CHECK(manager->InsertInstance(4, 22, cS3DVector3{100, 0, 100}, 1.0f));
    count = 8;
    CHECK(manager->GetDistributedGroupPositionsAvgIntersected(1, 4, rect, out, count, total, false));
    CHECK(count == 2);

    CHECK(manager->DestroyLayer(1));
    CHECK(manager->DestroyLayer(3));
    CHECK(manager->DestroyLayer(4));
    CHECK(manager->Shutdown());
    manager->Release();
}

// ---------------------------------------------------------------------------------- deletion without a sound system
void TestGroupDeletionWithoutSoundSystem() {
    cSC4AudioScapeManager* manager = NewManager();   // never Init'ed: no sound system is held
    const cRZString name("nosnd");
    CHECK(manager->CreateLayer(name, 1, 0, 10.0f));
    CHECK(manager->InsertInstance(1, 1, cS3DVector3{0, 0, 0}, 1.0f));
    int count = 8, total = 0;
    cS3DVector3 out[4];
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 1 && total == 1);
    // _DeleteGroup refuses to free the empty group without a sound system (quirk): the group stays.
    CHECK(manager->RemoveInstance(1, 1));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 1 && total == 0);
    // ClearAllLayers must still terminate (the original would loop on the refused group).
    CHECK(manager->ClearAllLayers(0, false));
    count = 8;
    CHECK(manager->GetGroupPositions(1, out, count, total));
    CHECK(count == 1 && total == 0);
    // DestroyLayer clears the layer through tLayer::Clear, which frees the group without a sound system.
    CHECK(manager->DestroyLayer(1));
    CHECK(manager->Shutdown());
    manager->Release();
}


// ---------------------------------------------------------------------------------- the nested table types
void TestNestedTypes() {
    using tGroup = cSC4AudioScapeManager::tGroup;
    using tLayer = cSC4AudioScapeManager::tLayer;

    tGroup group;
    CHECK(group.center.x == 0.0f && group.sum.x == 0.0f && group.count == 0);
    CHECK(group.instanceCount == 0 && group.soundCount == 0 && group.pendingExpiry == 0 && group.mark == 0);
    CHECK(group.volume[0] == 0 && group.volume[1] == 0);
    CHECK(group.targetVolume[0] == kAudioScapeMaxVolume && group.targetVolume[1] == kAudioScapeMaxVolume);
    CHECK(group.lastStartVolume == 0 && group.radius == 0.0f);
    group.CenterAddPosition(cS3DVector3{2, 4, 6});
    group.CenterAddPosition(cS3DVector3{4, 8, 10});
    CHECK(group.count == 2 && NearVec(group.center, cS3DVector3{3, 6, 8}));
    group.CenterRemovePosition(cS3DVector3{2, 4, 6});
    CHECK(group.count == 1 && NearVec(group.center, cS3DVector3{4, 8, 10}));
    group.CenterRemovePosition(cS3DVector3{4, 8, 10});
    CHECK(group.count == 0);
    CHECK(std::isnan(group.center.x));   // 1.0f / 0 * 0 quirk: removing the last position leaves NaN

    CHECK(group.SetVolume(1, 500) == 0);   // returns lastStartVolume, not the value just stored
    CHECK(group.volume[1] == 500);
    CHECK(group.StopAllSounds());          // no sound system: still true
    CHECK(!group._Start(nullptr));
    CHECK(!group._Stop(nullptr));

    auto* record = static_cast<cSC4AudioScapeManager::tInstance*>(std::malloc(kAudioScapeGroupAllocationSize));
    record->id = 77;
    record->x = record->y = record->z = 0.0f;
    group.instances.push_back(record);
    group.instanceCount = 1;
    CHECK(!group.RemoveInstance(78));
    CHECK(group.RemoveInstance(77));
    CHECK(group.instanceCount == 0 && group.instances.empty());
    CHECK(group.RemoveAllInstances());

    // tLayer::Clear keeps a group of a persistent layer while it still has positions, then frees it.
    tLayer layer;
    layer.flags = kAudioScapeLayerFlagPersistent;
    auto* kept = new tGroup();
    auto* keptRecord = static_cast<cSC4AudioScapeManager::tInstance*>(std::malloc(kAudioScapeGroupAllocationSize));
    keptRecord->id = 1;
    keptRecord->x = keptRecord->y = keptRecord->z = 0.0f;
    kept->instances.push_back(keptRecord);
    kept->instanceCount = 1;
    kept->count = 1;
    layer.groups.push_back(kept);
    layer.groupCount = 1;
    layer.instanceCount = 1;
    CHECK(layer.Clear());
    CHECK(layer.groupCount == 1 && layer.instanceCount == 0);
    tGroup* survivor = layer.groups.front();
    CHECK(survivor->count == 0 && survivor->instanceCount == 0);
    CHECK(layer.Clear());
    CHECK(layer.groupCount == 0 && layer.groups.empty());
    auto* definition = new cSC4AudioScapeManager::tSoundDef();
    layer.sounds.push_back(definition);
    CHECK(layer.UnregisterAllSounds());
    CHECK(layer.sounds.empty());
}

}  // namespace

int main() {
    InstallFakes();
    TestComAndLayout();
    TestInitShutdown();
    TestLayers();
    TestSounds();
    TestInstances();
    TestFade();
    TestPositions();
    TestGroupDeletionWithoutSoundSystem();
    TestNestedTypes();
    SetRZFramework(nullptr);
    SetRZServiceResolver(nullptr);
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("cSC4AudioScapeManager: all checks passed\n");
    return 0;
}
