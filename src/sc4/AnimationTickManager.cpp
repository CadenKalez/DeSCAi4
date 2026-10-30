#include "sc4/AnimationTickManager.h"

#include <cstring>

#include "gz/cIGZFrameWork.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
cIGZFrameWork* g_framework = nullptr;
}  // namespace

void SetAnimationTickManagerFramework(cIGZFrameWork* framework) { g_framework = framework; }

// 0x100364ace. The service registers as (0x86ad10ec, priority -2000000).
cSC4AnimationTickManager::cSC4AnimationTickManager() : cRZSystemService(kAnimationTickManagerServiceId, -2000000) {
    for (auto& bucket : buckets_) bucket.max_load_factor(1.0f);
    delayedObjects_.max_load_factor(1.0f);
}

// 0x100364cdc: the containers and timers only (Shutdown releases the objects).
cSC4AnimationTickManager::~cSC4AnimationTickManager() = default;

bool cSC4AnimationTickManager::QueryInterface(std::uint32_t iid, void** out) {   // 0x100364f8c
    if (iid == kGZIID_cISC4AnimationTickManager || iid == 1) {
        *out = static_cast<cISC4AnimationTickManager*>(this);
    } else if (iid == 0xe4fda3d4) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == 0x452294aa) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else {
        return cRZSystemService::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

// ---- start up / shut down -----------------------------------------------------------------------------------------------------

bool cSC4AnimationTickManager::Init() {   // 0x100365010
    if (initialized_) return true;
    initialized_ = true;
    pauseCount_ = 0;
    stepPending_ = false;
    timer_.Restart();
    cIGZSystemService* service = static_cast<cRZSystemService*>(this);
    if (g_framework) {
        g_framework->AddSystemService(service);
        g_framework->AddToTick(service);
    }
    if (!message_) {
        message_ = new cRZMessage2Standard;
        message_->AddRef();
        message_->SetType(kAnimationTickMessage);
        message_->SetVoid1(static_cast<cISC4AnimationTickManager*>(this));
        message_->SetIGZUnknown(static_cast<cISC4AnimationTickManager*>(this));
    }
    return true;
}

bool cSC4AnimationTickManager::Shutdown() {   // 0x1003650fc
    if (!initialized_) return true;
    if (message_) {
        message_->SetIGZUnknown(nullptr);
        message_->Release();
        message_ = nullptr;
    }
    if (g_framework) {
        cIGZSystemService* service = static_cast<cRZSystemService*>(this);
        g_framework->RemoveFromTick(service);
        g_framework->RemoveSystemService(service);
    }
    cRZSystemService::Shutdown();
    if (!delayed_.empty()) {
        for (const DelayedAddOrRemove& entry : delayed_) entry.object->Release();
        delayed_.clear();
        delayedObjects_.clear();
    }
    for (auto& bucket : buckets_) {
        for (cIGZMessageTarget2* object : bucket) object->Release();
        bucket.clear();
    }
    initialized_ = false;
    pauseCount_ = 0;
    stepPending_ = false;
    timer_.Reset();
    return true;
}

// ---- the tickable objects -----------------------------------------------------------------------------------------------------

void cSC4AnimationTickManager::QueueDelayed(cIGZMessageTarget2* object, bool add, std::uint32_t priority) {
    object->AddRef();
    delayed_.push_back(DelayedAddOrRemove{add, object, priority});
    delayedObjects_.insert(object);
}

bool cSC4AnimationTickManager::AddTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) {   // 0x100365624
    if (inTick_) {
        QueueDelayed(object, true, priority);
        return true;
    }
    auto& bucket = buckets_[BucketFor(priority)];
    if (bucket.find(object) != bucket.end()) return true;
    object->AddRef();
    bucket.insert(object);
    return true;
}

bool cSC4AnimationTickManager::RemoveTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) {   // 0x100365b5e
    if (inTick_) {
        QueueDelayed(object, false, 0);
        return true;
    }
    if (priority == 0xffffffffu) {
        for (int b = 3; b >= 0; --b) {   // the last bucket first
            auto it = buckets_[b].find(object);
            if (it == buckets_[b].end()) continue;
            buckets_[b].erase(it);
            object->Release();
            return true;
        }
        return true;
    }
    auto& bucket = buckets_[BucketFor(priority)];
    auto it = bucket.find(object);
    if (it == bucket.end()) return RemoveTickableObject(object, 0xffffffffu);   // vslot 0x30: not where its priority says, any bucket
    bucket.erase(it);
    object->Release();
    return true;
}

bool cSC4AnimationTickManager::FindTickableObject(cIGZMessageTarget2* object, std::uint32_t priority) {   // 0x100365eb6
    if (priority == 0xffffffffu) {
        for (int b = 3; b >= 0; --b)
            if (buckets_[b].find(object) != buckets_[b].end()) return true;
        return false;
    }
    const auto& bucket = buckets_[BucketFor(priority)];
    return bucket.find(object) != bucket.end();
}

void cSC4AnimationTickManager::SetNewPriority(cIGZMessageTarget2* object, std::uint32_t priority) {   // 0x100366388
    object->AddRef();
    RemoveTickableObject(object, 0xffffffffu);
    AddTickableObject(object, priority);
    object->Release();
}

void cSC4AnimationTickManager::DoDelayedAdditionsAndRemovals() {   // 0x1003661ea
    if (delayed_.empty()) return;
    for (const DelayedAddOrRemove& entry : delayed_) {
        if (!entry.add) RemoveTickableObject(entry.object, 0xffffffffu);
        else AddTickableObject(entry.object, entry.priority);
        entry.object->Release();
    }
    delayed_.clear();
    delayedObjects_.clear();
}

// ---- the tick -----------------------------------------------------------------------------------------------------------------

std::uint32_t cSC4AnimationTickManager::DoAnimationTick() {   // 0x100365f4e
    if (inTick_) return 0;
    const std::uint32_t previousTime = currentTickTime_;
    const std::uint32_t count = tickCount_++;
    inTick_ = true;
    std::uint32_t now = timer_.GetElapsedTimeU32();
    currentTickTime_ = now;
    if (now == 0xffffffffu) {   // the 32-bit microsecond clock ran out: start again from the last time
        timer_.Restart();
        currentTickTime_ = previousTime;
        now = previousTime;
    }
    const std::uint32_t slot = (count + 1) & 7;
    if (slot == 0) {
        const std::uint32_t span = (3u - previousTime) + now;   // u32 arithmetic, as the original
        ticksPerSecond_ = (1e6f / static_cast<float>(span) - ticksPerSecond_) * 0.125f + ticksPerSecond_;
    }

    std::uint32_t result = 0;
    bool runBuckets = false;
    if (autoStep_ && pauseCount_ != 0) {
        if (autoStepInterval_ <= autoStepTimer_.GetElapsedTimeU32()) {
            stepPending_ = true;
            autoStepTimer_.Reset();
        }
    }
    if (autoStep_ && pauseCount_ == 0) runBuckets = true;
    else runBuckets = !(pauseCount_ != 0 && !stepPending_);

    if (runBuckets) {
        result = minDelta_;
        for (int b = 0; b < 4; ++b) {
            if (((schedule_[slot] >> b) & 1u) == 0) continue;
            if (!buckets_[b].empty()) {
                const std::uint32_t last = lastBucketTime_[b];
                std::uint32_t time = currentTickTime_;
                if (stepPending_) {
                    time = stepDelta_ + last;
                    currentTickTime_ = time;
                }
                std::uint32_t delta = time - last;
                if (maxDelta_ <= delta) delta = maxDelta_;
                if (delta <= minDelta_) delta = minDelta_;
                message_->SetData2(delta);
                const float milliseconds = static_cast<float>(delta) * 0.001f;
                std::uint32_t bits;
                std::memcpy(&bits, &milliseconds, sizeof bits);
                message_->SetData3(bits);   // the float's bits, zero-extended (movd xmm0 -> esi)
                const std::uint32_t previousResult = result;
                for (auto it = buckets_[b].begin(); it != buckets_[b].end();) {
                    cIGZMessageTarget2* object = *it;
                    ++it;   // the next node is taken before the call, as the original
                    if (delayedObjects_.empty() || delayedObjects_.find(object) == delayedObjects_.end())
                        object->DoMessage(message_);
                }
                result = minDelta_ < previousResult ? previousResult : delta;
            }
            lastBucketTime_[b] = currentTickTime_;
        }
        if (stepPending_) stepPending_ = false;
    }
    inTick_ = false;
    if (!delayed_.empty()) DoDelayedAdditionsAndRemovals();
    return result;
}

void cSC4AnimationTickManager::SetMinAndMaxTickTimeDeltas(std::uint32_t minimum, std::uint32_t maximum) {   // 0x1003662e6
    minDelta_ = minimum;
    maxDelta_ = maximum;
    if (minimum == 0) {
        minDelta_ = 1;
        minimum = 1;
    }
    if (maximum < minimum) maxDelta_ = minimum;
}

void cSC4AnimationTickManager::PauseAnimationSystem(bool pause) {   // 0x100366280
    if (pause) {
        if (autoStep_ && pauseCount_ < 1) autoStepTimer_.Restart();
        ++pauseCount_;
    } else {
        --pauseCount_;
    }
}

void cSC4AnimationTickManager::EnableAutoStep(bool enable, std::uint32_t interval) {   // 0x10036634e
    autoStepInterval_ = interval;
    if (autoStep_ == enable) return;
    autoStep_ = enable;
    if (enable) autoStepTimer_.Restart();
    else autoStepTimer_.Reset();
}

void cSC4AnimationTickManager::StepAnimationSystem() {   // 0x10036630a
    if (IsAnimationSystemPaused()) stepPending_ = true;
    else PauseAnimationSystem(true);
}

bool cSC4AnimationTickManager::DoMessage(cIGZMessage2* message) {   // 0x1003655e6: reads the type and ignores it
    (void)message->GetType();
    return true;
}

// ---- persistence --------------------------------------------------------------------------------------------------------------

bool cSC4AnimationTickManager::Write(cIGZOStream& stream) const {   // 0x100365236
    stream.SetUint16(1);
    for (const auto& bucket : buckets_) {
        stream.SetUint32(static_cast<std::uint32_t>(bucket.size()));
        for (cIGZMessageTarget2* object : bucket) SC4WriteGZSerializable(stream, SC4IsGZSerializable(object) ? object : nullptr);
    }
    return !stream.GetError();
}

bool cSC4AnimationTickManager::Read(cIGZIStream& stream) {   // 0x1003652d8
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    for (auto& bucket : buckets_) {
        std::uint32_t count = 0;
        stream.GetUint32(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            void* raw = nullptr;
            SC4ReadGZSerializable(stream, 0x452294aa, &raw, false);
            if (raw) bucket.insert(static_cast<cIGZMessageTarget2*>(raw));   // the read reference becomes the bucket's
        }
    }
    return !stream.GetError();
}

}  // namespace sc4
