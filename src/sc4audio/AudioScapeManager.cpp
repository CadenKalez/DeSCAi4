// cSC4AudioScapeManager — see AudioScapeManager.h for the recovered layout, quirks and safety deviations.
#include "sc4audio/AudioScapeManager.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>

#include "gz/GZCOMDirector.h"
#include "rz/cRZSysServPtr.h"

namespace {

// __ZL8gpSndSys: the file-static cRZAutoRefCount<cIGZSndSys> the original mirrors the framework's sound system
// into in Init() (Shutdown clears it). The tGroup helpers post their events through it and follow the original's
// null checks: no sound system, no event.
cRZAutoRefCount<cIGZSndSys> gpSndSys;

bool PostSoundEvent(std::uint32_t id, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                    std::uint32_t data4) {
    if (!gpSndSys) return false;
    return gpSndSys->PostEventIds(id, data1, data2, data3, data4);
}

// _PostStart @ 0x1003f23d1: start the sound (sound id, instance key) and set its volume in one go.
bool AudioScapePostStart(std::uint32_t soundId, std::uint32_t instanceKey, bool /*unused*/, std::uint32_t volume) {
    if (!gpSndSys) return false;
    gpSndSys->PostEventIds(kAudioScapeEventStartSound, soundId, instanceKey, 0, 0);
    gpSndSys->PostEventIds(kAudioScapeEventSetVolume, instanceKey, volume, 0, 0);
    return true;
}

// The original's sound instance key: the low 32 bits of the heap-allocated definition copy in the group's list.
std::uint32_t InstanceKey(const void* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) & 0xffffffffu);
}

float Distance(const cS3DVector3& a, const cS3DVector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool InsideRect(const SC4RectFloat& rect, float x, float z) {
    return rect.x0 <= x && x <= rect.x1 && rect.z0 <= z && z <= rect.z1;
}

// The original casts a float quotient straight to int (cvttss2si), which is undefined for inf/NaN and for values
// outside the int range. The port answers 0 for those so UBSan-clean builds stay defined; sane rectangles are
// unaffected.
int TruncateToInt(float value) {
    if (!(value > -2147483000.0f && value < 2147483000.0f)) return 0;
    return static_cast<int>(value);
}

}  // namespace

// ---------------------------------------------------------------- tGroup

// @ 0x1003f2720
cSC4AudioScapeManager::tGroup::tGroup() {
    center = cS3DVector3{};
    sum = cS3DVector3{};
    count = 0;
    mark = 0;
    radius = 0.0f;
    pendingExpiry = 0;
    instanceCount = 0;
    soundCount = 0;
    volume[0] = volume[1] = 0;
    targetVolume[0] = targetVolume[1] = kAudioScapeMaxVolume;
    lastStartVolume = 0;
}

// @ 0x1003f0942: frees every instance record and empties the list; the accumulated position sum is left as it was
// (quirk) while the averaging count is reset.
bool cSC4AudioScapeManager::tGroup::RemoveAllInstances() {
    while (!instances.empty()) {
        tInstance* instance = instances.front();
        instances.pop_front();
        std::free(instance);
        --instanceCount;
    }
    count = 0;
    return true;
}

// @ 0x1003f17ee
bool cSC4AudioScapeManager::tGroup::RemoveInstance(std::uint32_t instanceId) {
    for (auto it = instances.begin(); it != instances.end(); ++it) {
        tInstance* instance = *it;
        if (instance->id != instanceId) continue;
        const cS3DVector3 position{instance->x, instance->y, instance->z};
        CenterRemovePosition(position);
        std::free(instance);
        instances.erase(it);
        --instanceCount;
        return true;
    }
    return false;
}

// @ 0x1003f1ac4
void cSC4AudioScapeManager::tGroup::CenterRemovePosition(const cS3DVector3& position) {
    if (count == 0) return;
    sum.x -= position.x;
    sum.y -= position.y;
    sum.z -= position.z;
    --count;
    // The original divides by the *decremented* count, so removing the last position multiplies a zero sum by
    // infinity: the centre becomes NaN. Kept as-is (ClearLayer uses RemoveAllInstances instead).
    const float scale = 1.0f / static_cast<float>(count);
    center.x = sum.x * scale;
    center.y = sum.y * scale;
    center.z = sum.z * scale;
}

// @ 0x1003f1b30
void cSC4AudioScapeManager::tGroup::CenterAddPosition(const cS3DVector3& position) {
    if (count == 0) {
        sum = position;
    } else {
        sum.x += position.x;
        sum.y += position.y;
        sum.z += position.z;
    }
    ++count;
    const float scale = 1.0f / static_cast<float>(count);
    center.x = sum.x * scale;
    center.y = sum.y * scale;
    center.z = sum.z * scale;
}

// @ 0x1003f1fca: stops every sound this group is playing (posting only when a sound system is held) and drops the
// definition copies. The instance count is untouched.
bool cSC4AudioScapeManager::tGroup::StopAllSounds() {
    while (!sounds.empty()) {
        tSoundDef* sound = sounds.front();
        sounds.pop_front();
        PostSoundEvent(kAudioScapeEventStopSound, InstanceKey(sound), 0, 0, 0);
        delete sound;
        --soundCount;
    }
    return true;
}

// @ 0x1003f205e: stores the current volume of one of the two channels and forwards it to every playing sound.
// Returns the volume computed by the last _Start (quirk: the argument is not what comes back).
std::uint32_t cSC4AudioScapeManager::tGroup::SetVolume(std::uint32_t index, std::uint32_t value) {
    volume[index] = value;
    for (tSoundDef* sound : sounds) {
        PostSoundEvent(kAudioScapeEventSetVolume, InstanceKey(sound), value, 0, 0);
    }
    return lastStartVolume;
}

// @ 0x1003f21c8: a definition plays while the group's instance count is inside [minInstances, maxInstances].
void cSC4AudioScapeManager::tGroup::HandleSound(tSoundDef* soundDef) {
    if (!soundDef) return;
    const std::uint32_t instances = static_cast<std::uint32_t>(instanceCount);
    if (instances < soundDef->minInstances || soundDef->maxInstances < instances) {
        _Stop(soundDef);
    } else {
        _Start(soundDef);
    }
}

// @ 0x1003f21ec
void cSC4AudioScapeManager::tGroup::UpdatePositions() {
    for (tSoundDef* sound : sounds) {
        PostSoundEvent(kAudioScapeEventSetPosition, InstanceKey(sound), static_cast<std::uint32_t>(static_cast<int>(center.x)),
                       static_cast<std::uint32_t>(static_cast<int>(center.y)),
                       static_cast<std::uint32_t>(static_cast<int>(center.z)));
    }
}

// @ 0x1003f2248: copies the definition into the group's list (unless that sound id already plays here) and starts
// it at the product of the two channel volumes / 1024.
bool cSC4AudioScapeManager::tGroup::_Start(tSoundDef* soundDef) {
    if (!soundDef) return false;
    for (const tSoundDef* playing : sounds) {
        if (playing->soundId == soundDef->soundId) return true;
    }
    auto* copy = new tSoundDef(*soundDef);
    sounds.push_back(copy);
    ++soundCount;
    // (volume[1] * volume[0]) / 1024, with the original's wrapping 32-bit multiply.
    const std::uint32_t product = volume[1] * volume[0];
    const std::int32_t scaled = static_cast<std::int32_t>(product) / static_cast<std::int32_t>(kAudioScapeMaxVolume);
    lastStartVolume = static_cast<std::uint32_t>(scaled);
    AudioScapePostStart(soundDef->soundId, InstanceKey(copy), false, lastStartVolume);
    return true;
}

// @ 0x1003f2316: stops the playing copy with this definition's sound id. Reports true even when nothing matched.
bool cSC4AudioScapeManager::tGroup::_Stop(tSoundDef* soundDef) {
    if (!soundDef) return false;
    for (auto it = sounds.begin(); it != sounds.end(); ++it) {
        tSoundDef* playing = *it;
        if (playing->soundId != soundDef->soundId) continue;
        PostSoundEvent(kAudioScapeEventStopSound, InstanceKey(playing), 0, 0, 0);
        sounds.erase(it);
        delete playing;
        --soundCount;
        break;
    }
    return true;
}

// ---------------------------------------------------------------- tLayer

// @ 0x1003f06c2: deletes the registered definitions and empties the sound list.
bool cSC4AudioScapeManager::tLayer::UnregisterAllSounds() {
    while (!sounds.empty()) {
        tSoundDef* sound = sounds.front();
        sounds.pop_front();
        delete sound;
    }
    return true;
}

// @ 0x1003f07a6: empties a layer; a persistent layer (flag 0x10) keeps the groups that still have instances (their
// instances are removed but their sounds are not stopped - quirk), every other layer frees all its groups.
bool cSC4AudioScapeManager::tLayer::Clear() {
    auto it = groups.begin();
    while (it != groups.end()) {
        tGroup* group = *it;
        if ((flags & kAudioScapeLayerFlagPersistent) == 0) {
            group->RemoveAllInstances();
            group->StopAllSounds();
            it = groups.erase(it);
            delete group;
            --groupCount;
        } else if (group->count == 0) {
            group->StopAllSounds();
            it = groups.erase(it);
            delete group;
            --groupCount;
        } else {
            group->RemoveAllInstances();
            ++it;
        }
    }
    instanceCount = 0;
    return true;
}

// @ 0x1003f2794: the original clears the sound list and then the group list (nodes only: a non-empty layer leaks
// its groups) and destroys the name string. The member destructors do exactly that.
cSC4AudioScapeManager::tLayer::~tLayer() = default;

// ---------------------------------------------------------------- cSC4AudioScapeManager

// @ 0x1003ded76
cIGZSystemService* cSC4AudioScapeManager::Create() {
    // The factory returns the second view (+8, cIGZSystemService), as the binary's `addq $0x8, %rbx` does.
    return static_cast<cIGZSystemService*>(new cSC4AudioScapeManager());
}

// @ 0x1003f242c (the other constructor symbol @ 0x1003f25c0 is an alias)
cSC4AudioScapeManager::cSC4AudioScapeManager() : timer_(cRZTimer::kMilliseconds) {}

// @ 0x1003f25ca (complete @ 0x1003f26e6, deleting @ 0x1003f26f0). The layer list is only emptied, not freed
// (quirk: the original leaks any layer still registered at destruction).
cSC4AudioScapeManager::~cSC4AudioScapeManager() = default;

// @ 0x1003f0196
bool cSC4AudioScapeManager::Init() {
    if (initialized_) return true;
    if (!RZGetFramework()) return false;

    cRZSysServPtr<cIGZSndSys, kGZSID_cGZSndSys, kGZIID_cIGZSndSys> soundSystem;
    if (!soundSystem) return false;
    soundSystem_ = soundSystem.get();
    gpSndSys = soundSystem.get();

    cRZSysServPtr<cIGZSndMgr, kGZSID_cGZSndMgr, kGZIID_cIGZSndMgr> soundManager;
    if (!soundManager) return false;   // the original keeps the sound system held on this failure path
    soundManager_ = soundManager.get();

    if (cIGZFrameWork* framework = RZGetFramework()) {
        framework->AddSystemService(this);   // neither result is checked in the original
        framework->AddToTick(this);
    }
    timer_.Start();
    initialized_ = true;
    return true;
}

// @ 0x1003f034a
bool cSC4AudioScapeManager::Shutdown() {
    if (!initialized_) return true;
    lock60_.Lock();
    while (!layers_.empty()) {
        DestroyLayer(layers_.front()->id);
    }
    if (cIGZFrameWork* framework = RZGetFramework()) {
        framework->RemoveFromTick(this);
        framework->RemoveSystemService(this);
    }
    soundSystem_ = nullptr;
    soundManager_ = nullptr;
    gpSndSys = nullptr;
    initialized_ = false;
    lock60_.Unlock();
    return true;
}

// @ 0x1003f0450
bool cSC4AudioScapeManager::CreateLayer(const cIGZString& name, std::uint32_t layerId, std::uint32_t flags,
                                        float radius) {
    lock60_.Lock();
    for (const tLayer* layer : layers_) {
        if (layer->id == layerId) {
            lock60_.Unlock();
            return false;
        }
    }
    auto* layer = new tLayer();
    layer->name.Copy(name);
    layer->id = layerId;
    layer->flags = flags;
    layer->radius = radius;
    layers_.push_back(layer);
    if (flags & kAudioScapeLayerFlagOwnSound) {
        // Through the interface's own slot 0x80, as the original does.
        RegisterLayerSound(layerId, layerId, false, 0, 1, 0x7fffffff);
    }
    lock60_.Unlock();
    return true;
}

// @ 0x1003f05d8
bool cSC4AudioScapeManager::DestroyLayer(std::uint32_t layerId) {
    lock60_.Lock();
    for (auto it = layers_.begin(); it != layers_.end(); ++it) {
        tLayer* layer = *it;
        layer->flags = 0;   // quirk: every inspected layer loses its flags, not only the one destroyed
        if (layer->id != layerId) continue;
        if (!ClearLayer(layerId)) {
            lock60_.Unlock();
            return false;
        }
        layer->UnregisterAllSounds();
        delete layer;
        layers_.erase(it);
        lock60_.Unlock();
        return true;
    }
    lock60_.Unlock();
    return false;
}

// @ 0x1003f072a
bool cSC4AudioScapeManager::ClearLayer(std::uint32_t layerId) {
    lock60_.Lock();
    for (tLayer* layer : layers_) {
        if (layer->id != layerId) continue;
        layer->Clear();
        lock60_.Unlock();
        return true;
    }
    lock60_.Unlock();
    return false;
}

// @ 0x1003f085e
bool cSC4AudioScapeManager::ClearAllLayers(std::uint32_t flagsMask, bool keepInstances) {
    lock60_.Lock();
    for (tLayer* layer : layers_) {
        if ((layer->flags & flagsMask) != flagsMask) continue;
        auto it = layer->groups.begin();
        while (it != layer->groups.end()) {
            tGroup* group = *it;
            group->RemoveAllInstances();
            if (keepInstances) {
                ++it;
            } else if (!_DeleteGroup(layer, it)) {
                // The original would loop on this group forever when no sound system is held.
                ++it;
            }
        }
    }
    lock60_.Unlock();
    return true;
}

// @ 0x1003f09ac
bool cSC4AudioScapeManager::_DeleteGroup(tLayer* layer, std::list<tGroup*>::iterator& group) {
    tGroup* value = (group != layer->groups.end()) ? *group : nullptr;
    if (!value || !soundSystem_) return false;
    value->StopAllSounds();
    value->sounds.clear();
    value->instances.clear();
    delete value;
    group = layer->groups.erase(group);
    --layer->groupCount;
    return true;
}

// @ 0x1003f1522: |this| and the radius argument are unused by the original. Groups are matched by distance to
// their centre (the nearest group whose radius covers the position wins); with no match a new group is created at
// the position with the layer's radius. The instance record joins the group's list unless the layer forbids it.
bool cSC4AudioScapeManager::_AddInstanceToCorrectGroup(tLayer* layer, tSoundDef* soundDef, tInstance* instance,
                                                       const cS3DVector3& position, float /*radius*/, bool add) {
    if (!layer) return false;
    tGroup* group = nullptr;
    if (!layer->groups.empty()) {
        float bestDistance = FLT_MAX;
        for (tGroup* candidate : layer->groups) {
            const float distance = Distance(candidate->center, position);
            if (distance <= candidate->radius && distance < bestDistance) {
                group = candidate;
                bestDistance = distance;
            }
        }
    }
    if (!add) return false;
    if (!group) {
        group = new tGroup();
        group->radius = layer->radius;
        group->center = position;
        layer->groups.push_back(group);
        ++layer->groupCount;
    }
    if ((layer->flags & kAudioScapeLayerFlagNoInstanceList) == 0) {
        group->instances.push_back(instance);
        ++group->instanceCount;
    }
    group->CenterAddPosition(position);
    ++group->mark;
    _UpdateLayerGroup(layer, soundDef, group);
    return true;
}

// @ 0x1003f1866: |this| is unused in the original as well.
void cSC4AudioScapeManager::_UpdateLayerGroup(tLayer* layer, tSoundDef* soundDef, tGroup* group) {
    if (!group) return;
    if (soundDef) group->HandleSound(soundDef);
    for (tSoundDef* registered : layer->sounds) {
        group->HandleSound(registered);
    }
    group->UpdatePositions();
}

// @ 0x1003f12d6: the 4-argument form inserts an instance without a one-off sound (sound id 0, flag false).
bool cSC4AudioScapeManager::InsertInstance(std::uint32_t layerId, std::uint32_t instanceId,
                                           const cS3DVector3& position, float radius) {
    return InsertInstance(layerId, 0, false, instanceId, position, radius);
}

// @ 0x1003f12ee
bool cSC4AudioScapeManager::InsertInstance(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag,
                                           std::uint32_t instanceId, const cS3DVector3& position, float radius) {
    lock60_.Lock();
    tLayer* layer = _FindLayerNoLock(layerId);
    if (!layer) {
        const cRZString name(kAudioScapeImplicitLayerName);
        if (!CreateLayer(name, layerId, kAudioScapeLayerFlagOwnSound, kAudioScapeImplicitLayerRadius)) {
            lock60_.Unlock();
            return false;
        }
        // Quirk: the 4-argument form is re-entered, so the one-off sound definition is lost.
        const bool inserted = InsertInstance(layerId, instanceId, position, radius);
        lock60_.Unlock();
        return inserted;
    }

    tSoundDef soundDef;
    tSoundDef* soundDefPointer = nullptr;
    if (soundId != 0) {
        soundDef.minInstances = 1;
        soundDef.maxInstances = 0x7fffffff;
        soundDef.unknown08 = 0;
        soundDef.soundId = soundId;
        soundDef.soundFlag = soundFlag;
        soundDef.value = radius;
        soundDefPointer = &soundDef;
    }

    tInstance* record = nullptr;
    if ((layer->flags & kAudioScapeLayerFlagNoInstanceList) == 0) {
        for (const tGroup* group : layer->groups) {
            for (const tInstance* instance : group->instances) {
                if (instance->id == instanceId) {
                    lock60_.Unlock();
                    return false;
                }
            }
        }
        // The original allocates with malloc(sGroupAllocator = sizeof(tGroup)) for every record.
        record = static_cast<tInstance*>(std::malloc(kAudioScapeGroupAllocationSize));
        record->id = instanceId;
        record->x = position.x;
        record->y = position.y;
        record->z = position.z;
    }

    const bool inserted = _AddInstanceToCorrectGroup(layer, soundDefPointer, record, position, radius, true);
    if (!inserted) {
        if (record) std::free(record);
        lock60_.Unlock();
        return false;
    }
    ++layer->instanceCount;
    lock60_.Unlock();
    return true;
}

// @ 0x1003f16cc
bool cSC4AudioScapeManager::RemoveInstance(std::uint32_t layerId, std::uint32_t instanceId) {
    lock60_.Lock();
    for (tLayer* layer : layers_) {
        if (layer->id != layerId) continue;
        for (auto it = layer->groups.begin(); it != layer->groups.end(); ++it) {
            tGroup* group = *it;
            if (!group->RemoveInstance(instanceId)) continue;
            --layer->instanceCount;
            _UpdateLayerGroup(layer, nullptr, group);
            if (group->instances.empty()) {
                _DeleteGroup(layer, it);
            }
            lock60_.Unlock();
            return true;
        }
    }
    lock60_.Unlock();
    return false;
}

// @ 0x1003f18c8: the layer-id argument is ignored by the original (every layer is scanned).
bool cSC4AudioScapeManager::UpdateInstance(std::uint32_t /*layerId*/, std::uint32_t instanceId,
                                           const cS3DVector3& position, float radius) {
    lock60_.Lock();
    for (tLayer* layer : layers_) {
        for (auto groupIt = layer->groups.begin(); groupIt != layer->groups.end(); ++groupIt) {
            tGroup* group = *groupIt;
            for (auto instanceIt = group->instances.begin(); instanceIt != group->instances.end(); ++instanceIt) {
                tInstance* instance = *instanceIt;
                if (instance->id != instanceId) continue;
                const cS3DVector3 previous{instance->x, instance->y, instance->z};
                group->CenterRemovePosition(previous);
                group->CenterAddPosition(position);
                instance->x = position.x;
                instance->y = position.y;
                instance->z = position.z;
                _UpdateLayerGroup(layer, nullptr, group);
                if (Distance(position, group->center) <= radius + group->radius) {
                    lock60_.Unlock();
                    return true;   // still inside this group
                }
                group->instances.erase(instanceIt);
                --group->instanceCount;
                if (group->instances.empty()) {
                    _DeleteGroup(layer, groupIt);
                }
                const bool added = _AddInstanceToCorrectGroup(layer, nullptr, instance, position, radius, true);
                if (!added) std::free(instance);
                lock60_.Unlock();
                return added;
            }
        }
    }
    lock60_.Unlock();
    return true;   // the original reports success even when no instance matched
}

// @ 0x1003f1bc8
bool cSC4AudioScapeManager::RegisterLayerSound(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag,
                                               int arg4, int arg5, int arg6) {
    lock60_.Lock();
    lock60_.Lock();
    tLayer* layer = _FindLayerNoLock(layerId);
    lock60_.Unlock();
    bool result = false;
    if (layer) {
        bool duplicate = false;
        for (const tSoundDef* sound : layer->sounds) {
            if (sound->soundId == soundId) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            auto* sound = new tSoundDef();
            sound->value = kAudioScapeDefaultSoundValue;
            sound->soundId = soundId;
            sound->soundFlag = soundFlag;
            sound->unknown08 = static_cast<std::uint32_t>(arg4);
            sound->minInstances = static_cast<std::uint32_t>(arg5);
            sound->maxInstances = static_cast<std::uint32_t>(arg6);
            layer->sounds.push_back(sound);
            result = true;
        }
    }
    lock60_.Unlock();
    return result;
}

// @ 0x1003f1cfa
bool cSC4AudioScapeManager::UnregisterLayerSound(std::uint32_t layerId, std::uint32_t soundId) {
    lock60_.Lock();
    tLayer* layer = _FindLayerNoLock(layerId);
    lock60_.Unlock();
    if (!layer) return false;
    lock60_.Lock();
    bool removed = false;
    for (auto it = layer->sounds.begin(); it != layer->sounds.end(); ++it) {
        tSoundDef* sound = *it;
        if (sound->soundId != soundId) continue;
        layer->sounds.erase(it);
        delete sound;
        removed = true;
        break;
    }
    lock60_.Unlock();
    return removed;
}

// @ 0x1003f1dd4
bool cSC4AudioScapeManager::OnTick(std::uint32_t /*elapsedMs*/) {
    lock70_.Lock();
    const std::uint32_t elapsed = timer_.GetElapsedTimeU32();
    const std::uint32_t previous = lastTickMs_;
    lastTickMs_ = elapsed;
    const std::uint32_t delta = elapsed - previous;

    // The original walks a pending-group list here (groups with an expiry at +0x58). Nothing in the recovered
    // code ever fills it, so the loop only exists for fidelity.
    for (auto it = pendingGroups_.begin(); it != pendingGroups_.end();) {
        tGroup* group = *it;
        if (!group) {
            lock70_.Unlock();
            return false;
        }
        const std::uint32_t remaining = static_cast<std::uint32_t>(group->pendingExpiry);
        const std::uint32_t consumed = delta < remaining ? delta : remaining;
        group->pendingExpiry = static_cast<std::int32_t>(remaining - consumed);
        if (remaining - consumed == 0) {
            group->StopAllSounds();
            it = pendingGroups_.erase(it);
            delete group;
        } else {
            ++it;
        }
    }

    lock60_.Lock();
    for (tLayer* layer : layers_) {
        for (tGroup* group : layer->groups) {
            if (group->volume[0] == group->targetVolume[0] && group->volume[1] == group->targetVolume[1]) {
                continue;
            }
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const std::int32_t current = static_cast<std::int32_t>(group->volume[channel]);
                const std::int32_t target = static_cast<std::int32_t>(group->targetVolume[channel]);
                const std::int32_t difference = target - current;
                std::int32_t step = difference / kAudioScapeFadeDivisor;
                if (static_cast<std::uint32_t>(difference + 6) < 13) step = difference;   // -6..+6 applied whole
                if (step != 0) group->SetVolume(channel, static_cast<std::uint32_t>(current + step));
            }
        }
    }
    lock60_.Unlock();
    lock70_.Unlock();
    return true;
}

// @ 0x1003f1212
bool cSC4AudioScapeManager::GetGroupPositions(std::uint32_t layerId, cS3DVector3* out, int& count, int& total) {
    lock60_.Lock();
    bool result = false;
    for (tLayer* layer : layers_) {
        if (layer->id != layerId) continue;
        const int groups = layer->groupCount;
        if (groups <= count) {
            count = groups;
            total = layer->instanceCount;
            result = true;
            if (groups != 0) {
                int index = 0;
                for (const tGroup* group : layer->groups) {
                    if (index >= groups) break;
                    out[index++] = group->center;
                }
            }
        }
        break;
    }
    lock60_.Unlock();
    return result;
}

// @ 0x1003f10fa
bool cSC4AudioScapeManager::GetDistributedGroupPositionsAvg(std::uint32_t layerId, SC4RectFloat& rect,
                                                            cS3DVector3* out, int& count, int& total, bool clip) {
    lock60_.Lock();
    lock60_.Lock();
    tLayer* layer = _FindLayerNoLock(layerId);
    lock60_.Unlock();
    bool result = false;
    if (layer) {
        result = _GetDistributedGroupPositionsAvg(layer, rect, out, count, total, clip);
    } else {
        count = 0;
    }
    lock60_.Unlock();
    return result;
}

// @ 0x1003f0a2a: builds a temporary layer whose groups are the (layerA group, layerB group) pairs whose distance is
// below the sum of the two layer radii (each layerA group is used at most once), then bins that temporary layer.
bool cSC4AudioScapeManager::GetDistributedGroupPositionsAvgIntersected(std::uint32_t layerA, std::uint32_t layerB,
                                                                       SC4RectFloat& rect, cS3DVector3* out,
                                                                       int& count, int& total, bool clip) {
    lock60_.Lock();
    lock60_.Lock();
    tLayer* first = _FindLayerNoLock(layerA);
    lock60_.Unlock();
    if (first) {
        lock60_.Lock();
        tLayer* second = _FindLayerNoLock(layerB);
        lock60_.Unlock();
        if (second) {
            tLayer combined;
            combined.radius = first->radius;
            for (tGroup* group : first->groups) group->mark = 0;
            int remaining = first->groupCount;
            if (remaining != 0) {
                for (tGroup* other : second->groups) {
                    for (tGroup* own : first->groups) {
                        if (own->mark != 0) continue;
                        if (Distance(own->center, other->center) >= first->radius + second->radius) continue;
                        auto* created = new tGroup();
                        created->radius = combined.radius;
                        created->center = other->center;
                        combined.groups.push_back(created);
                        ++combined.groupCount;
                        ++combined.instanceCount;
                        ++own->mark;
                        if (--remaining == 0) break;
                    }
                    if (remaining == 0) break;
                }
            }
            const bool result = _GetDistributedGroupPositionsAvg(&combined, rect, out, count, total, clip);
            combined.Clear();
            lock60_.Unlock();
            return result;
        }
    }
    count = 0;
    lock60_.Unlock();
    return false;
}

// @ 0x1003f0da6
bool cSC4AudioScapeManager::_GetDistributedGroupPositionsAvg(tLayer* layer, SC4RectFloat& rect, cS3DVector3* out,
                                                             int& count, int& total, bool clip) {
    lock60_.Lock();
    bool result;
    if (layer->groupCount == 0) {
        count = 0;
        total = 0;
        result = false;
    } else {
        total = layer->instanceCount;
        if (count < layer->groupCount) {
            const int requested = count;
            const int side = requested > 0 ? static_cast<int>(std::sqrt(static_cast<float>(requested))) : 0;
            const std::size_t binCount = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
            if (sortingBins_.size() < binCount) sortingBins_.resize(binCount);
            for (std::size_t i = 0; i < binCount; ++i) sortingBins_[i] = tSortingBin{};
            if (binCount != 0) {
                // The original truncates the bucket size to int before dividing by it.
                const float bucketX =
                    static_cast<float>(TruncateToInt((rect.x1 - rect.x0) / static_cast<float>(side)));
                const float bucketZ =
                    static_cast<float>(TruncateToInt((rect.z1 - rect.z0) / static_cast<float>(side)));
                if (bucketX != 0.0f && bucketZ != 0.0f) {
                    for (const tGroup* group : layer->groups) {
                        const cS3DVector3& position = group->center;
                        if (clip && !InsideRect(rect, position.x, position.z)) continue;
                        const float binX = (position.x - rect.x0) / bucketX;
                        const float binZ = (position.z - rect.z0) / bucketZ;
                        if (!(binX >= 0.0f) || !(binZ >= 0.0f)) continue;   // see the header's safety notes
                        const std::size_t index = static_cast<std::size_t>(TruncateToInt(binZ)) *
                                                      static_cast<std::size_t>(side) +
                                                  static_cast<std::size_t>(TruncateToInt(binX));
                        if (index >= binCount) continue;
                        tSortingBin& bin = sortingBins_[index];
                        bin.x += position.x;
                        bin.y += position.y;
                        bin.z += position.z;
                        ++bin.count;
                    }
                }
            }
            count = 0;
            for (std::size_t i = 0; i < binCount; ++i) {
                const tSortingBin& bin = sortingBins_[i];
                if (bin.count == 0) continue;
                const float scale = 1.0f / static_cast<float>(bin.count);
                out[count].x = bin.x * scale;
                out[count].y = bin.y * scale;
                out[count].z = bin.z * scale;
                ++count;
            }
            result = true;
        } else {
            count = 0;
            for (const tGroup* group : layer->groups) {
                if (clip && !InsideRect(rect, group->center.x, group->center.z)) continue;
                out[count++] = group->center;
            }
            result = true;
        }
    }
    lock60_.Unlock();
    return result;
}

cSC4AudioScapeManager::tLayer* cSC4AudioScapeManager::_FindLayerNoLock(std::uint32_t layerId) {
    for (tLayer* layer : layers_) {
        if (layer->id == layerId) return layer;
    }
    return nullptr;
}

// @ 0x1003f20e0
bool cSC4AudioScapeManager::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cISC4AudioScapeManager || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cISC4AudioScapeManager*>(this);
    } else if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// @ 0x1003f2130
std::uint32_t cSC4AudioScapeManager::AddRef() { return ++refCount_; }

// @ 0x1003f215a: destroys at a count of 0 or 1 without decrementing first (quirk).
std::uint32_t cSC4AudioScapeManager::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// @ 0x1003f27cc
std::uint32_t cSC4AudioScapeManager::GetServiceID() const { return serviceId_; }

// @ 0x1003f27d8
void cSC4AudioScapeManager::SetServiceID(std::uint32_t id) { serviceId_ = id; }

// @ 0x1003f27e4
std::int32_t cSC4AudioScapeManager::GetServicePriority() const { return kAudioScapeServicePriority; }

// @ 0x1003f27f0
bool cSC4AudioScapeManager::IsServiceRunning() const { return serviceRunning_; }

// @ 0x1003f27fc
void cSC4AudioScapeManager::SetServiceRunning(bool running) { serviceRunning_ = running; }

// @ 0x1003f280a
bool cSC4AudioScapeManager::OnIdle(std::uint32_t /*elapsedMs*/) { return false; }

// @ 0x1003f20c8: the original answers whether the message pointer is non-null, i.e. it claims every message.
bool cSC4AudioScapeManager::DoMessage(cIGZMessage2* message) { return message != nullptr; }
