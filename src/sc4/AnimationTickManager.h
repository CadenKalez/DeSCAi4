// cSC4AnimationTickManager - the animation tick service (SL::ATM(), service 0x86ad10ec, interface 0x86ad10ed, class 0xa9c73857).
// Recovered from 0x100364ab8..0x100366388 (SimCity 4 Deluxe Editionsub, Aspyr macOS x86_64 v1.2.1).
//
// Objects register with a priority; the priority picks one of four buckets (>= 0xc0, >= 0x80, >= 0x40, below) and each bucket runs on
// the ticks whose bit is set in an 8-entry schedule (bucket 0 every tick, 1 every other tick, 2 every fourth, 3 every eighth).
// DoAnimationTick sends every object of a due bucket the manager's one message (type 0x86ad10ee, void1 / unknown = the manager), with
// data2 = the bucket's elapsed microseconds clamped to [min, max] and data3 = the same in milliseconds as float bits. Additions and
// removals made during a tick are queued and applied after it (an object queued for removal is skipped by the rest of the tick).
//
// Layout (0x1b8 bytes): +0 cISC4AnimationTickManager, +8 cIGZMessageTarget2, +0x10 cIGZSerializable, +0x18 cRZSystemService (its
// refcount at +0x3c); +0x40 initialised, +0x41 in a tick; +0x48 the tick message; +0x50 cRZTimer (microseconds); +0x68 / +0x6c minimum
// and maximum tick delta (2000 / 500000 us); +0x70 current tick time; +0x74 tick count; +0x78 smoothed ticks per second; +0x7c the 8
// schedule masks {3,5,3,9,3,5,3,1}; +0x9c pause count; +0xa0 step delta (100000 us); +0xa4 step pending; +0xa5 auto step; +0xa8 auto
// step interval; +0xb0 auto-step cRZTimer; +0xc8 list of delayed additions / removals; +0xe0 the set of queued objects; +0x108 four
// buckets (std::unordered_set, hash = pointer >> 3); +0x1a8 the four buckets' last tick times.
//
// Quirks kept: OnTick does nothing (the tick is DoAnimationTick, called by its owner); DoMessage answers true to everything; the
// smoothed rate is updated every eighth tick as avg += (1e6 / (3 + now - previous) - avg) / 8 (the 3 and the previous time are the
// original's); Read inserts the read objects without an extra reference and does not clear the buckets first.
#pragma once

#include <cstdint>
#include <list>
#include <unordered_set>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZSystemService.h"
#include "rz/cRZTimer.h"

class cIGZFrameWork;
class cRZMessage2Standard;

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AnimationTickManager = 0xa9c73857;
constexpr std::uint32_t kGZIID_cISC4AnimationTickManager = 0x86ad10ed;
constexpr std::uint32_t kAnimationTickManagerServiceId = 0x86ad10ec;
constexpr std::uint32_t kAnimationTickMessage = 0x86ad10ee;

// The framework the manager adds itself to (RZGetFramework(): AddSystemService / AddToTick in Init, the reverse in Shutdown).
void SetAnimationTickManagerFramework(cIGZFrameWork* framework);

class cISC4AnimationTickManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                          // [3]  0x18
    virtual bool Shutdown() = 0;                                                      // [4]  0x20
    virtual bool AddTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) = 0;      // [5]  0x28
    virtual bool RemoveTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) = 0;   // [6]  0x30 (0xffffffff: any)
    virtual bool FindTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) = 0;     // [7]  0x38
    virtual void SetNewPriority(cIGZMessageTarget2* object, std::uint32_t priority) = 0;         // [8]  0x40
    virtual std::uint32_t DoAnimationTick() = 0;                                      // [9]  0x48
    virtual std::uint32_t GetCurrentTickTime() const = 0;                             // [10] 0x50
    virtual void SetMinAndMaxTickTimeDeltas(std::uint32_t minimum, std::uint32_t maximum) = 0;   // [11] 0x58
    virtual void PauseAnimationSystem(bool pause) = 0;                                // [12] 0x60
    virtual bool IsAnimationSystemPaused() const = 0;                                 // [13] 0x68
    virtual void EnableAutoStep(bool enable, std::uint32_t interval) = 0;             // [14] 0x70
    virtual void SetStepTickTimeDelta(std::uint32_t delta) = 0;                       // [15] 0x78
    virtual void StepAnimationSystem() = 0;                                           // [16] 0x80
};

class cSC4AnimationTickManager : public cISC4AnimationTickManager,
                                 public cIGZMessageTarget2,
                                 public cIGZSerializable,
                                 public cRZSystemService {
public:
    cSC4AnimationTickManager();             // 0x100364ace
    virtual ~cSC4AnimationTickManager();    // 0x100364cdc

    // cIGZUnknown (one implementation for every base)
    bool QueryInterface(std::uint32_t iid, void** out) override;          // 0x100364f8c
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }   // 0x100364e96
    // 0x100364edc: above one reference it counts down, the last one runs the deleting destructor (cRZUnknown::Release is the same)
    std::uint32_t Release() override { return cRZSystemService::Release(); }

    // cISC4AnimationTickManager / cIGZSystemService
    bool Init() override;                                                 // 0x100365010
    bool Shutdown() override;                                             // 0x1003650fc
    bool AddTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) override;      // 0x100365624
    bool RemoveTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) override;   // 0x100365b5e
    bool FindTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) override;     // 0x100365eb6
    void SetNewPriority(cIGZMessageTarget2* object, std::uint32_t priority) override;         // 0x100366388
    std::uint32_t DoAnimationTick() override;                             // 0x100365f4e
    std::uint32_t GetCurrentTickTime() const override { return currentTickTime_; }            // 0x1003662dc
    void SetMinAndMaxTickTimeDeltas(std::uint32_t minimum, std::uint32_t maximum) override;   // 0x1003662e6
    void PauseAnimationSystem(bool pause) override;                       // 0x100366280
    bool IsAnimationSystemPaused() const override { return 0 < pauseCount_; }                 // 0x1003662cc
    void EnableAutoStep(bool enable, std::uint32_t interval) override;    // 0x10036634e
    void SetStepTickTimeDelta(std::uint32_t delta) override { stepDelta_ = delta; }           // 0x100366342
    void StepAnimationSystem() override;                                  // 0x10036630a
    bool OnTick(std::uint32_t) override { return true; }                  // 0x10036560a

    // cIGZMessageTarget2
    bool DoMessage(cIGZMessage2* message) override;                       // 0x1003655e6

    // cIGZSerializable
    bool Write(cIGZOStream& stream) const override;                       // 0x100365236
    bool Read(cIGZIStream& stream) override;                              // 0x1003652d8
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AnimationTickManager; }   // 0x1003663d8

    // inspection
    std::size_t BucketSize(int bucket) const { return buckets_[bucket].size(); }
    std::uint32_t TickCount() const { return tickCount_; }
    float TicksPerSecond() const { return ticksPerSecond_; }

private:
    struct TickEntryHash {          // the buckets: the pointer shifted right by 3
        std::size_t operator()(cIGZMessageTarget2* p) const { return reinterpret_cast<std::uintptr_t>(p) >> 3; }
    };
    struct MessageTargetPtrHash {   // the queued set: the pointer itself
        std::size_t operator()(cIGZMessageTarget2* p) const { return reinterpret_cast<std::uintptr_t>(p); }
    };
    struct DelayedAddOrRemove {     // 0x28-byte list node payload: add flag, object (referenced), priority
        bool add = false;
        cIGZMessageTarget2* object = nullptr;
        std::uint32_t priority = 0;
    };
    static int BucketFor(std::uint32_t priority) { return priority >= 0xc0 ? 0 : priority >= 0x80 ? 1 : priority >= 0x40 ? 2 : 3; }
    void DoDelayedAdditionsAndRemovals();                                 // 0x1003661ea
    void QueueDelayed(cIGZMessageTarget2* object, bool add, std::uint32_t priority);

    bool initialized_ = false;                  // +0x40
    bool inTick_ = false;                       // +0x41
    cRZMessage2Standard* message_ = nullptr;    // +0x48
    cRZTimer timer_{cRZTimer::kMicroseconds};   // +0x50
    std::uint32_t minDelta_ = 2000;             // +0x68
    std::uint32_t maxDelta_ = 500000;           // +0x6c
    std::uint32_t currentTickTime_ = 0;         // +0x70
    std::uint32_t tickCount_ = 0;               // +0x74
    float ticksPerSecond_ = 0.0f;               // +0x78
    std::uint32_t schedule_[8] = {3, 5, 3, 9, 3, 5, 3, 1};   // +0x7c
    std::int32_t pauseCount_ = 0;               // +0x9c
    std::uint32_t stepDelta_ = 100000;          // +0xa0
    bool stepPending_ = false;                  // +0xa4
    bool autoStep_ = false;                     // +0xa5
    std::uint32_t autoStepInterval_ = 0;        // +0xa8
    cRZTimer autoStepTimer_{cRZTimer::kMicroseconds};   // +0xb0
    std::list<DelayedAddOrRemove> delayed_;     // +0xc8
    std::unordered_set<cIGZMessageTarget2*, MessageTargetPtrHash> delayedObjects_;   // +0xe0
    std::unordered_set<cIGZMessageTarget2*, TickEntryHash> buckets_[4];              // +0x108
    std::uint32_t lastBucketTime_[4] = {0, 0, 0, 0};                                 // +0x1a8
};

}  // namespace sc4
