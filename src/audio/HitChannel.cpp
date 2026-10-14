#include "audio/HitChannel.h"

#include "audio/HitClassIds.h"
#include "gz/cGZAny.h"

namespace {

using nsHitReg::GetPropertyIdFromLegacyRegisterId;

constexpr std::uint32_t kIidSnd = 0x206077d2;
constexpr std::uint32_t kIidSnd3D = 0x306077d2;
constexpr std::uint32_t kIidUpdateTarget = 0x5b34f0af;
constexpr std::uint32_t kIidRegisterSet = 0xfd584467;
constexpr std::uint32_t kIidViewTarget = 0xaa11a64f;
constexpr std::uint32_t kIidDataSourceHolder = 0x09d002d2;
constexpr std::uint32_t kIidRolloffEffects = 0x6b0446eb;   // snd interface with Set/GetRolloffFactor
constexpr std::uint32_t kIidRendererEffects = 0x4b79c1a9;  // cIGZSndEaRendererEffects

// Register ids that have behaviour attached (see HandleNotifySetPropertyValue).
constexpr std::uint32_t kRegParentPropertySet = 0xff4c97be;
constexpr std::uint32_t kRegPitchMultiplier = 0xff3071b9;
constexpr std::uint32_t kRegVolumeScale = 0xff53dbf3;
constexpr std::uint32_t kRegPause = 0x18;  // legacy id used by Pause/Unpause/IsPaused (not translated!)

std::uint32_t TranslateLegacy(std::int32_t id) {
    const auto uid = static_cast<std::uint32_t>(id);
    if (uid < 0x100) {
        const std::uint32_t mapped = GetPropertyIdFromLegacyRegisterId(uid);
        if (mapped != 0) return mapped;
    }
    return uid;
}

// Calls fn(extension interface) when the underlying sound exposes it; returns false otherwise.
template <class Iface, class F>
bool WithInterface(cIGZSnd* snd, std::uint32_t iid, F&& fn) {
    if (snd == nullptr) return false;
    void* p = nullptr;
    if (!snd->QueryInterface(iid, &p) || p == nullptr) return false;
    auto* iface = static_cast<Iface*>(p);
    fn(*iface);
    iface->Release();
    return true;
}

}  // namespace

// Effects interface 0x4b79c1a9 of the underlying sound, slots as called by the setters below.
class cIGZSndRendererEffects : public cIGZUnknown {
public:
    virtual bool SetInputDryLevel(float) = 0;      // [3] +0x18
    virtual bool SetInputEffectLevel(float) = 0;   // [4] +0x20
    virtual bool SetLowPassCutoff(std::int32_t) = 0;   // [5] +0x28
    virtual bool SetHighPassCutoff(std::int32_t) = 0;  // [6] +0x30
    virtual bool SetTimeMultiplier(float) = 0;     // [7] +0x38
};
// Rolloff interface 0x6b0446eb.
class cIGZSndRolloff : public cIGZUnknown {
public:
    virtual void Pad0() = 0;  // [3]..[5] not called by the channel
    virtual void Pad1() = 0;
    virtual void Pad2() = 0;
    virtual void SetRolloffFactor(float) = 0;      // [6] +0x30
    virtual void GetRolloffFactor(float*) const = 0;  // [7] +0x38
};

// ---- construction / interfaces ------------------------------------------------------------------

cHitChannel::cHitChannel() {
    auto* own = new cGZPropertySet();
    layers_.emplace_back(own);  // the list takes the only long-lived reference
}

cHitChannel::~cHitChannel() {
    if (snd_) {
        snd_->Shutdown();  // slot +0x20 of the underlying sound (cIGZSnd::Shutdown)
        snd_ = nullptr;
    }
    layers_.clear();
}

cHitChannel& cHitChannel::operator=(const cHitChannel& other) {
    unusedFlag_ = other.unusedFlag_;
    refCount_ = other.refCount_;
    snd_ = other.snd_;
    if (this != &other) {
        holder_ = other.holder_;
        dataSource_ = other.dataSource_;
        layers_ = other.layers_;
    }
    return *this;
}

bool cHitChannel::QueryInterface(std::uint32_t iid, void** out) {
    switch (iid) {
        case kIidSnd: *out = static_cast<cIGZSnd*>(this); break;
        case kIidSnd3D: *out = static_cast<cIGZSnd3D*>(this); break;
        case kIidUpdateTarget: *out = static_cast<cIGZSndUpdateTarget*>(this); break;
        case kIidRegisterSet: *out = static_cast<cIGZSndRegisterSet*>(this); break;
        case kIidViewTarget: *out = static_cast<cIGZSndViewUpdateTarget*>(this); break;
        case kIidDataSourceHolder: *out = static_cast<cIGZSndDataSourceHolder*>(this); break;
        default: return false;
    }
    AddRef();
    return true;
}

// The original destroys the object when the count is below 2.
std::uint32_t cHitChannel::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// ---- registers ----------------------------------------------------------------------------------

bool cHitChannel::SetRegisterValue(std::int32_t id, std::int32_t value, bool suppress) {  // @ 0x100484176
    const std::uint32_t key = TranslateLegacy(id);
    layers_.front()->SetValue(key, AnyUint32(static_cast<std::uint32_t>(value)));
    return suppress ? true : HandleNotifySetPropertyValue(static_cast<std::int32_t>(key));
}

bool cHitChannel::SetRegisterFloat(std::uint32_t id, float value, bool suppress) {  // @ 0x100484206
    const std::uint32_t key = TranslateLegacy(static_cast<std::int32_t>(id));
    layers_.front()->SetValue(key, AnyFloat32(value));
    return suppress ? true : HandleNotifySetPropertyValue(static_cast<std::int32_t>(key));
}

// Searches the layers front to back; a stored value of another type counts as missing.
std::int32_t cHitChannel::GetRegisterValue(std::int32_t id) const {  // @ 0x1004842a0
    const std::uint32_t key = TranslateLegacy(id);
    AnyUint32 any;
    for (const auto& layer : layers_) {
        if (layer && layer->Value(key, &any)) return static_cast<std::int32_t>(any.get());
    }
    return 0;
}

float cHitChannel::GetRegisterFloat(std::uint32_t id) const {  // @ 0x10048434c
    const std::uint32_t key = TranslateLegacy(static_cast<std::int32_t>(id));
    AnyFloat32 any;
    for (const auto& layer : layers_) {
        if (layer && layer->Value(key, &any)) return any.get();
    }
    return 0.0f;
}

// Binary quirk: the legacy-id translation result is computed and dropped, so legacy numbers
// (< 0x100) clear the *untranslated* key. Only the own layer is touched.
void cHitChannel::ClearRegisterValue(std::int32_t id, bool) {  // @ 0x1004843fe
    layers_.front()->ClearValue(static_cast<std::uint32_t>(id));
}

bool cHitChannel::HasRegisterValue(std::int32_t id) const {  // @ 0x100484456 (own layer only)
    return layers_.front()->HasValue(TranslateLegacy(id));
}

// ---- simple register-backed accessors -------------------------------------------------------------

std::int32_t cHitChannel::GetVolume() const { return GetRegisterValue(nsHitReg::kPropVolume); }
bool cHitChannel::SetVolume(std::int32_t v) { SetRegisterValue(nsHitReg::kPropVolume, v, false); return true; }
std::int32_t cHitChannel::GetPan() const { return GetRegisterValue(nsHitReg::kPropPan); }
bool cHitChannel::SetPan(std::int32_t v) { SetRegisterValue(nsHitReg::kPropPan, v, false); return true; }
std::int32_t cHitChannel::GetFrequency() const { return GetRegisterValue(nsHitReg::kPropFrequency); }
bool cHitChannel::SetFrequency(std::int32_t v) { SetRegisterValue(nsHitReg::kPropFrequency, v, false); return true; }
std::int32_t cHitChannel::GetOriginalFrequency() const { return snd_ ? snd_->GetOriginalFrequency() : 0; }
std::uint32_t cHitChannel::GetID() const { return snd_ ? snd_->GetID() : 0; }
void cHitChannel::SetID(std::uint32_t id) { if (snd_) snd_->SetID(id); }
bool cHitChannel::Pause(bool) { SetRegisterValue(kRegPause, 1, false); return true; }
bool cHitChannel::Unpause() { SetRegisterValue(kRegPause, 0, false); return true; }
bool cHitChannel::IsPaused() const { return GetRegisterValue(kRegPause) != 0; }
bool cHitChannel::IsLoaded() const { return snd_ && snd_->IsLoaded(); }

bool cHitChannel::GetPosition(float* x, float* y, float* z) const {  // @ 0x100483b78
    if (x) *x = RegisterAsFloat(nsHitReg::kPropPositionX);
    if (y) *y = RegisterAsFloat(nsHitReg::kPropPositionY);
    if (z) *z = RegisterAsFloat(nsHitReg::kPropPositionZ);
    return true;
}
bool cHitChannel::SetPosition(float x, float y, float z) {  // @ 0x100483c0a (float -> int by truncation)
    SetRegisterValue(nsHitReg::kPropPositionX, static_cast<std::int32_t>(x), false);
    SetRegisterValue(nsHitReg::kPropPositionY, static_cast<std::int32_t>(y), false);
    SetRegisterValue(nsHitReg::kPropPositionZ, static_cast<std::int32_t>(z), false);
    return true;
}
bool cHitChannel::GetVelocity(float* x, float* y, float* z) const {  // @ 0x100483cdc
    if (x) *x = RegisterAsFloat(nsHitReg::kPropVelocityX);
    if (y) *y = RegisterAsFloat(nsHitReg::kPropVelocityY);
    if (z) *z = RegisterAsFloat(nsHitReg::kPropVelocityZ);
    return true;
}
bool cHitChannel::SetVelocity(float x, float y, float z) {  // @ 0x100483d6e
    SetRegisterValue(nsHitReg::kPropVelocityX, static_cast<std::int32_t>(x), false);
    SetRegisterValue(nsHitReg::kPropVelocityY, static_cast<std::int32_t>(y), false);
    SetRegisterValue(nsHitReg::kPropVelocityZ, static_cast<std::int32_t>(z), false);
    return true;
}
bool cHitChannel::GetAll(float* px, float* py, float* pz, float* vx, float* vy, float* vz) const {  // @ 0x100483e40
    GetVelocity(vx, vy, vz);
    GetPosition(px, py, pz);
    return true;
}
// Binary bug kept: the position-z register is written from `vz`, not `pz`.
bool cHitChannel::SetAll(float px, float py, float pz, float vx, float vy, float vz) {  // @ 0x100483f52
    (void)pz;
    SetRegisterValue(nsHitReg::kPropVelocityX, static_cast<std::int32_t>(vx), false);
    SetRegisterValue(nsHitReg::kPropVelocityY, static_cast<std::int32_t>(vy), false);
    SetRegisterValue(nsHitReg::kPropVelocityZ, static_cast<std::int32_t>(vz), false);
    SetRegisterValue(nsHitReg::kPropPositionX, static_cast<std::int32_t>(px), false);
    SetRegisterValue(nsHitReg::kPropPositionY, static_cast<std::int32_t>(py), false);
    SetRegisterValue(nsHitReg::kPropPositionZ, static_cast<std::int32_t>(vz), false);
    return true;
}
// Binary quirk: the getters ignore their output pointer and return the register (low byte).
bool cHitChannel::GetMinimumDistance(float*) const {  // @ 0x100484020
    return (GetRegisterValue(nsHitReg::kPropMinimumDistance) & 0xff) != 0;
}
bool cHitChannel::SetMinimumDistance(float d) {  // @ 0x100484052
    return SetRegisterValue(nsHitReg::kPropMinimumDistance, static_cast<std::int32_t>(d), false);
}
bool cHitChannel::GetMaximumDistance(float*) const {  // @ 0x100484090
    return (GetRegisterValue(nsHitReg::kPropMaximumDistance) & 0xff) != 0;
}
bool cHitChannel::SetMaximumDistance(float d) {  // @ 0x1004840c2
    return SetRegisterValue(nsHitReg::kPropMaximumDistance, static_cast<std::int32_t>(d), false);
}
bool cHitChannel::SetHeadRelative(bool v) {  // @ 0x100484100
    return SetRegisterValue(nsHitReg::kPropHeadRelative, v ? 1 : 0, false);
}
bool cHitChannel::GetHeadRelative() const { return GetRegisterValue(nsHitReg::kPropHeadRelative) != 0; }

// ---- data source -----------------------------------------------------------------------------------

bool cHitChannel::SetDataSource(cIGZSndDataSource* source, cIGZSnd::SndStreamingType) {  // @ 0x100483414
    dataSource_ = source;
    return true;
}

cIGZSndDataSource* cHitChannel::GetDataSource() {  // @ 0x100484978: asks the underlying sound's holder
    return holder_ ? holder_->GetDataSource() : nullptr;
}

// ---- the underlying sound ----------------------------------------------------------------------------

// CreateSnd @ 0x1004834a0: picks the default 2D or 3D player class from the sound manager and
// instantiates it; for 3D it applies head-relative and the distance registers.
bool cHitChannel::CreateSnd() {
    Hit::cIHitManServices* services = Hit::GetServices();
    const bool positional = GetRegisterValue(nsHitReg::kPropUse3DPositioning) != 0;
    std::uint32_t classId = 0;
    if (services != nullptr) services->GetDefaultSndPlayerClassId(positional, classId);

    snd_ = nullptr;
    void* created = nullptr;
    if (services != nullptr && services->CreateComInstance(classId, kIidSnd, &created) && created != nullptr) {
        snd_ = static_cast<cIGZSnd*>(created);
        snd_->Release();  // CreateComInstance's reference; snd_ now owns one
        snd_->Init();
        void* holder = nullptr;
        holder_ = nullptr;
        if (snd_->QueryInterface(kIidDataSourceHolder, &holder) && holder != nullptr) {
            holder_ = static_cast<cIGZSndDataSourceHolder*>(holder);
            holder_->Release();
        }
    }
    if (positional && snd_) {
        WithInterface<cIGZSnd3D>(snd_.get(), kIidSnd3D, [this](cIGZSnd3D& s) {
            s.SetHeadRelative(GetRegisterValue(nsHitReg::kPropHeadRelative) != 0);
            if (const std::int32_t mn = GetRegisterValue(nsHitReg::kPropMinimumDistance); mn != 0)
                s.SetMinimumDistance(static_cast<float>(mn));
            if (const std::int32_t mx = GetRegisterValue(nsHitReg::kPropMaximumDistance); mx != 0)
                s.SetMaximumDistance(static_cast<float>(mx));
        });
    }
    return static_cast<bool>(holder_);
}

// UpdatePatch @ 0x100483072
bool cHitChannel::UpdatePatch() {
    if (!snd_) CreateSnd();
    if (holder_) holder_->SetDataSource(dataSource_.get(), 2);  // the original dereferences unconditionally
    UpdateVolPan();
    UpdatePitch();
    return true;
}

bool cHitChannel::Load() {  // @ 0x1004830be
    UpdatePatch();
    if (!snd_) return true;
    const bool loaded = snd_->Load();
    UpdatePatch();
    UpdatePitch();
    return loaded;
}

bool cHitChannel::Unload() {  // @ 0x100483100
    if (snd_) snd_->Unload();
    return true;
}

bool cHitChannel::NoteOn() {  // @ 0x100483014
    UpdatePatch();
    if (!snd_) return true;
    if (!Load()) return false;
    const bool loop = GetRegisterValue(nsHitReg::kPropLoopFlag) != 0;
    snd_->PlayWithVolumeAndLoop(volume_, loop, 1);
    return true;
}

bool cHitChannel::NoteKill() {  // @ 0x1004849a2
    if (snd_) snd_->Kill();
    return true;
}

bool cHitChannel::NoteIsPlaying() const { return snd_ && snd_->IsPlaying(); }  // @ 0x1004849ba

// UpdatePitch @ 0x100483118. Returns the inverse of SetFrequency's result when a frequency
// register exists (as compiled); with no register it records the sound's own frequency.
bool cHitChannel::UpdatePitch() {
    if (!snd_ || !snd_->IsLoaded()) return false;
    const float multiplier = HasRegisterValue(static_cast<std::int32_t>(kRegPitchMultiplier))
                                 ? GetRegisterFloat(kRegPitchMultiplier)
                                 : 1.0f;
    if (HasRegisterValue(static_cast<std::int32_t>(nsHitReg::kPropFrequency))) {
        const std::int32_t base = GetRegisterValue(nsHitReg::kPropFrequency);
        return !snd_->SetFrequency(static_cast<std::int32_t>(multiplier * static_cast<float>(base)));
    }
    const std::int32_t own = snd_->GetFrequency();
    SetRegisterValue(nsHitReg::kPropFrequency, static_cast<std::int32_t>(multiplier * static_cast<float>(own)), true);
    return true;
}

// UpdateVolPan @ 0x100483200: 2D sounds get the pan register; 3D sounds get distances, position
// and velocity. The effective volume is volume * volumeScale / 1024 (truncating).
bool cHitChannel::UpdateVolPan() {
    if (!snd_) return true;
    if (GetRegisterValue(nsHitReg::kPropUse3DPositioning) == 0) {
        snd_->SetPan(GetRegisterValue(nsHitReg::kPropPan));
    } else {
        WithInterface<cIGZSnd3D>(snd_.get(), kIidSnd3D, [this](cIGZSnd3D& s) {
            s.SetMinimumDistance(RegisterAsFloat(nsHitReg::kPropMinimumDistance));
            const std::int32_t mx = GetRegisterValue(nsHitReg::kPropMaximumDistance);
            s.SetMaximumDistance(mx == 0 ? 100000.0f : static_cast<float>(mx));
            s.SetPosition(RegisterAsFloat(nsHitReg::kPropPositionX), RegisterAsFloat(nsHitReg::kPropPositionY),
                          RegisterAsFloat(nsHitReg::kPropPositionZ));
            s.SetVelocity(RegisterAsFloat(nsHitReg::kPropVelocityX), RegisterAsFloat(nsHitReg::kPropVelocityY),
                          RegisterAsFloat(nsHitReg::kPropVelocityZ));
        });
    }
    const std::int32_t product = GetRegisterValue(nsHitReg::kPropVolume) * GetRegisterValue(kRegVolumeScale);
    volume_ = (product + static_cast<std::int32_t>(static_cast<std::uint32_t>(product >> 31) >> 22)) >> 10;
    snd_->SetVolume(volume_);
    return true;
}

// ---- effects on the underlying sound -----------------------------------------------------------------

void cHitChannel::SetRolloffFactor(float factor) {  // @ 0x1004844a8
    WithInterface<cIGZSndRolloff>(snd_.get(), kIidRolloffEffects, [factor](cIGZSndRolloff& r) { r.SetRolloffFactor(factor); });
}
void cHitChannel::GetRolloffFactor(float& factor) const {  // @ 0x100484520
    WithInterface<cIGZSndRolloff>(snd_.get(), kIidRolloffEffects, [&factor](cIGZSndRolloff& r) { r.GetRolloffFactor(&factor); });
}
bool cHitChannel::SetInputDryLevel(float level) {  // @ 0x100484592
    bool ok = false;
    WithInterface<cIGZSndRendererEffects>(snd_.get(), kIidRendererEffects, [&](cIGZSndRendererEffects& e) { ok = e.SetInputDryLevel(level); });
    return ok;
}
bool cHitChannel::SetInputEffectLevel(float level) {  // @ 0x100484616
    bool ok = false;
    WithInterface<cIGZSndRendererEffects>(snd_.get(), kIidRendererEffects, [&](cIGZSndRendererEffects& e) { ok = e.SetInputEffectLevel(level); });
    return ok;
}
bool cHitChannel::SetLowPassCutoff(std::int32_t hz) {  // @ 0x10048469a
    bool ok = false;
    WithInterface<cIGZSndRendererEffects>(snd_.get(), kIidRendererEffects, [&](cIGZSndRendererEffects& e) { ok = e.SetLowPassCutoff(hz); });
    return ok;
}
bool cHitChannel::SetHighPassCutoff(std::int32_t hz) {  // @ 0x100484716
    bool ok = false;
    WithInterface<cIGZSndRendererEffects>(snd_.get(), kIidRendererEffects, [&](cIGZSndRendererEffects& e) { ok = e.SetHighPassCutoff(hz); });
    return ok;
}
bool cHitChannel::SetTimeMultiplier(float m) {  // @ 0x100484792
    bool ok = false;
    WithInterface<cIGZSndRendererEffects>(snd_.get(), kIidRendererEffects, [&](cIGZSndRendererEffects& e) { ok = e.SetTimeMultiplier(m); });
    return ok;
}

// ---- change notification -------------------------------------------------------------------------------

// HandleNotifySetPropertyValue @ 0x100483674: reacts to a register write (unless the writer
// suppressed notification). Returns true unless a pitch/volume refresh reports failure.
bool cHitChannel::HandleNotifySetPropertyValue(std::int32_t id) {
    switch (static_cast<std::uint32_t>(id)) {
        case 0xff0150f2: SetRolloffFactor(GetRegisterFloat(0xff0150f2)); return true;
        case 0xff099db2:  // frequency
        case 0xff3071b9:  // pitch multiplier
            return UpdatePitch();
        case 0xff0ad592:  // "active" flag: forced back to 0 while no note is playing
            // The binary rewrites the register with notification enabled, which re-enters this
            // handler forever; the port stops once the value is already 0.
            if (!NoteIsPlaying() && GetRegisterValue(0xff0ad592) != 0) SetRegisterValue(0xff0ad592, 0, false);
            return true;
        case 0xff4c97be: {  // parent property set: rebuild the inheritance chain
            cRZAutoRefCount<cIGZPropertySet> current = layers_.front();
            layers_.clear();
            layers_.push_back(current);
            Hit::cIHitManServices* services = Hit::GetServices();
            for (int hops = 0; hops < 64 && current; ++hops) {  // the binary has no hop limit
                AnyUint32 parent;
                const std::uint32_t key = kRegParentPropertySet;
                if (!current->Value(key, &parent) || parent.get() == 0) break;
                void* resource = nullptr;
                const cGZPersistResourceKey rk{0xebfee345, 0xfc45caf7, parent.get()};
                if (services == nullptr || !services->GetPersistResource(rk, kGZIID_cIGZPropertySet, &resource) ||
                    resource == nullptr) {
                    return UpdateVolPan();
                }
                cRZAutoRefCount<cIGZPropertySet> next(static_cast<cIGZPropertySet*>(resource));
                static_cast<cIGZPropertySet*>(resource)->Release();  // GetPersistResource's reference
                layers_.push_back(next);
                current = next;
            }
            return UpdateVolPan();
        }
        case 0xff734343: return SetInputDryLevel(GetRegisterFloat(0xff734343));
        case 0xffd3d39b: return SetInputEffectLevel(GetRegisterFloat(0xffd3d39b));
        case 0xffbdb196: return SetLowPassCutoff(GetRegisterValue(0xffbdb196));
        case 0xffa42951: return SetHighPassCutoff(GetRegisterValue(0xffa42951));
        case 0xff86fe79: return SetTimeMultiplier(GetRegisterFloat(0xff86fe79));
        case 0xffaec905:  // data source id: reading it, then dropping the data source
            (void)GetRegisterValue(0xffaec905);
            SetDataSource(nullptr, 2);
            return true;
        case 0xffb36659:  // pause switch of the underlying sound
            if (snd_) {
                if (GetRegisterValue(0xffb36659) == 0) snd_->Unpause(); else snd_->Pause(true);
            }
            return true;
        case 0xff3ec880:  // pan
        case 0xff444c9a: case 0xffc20061: case 0xffce9997:  // velocity
        case 0xffcc4d2d: case 0xff4a01d6: case 0xff469820:  // position
        case 0xff90aac8: case 0xff53dbf3:                   // volume, volume scale
        case 0xffaab623: case 0xffa6a614:                   // distances
            return UpdateVolPan();
        default:
            return true;
    }
}
