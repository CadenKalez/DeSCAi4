// cSC4AnimationTickManager: priority buckets, the 8-tick schedule, queued additions / removals during a tick, pause and step (the
// stepped time is deterministic: stepDelta + the bucket's last time), the delta clamp and the message contents.
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "rz/cRZMessage2Standard.h"
#include "sc4/AnimationTickManager.h"

using namespace sc4;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Target final : cIGZMessageTarget2 {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cIGZMessage2* message) override {
        auto* m = static_cast<cIGZMessage2Standard*>(message);
        ++calls;
        type = message->GetType();
        delta = static_cast<std::uint32_t>(m->GetData2());
        const std::uint32_t bits = static_cast<std::uint32_t>(m->GetData3());
        std::memcpy(&milliseconds, &bits, sizeof bits);
        if (onTick) onTick();
        return true;
    }
    std::uint32_t refs = 1;
    int calls = 0;
    std::uint32_t type = 0, delta = 0;
    float milliseconds = 0;
    std::function<void()> onTick;
};
}  // namespace

int main() {
    auto* atm = new cSC4AnimationTickManager;
    atm->AddRef();
    CHECK(atm->Init());

    Target high, mid, low, lowest;
    CHECK(atm->AddTickableObject(&high, 0xc0));     // bucket 0
    CHECK(atm->AddTickableObject(&mid, 0x80));      // bucket 1
    CHECK(atm->AddTickableObject(&low, 0x40));      // bucket 2
    CHECK(atm->AddTickableObject(&lowest, 0x3f));   // bucket 3
    CHECK(atm->AddTickableObject(&high, 0xc0));     // already there: no second reference
    CHECK(high.refs == 2 && lowest.refs == 2);
    CHECK(atm->BucketSize(0) == 1 && atm->BucketSize(1) == 1 && atm->BucketSize(2) == 1 && atm->BucketSize(3) == 1);
    CHECK(atm->FindTickableObject(&mid, 0xffffffffu) && atm->FindTickableObject(&mid, 0x90) && !atm->FindTickableObject(&mid, 0xc0));

    // Stepping: paused, every DoAnimationTick with a pending step advances each due bucket by the step delta (100000 us).
    atm->PauseAnimationSystem(true);
    CHECK(atm->IsAnimationSystemPaused());
    CHECK(atm->DoAnimationTick() == 0);   // paused, nothing pending: no bucket runs
    CHECK(high.calls == 0);
    // ticks 2..9 (slot = count & 7): schedule {3,5,3,9,3,5,3,1}
    int highCalls = 0, midCalls = 0, lowCalls = 0, lowestCalls = 0;
    for (int i = 0; i < 8; ++i) {
        atm->StepAnimationSystem();
        atm->DoAnimationTick();
    }
    highCalls = high.calls; midCalls = mid.calls; lowCalls = low.calls; lowestCalls = lowest.calls;
    CHECK(highCalls == 8);    // bit 0 is in every mask
    CHECK(midCalls == 4);     // masks 3 at slots 0, 2, 4, 6
    CHECK(lowCalls == 2);     // masks 5 at slots 1, 5
    CHECK(lowestCalls == 1);  // mask 9 at slot 3
    CHECK(high.type == kAnimationTickMessage);
    CHECK(high.delta == 100000 && high.milliseconds == 100000.0f * 0.001f);   // stepDelta, clamped to [2000, 500000]; ms = delta * 0.001f (0x100a16da0)

    // The clamp: a larger step is cut to the maximum.
    atm->SetStepTickTimeDelta(900000);
    atm->StepAnimationSystem();
    atm->DoAnimationTick();
    CHECK(high.delta == 500000);
    atm->SetMinAndMaxTickTimeDeltas(0, 0);   // minimum 0 becomes 1, maximum below it becomes 1
    atm->StepAnimationSystem();
    atm->DoAnimationTick();
    CHECK(high.delta == 1);
    atm->SetMinAndMaxTickTimeDeltas(2000, 500000);
    atm->SetStepTickTimeDelta(100000);

    // A removal during the tick is queued: the object is skipped for the rest of that tick and removed after it.
    static cSC4AnimationTickManager* s_atm = atm;
    static Target* s_mid = &mid;
    high.onTick = [] { s_atm->RemoveTickableObject(s_mid, 0xffffffffu); };
    const int midBefore = mid.calls;
    // find a tick where bucket 1 runs (slots 0, 2, 4, 6): step until mid has been removed
    for (int i = 0; i < 2; ++i) {
        atm->StepAnimationSystem();
        atm->DoAnimationTick();
    }
    CHECK(mid.calls == midBefore);   // skipped while queued, then gone
    CHECK(!atm->FindTickableObject(&mid, 0xffffffffu));
    CHECK(mid.refs == 1);            // the queue's and the bucket's references are both released
    high.onTick = nullptr;

    // An addition during the tick lands after it.
    Target late;
    static Target* s_late = &late;
    high.onTick = [] { s_atm->AddTickableObject(s_late, 0xff); };
    atm->StepAnimationSystem();
    atm->DoAnimationTick();
    CHECK(late.calls == 0 && atm->FindTickableObject(&late, 0xff));
    high.onTick = nullptr;
    atm->StepAnimationSystem();
    atm->DoAnimationTick();
    CHECK(late.calls == 1);

    // SetNewPriority moves an object between buckets.
    atm->SetNewPriority(&late, 0x10);
    CHECK(atm->FindTickableObject(&late, 0x10) && !atm->FindTickableObject(&late, 0xff) && late.refs == 2);

    // Shutdown releases every object.
    CHECK(atm->Shutdown());
    CHECK(high.refs == 1 && low.refs == 1 && lowest.refs == 1 && late.refs == 1);
    CHECK(atm->BucketSize(0) == 0);
    atm->Release();

    if (failures == 0) std::printf("sc4_animationtickmanager: all ok\n");
    return failures ? 1 : 0;
}
