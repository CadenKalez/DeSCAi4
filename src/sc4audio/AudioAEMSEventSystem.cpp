#include "sc4audio/AudioAEMSEventSystem.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "gz/cIGZFile.h"
#include "sc4/AudioAEMSSoundBank.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4audio/AEMSEvents.h"

namespace {

SC4Audio::IAemsEngineHost* g_engineHost = nullptr;

// _ReadTuningValues()'s defaults (kAEMSZoomLevelVolumes @ 0x100a2b070): the AEMS master volume per
// zoom level 0..4. Zoom levels 0..2 are silent, 3 half volume, 4 full volume.
constexpr float kAEMSZoomLevelVolumes[5] = {0.0f, 0.0f, 0.0f, 0.5f, 1.0f};

// x86 cvttss2si: NaN and out-of-range inputs produce the "integer indefinite" value INT32_MIN.
std::int32_t Trunc32(float value) {
    if (!(value > -2147483904.0f && value < 2147483648.0f)) return INT_MIN;
    return static_cast<std::int32_t>(value);
}

// MINSS with 1.0 as the destination and the percentage as the source: the smaller of the two, but a
// NaN source is passed through (the hardware returns the second operand when either is a NaN).
float MinWithOne(float value) {
    if (std::isnan(value)) return value;
    return value < 1.0f ? value : 1.0f;
}

// The three SetParam calls share this: 0..1 -> 0..127, above 1 clamps to 127, below 0 becomes 0.
std::int32_t PercentageToMidi(sc4::SC4Percentage percentage) {
    std::int32_t value = Trunc32(MinWithOne(percentage.value) * 127.0f + 0.0f);
    if (percentage.value < 0.0f) value = 0;
    return value;
}

}  // namespace

namespace SC4Audio {

IAemsEngineHost* AemsEngineHost() { return g_engineHost; }
void SetAemsEngineHost(IAemsEngineHost* host) { g_engineHost = host; }

}  // namespace SC4Audio

bool cSC4AudioAEMSEventSystem::sbInitialized = false;

// ---------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem() @ 0x1007b56a0 (the base-object constructor at 0x1007b58ec only forwards
// here). The vtable pointers, the service base and the members are installed by the compiler; the
// original then clears the three vectors, sets the tuning defaults (+0x170..+0x178), constructs the
// response curve, the bank list and the two volume scales (+0x1d0/+0x1d4), and assigns the
// 21-entry event-state vector.
cSC4AudioAEMSEventSystem::cSC4AudioAEMSEventSystem()
    : cRZSystemService(kServiceId, kServicePriority) {
    eventStates_.assign(0x15, 0);   // 21 event slots, all "not playing"
}

// ~cSC4AudioAEMSEventSystem() @ 0x1007b58f6. Frees the zoom-volume vector, clears the bank list
// (deleting the nodes but NOT releasing the banks - Shutdown does that: quirk kept), then frees the
// system blob, the curve, the engine memory and the event-state vector, and destroys the service
// base. The members are destroyed in the same reverse order by the compiler here.
cSC4AudioAEMSEventSystem::~cSC4AudioAEMSEventSystem() = default;

// ---------------------------------------------------------------------------------------------
// COM
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::QueryInterface @ 0x1007b6694. The class answers four views: 0xbb523bb2
// (cIGZSndEventHandler, the object itself), 0x8c1faeb7 (cISC4AEMSEvents, +8) and 0x452294aa
// (cIGZMessageTarget2, +0x10) with a reference, and everything else through cRZSystemService, which
// answers iid 1 with its cRZUnknown subobject (+0x20) and 0x287fb697 with the service view (+0x18).
bool cSC4AudioAEMSEventSystem::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cISC4AEMSEvents) {
        *out = static_cast<cISC4AEMSEvents*>(this);
        AddRef();
        return true;
    }
    if (iid == 0x452294aa) {   // cIGZMessageTarget2
        *out = static_cast<cIGZMessageTarget2*>(this);
        AddRef();
        return true;
    }
    if (iid == 0xbb523bb2) {   // cIGZSndEventHandler
        *out = static_cast<cIGZSndEventHandler*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

std::uint32_t cSC4AudioAEMSEventSystem::AddRef() { return cRZSystemService::AddRef(); }        // @ 0x1007b6a22
std::uint32_t cSC4AudioAEMSEventSystem::Release() { return cRZSystemService::Release(); }      // @ 0x1007b6a30

// ---------------------------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::DoEvent @ 0x1007b65ca. The only event handled here is the sound
// manager's SetMasterVolume (0x2a5b69c0): its data1 is the master volume (0..1024), which scales the
// AEMS master volume. The return value is always false, even when the event was consumed (quirk).
bool cSC4AudioAEMSEventSystem::DoEvent(cIGZSndEventStd* event) {
    if (!event) return false;
    if (event->GetEventId() == 0x2a5b69c0) {
        if (cIGZMessage2Standard* message = event->AsMessage2Standard()) {
            masterVolumeScale_ = static_cast<float>(message->GetData1()) * (1.0f / 1024.0f);
            UpdateVolume();
        }
    }
    return false;
}

// cSC4AudioAEMSEventSystem::DoMessage @ 0x1007b663a. The zoom-changed message (0xa6b79602) copies
// the per-zoom-level volume into +0x1d4 and re-applies the master volume.
bool cSC4AudioAEMSEventSystem::DoMessage(cIGZMessage2* message) {
    if (!message) return false;
    if (message->GetType() != 0xa6b79602) return false;
    // The original dereferences SL::spViewUtilities and indexes the table unchecked; the port skips
    // a missing viewport (zoom 0) and an out-of-range level (the slot is left as it is).
    SC4Audio::cIViewport* view = SC4Audio::ViewUtilities();
    const std::int32_t zoom = view ? view->GetZoomLevel() : 0;
    if (zoom >= 0 && static_cast<std::size_t>(zoom) < zoomLevelVolumes_.size()) {
        zoomVolumeScale_ = zoomLevelVolumes_[static_cast<std::size_t>(zoom)];
    }
    UpdateVolume();
    return true;
}

// ---------------------------------------------------------------------------------------------
// sound banks and the system blob
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::LoadSoundBank @ 0x1007b670e: read the bank resource instance id from the
// caller's exemplar (property `propertyId`, default 0xebf44df7), stop every event, then load the
// bank resource {type 0, group 0x8a5971c5, instance id}.
void cSC4AudioAEMSEventSystem::LoadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) {
    std::uint32_t instance = 0xebf44df7;   // the value the local starts with (also the fallback)
    if (holder) sc4::cSCPropertyHelp::GetPropertyValue(holder, propertyId, instance);
    StopAllSounds();
    std::fill(eventStates_.begin(), eventStates_.end(), 0);
    cGZPersistResourceKey key{0, 0x8a5971c5, instance};
    LoadBankFromResource(key);
}

// cSC4AudioAEMSEventSystem::UnloadSoundBank @ 0x1007b6798: same property read, then stop every event
// and remove the bank with that resource instance id.
void cSC4AudioAEMSEventSystem::UnloadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) {
    std::uint32_t instance = 0xebf44df7;
    if (holder) sc4::cSCPropertyHelp::GetPropertyValue(holder, propertyId, instance);
    StopAllSounds();
    std::fill(eventStates_.begin(), eventStates_.end(), 0);
    RemoveBank(instance);
}

// cSC4AudioAEMSEventSystem::LoadFromResource @ 0x1007b5f64: open the resource, read it into the
// system blob, close the file (its Close result is the return value) and release it.
bool cSC4AudioAEMSEventSystem::LoadFromResource(cGZPersistResourceKey& key) {
    cIGZFile* file = nullptr;
    bool result = false;
    if (sc4::SoundBankHost() && sc4::SoundBankHost()->GetResourceAsFile(key, &file, false) && file) {
        if (LoadFromFile(file)) {
            result = file->Close();
        } else {
            file->Close();
        }
    }
    if (file) file->Release();
    return result;
}

// cSC4AudioAEMSEventSystem::LoadFromFile @ 0x1007b6516: the file's whole length becomes the system
// blob (shrinking or growing the vector), then the engine is told to replace its system.
bool cSC4AudioAEMSEventSystem::LoadFromFile(cIGZFile* file) {
    if (!file) return false;
    const std::uint32_t length = file->Length();
    systemData_.resize(length);
    // The original calls Length() a second time for the Read count; the port reuses the value so a
    // file that grew between the two calls cannot overflow the buffer.
    if (!file->Read(systemData_.data(), length)) return false;
    SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost();
    if (!engine) return false;   // no engine: nothing can accept the system
    engine->SndSysEnterCritical();
    engine->SndAemsRemoveSystem();
    const int added = engine->SndAemsAddSystem(systemData_.data());
    engine->SndSysLeaveCritical();
    return added == 0;
}

// cSC4AudioAEMSEventSystem::LoadBankFromResource @ 0x1007b62e8: create a bank, give it the caller's
// reference, load the resource into it and add it; the temporary reference is dropped at the end.
bool cSC4AudioAEMSEventSystem::LoadBankFromResource(cGZPersistResourceKey& key) {
    sc4::cSC4AudioAEMSSoundBank* bank = new sc4::cSC4AudioAEMSSoundBank();
    bank->AddRef();
    bool result = false;
    if (bank->LoadFromResource(key)) result = AddBank(bank);
    bank->Release();
    return result;
}

// cSC4AudioAEMSEventSystem::AddBank @ 0x1007b6378: hand the bank's bytes to the engine as a module
// bank and append it to the list with one reference. The engine's status is the only success test;
// the module-bank handle is always set to 0 (quirk kept).
bool cSC4AudioAEMSEventSystem::AddBank(sc4::cSC4AudioAEMSSoundBank* bank) {
    if (!bank) return false;
    SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost();
    if (!engine) return false;
    const int added = engine->SndAemsAddModuleBank(bank->Data(), 0, 0, &sc4::cSC4AudioAEMSSoundBank::modulebankmalloc);
    if (added != 0) return false;
    bank->SetModuleBankHandle(0);
    bank->AddRef();
    banks_.push_back(bank);
    return true;
}

// cSC4AudioAEMSEventSystem::RemoveBank @ 0x1007b6414: find the bank whose key instance is
// `resourceInstance` (front to back), remove its module bank and drop both the list's reference and
// the temporary one taken for the search.
bool cSC4AudioAEMSEventSystem::RemoveBank(std::uint32_t resourceInstance) {
    for (auto it = banks_.begin(); it != banks_.end(); ++it) {
        sc4::cSC4AudioAEMSSoundBank* bank = *it;
        if (!bank) continue;
        bank->AddRef();
        cGZPersistResourceKey key{};
        bank->GetKey(key);
        SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost();
        if (key.instance == resourceInstance) {
            if (engine) engine->SndAemsRemoveModuleBank(bank->GetModuleBankHandle());
            bank->Release();
            banks_.erase(it);
            bank->Release();
            return true;
        }
        bank->Release();
    }
    return false;
}

// cSC4AudioAEMSEventSystem::RemoveAllBanks @ 0x1007b621c: pop banks from the front, remove each
// module bank and drop the list's reference plus the temporary one.
void cSC4AudioAEMSEventSystem::RemoveAllBanks() {
    while (!banks_.empty()) {
        sc4::cSC4AudioAEMSSoundBank* bank = banks_.front();
        if (bank) bank->AddRef();
        banks_.pop_front();
        if (!bank) continue;
        SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost();
        if (engine) engine->SndAemsRemoveModuleBank(bank->GetModuleBankHandle());
        bank->Release();
        bank->Release();
    }
}

// ---------------------------------------------------------------------------------------------
// engine lifetime
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::Init @ 0x1007b5a44. One process-wide flag guards the whole engine: the
// first call resets the event slots, resets the three event parameters to 0, takes the player's
// audio volume from the game options (volume / 1024, 0 when sound is off), reads the tuning
// exemplar, allocates the 200,000-byte engine block, initialises SNDSYS and the effects bus
// (flags 0x72b00, bus 0, volume +0x174, flags +0x178), applies the volume, registers the event
// handler with the sound system, loads the system blob and subscribes to the zoom-changed message.
// Any engine failure calls Shutdown and returns false.
bool cSC4AudioAEMSEventSystem::Init() {
    if (sbInitialized) return true;
    sbInitialized = true;

    std::fill(eventStates_.begin(), eventStates_.end(), 0);
    SetParamVelocity(sc4::SC4Percentage(0.0f));
    SetParamThrottle(sc4::SC4Percentage(0.0f));
    SetParamWobble(sc4::SC4Percentage(0.0f));

    float volumeScale = 0.0f;
    if (SC4Audio::cIGameAudioContext* game = SC4Audio::GetServices().game) {
        const SC4Audio::AudioPreferences preferences = game->GetAudioPreferences();
        if (preferences.soundEnabled) {
            volumeScale = static_cast<float>(static_cast<std::int32_t>(preferences.masterVolume)) * (1.0f / 1024.0f);
        }
    }
    masterVolumeScale_ = volumeScale;   // the original dereferences SL::spSC4; no game means silence
    ReadTuningValues();

    soundSystemMemory_.resize(200000);   // 0x30d40 bytes, exactly

    SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost();
    bool ok = engine != nullptr;
    if (ok) {
        ok = engine->SndSysGetOpts(sndSysOptions_.data()) >= 0 &&
             engine->SndSysSetOpts(sndSysOptions_.data()) >= 0 &&
             engine->SndSysInit(soundSystemMemory_.data(), static_cast<std::int32_t>(soundSystemMemory_.size()),
                                0x72b00) >= 0 &&
             engine->SndFxInitBus(0, Trunc32(tuningBusVolume_), static_cast<std::int32_t>(tuningBusFlags_), 0, 0) >= 0;
    }
    if (!ok) {
        Shutdown();   // virtual call in the original; it clears the flag and unwinds the engine
        return false;
    }

    UpdateVolume();

    if (cIGZSndSys* soundSystem = SC4Audio::GetServices().soundSystem) {
        soundSystem->RegisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
    }

    cGZPersistResourceKey systemKey{0, 0x8a5971c5, 0x6b955c37};
    LoadFromResource(systemKey);

    if (cIGZMessageServer2* server = SC4Audio::GetServices().messageServer) {
        server->AddNotification(static_cast<cIGZMessageTarget2*>(this), 0xa6b79602);
    }
    return true;
}

// cSC4AudioAEMSEventSystem::Shutdown @ 0x1007b5ff6: stop every event, clear the slots, remove the
// banks, tear the system down (under the sound-system critical section), restore SNDSYS, empty the
// engine memory, unregister the event handler and the message subscription.
bool cSC4AudioAEMSEventSystem::Shutdown() {
    if (!sbInitialized) return true;
    sbInitialized = false;

    StopAllSounds();
    std::fill(eventStates_.begin(), eventStates_.end(), 0);
    RemoveAllBanks();

    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        engine->SndAemsRemoveSystem();
        engine->SndSysLeaveCritical();
        engine->SndSysRestore();
    }

    soundSystemMemory_.clear();   // end = begin; the storage is kept

    if (cIGZSndSys* soundSystem = SC4Audio::GetServices().soundSystem) {
        soundSystem->UnregisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
    }
    if (cIGZMessageServer2* server = SC4Audio::GetServices().messageServer) {
        server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), 0xa6b79602);
    }
    return true;
}

// cSC4AudioAEMSEventSystem::StopAllSounds @ 0x1007b6106: end the events in the original's fixed
// order. Slots 0x0f and 0x10 are skipped (quirk kept), and 0x13 is ended last (the original
// tail-calls it).
void cSC4AudioAEMSEventSystem::StopAllSounds() {
    static constexpr std::uint32_t kSlots[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                               0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x11, 0x12, 0x14, 0x13};
    for (std::uint32_t slot : kSlots) EndSound(static_cast<AEMSEventType>(slot));
}

// ---------------------------------------------------------------------------------------------
// tuning / volume
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::ReadTuningValues @ 0x1007b5c5c. The original formats an error and drops
// it when the exemplar is missing (and then leaves the curve empty, so the master volume is 0).
// Properties: 0xcbfb03fa volume, 0xcbfb03ff bus volume, 0xebfb0404 bus flags, 0x4bfdd955 the volume
// response curve (interleaved x,y floats; the default is a straight (0,0)-(1,1) line) and 0x8c376d4f
// the five per-zoom-level volumes.
bool cSC4AudioAEMSEventSystem::ReadTuningValues() {
    const cGZPersistResourceKey key{0x6534284a, 0xca25875d, 0x6a359c5c};   // audio tuning exemplar
    SC4Audio::cIExemplarSource* exemplars = SC4Audio::GetServices().exemplars;
    if (!exemplars || !exemplars->ExemplarExists(key)) return false;

    exemplars->GetFloatProperty(key, 0xcbfb03fa, tuningVolume_);
    exemplars->GetFloatProperty(key, 0xcbfb03ff, tuningBusVolume_);
    exemplars->GetUint32Property(key, 0xebfb0404, tuningBusFlags_);

    responseCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {1.0f, 1.0f}});
    {
        std::vector<float> values;
        if (exemplars->GetFloatArrayProperty(key, 0x4bfdd955, values)) {
            responseCurve_.SetResponseCurveData(values.data(), values.size());
        }
    }

    zoomLevelVolumes_.clear();
    for (float value : kAEMSZoomLevelVolumes) zoomLevelVolumes_.push_back(value);
    exemplars->GetFloatArrayProperty(key, 0x8c376d4f, zoomLevelVolumes_);
    return true;
}

// cSC4AudioAEMSEventSystem::UpdateVolume @ 0x1007b5f2a:
// curve(master volume * zoom volume * tuning volume) * 127 -> _SNDmastervol.
void cSC4AudioAEMSEventSystem::UpdateVolume() {
    const float output = responseCurve_.GetOutputValue(masterVolumeScale_ * zoomVolumeScale_ * tuningVolume_);
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndMasterVol(Trunc32(output * 127.0f));
    }
}

// ---------------------------------------------------------------------------------------------
// events (start / end) and the three global parameters
// ---------------------------------------------------------------------------------------------

// cSC4AudioAEMSEventSystem::StartSound @ 0x1007b680a: begin the event unless its slot is busy. The
// slot stores the engine handle + 1 (0 means not playing, so a refused begin leaves it 0); the call
// reports true even when the engine refused (quirk kept).
bool cSC4AudioAEMSEventSystem::StartSound(AEMSEventType event) {
    const std::int32_t index = static_cast<std::int32_t>(event);
    // The original indexes the state vector unchecked; the port ignores an impossible index.
    if (index < 0 || static_cast<std::size_t>(index) >= eventStates_.size()) return false;
    if (eventStates_[static_cast<std::size_t>(index)] != 0) return false;
    const std::int32_t id = static_cast<std::int32_t>(event);
    std::int32_t handle = -1;
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        handle = engine->SndAemsBeginEvent(&id);
        engine->SndSysLeaveCritical();
    }
    eventStates_[static_cast<std::size_t>(index)] =
        handle < 0 ? 0 : static_cast<std::int32_t>(static_cast<std::uint32_t>(handle) + 1u);
    return true;
}

// cSC4AudioAEMSEventSystem::EndSound @ 0x1007b687e: end the event when its slot is busy.
bool cSC4AudioAEMSEventSystem::EndSound(AEMSEventType event) {
    const std::int32_t index = static_cast<std::int32_t>(event);
    if (index < 0 || static_cast<std::size_t>(index) >= eventStates_.size()) return false;
    const std::int32_t state = eventStates_[static_cast<std::size_t>(index)];
    if (state == 0) return false;
    const std::int32_t id = static_cast<std::int32_t>(event);
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        engine->SndAemsEndEvent(state - 1, &id);
        engine->SndSysLeaveCritical();
    }
    eventStates_[static_cast<std::size_t>(index)] = 0;
    return true;
}

// cSC4AudioAEMSEventSystem::SetParamVelocity @ 0x1007b68ee (global 0)
bool cSC4AudioAEMSEventSystem::SetParamVelocity(sc4::SC4Percentage percentage) {
    const std::int32_t value = PercentageToMidi(percentage);
    int result = -1;
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        result = engine->SndAemsSetGlobal(0, value);
        engine->SndSysLeaveCritical();
    }
    return result >= 0;
}

// cSC4AudioAEMSEventSystem::SetParamThrottle @ 0x1007b694a (global 1)
bool cSC4AudioAEMSEventSystem::SetParamThrottle(sc4::SC4Percentage percentage) {
    const std::int32_t value = PercentageToMidi(percentage);
    int result = -1;
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        result = engine->SndAemsSetGlobal(1, value);
        engine->SndSysLeaveCritical();
    }
    return result >= 0;
}

// cSC4AudioAEMSEventSystem::SetParamWobble @ 0x1007b69aa (global 2)
bool cSC4AudioAEMSEventSystem::SetParamWobble(sc4::SC4Percentage percentage) {
    const std::int32_t value = PercentageToMidi(percentage);
    int result = -1;
    if (SC4Audio::IAemsEngineHost* engine = SC4Audio::AemsEngineHost()) {
        engine->SndSysEnterCritical();
        result = engine->SndAemsSetGlobal(2, value);
        engine->SndSysLeaveCritical();
    }
    return result >= 0;
}
