#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/AutomataAttractor.h"   // tAttractorTemplate: the attractor branch of CreateTemplateInstance
#include "sc4/AutomataController.h"
#include "sc4/DBSegment.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct FakeOccupant final : cSC4Occupant {
    float pos[3] = {40, 7, 56};
    float lo[3] = {32, 0, 48}, hi[3] = {47, 20, 63};
    void GetPosition(float* xyz) override { xyz[0] = pos[0]; xyz[1] = pos[1]; xyz[2] = pos[2]; }
    void GetBoundingBox(float* a, float* b) override { for (int i = 0; i < 3; ++i) { a[i] = lo[i]; b[i] = hi[i]; } }
};
struct FakeLeader final : IControllerLeader {
    float p[3] = {10, 0, 20};
    float length = 3.0f;
    void GetPosition(float* xyz) override { for (int i = 0; i < 3; ++i) xyz[i] = p[i]; }
    float GetLength() override { return length; }
};
struct FakeAutomaton final : IControllerAutomaton {
    int released = 0, detached = 0;
    float set[3] = {-1, -1, -1};
    FakeLeader* leader = nullptr;
    int refs = 0;
    void AddRef() override { ++refs; }
    void Release() override { ++released; }
    void SetController(void* c) override { if (!c) ++detached; }
    void SetPosition(const float* xyz) override { for (int i = 0; i < 3; ++i) set[i] = xyz[i]; }
    IControllerLeader* GetLeader() override { return leader; }
};
struct FakeQueue final : IControllerQueue {
    bool valid = true;
    float point[3] = {100, 2, 200}, dir[3] = {1, 0, 2};
    std::vector<IControllerAutomaton*> queued;
    int inits = 0;
    void InitQueuePoint(cISC4Occupant*) override { ++inits; }
    bool IsValid() override { return valid; }
    const float* QueuePoint() override { return point; }
    const float* QueueDirection() override { return dir; }
    void AddToQueue(IControllerAutomaton* a) override { queued.push_back(a); }
    void RemoveFromQueue(IControllerAutomaton* a) override {
        for (auto it = queued.begin(); it != queued.end(); ++it) if (*it == a) { queued.erase(it); break; }
    }
};
struct Unknown final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Host final : IAutomataControllerHost {
    Unknown city;
    bool hasLot = true, inCity = true, effectView = true, sound = true, view = true;
    SC4CellRect lot{2, 3, 3, 4};
    std::uint32_t facing = 0;
    FakeQueue queue;
    bool haveQueue = true;
    std::map<std::uint32_t, bool> effects;
    int effectBegins = 0, effectEnds = 0, registered = 0;
    struct Msg { std::uint32_t id; std::uint32_t a; std::int64_t b, c, d; };
    std::vector<Msg> messages;
    std::vector<std::vector<float>> frustumPoints;
    bool inFrustumAfter = 99;
    int frustumHitAt = -1;   // the n-th tested point is visible (-1: none)
    cIGZUnknown* CityContext() override { return &city; }
    bool GetOccupantLot(cISC4Occupant*, SC4CellRect& r, std::uint32_t& f) override { if (!hasLot) return false; r = lot; f = facing; return true; }
    bool PointInCity(float, float) override { return inCity; }
    float TerrainHeight(float x, float z) override { return x * 0.5f + z * 0.25f; }
    float CityTerrainHeight(float x, float z) override { return x + z; }
    float GroundHeight(float, float) override { return 9.0f; }
    float CityWidth() override { return 100.0f; }
    float CityDepth() override { return 200.0f; }
    IControllerQueue* GetQueue(cISC4Occupant*, bool) override { return haveQueue ? &queue : nullptr; }
    void RegisterActiveController(void*) override { ++registered; }
    bool BeginEffectQuery() override { ++effectBegins; return effectView; }
    bool IsEffectActive(std::uint32_t id) override { return effects[id]; }
    void EndEffectQuery() override { ++effectEnds; }
    bool SoundAvailable() override { return sound; }
    void PostSound(std::uint32_t m, std::uint32_t a, std::int64_t b, std::int64_t c, std::int64_t d) override { messages.push_back({m, a, b, c, d}); }
    std::uint32_t RandomBelow(std::uint32_t n) override { return n - 1; }
    bool ViewAvailable() override { return view; }
    bool IsPointInViewFrustum(const float* p) override {
        frustumPoints.push_back({p[0], p[1], p[2]});
        return static_cast<int>(frustumPoints.size()) - 1 == frustumHitAt;
    }
};

struct Controller final : cSC4AutomataController {
    using cSC4AutomataController::pos_;
    using cSC4AutomataController::flags_;
};
}  // namespace

int main() {
    Host host;
    SetAutomataControllerHost(&host);

    // ---- templates
    cRZString name("Ped Gen");
    CHECK(cSC4AutomataController::CreateTemplateInstance(name, 12345) == nullptr);   // unknown type: null
    // type 0x6a1628ba: the attractor branch constructs cSC4AutomataAttractor::tTemplate itself (no factory seam)
    auto* attractorTmpl = static_cast<tAttractorTemplate*>(cSC4AutomataController::CreateTemplateInstance(name, kControllerTypeAttractor));
    CHECK(attractorTmpl != nullptr);
    CHECK(attractorTmpl->controllerType == kControllerTypeAttractor && attractorTmpl->classKey == kControllerTypeAttractor);
    CHECK(attractorTmpl->field60 == -1 && attractorTmpl->radius == 0.0f && attractorTmpl->curve.PointCount() == 2);
    CHECK(std::string(attractorTmpl->name.ToChar()) == "Ped Gen" && attractorTmpl->behaviors.empty() && attractorTmpl->groups.empty());
    attractorTmpl->strengthCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1.0f}, {50.0f, 0.0f}});
    CHECK(attractorTmpl->strengthCurve.PointCount() == 2);   // the attractor-only field at +0x128 exists
    // Layout note: the binary allocates 0x148 (attractor) and 0x158 (generator) bytes; the port's structs are 0x138 and
    // 0x150 - it carries a phantom `tControllerTemplate::field28` (+0x28, in the original that word is cRZString's
    // refcount) and a cSC4ResponseCurve 8 bytes smaller than the original (0x18 vs 0x20), which shifts every template
    // field after +0x40 by -8. Both deviations predate this test (see the class report); they cancel for code that only
    // uses named members, which is what the port does.
    tControllerTemplate* tmpl = cSC4AutomataController::CreateTemplateInstance(name, kControllerTypeGenerator);
    CHECK(tmpl && tmpl->controllerType == kControllerTypeGenerator && tmpl->classKey == kControllerTypeGenerator && tmpl->field60 == -1);
    CHECK(std::string(tmpl->name.ToChar()) == "Ped Gen" && tmpl->radius == 0.0f);
    {
        std::vector<SC4Point> curve;
        tmpl->curve.GetResponseCurveData(curve);
        CHECK(tmpl->curve.PointCount() == 2);
    }
    tmpl->radius = 10.0f;
    tmpl->lifetime = 5.0f;
    tmpl->startDelay = 2.0f;
    tmpl->flags = kCtrlFlagReady | kCtrlFlagInView;
    tmpl->maxAutomata = 2;
    tmpl->sounds = {11, 22, 33};

    // ---- plain accessors and flags
    auto* c = new Controller();
    c->AddRef();
    CHECK(c->GetGZCLSID() == 0x4a18672b && !c->IsActive() && !c->IsExpired() && c->ReadyToExpire() && !c->IsTickable());
    CHECK(c->GetRadius() == 0 && std::string(c->GetTemplateName().ToChar()).empty() && !c->IsFull());
    c->SetFlag(0x30, true);
    CHECK(c->CheckFlag(0x10) && c->CheckFlag(0x20) && c->CheckFlag(0x1000 | 0x20));
    c->SetFlag(0x10, false);
    CHECK(!c->CheckFlag(0x10) && c->CheckFlag(0x20));
    c->SetFlag(0x20, false);
    c->SetTemplate(tmpl);
    CHECK(c->GetTemplate() == tmpl && c->GetRadius() == 10.0f && c->Flags() == (kCtrlFlagReady | kCtrlFlagInView));   // flags replaced
    CHECK(std::string(c->GetTemplateName().ToChar()) == "Ped Gen");
    c->SetLifetime(3.0f);
    CHECK(!c->IsExpired());
    CHECK(c->TractRect() != nullptr);

    // ---- positions, boxes with a lot
    auto* occ = new FakeOccupant();
    occ->AddRef();
    c->SetOccupant(occ);
    float p[3] = {0, 0, 0};
    c->GetPosition(p);
    CHECK(p[0] == 40 && p[1] == 7 && p[2] == 56);
    float lo[3], hi[3];
    c->GetOccupantBoundingBox(lo, hi);   // lot (2,3)-(3,4) -> 32..64, 48..80
    CHECK(lo[0] == 32 && lo[2] == 48 && hi[0] == 64 && hi[2] == 80 && lo[1] == 0 && hi[1] == 20);
    host.hasLot = false;
    c->GetOccupantBoundingBox(lo, hi);
    CHECK(lo[0] == 32 && hi[0] == 47 && hi[2] == 63);
    host.hasLot = true;
    c->GetBoundingBox(lo, hi);   // +-10, clamped to 100 x 200; heights from the city terrain
    CHECK(lo[0] == 22 && lo[2] == 38 && hi[0] == 74 && hi[2] == 90 && lo[1] == 60 && hi[1] == 164);
    // facing points
    const float expected[4][2] = {{48, 47}, {65, 64}, {48, 81}, {31, 64}};
    for (std::uint32_t f = 0; f < 4; ++f) {
        host.facing = f;
        float q[3];
        c->GetLotFacingPoint(q);
        CHECK(q[1] == 0 && q[0] == expected[f][0] && q[2] == expected[f][1]);
    }
    host.facing = 9;
    { float q[3] = {5, 5, 5}; c->GetLotFacingPoint(q); CHECK(q[0] == 0 && q[1] == 0 && q[2] == 0); }
    host.facing = 0;
    host.hasLot = false;
    { float q[3] = {0, 0, 0}; c->GetLotFacingPoint(q); CHECK(q[0] == 40 && q[2] == 56); }   // falls back to the occupant position
    host.hasLot = true;

    // within radius
    {
        float dist = -1;
        const float inside[3] = {50, 0, 70};
        CHECK(c->IsPointWithinRadius(inside, &dist));
        // from the position (40,56) the segment to (50,70) leaves the box 32..64 x 48..80? both inside: no crossing
        CHECK(std::fabs(dist - std::sqrt(10.0f * 10.0f + 14.0f * 14.0f)) < 1e-3f);
        const float outside[3] = {200, 0, 70};
        CHECK(!c->IsPointWithinRadius(outside, &dist));
        const float near[3] = {72, 0, 70};   // outside the box (64) but within the radius 10
        CHECK(c->IsPointWithinRadius(near, &dist));
        // crossing the box edge x = 64: the distance is measured from the crossing
        CHECK(std::fabs(dist - std::sqrt(8.0f * 8.0f + 3.5f * 3.5f)) < 1e-3f);   // (40,56)->(72,70) crosses x=64 at z=66.5
        c->SetFlag(kCtrlFlagTerrainBound, true);
        const float high[3] = {50, 100, 70};   // terrain height there is 25+17.5; +5 < 100
        CHECK(!c->IsPointWithinRadius(high, nullptr));
        const float low[3] = {50, 40, 70};
        CHECK(c->IsPointWithinRadius(low, nullptr));
        c->SetFlag(kCtrlFlagTerrainBound, false);
    }

    // ---- Init: fixed position uses the lot centre
    c->SetFlag(kCtrlFlagFixedPosition, true);
    CHECK(c->Init() && c->Init());
    CHECK(c->pos_[0] == 48 && c->pos_[2] == 64 && c->pos_[1] == 48 * 0.5f + 64 * 0.25f);
    CHECK(host.city.refs == 2);
    CHECK(c->CheckFlag(kCtrlFlagActive) && host.registered == 1);          // Init activated it
    CHECK(c->CheckFlag(kCtrlFlagDelayed));                                  // start delay pending
    CHECK(c->SoundHandle() == 33);                                          // RandomBelow picks the last sound
    // quirk: Init's Activate(true) starts the loop and Init then calls PlaySFX(true) again -> two start/position pairs
    CHECK(host.messages.size() == 4 && host.messages[2].id == kSoundMsgStartLoop && host.messages[2].a == 33 &&
          host.messages[2].b == c->ControllerId() && host.messages[3].id == kSoundMsgSetPosition && host.messages[3].b == 48 &&
          host.messages[3].d == 64);
    CHECK(!c->IsActive());                                                  // delayed
    c->DoAnimationTick(1500.0f, 0, ControllerTime{});
    CHECK(c->CheckFlag(kCtrlFlagDelayed));
    c->DoAnimationTick(600.0f, 0, ControllerTime{});                        // 2.1 s >= 2.0
    CHECK(!c->CheckFlag(kCtrlFlagDelayed));
    host.frustumPoints.clear();
    CHECK(c->HandleViewChanged());   // frustum tests run (none hit) -> not in view
    CHECK(!c->CheckFlag(kCtrlFlagInView) && host.frustumPoints.size() == 5);
    c->SetFlag(kCtrlFlagReady, true);
    host.frustumPoints.clear();
    host.frustumHitAt = 0;
    c->HandleViewChanged();
    CHECK(c->CheckFlag(kCtrlFlagInView) && host.frustumPoints.size() == 1 && c->IsActive() && c->IsTickable());
    host.frustumPoints.clear();
    host.frustumHitAt = -1;
    c->SetFlag(kCtrlFlagAlwaysVisible, true);
    c->HandleViewChanged();
    CHECK(c->CheckFlag(kCtrlFlagInView) && host.frustumPoints.empty());
    c->SetFlag(kCtrlFlagAlwaysVisible, false);

    // ---- age and expiry
    c->DoAnimationTick(1000.0f, 0, ControllerTime{});
    CHECK(std::fabs(c->Age() - 1.6f) < 1e-5f && !c->IsExpired());
    c->SetLifetime(1.0f);
    CHECK(c->IsExpired());
    c->SetLifetime(5.0f);
    c->DoAnimationTick(5000.0f, 0, ControllerTime{});   // age 6 > 5 -> deactivates
    CHECK(!c->CheckFlag(kCtrlFlagActive) && host.messages.back().id == kSoundMsgStopLoop);
    c->Activate(true);
    CHECK(c->CheckFlag(kCtrlFlagActive) && host.registered == 2);

    // ---- NotifySourceDataChanged
    c->SetFlag(kCtrlFlagReady, false);
    c->NotifySourceDataChanged();
    CHECK(c->CheckFlag(kCtrlFlagReady | kCtrlFlagInView));

    // ---- effect triggers
    tmpl->effectsOn = {1, 2};
    tmpl->effectsOff = {3};
    host.effects = {{1, true}, {2, true}, {3, false}};
    c->CheckEffectTriggers();
    CHECK(c->CheckFlag(kCtrlFlagActive) && host.effectBegins == host.effectEnds);
    host.effects[2] = false;
    c->CheckEffectTriggers();
    CHECK(!c->CheckFlag(kCtrlFlagActive));
    host.effects[2] = true;
    host.effects[3] = true;   // an "off" effect is on
    c->CheckEffectTriggers();
    CHECK(!c->CheckFlag(kCtrlFlagActive));
    tmpl->effectsOn.clear();
    tmpl->effectsOff.clear();
    c->CheckEffectTriggers();   // nothing configured: unchanged
    CHECK(!c->CheckFlag(kCtrlFlagActive));
    host.effectView = false;
    tmpl->effectsOn = {1};
    c->CheckEffectTriggers();
    CHECK(!c->CheckFlag(kCtrlFlagActive));
    host.effectView = true;
    c->CheckEffectTriggers();   // effect 1 on, nothing off
    CHECK(c->CheckFlag(kCtrlFlagActive));

    // ---- queue handling and behaviours
    FakeAutomaton a1, a2;
    FakeLeader leader;
    CHECK(!c->HandleEnterState(&a1, 7) && host.queue.queued.empty());
    CHECK(!c->HandleEnterState(&a1, kBehaviorTypeQueue) && host.queue.queued.size() == 1);
    CHECK(!c->HandleExitState(&a1, 7) && host.queue.queued.size() == 1);
    CHECK(!c->HandleExitState(&a1, kBehaviorTypeQueue) && host.queue.queued.empty());
    CHECK(!c->HandleStateTick(&a1, 3, 0.1f));
    CHECK(!c->HandleStateTick(&a1, kBehaviorTypeQueue, 0.1f) && a1.set[0] == 100 && a1.set[1] == 9 && a1.set[2] == 200);
    a2.leader = &leader;
    CHECK(!c->HandleStateTick(&a2, kBehaviorTypeQueue, 0.1f) && a2.set[0] == 13 && a2.set[1] == 9 && a2.set[2] == 26);
    leader.length = 0.5f;   // lengths under 1 count as 1
    c->HandleStateTick(&a2, kBehaviorTypeQueue, 0.1f);
    CHECK(a2.set[0] == 11 && a2.set[2] == 22);
    host.queue.valid = false;
    CHECK(c->HandleStateTick(&a1, kBehaviorTypeQueue, 0.1f) && a1.detached == 1);
    host.queue.valid = true;

    tmpl->behaviors.resize(3);
    tmpl->behaviors[0].state = 1; tmpl->behaviors[0].radius = 5.0f;
    tmpl->behaviors[1].state = kBehaviorTypeQueue; tmpl->behaviors[1].radius = 20.0f;
    tmpl->behaviors[2].state = 2; tmpl->behaviors[2].radius = 1000.0f;
    const float nearQueue[3] = {105, 0, 200};
    CHECK(c->GetBehaviorTemplate(3.0f, nearQueue) == &tmpl->behaviors[0]);
    CHECK(c->GetBehaviorTemplate(10.0f, nearQueue) == &tmpl->behaviors[1]);   // within 20 of the queue point
    const float farQueue[3] = {500, 0, 500};
    CHECK(c->GetBehaviorTemplate(10.0f, farQueue) == &tmpl->behaviors[2]);    // skips the queue entry
    host.queue.valid = false;
    CHECK(c->GetBehaviorTemplate(10.0f, nearQueue) == &tmpl->behaviors[2]);
    host.queue.valid = true;
    CHECK(c->GetBehaviorTemplate(5000.0f, nearQueue) == nullptr);

    // ---- automata list, IsFull, HandleRemoveAutomaton, Shutdown
    c->Automata().push_back(&a1);
    c->Automata().push_back(&a2);
    c->SetAutomataCount(2);
    CHECK(c->IsFull());
    CHECK(c->HandleRemoveAutomaton(&a1) && a1.released == 1 && c->Automata().size() == 1 && c->AutomataCount() == 1 && !c->IsFull());
    CHECK(c->HandleRemoveAutomaton(&a1) && a1.released == 1 && c->AutomataCount() == 1);   // not in the list: nothing released
    tmpl->maxAutomata = 0;
    CHECK(!c->IsFull());
    c->SetFlag(kCtrlFlagDetachOnShutdown, true);

    // ---- stream round trip (before Shutdown clears things)
    {
        PortMemoryOStream os;
        c->SetOccupant(nullptr);
        c->Automata().clear();
        c->SetAutomataCount(0);
        CHECK(c->Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        auto* r = new Controller();
        r->AddRef();
        CHECK(r->Read(is) && is.RemainingBytes() == 0);
        CHECK(std::string(r->GetTemplateName().ToChar()) == "Ped Gen" && r->pos_[0] == c->pos_[0] && r->Flags() == c->Flags());
        CHECK(std::fabs(r->Age() - c->Age()) < 1e-6f && r->SoundHandle() == 33 && r->AutomataCount() == 0);
        r->Release();
    }

    c->Automata().push_back(&a2);
    c->SetAutomataCount(1);
    c->SetOccupant(occ);
    CHECK(c->Shutdown());
    CHECK(a2.released == 1 && a2.detached == 1 && c->Automata().empty() && c->AutomataCount() == 0 && c->GetOccupant() == nullptr);
    CHECK(c->GetTemplate() == nullptr && host.city.refs == 1 && !c->CheckFlag(kCtrlFlagActive));
    CHECK(c->Shutdown());

    // ---- interfaces and reference counting
    void* iface = nullptr;
    CHECK(c->QueryInterface(kGZIID_cISC4AutomataController, &iface) && iface == static_cast<void*>(c));
    CHECK(c->QueryInterface(kGZIID_cIGZSerializable, &iface) && iface == static_cast<cIGZSerializable*>(c));
    CHECK(c->QueryInterface(1, &iface));
    CHECK(!c->QueryInterface(0x1234, &iface));
    CHECK(c->Release() == 3 && c->Release() == 2 && c->Release() == 1);
    c->Release();   // refcount <= 1: FinalRelease -> Shutdown + delete
    occ->Release();
    delete tmpl;
    delete attractorTmpl;
    std::printf(failures ? "FAILED\n" : "sc4_automatacontroller OK\n");
    return failures ? 1 : 0;
}
