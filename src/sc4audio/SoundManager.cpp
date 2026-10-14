#include "sc4audio/SoundManager.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <vector>

#include "audio/interfaces/cIHitTrackPlayer.h"
#include "audio/interfaces/cIGZSndCompletionNotification.h"

using namespace SC4Audio;

namespace {

constexpr float kOneOver1024 = 0.0009765625f;
constexpr std::uint32_t kInterfaceIdSndCompletionNotification = 0xe9d00a9a;   // cIGZSndCompletionNotification
constexpr std::uint32_t kRetriggerWindowMs = 0x32;                              // same sound id, 50 ms
constexpr std::uint32_t kUpdateIntervalMs = 0x14;                               // RegisterTarget interval
constexpr std::int32_t kFocusRefreshTicks = 0x32;

// (int) conversion of the original's cvttss2si: NaN and out-of-range give INT_MIN.
std::int32_t FloatToInt32(float value) {
    if (!(value > -2147483648.0f && value < 2147483648.0f)) return INT_MIN;
    return static_cast<std::int32_t>(value);
}
// (int)(long)value: 64-bit conversion, then the low 32 bits (out of range -> 0).
std::int32_t FloatToInt32ViaInt64(float value) {
    if (!(value > -9.2e18f && value < 9.2e18f)) return 0;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
}

float Length(const float v[3]) { return std::sqrt(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]); }

// BruteForceVolumeUpdateEnumFunc @ 0x1003ba110: bounces every sound's volume through zero so the
// platform mixer re-applies it after the application regains focus.
bool BruteForceVolumeUpdate(cIGZSndUpdateTarget* target, std::uint32_t, void*) {
    if (!target) return true;
    void* raw = nullptr;
    if (target->QueryInterface(kGZIID_cIGZSnd, &raw)) {
        auto* snd = static_cast<cIGZSnd*>(raw);
        const std::int32_t volume = snd->GetVolume();
        snd->SetVolume(0);
        snd->SetVolume(volume);
        snd->Release();
    }
    return true;
}

std::uint32_t& InstanceIdCounter() {
    static std::uint32_t id = 0;   // cSC4AudioSoundManager::GenerateNewInstanceId()::id
    return id;
}

}  // namespace

// ============================================================================================
// tSound
// ============================================================================================

tSound::tSound() = default;
tSound::~tSound() = default;   // slaves cleared, then manager (+0x20), registers (+0x18), snd (+0x10)

// tSound::Release() @ 0x1003b9f32
std::int32_t tSound::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return --refCount_;
}

// tSound::Pause(bool) @ 0x1003b9952. Only the voice owner (a sound that is not a slave) touches the
// underlying sound, and only on the 0 -> 1 transition.
std::int32_t tSound::Pause(bool kill) {
    const std::int32_t previous = pauseCount_++;
    if (previous == 0 && master_ == nullptr) {
        EnableCompletionNotification(false);
        if (snd_) {
            if (kill) snd_->Kill(); else snd_->Stop();
        }
    }
    return pauseCount_;
}

// tSound::Resume() @ 0x1003b999c. Restarts the voice from the beginning (the track is reset first).
// Deviation: the original leaks the reference taken by its cIHitTrackPlayer query; it is released here.
std::int32_t tSound::Resume() {
    const std::int32_t count = --pauseCount_;
    if (count == 0 && master_ == nullptr) {
        EnableCompletionNotification(true);
        if (snd_) {
            void* raw = nullptr;
            if (snd_->QueryInterface(kGZIID_cIHitTrackPlayer, &raw)) {
                auto* player = static_cast<cIHitTrackPlayer*>(raw);
                player->Reset();
                player->Release();
            }
        }
        if (registers_) registers_->SetRegisterValue(kRegArgMode, 3, false);
        if (snd_) snd_->PlayWithScriptArguments(static_cast<std::int32_t>(instanceId_), volume_, 0x200, 0);
        UpdatePosition();
        SetVolume(volume_);
    }
    return pauseCount_;
}

// tSound::EnableCompletionNotification(bool) @ 0x1003b9b08. Installs / removes the manager as the
// sound's completion callback.
bool tSound::EnableCompletionNotification(bool enable) {
    if (!snd_) return false;
    void* raw = nullptr;
    if (!snd_->QueryInterface(kInterfaceIdSndCompletionNotification, &raw)) return false;
    auto* notification = static_cast<cIGZSndCompletionNotification*>(raw);
    cIGZSndCompletionFunctionObject* callback = nullptr;
    if (enable && manager_) callback = manager_->AsCompletionFunctionObject();
    const bool result = notification->SetCompletionNotification(callback);
    notification->Release();
    return result;
}

// tSound::AddSlave(tSound*) @ 0x1003b9c2a
bool tSound::AddSlave(tSound* slave) {
    if (!slave) return false;
    slave->master_ = this;
    slaves_.push_back(slave);
    return true;
}

// tSound::RemoveSlave(tSound*) @ 0x1003b9edc. When the slave is not in the list the master's
// position is refreshed and false is returned; a successful removal does not refresh it.
bool tSound::RemoveSlave(tSound* slave) {
    const auto it = std::find(slaves_.begin(), slaves_.end(), slave);
    if (it == slaves_.end()) {
        UpdatePosition();
        return false;
    }
    slave->master_ = nullptr;
    slaves_.erase(it);
    return true;
}

// tSound::TransferSlaves() @ 0x1003b9dc8. The first slave becomes the new master: it takes over
// the playing sound and register set and the remaining slaves.
void tSound::TransferSlaves() {
    if (slaves_.empty()) return;
    tSound* const next = slaves_.front();
    slaves_.pop_front();
    for (tSound* slave : slaves_) slave->master_ = next;
    if (next != this) {
        next->snd_ = snd_;
        next->registers_ = registers_;
    }
    std::swap(next->slaves_, slaves_);
    next->master_ = nullptr;
    snd_ = nullptr;
    registers_ = nullptr;
    next->UpdatePosition();
}

// tSound::StartStopSoundForSimulationPause() @ 0x1003b9cf0
void tSound::StartStopSoundForSimulationPause() {
    cIGameAudioContext* game = GetServices().game;
    const auto simulationPaused = [game] { return game && game->IsSimulationPaused(); };
    std::uint32_t f = flags_;
    if ((f & 0x30) == 0x20 && game) {
        if (simulationPaused()) {
            flags_ |= 0x10;
            Pause(false);
            return;
        }
        f = flags_;
    }
    if (f & 0x10) {
        if ((f & 0x20) && game) {
            if (simulationPaused()) return;
            f = flags_;
        }
        flags_ = f & ~0x10u;
        Resume();
    }
}

// tSound::ShouldPauseForDistance() @ 0x1003ba3f4. Looping 3D sounds that are further from the
// camera than their maximum distance are stopped until the camera comes back. Slaves and the 2D /
// effects models are never paused for distance.
bool tSound::ShouldPauseForDistance() {
    if (master_ != nullptr) return false;
    const std::int32_t model = registers_ ? registers_->GetRegisterValue(kRegEmanationModel) : 0;
    if (model == static_cast<std::int32_t>(kModelEmanation3DEffects) ||
        model == static_cast<std::int32_t>(kModelEmanation2D)) {
        return false;
    }
    cIGameAudioContext* game = GetServices().game;
    ListenerView view;
    if (!game || !game->GetListenerView(view)) return false;   // original dereferences a null listener
    const float* p = slaves_.empty() ? position_ : averagePosition_;
    const float d[3] = {view.eye[0] - p[0], view.eye[1] - p[1], view.eye[2] - p[2]};
    return maximumDistance_ <= Length(d);
}

// tSound::StartStopSoundForDistance() @ 0x1003ba068
void tSound::StartStopSoundForDistance() {
    if (!registers_ || registers_->GetRegisterValue(kRegLoopFlag) == 0) return;
    const std::int32_t model = registers_->GetRegisterValue(kRegEmanationModel);
    if (master_ != nullptr || model == static_cast<std::int32_t>(kModelEmanation3DEffects) ||
        model == static_cast<std::int32_t>(kModelEmanation2D)) {
        return;
    }
    std::uint32_t f = flags_;
    if ((f & 8) == 0) {
        if (ShouldPauseForDistance()) {
            flags_ |= 8;
            Pause(false);
            return;
        }
        f = flags_;
    }
    if ((f & 8) != 0 && !ShouldPauseForDistance()) {
        flags_ &= ~8u;
        Resume();
    }
}

// tSound::SetPosition(cS3DVector3 const&) @ 0x1003b9f74
void tSound::SetPosition(const float position[3]) {
    position_[0] = position[0];
    position_[1] = position[1];
    position_[2] = position[2];
    UpdatePosition();
    if (master_) master_->UpdatePosition();
    StartStopSoundForDistance();
}

// tSound::SetVolume(int) @ 0x1003b9fb2
void tSound::SetVolume(std::int32_t volume) {
    volume_ = volume;
    if (registers_) {
        registers_->SetRegisterValue(kRegVolume, FloatToInt32(static_cast<float>(volume) * volumeScale_), false);
    }
    if (!slaves_.empty() || master_ != nullptr) UpdatePosition();
}

// tSound::Update(unsigned int) @ 0x1003ba002: advances the volume ramp.
void tSound::Update(std::uint32_t elapsedMs) {
    const float current = rampCurrent_;
    const float target = rampTarget_;
    if (target == current) return;
    float step = static_cast<float>(elapsedMs) * rampRate_;
    const float remaining = target - current;
    bool withinStep = step < remaining;
    if (rampRate_ <= 0.0f) withinStep = remaining < step;
    if (!withinStep) step = remaining;
    rampCurrent_ = current + step;
    SetVolume(FloatToInt32(current + step));
}

// tSound::UpdatePosition() @ 0x1003ba226: pushes the position (the weighted average for a master
// with slaves) into the sound's position registers. The x and y writes do not notify the sound;
// the z write does.
void tSound::UpdatePosition() {
    if (!slaves_.empty() && registers_) {
        const std::int32_t model = registers_->GetRegisterValue(kRegEmanationModel);
        if (model == static_cast<std::int32_t>(kModelEmanation3DEffects)) UpdateVolPanEffects();
        else if (model == static_cast<std::int32_t>(kModelEmanation3D)) UpdateVolPan3D();
    }
    if (!registers_) return;
    const float* p = slaves_.empty() ? position_ : averagePosition_;
    registers_->SetRegisterValue(kRegPositionX, FloatToInt32(p[0]), true);
    registers_->SetRegisterValue(kRegPositionY, FloatToInt32(p[1]), true);
    registers_->SetRegisterValue(kRegPositionZ, FloatToInt32(p[2]), false);
}

// tSound::UpdateVolPan3D() @ 0x1003ba2dc: orientation relative to the listener (eye / look-at).
void tSound::UpdateVolPan3D() {
    ListenerView view;
    cIGameAudioContext* game = GetServices().game;
    if (!game || !game->GetListenerView(view)) view = ListenerView{};
    float direction[3] = {view.target[0] - view.eye[0], view.target[1] - view.eye[1], view.target[2] - view.eye[2]};
    UpdateAveragePosition(direction, view.eye);
}

// tSound::UpdateVolPanEffects() @ 0x1003ba376: same, using the camera of the view utilities.
void tSound::UpdateVolPanEffects() {
    cIViewport* view = ViewUtilities();
    if (!view) return;
    float eye[3], target[3];
    view->GetCameraEye(eye);
    view->GetCameraTarget(target);
    float direction[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    UpdateAveragePosition(direction, eye);
}

// tSound::UpdateAveragePosition(cS3DVector3&, cS3DVector3&) @ 0x1003ba4fc
//
// Averages the positions of this sound and its slaves, weighting each by its volume and by how
// well it lines up with the listener's view direction (a source straight ahead counts more), then
// pushes the result away from the listener when the nearest source is further than the minimum
// distance, so the merged voice does not sound closer than the closest real source.
// `direction` is normalised in place.
//
// Deviation: when the total weight is zero or not finite (every source perpendicular to the view
// direction, silent, or an unusable direction) the original produces NaN positions, which end up
// as INT_MIN register values; the port keeps this sound's own position instead.
void tSound::UpdateAveragePosition(float direction[3], const float listener[3]) {
    const float directionLength = Length(direction);
    if (directionLength > 0.0f) {
        const float inverse = 1.0f / directionLength;
        direction[0] *= inverse;
        direction[1] *= inverse;
        direction[2] *= inverse;
    }

    float totalWeight = 0.0f;
    float weighted[3] = {0, 0, 0};
    float nearestDistance = FLT_MAX;
    float nearest[3] = {0, 0, 0};

    const auto accumulate = [&](tSound* sound) {
        const float* p = sound->position_;
        const float d[3] = {p[0] - listener[0], p[1] - listener[1], p[2] - listener[2]};
        const float distance = Length(d);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest[0] = p[0]; nearest[1] = p[1]; nearest[2] = p[2];
        }
        const float inverseDistance = 1.0f / distance;
        const float cosine = std::fabs(d[2] * inverseDistance * direction[2] + d[1] * inverseDistance * direction[1] +
                                       d[0] * inverseDistance * direction[0]);
        const std::int32_t volume = sound->registers_ ? sound->registers_->GetRegisterValue(kRegVolume) : 0;
        const float weight = (1.0f / (1.0f - cosine)) * cosine * static_cast<float>(volume) * kOneOver1024;
        totalWeight += weight;
        weighted[0] += p[0] * weight;
        weighted[1] += p[1] * weight;
        weighted[2] += p[2] * weight;
    };
    accumulate(this);
    for (tSound* slave : slaves_) accumulate(slave);

    const float inverseWeight = 1.0f / totalWeight;
    averagePosition_[0] = weighted[0] * inverseWeight;
    averagePosition_[1] = weighted[1] * inverseWeight;
    averagePosition_[2] = weighted[2] * inverseWeight;
    if (!(totalWeight > 0.0f) || !std::isfinite(averagePosition_[0]) || !std::isfinite(averagePosition_[1]) ||
        !std::isfinite(averagePosition_[2])) {
        averagePosition_[0] = position_[0];
        averagePosition_[1] = position_[1];
        averagePosition_[2] = position_[2];
        return;
    }

    const float toAverage[3] = {averagePosition_[0] - listener[0], averagePosition_[1] - listener[1], averagePosition_[2] - listener[2]};
    const float toNearest[3] = {nearest[0] - listener[0], nearest[1] - listener[1], nearest[2] - listener[2]};
    const float inverseAverageLength = 1.0f / Length(toAverage);
    const float inverseNearestLength = 1.0f / Length(toNearest);
    const float a[3] = {toAverage[0] * inverseAverageLength, toAverage[1] * inverseAverageLength, toAverage[2] * inverseAverageLength};
    const float n[3] = {toNearest[0] * inverseNearestLength, toNearest[1] * inverseNearestLength, toNearest[2] * inverseNearestLength};
    const float alignmentAverage = std::fabs(direction[2] * a[2] + direction[1] * a[1] + direction[0] * a[0]);
    const float alignmentNearest = std::fabs(direction[2] * n[2] + direction[1] * n[1] + direction[0] * n[0]);
    if (0.0f < alignmentAverage - alignmentNearest) {
        const float inverse = 1.0f / std::sqrt(a[2] * a[2] + a[1] * a[1] + a[0] * a[0]);
        const float minimum = minimumDistance_;
        const float push = std::max(minimum, nearestDistance) - minimum;
        averagePosition_[0] += a[0] * inverse * push;
        averagePosition_[1] += a[1] * inverse * push;
        averagePosition_[2] += a[2] * inverse * push;
    }
}

// ============================================================================================
// cSC4AudioSoundManager
// ============================================================================================

// cSC4AudioSoundManager::cSC4AudioSoundManager() @ 0x1003b7880
cSC4AudioSoundManager::cSC4AudioSoundManager() = default;

// ~cSC4AudioSoundManager @ 0x1003b79d6: the curve, sound system / manager references, list and map
// are destroyed; sounds still registered are not released by the original (Shutdown stops them).
cSC4AudioSoundManager::~cSC4AudioSoundManager() = default;

// cSC4AudioSoundManager::QueryInterface @ 0x1003b8ffe (and the three thunks)
bool cSC4AudioSoundManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0x4a47dc0d || iid == kGZIID_cIGZUnknown) {
        if (out) *out = static_cast<cIGZSndEventHandler*>(this);
        AddRef();
        return true;
    }
    return false;
}

// cSC4AudioSoundManager::Release @ 0x1003b90ca: destroys at counts below 2.
std::uint32_t cSC4AudioSoundManager::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

std::uint32_t cSC4AudioSoundManager::GenerateNewInstanceId() {
    std::uint32_t& counter = InstanceIdCounter();
    std::uint32_t id = counter;
    if (id == 0) id = 1;
    counter = id + 1;
    return id;
}

void cSC4AudioSoundManager::ResetInstanceIdCounterForTesting() { InstanceIdCounter() = 0; }

tSound* cSC4AudioSoundManager::FindSound(std::uint32_t instanceId) const {
    const auto it = byInstance_.find(instanceId);
    return it == byInstance_.end() ? nullptr : it->second;
}

// Creates event `eventId` carrying four integers and dispatches it synchronously to the sound
// system's handlers (every poster method of the original has this shape).
bool cSC4AudioSoundManager::PostEvent(std::uint32_t eventId, std::uint32_t d1, std::uint32_t d2, std::uint32_t d3,
                                      std::uint32_t d4) {
    if (!soundSystem_) return false;
    void* raw = nullptr;
    const bool created = soundSystem_->EventCreateWithData(eventId, kGZCLSID_cGZSndEventStd, &raw, d1, d2, d3, d4);
    auto* event = static_cast<cIGZSndEventStd*>(raw);
    if (created && event) soundSystem_->DoEvent(event);
    if (event) event->Release();
    return created;
}

// cSC4AudioSoundManager::Init() @ 0x1003b7ac0
bool cSC4AudioSoundManager::Init() {
    Services& services = GetServices();
    if (!services.soundSystem) return false;
    soundSystem_ = services.soundSystem;
    if (!services.soundManager) return false;
    soundManager_ = services.soundManager;

    ReadTuningValues();

    soundSystem_->RegisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
    soundSystem_->RegisterTarget(static_cast<cIGZSndUpdateTarget*>(this), kUpdateIntervalMs);

    if (cIGZMessageServer2* server = services.messageServer) {
        cIGZMessageTarget2* target = this;
        server->AddNotification(target, kMessageSceneStart);
        server->AddNotification(target, kMessageSceneStopAll);
        server->AddNotification(target, kMessageStopAllSounds);
        server->AddNotification(target, kMessageUpdateAll1);
        server->AddNotification(target, kMessageFocusChange);
        server->AddNotification(target, kMessageUpdateAll2);
        server->AddNotification(target, kMessageUpdateAll3);
    }

    // Deviation: without a game context the default volume (1024) is kept.
    if (services.game) {
        const AudioPreferences preferences = services.game->GetAudioPreferences();
        masterVolume_ = preferences.soundEnabled ? preferences.masterVolume : 0;
    }
    UpdateAllSoundsNow();
    return true;
}

// cSC4AudioSoundManager::_ReadTuningValues() @ 0x1003b7d2a. Resets the tuning defaults and the
// curve (a flat 1024 line), then overrides them from the audio tuning exemplar
// (type 0x6534284a, group 0xca25875d, instance 0x6a359c5c).
bool cSC4AudioSoundManager::ReadTuningValues() {
    maxInstancesPerSound_ = 3;
    maxNonMasterInstances_ = 10;
    tuningValue30_ = 30;
    responseCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1024.0f}});

    cIExemplarSource* exemplars = GetServices().exemplars;
    const cGZPersistResourceKey key{0x6534284a, 0xca25875d, 0x6a359c5c};
    if (!exemplars || !exemplars->ExemplarExists(key)) return false;   // original formats an error and drops it

    std::vector<float> curve;
    if (exemplars->GetFloatArrayProperty(key, 0x6a946a50, curve)) {
        responseCurve_.SetResponseCurveData(curve.data(), curve.size());
    }
    exemplars->GetUint32Property(key, 0xaabaa0d3, maxInstancesPerSound_);
    exemplars->GetUint32Property(key, 0x0a946a57, maxNonMasterInstances_);
    exemplars->GetUint32Property(key, 0x4a946a5d, tuningValue30_);
    return true;
}

// cSC4AudioSoundManager::Shutdown() @ 0x1003b7f62
bool cSC4AudioSoundManager::Shutdown() {
    StopAllSounds(true);
    if (soundSystem_) soundSystem_->Panic();

    if (cIGZMessageServer2* server = GetServices().messageServer) {
        cIGZMessageTarget2* target = this;
        server->RemoveNotification(target, kMessageUpdateAll3);
        server->RemoveNotification(target, kMessageUpdateAll2);
        server->RemoveNotification(target, kMessageFocusChange);
        server->RemoveNotification(target, kMessageUpdateAll1);
        server->RemoveNotification(target, kMessageStopAllSounds);
        server->RemoveNotification(target, kMessageSceneStopAll);
        server->RemoveNotification(target, kMessageSceneStart);
    }
    if (soundSystem_) {
        soundSystem_->UnregisterTarget(static_cast<cIGZSndUpdateTarget*>(this));
        soundSystem_->UnregisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
        soundSystem_ = nullptr;
    }
    soundManager_ = nullptr;
    return true;
}

// cSC4AudioSoundManager::StopAllSounds(bool) @ 0x1003b80dc
bool cSC4AudioSoundManager::StopAllSounds(bool immediate) {
    return PostEvent(kEventStopAllSounds, immediate ? 1u : 0u, 0, 0, 0);
}

// cSC4AudioSoundManager::SetMasterVolume(unsigned int) @ 0x1003b817c
bool cSC4AudioSoundManager::SetMasterVolume(std::uint32_t volume) {
    return PostEvent(kEventSetMasterVolume, volume, 0, 0, 0);
}

// cSC4AudioSoundManager::StartSound(unsigned int, unsigned int) @ 0x1003b8236
bool cSC4AudioSoundManager::StartSound(std::uint32_t soundId, std::uint32_t instanceId) {
    return PostEvent(kEventStartSound, soundId, instanceId, 0, 0);
}

// cSC4AudioSoundManager::SetSoundPosition @ 0x1003b82da: coordinates travel as truncated integers.
bool cSC4AudioSoundManager::SetSoundPosition(std::uint32_t instanceId, float x, float y, float z) {
    return PostEvent(kEventSetSoundPosition, instanceId, static_cast<std::uint32_t>(FloatToInt32ViaInt64(x)),
                     static_cast<std::uint32_t>(FloatToInt32ViaInt64(y)),
                     static_cast<std::uint32_t>(FloatToInt32ViaInt64(z)));
}

// cSC4AudioSoundManager::UpdateAllSounds() @ 0x1003b8388
bool cSC4AudioSoundManager::UpdateAllSounds() { return PostEvent(kEventUpdateAllSounds, 0, 0, 0, 0); }

// cSC4AudioSoundManager::Update(int) @ 0x1003b8426 (the cIGZSndUpdateTarget tick, every 20 ms)
void cSC4AudioSoundManager::Update(std::int32_t elapsedMs) {
    elapsedMs_ += static_cast<std::uint32_t>(elapsedMs);
    if (focusRefreshTicks_ != 0) {
        focusRefreshTicks_ = std::min(focusRefreshTicks_, kFocusRefreshTicks) - 1;
        if (soundSystem_) soundSystem_->EnumerateTargets(BruteForceVolumeUpdate, 0, this);
    }
    ++reentryCount_;
    for (tSound* sound : sounds_) sound->Update(static_cast<std::uint32_t>(elapsedMs));
    --reentryCount_;
}

// cSC4AudioSoundManager::DoEvent(cIGZSndEventStd*) @ 0x1003b84c6
bool cSC4AudioSoundManager::DoEvent(cIGZSndEventStd* event) {
    switch (event->GetEventId()) {
        case kEventSetSoundPosition: return HandleSetSoundPosition(event);
        case kEventStopAllSounds: {
            cIGZMessage2Standard* message = event->AsMessage2Standard();
            StopAllSoundsNow(message->GetData1() != 0);
            return false;
        }
        case kEventStartSound:
        case kEventStartSoundAlias: return HandleStartSound(event);
        case kEventStopSound:
        case kEventStopSoundAlias: return HandleStopSound(event);
        case kEventSetMasterVolume: HandleSetMasterVolume(event); return false;
        case kEventSetSoundVolume: return HandleSetSoundVolume(event);
        case kEventResetSound: return HandleResetSound(event);
        case kEventEnumerateSounds: return HandleEnumerateSounds(event);
        case kEventUpdateAllSounds: UpdateAllSoundsNow(); return true;
        case kEventFocusChange: HandleFocusChangeEvent(event); return false;
        default: return false;
    }
}

// _HandleStartSound @ 0x1003b8622: data1 = soundId, data2 = instanceId, data3 = flags (optional).
bool cSC4AudioSoundManager::HandleStartSound(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    const std::uint32_t flags = message->GetHasData3() ? static_cast<std::uint32_t>(message->GetData3()) : 0u;
    StartSoundNow(static_cast<std::uint32_t>(message->GetData1()), static_cast<std::uint32_t>(message->GetData2()), flags);
    message->Release();
    return true;
}

// _HandleStopSound @ 0x1003b86ca: data1 = instanceId, data2 = immediate.
bool cSC4AudioSoundManager::HandleStopSound(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    StopSoundNow(static_cast<std::uint32_t>(message->GetData1()), message->GetData2() != 0);
    message->Release();
    return true;
}

// _HandleSetSoundPosition @ 0x1003b8754: data1 = instanceId, data2..4 = x, y, z (signed ints).
bool cSC4AudioSoundManager::HandleSetSoundPosition(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    SetSoundPositionNow(static_cast<std::uint32_t>(message->GetData1()),
                        static_cast<float>(static_cast<std::int32_t>(message->GetData2())),
                        static_cast<float>(static_cast<std::int32_t>(message->GetData3())),
                        static_cast<float>(static_cast<std::int32_t>(message->GetData4())));
    message->Release();
    return true;
}

// _HandleSetSoundVolume @ 0x1003b880a
bool cSC4AudioSoundManager::HandleSetSoundVolume(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    SetSoundVolumeNow(static_cast<std::uint32_t>(message->GetData1()), static_cast<std::uint32_t>(message->GetData2()));
    message->Release();
    return true;
}

// _HandleResetSound @ 0x1003b888e
bool cSC4AudioSoundManager::HandleResetSound(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    ResetSound(static_cast<std::uint32_t>(message->GetData1()));
    message->Release();
    return true;
}

// _HandleEnumerateSounds @ 0x1003b88fa: the event's unknown must support cISC4AudioSoundVisitor;
// it is called for every sound until it returns false. False only when the visitor is missing.
bool cSC4AudioSoundManager::HandleEnumerateSounds(cIGZSndEventStd* event) {
    cIGZUnknown* unknown = event->AsMessage2Standard()->GetIGZUnknown();
    if (!unknown) return false;
    unknown->AddRef();
    void* raw = nullptr;
    bool ok = unknown->QueryInterface(kGZIID_cISC4AudioSoundVisitor, &raw);
    if (ok) {
        auto* visitor = static_cast<cISC4AudioSoundVisitor*>(raw);
        // Port: a snapshot (with references) so a visitor may stop sounds while enumerating.
        std::vector<tSound*> snapshot(sounds_.begin(), sounds_.end());
        for (tSound* sound : snapshot) sound->AddRef();
        for (tSound* sound : snapshot) {
            if (!visitor->VisitSound(sound)) break;
        }
        for (tSound* sound : snapshot) sound->Release();
        visitor->Release();
    }
    unknown->Release();
    return ok;
}

// _HandleSetMasterVolume @ 0x1003b8a00
bool cSC4AudioSoundManager::HandleSetMasterVolume(cIGZSndEventStd* event) {
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    masterVolume_ = static_cast<std::uint32_t>(message->GetData1());
    UpdateAllSoundsNow();
    message->Release();
    return false;
}

// _UpdateAllSounds @ 0x1003b8a6a: re-applies every sound's volume and its pause states.
bool cSC4AudioSoundManager::UpdateAllSoundsNow() {
    std::vector<tSound*> snapshot(sounds_.begin(), sounds_.end());
    for (tSound* sound : snapshot) sound->AddRef();
    for (tSound* sound : snapshot) {
        sound->SetVolume(sound->volume_);
        sound->StartStopSoundForDistance();
        sound->StartStopSoundForSimulationPause();
    }
    for (tSound* sound : snapshot) sound->Release();
    return true;
}

// _StopAllSounds @ 0x1003b8ab6
void cSC4AudioSoundManager::StopAllSoundsNow(bool immediate) {
    while (!sounds_.empty()) {
        tSound* first = sounds_.front();
        if (!StopSoundNow(first->instanceId_, immediate)) {
            // Port safety: a sound that is not in the instance map cannot be stopped by id; drop it.
            RemoveFromSoundList(first);
            --totalCount_;
        }
    }
}

// _HandleFocusChangeEvent @ 0x1003b8af4: data1 = has focus. Losing focus pauses (kills) every voice
// and mutes; regaining it resumes them, restores the player's volume and schedules 50 volume
// refresh ticks (see Update).
bool cSC4AudioSoundManager::HandleFocusChangeEvent(cIGZSndEventStd* event) {
    if (!event || !soundSystem_) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    if (!message) return false;
    message->AddRef();
    const bool focused = message->GetData1() != 0;

    std::vector<tSound*> snapshot(sounds_.begin(), sounds_.end());
    for (tSound* sound : snapshot) sound->AddRef();
    for (tSound* sound : snapshot) {
        if (!focused) sound->Pause(true); else sound->Resume();
    }
    for (tSound* sound : snapshot) sound->Release();

    AudioPreferences preferences;
    if (cIGameAudioContext* game = GetServices().game) preferences = game->GetAudioPreferences();
    if (!focused || !preferences.soundEnabled) SetMasterVolume(0);
    else SetMasterVolume(preferences.masterVolume);
    if (focused) focusRefreshTicks_ = kFocusRefreshTicks;
    message->Release();
    return true;
}

// DoCompletionFunction @ 0x1003b8bec: a sound finished by itself; stop (kill) its managed entry.
// The managed sound is found through the game-object register, which holds the instance id.
void cSC4AudioSoundManager::OnSoundCompleted(cIGZSnd* sound) {
    if (!sound) return;
    void* raw = nullptr;
    if (!sound->QueryInterface(kGZIID_cIGZSndRegisterSet, &raw)) return;
    auto* registers = static_cast<cIGZSndRegisterSet*>(raw);
    const auto instanceId = static_cast<std::uint32_t>(registers->GetRegisterValue(kRegGameObject));
    if (tSound* managed = FindSound(instanceId)) {
        managed->AddRef();
        StopSoundNow(managed->instanceId_, true);
        managed->Release();
    }
    registers->Release();
}

// _StopSound @ 0x1003b8cac. `immediate` kills the voice; otherwise only looping sounds are asked to
// stop (they fade out), while one-shots are left to finish.
bool cSC4AudioSoundManager::StopSoundNow(std::uint32_t instanceId, bool immediate) {
    const auto it = byInstance_.find(instanceId);
    if (it == byInstance_.end()) return false;
    tSound* sound = it->second;
    sound->AddRef();
    RemoveFromSoundList(sound);
    byInstance_.erase(it);
    --totalCount_;

    if (sound->slaves_.empty()) {
        sound->EnableCompletionNotification(false);
        const bool looping = sound->registers_ && sound->registers_->GetRegisterValue(kRegLoopFlag) != 0;
        if (sound->snd_ && sound->snd_->IsPlaying()) {
            if (immediate) sound->snd_->Kill();
            else if (looping) sound->snd_->Stop();
        }
        if (!looping) --nonMasterCount_;
        else if (sound->master_ == nullptr) --masterCount_;
    } else {
        sound->TransferSlaves();
    }
    if (sound->master_) sound->master_->RemoveSlave(sound);

    sound->registers_ = nullptr;
    sound->snd_ = nullptr;
    sound->Release();   // the instance map's reference
    sound->Release();   // the reference taken above
    return true;
}

// _ResetSound @ 0x1003b9146: resets the track and re-sends the volume register.
bool cSC4AudioSoundManager::ResetSound(std::uint32_t instanceId) {
    tSound* sound = FindSound(instanceId);
    if (!sound) return false;
    sound->AddRef();
    void* raw = nullptr;
    if (sound->snd_ && sound->snd_->QueryInterface(kGZIID_cIHitTrackPlayer, &raw)) {
        auto* player = static_cast<cIHitTrackPlayer*>(raw);
        player->Reset();
        void* rawRegisters = nullptr;
        if (player->QueryInterface(kGZIID_cIGZSndRegisterSet, &rawRegisters)) {
            auto* registers = static_cast<cIGZSndRegisterSet*>(rawRegisters);
            registers->SetRegisterValue(kRegVolume, registers->GetRegisterValue(kRegVolume), false);
            registers->Release();
        }
        player->Release();
    }
    sound->Release();
    return true;
}

// _GetPlayingSoundMinPlayTime @ 0x1003b9a74: the last started sound with this id (not referenced
// here, unlike the original) and the smallest (startTime - now) over all of them.
bool cSC4AudioSoundManager::GetPlayingSoundMinPlayTime(std::uint32_t soundId, tSound** newest, std::int32_t& minPlayTime) {
    if (!newest) return false;
    *newest = nullptr;
    minPlayTime = INT_MAX;
    for (tSound* sound : sounds_) {
        if (sound->soundId_ != soundId) continue;
        const std::int32_t delta = static_cast<std::int32_t>(sound->startTime_ - elapsedMs_);
        minPlayTime = std::min(minPlayTime, delta);
        *newest = sound;
    }
    return minPlayTime != INT_MAX;
}

// _RemoveFromSoundList @ 0x1003b9d82
void cSC4AudioSoundManager::RemoveFromSoundList(tSound* sound) {
    const auto it = std::find(sounds_.begin(), sounds_.end(), sound);
    if (it != sounds_.end()) sounds_.erase(it);
}

// _StartSound @ 0x1003b925e
//
// 1. A sound with the same instance id is stopped first.
// 2. A non-looping retrigger of the same sound id within 50 ms is ignored.
// 3. Per-id (maxInstancesPerSound) and total (maxNonMasterInstances) limits reject new non-looping
//    sounds.
// 4. The sound is created through the sound manager; a looping sound whose id already has a
//    looping master becomes that master's slave (and is never played itself), otherwise it is
//    played with the instance id, master volume and centre pan.
// 5. When other sounds with the same id are playing, the response curve (evaluated at that count)
//    scales every one of them.
bool cSC4AudioSoundManager::StartSoundNow(std::uint32_t soundId, std::uint32_t instanceId, std::uint32_t flags) {
    if (byInstance_.find(instanceId) != byInstance_.end()) StopSoundNow(instanceId, true);

    tSound* newestRaw = nullptr;
    std::int32_t minPlayTime = 0;
    const bool anyPlaying = GetPlayingSoundMinPlayTime(soundId, &newestRaw, minPlayTime);
    if (anyPlaying && newestRaw->registers_ && newestRaw->registers_->GetRegisterValue(kRegLoopFlag) == 0 &&
        (elapsedMs_ - newestRaw->startTime_) < kRetriggerWindowMs) {
        return false;
    }

    std::uint32_t sameIdCount = 0;
    cRZAutoRefCount<tSound> lastSameId;
    for (tSound* sound : sounds_) {
        if (sound->soundId_ == soundId) {
            ++sameIdCount;
            lastSameId = sound;
        }
    }
    if (sameIdCount > 0 && lastSameId->registers_ && lastSameId->registers_->GetRegisterValue(kRegLoopFlag) == 0 &&
        (maxNonMasterInstances_ <= static_cast<std::uint32_t>(nonMasterCount_) || maxInstancesPerSound_ <= sameIdCount)) {
        return false;
    }

    if (!soundManager_) return false;   // original dereferences null
    void* rawSnd = nullptr;
    if (!soundManager_->GetSoundById(soundId, 0, nullptr, kGZIID_cIGZSnd, &rawSnd) || !rawSnd) return false;
    cRZAutoRefCount<cIGZSnd> snd;
    snd = static_cast<cIGZSnd*>(rawSnd);
    static_cast<cIGZSnd*>(rawSnd)->Release();   // QueryInterface-style result: drop the call's reference
    void* rawRegisters = nullptr;
    if (!snd->QueryInterface(kGZIID_cIGZSndRegisterSet, &rawRegisters) || !rawRegisters) return false;
    cRZAutoRefCount<cIGZSndRegisterSet> registers;
    registers = static_cast<cIGZSndRegisterSet*>(rawRegisters);
    static_cast<cIGZSndRegisterSet*>(rawRegisters)->Release();

    if (instanceId == 0) instanceId = GenerateNewInstanceId();

    tSound* sound = new tSound();   // reference count 1
    sound->manager_ = this;
    sound->snd_ = snd;
    sound->registers_ = registers;
    sound->soundId_ = soundId;
    sound->instanceId_ = instanceId;
    sound->volume_ = 0x400;
    sound->startTime_ = elapsedMs_;
    if (flags & 1) sound->flags_ |= 0x20;
    const std::int32_t nearHundredths = registers->GetRegisterValue(kRegDefaultMinimumDistance);
    const float minimumDistance = (static_cast<float>(nearHundredths) / 100.0f) * 100.0f;
    sound->minimumDistance_ = minimumDistance;
    sound->maximumDistance_ = minimumDistance * 4.0f;
    sound->EnableCompletionNotification(true);

    bool started = false;
    bool becameSlave = false;
    for (tSound* candidate : sounds_) {
        if (candidate->soundId_ == soundId && candidate->master_ == nullptr) {
            if (candidate->registers_ && candidate->registers_->GetRegisterValue(kRegLoopFlag) != 0) {
                candidate->AddSlave(sound);
                becameSlave = true;
            }
            break;
        }
    }

    if (becameSlave) {
        started = true;
    } else {
        registers->SetRegisterValue(kRegArgMode, 3, false);
        started = snd->PlayWithScriptArguments(static_cast<std::int32_t>(sound->instanceId_),
                                               static_cast<std::int32_t>(masterVolume_), 0x200, 0);
        if (started) {
            if (registers->GetRegisterValue(kRegLoopFlag) == 0) ++nonMasterCount_;
            else if (sound->master_ == nullptr) ++masterCount_;
        }
    }

    if (started) {
        ++totalCount_;
        sound->AddRef();                   // the instance map / list reference
        byInstance_[instanceId] = sound;
        sounds_.push_back(sound);
        if (sameIdCount != 0 && registers->GetRegisterValue(kRegLoopFlag) == 0) {
            const float scale = responseCurve_.GetOutputValue(static_cast<float>(sameIdCount)) * kOneOver1024;
            for (tSound* other : sounds_) {
                if (other->soundId_ == soundId) {
                    other->volumeScale_ = scale;
                    other->SetVolume(other->volume_);
                }
            }
        }
        sound->StartStopSoundForSimulationPause();
    }
    sound->Release();
    return started;
}

// _SetSoundPosition @ 0x1003b9828
bool cSC4AudioSoundManager::SetSoundPositionNow(std::uint32_t instanceId, float x, float y, float z) {
    tSound* sound = FindSound(instanceId);
    if (!sound) return false;
    sound->AddRef();
    const float position[3] = {x, y, z};
    sound->SetPosition(position);
    sound->flags_ |= 2;
    sound->Release();
    return true;
}

// _SetSoundVolume @ 0x1003b98d2
bool cSC4AudioSoundManager::SetSoundVolumeNow(std::uint32_t instanceId, std::uint32_t volume) {
    tSound* sound = FindSound(instanceId);
    if (!sound) return false;
    sound->AddRef();
    sound->SetVolume(static_cast<std::int32_t>(volume));
    sound->flags_ |= 1;
    sound->Release();
    return true;
}

// _HandleFocusChangeMessage @ 0x1003b8ef4: forwards the application's focus message to the sound
// system as a kEventFocusChange event.
bool cSC4AudioSoundManager::HandleFocusChangeMessage(cIGZMessage2* message) {
    if (!soundSystem_ || !message) return false;
    void* raw = nullptr;
    if (!message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw)) return false;
    auto* standard = static_cast<cIGZMessage2Standard*>(raw);
    const bool focused = standard->GetData1() != 0;
    const bool posted = PostEvent(kEventFocusChange, focused ? 1u : 0u, 0, 0, 0);
    standard->Release();
    return posted;
}

// cSC4AudioSoundManager::DoMessage(cIGZMessage2*) @ 0x1003b8e3a
bool cSC4AudioSoundManager::DoMessage(cIGZMessage2* message) {
    switch (message->GetType()) {
        case kMessageUpdateAll1:
        case kMessageUpdateAll2:
        case kMessageUpdateAll3: return UpdateAllSounds();
        case kMessageReloadTuning: return ReadTuningValues();
        case kMessageStopAllSounds:
        case kMessageSceneStopAll: return StopAllSounds(false);
        case kMessageSceneStart: return true;
        case kMessageFocusChange: return HandleFocusChangeMessage(message);
        default: return false;
    }
}
