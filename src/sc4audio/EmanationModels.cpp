#include "sc4audio/EmanationModels.h"

#include <cmath>

#include "audio/interfaces/cIGZSndRegisterSet.h"

namespace {

constexpr std::uint32_t kIidSem = 0xddcf3bd7;

// Copies register `id` from the source set to the channel without notification.
inline void Copy(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel, std::int32_t id) {
    channel->SetRegisterValue(id, source->GetRegisterValue(id), true);
}

// The pass-through registers shared by every model's ConfigureChannel.
inline void CopyCommon(cIGZSndRegisterSet* s, cIGZSndRegisterSet* c) {
    for (std::int32_t id : {0x13, 0x19, 0x1a, 0x1b, 0x23, 0x24, 0x25, 0x26, 0x33}) Copy(s, c, id);
}

}  // namespace

bool cSC4AudioEmanationModelBase::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kIidSem) {
        *out = static_cast<cIHitSem*>(this);
    } else if (iid == kGZIID_cIGZInitShutdown) {
        *out = static_cast<cIGZInitShutdown*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4AudioEmanationModelBase::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

bool cSC4AudioEmanationModelBase::Init() {
    sound_ = SC4Audio::SoundManager();
    return static_cast<bool>(sound_);
}

bool cSC4AudioEmanationModelBase::Shutdown() {
    sound_ = nullptr;
    return true;
}

// ---- 2D ----------------------------------------------------------------------------------------------

// UpdateVolPan @ 0x1003f30d0: pan is passed through; the volume becomes
// master * (volume * scale / 1024) / 1024 (unsigned shift) and is also stored in the source's
// register 0x0c (silently) so listeners can read the effective volume.
bool cSC4AudioEmanationModel2D::UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    const std::int32_t pan = source->GetRegisterValue(0x16);
    const std::int32_t volume = source->GetRegisterValue(0x15);
    const std::int32_t scale = source->GetRegisterValue(0x14);
    const std::int32_t master = MasterVolume();
    const std::uint32_t effective =
        static_cast<std::uint32_t>(master * ScaleBy1024(scale * volume)) >> 10;
    source->SetRegisterValue(0x0c, static_cast<std::int32_t>(effective), true);
    channel->SetRegisterValue(0x16, pan, true);
    channel->SetRegisterValue(0x15, static_cast<std::int32_t>(effective), false);
    return true;
}

bool cSC4AudioEmanationModel2D::ConfigureChannel(cIGZSndRegisterSet* s, cIGZSndRegisterSet* c) const {  // @ 0x1003f3198
    CopyCommon(s, c);
    return true;
}

// ---- 3D ---------------------------------------------------------------------------------------------

// UpdateVolPan @ 0x1003f346a: position and velocity pass through; min/max distances come from
// register 0x38 (percent) times the current zoom distance (max = 16x min).
bool cSC4AudioEmanationModel3D::UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    for (std::int32_t id : {0x20, 0x21, 0x22, 0x40, 0x41, 0x42}) Copy(source, channel, id);
    const std::uint32_t percent = static_cast<std::uint32_t>(source->GetRegisterValue(0x38));
    const float distance = (static_cast<float>(percent) / 100.0f) * SC4Audio::ZoomDistances()[4];
    channel->SetRegisterValue(0x1e, static_cast<std::int32_t>(distance), true);
    channel->SetRegisterValue(0x1f, static_cast<std::int32_t>(distance * 16.0f), true);
    const std::int32_t volume = source->GetRegisterValue(0x15);
    const std::int32_t scale = source->GetRegisterValue(0x14);
    const std::uint32_t effective =
        static_cast<std::uint32_t>(MasterVolume() * ScaleBy1024(scale * volume)) >> 10;
    source->SetRegisterValue(0x0c, static_cast<std::int32_t>(effective), true);
    return channel->SetRegisterValue(0x15, static_cast<std::int32_t>(effective), false);
}

bool cSC4AudioEmanationModel3D::ConfigureChannel(cIGZSndRegisterSet* s, cIGZSndRegisterSet* c) const {  // @ 0x1003f365a
    c->SetRegisterValue(0x1c, 1, true);  // 3D positioning on
    Copy(s, c, 0x1d);
    CopyCommon(s, c);
    Copy(s, c, 0x1e);
    Copy(s, c, 0x1f);
    return true;
}

// ---- 3D with screen effects ------------------------------------------------------------------------------

// UpdateVolPan @ 0x1003f39b2: the sound is projected to the viewport; pan follows its horizontal
// screen position (a sine ramp mirrored right of the middle) and the volume falls off with the
// pixel distance from the screen centre beyond (width+height)/2 * (register 0x38 / 100).
// Returns false when no viewport utilities exist.
bool cSC4AudioEmanationModel3DEffects::UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    for (std::int32_t id : {0x20, 0x21, 0x22, 0x40, 0x41, 0x42}) Copy(source, channel, id);
    const std::uint32_t percent = static_cast<std::uint32_t>(source->GetRegisterValue(0x38));
    float point[3] = {static_cast<float>(source->GetRegisterValue(0x20)),
                      static_cast<float>(source->GetRegisterValue(0x21)),
                      static_cast<float>(source->GetRegisterValue(0x22))};
    SC4Audio::cIViewport* view = SC4Audio::ViewUtilities();
    if (view == nullptr) return false;
    view->ProjectPointToViewportCoordinates2D(point);
    const auto x = static_cast<std::int32_t>(point[0]);
    const auto y = static_cast<std::int32_t>(point[1]);
    const std::int32_t width = view->ViewportWidth(), height = view->ViewportHeight();
    const std::int32_t dy = y - height / 2;
    const std::int32_t dx = x - width / 2;
    const float falloffStart = static_cast<float>(height + width) * 0.5f * (static_cast<float>(percent) / 100.0f);
    const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    std::int32_t distanceScale = 0x400;
    if (distance > falloffStart) distanceScale = static_cast<std::int32_t>(1024.0f / (distance / falloffStart));

    const float angle = static_cast<float>(x) * 3.1415927f * 0.0009765625f;
    const float s = std::sin(angle) * 512.0f;
    const float panValue = (1.5707964f < angle) ? 1024.0f - s : s;
    std::int32_t pan = static_cast<std::int32_t>(panValue);
    std::int32_t clamped = pan < 0x401 ? pan : 0x400;
    if (pan < 1) clamped = 0;
    channel->SetRegisterValue(0x16, clamped, true);

    const std::int32_t volume = source->GetRegisterValue(0x15);
    const std::int32_t scale = source->GetRegisterValue(0x14);
    const std::int32_t product = ScaleBy1024(scale * volume) * distanceScale;
    const std::uint32_t effective = static_cast<std::uint32_t>(MasterVolume() * ScaleBy1024(product)) >> 10;
    source->SetRegisterValue(0x0c, static_cast<std::int32_t>(effective), true);
    return channel->SetRegisterValue(0x15, static_cast<std::int32_t>(effective), false);
}

bool cSC4AudioEmanationModel3DEffects::ConfigureChannel(cIGZSndRegisterSet* s, cIGZSndRegisterSet* c) const {  // @ 0x1003f3cf4
    c->SetRegisterValue(0x1c, 0, true);  // positioning is faked by the pan, so the channel stays 2D
    Copy(s, c, 0x1d);
    CopyCommon(s, c);
    Copy(s, c, 0x1e);
    Copy(s, c, 0x1f);
    return true;
}
