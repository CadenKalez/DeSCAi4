#include "audio/HitSem.h"

#include "audio/HitManAccess.h"
#include "audio/HitRegisters.h"

using namespace nsHitReg;

namespace {

// Legacy register numbers copied verbatim by ConfigureChannel (the original names these only by
// number; the property ids behind them are in HitRegisters.cpp). Order is the original's.
constexpr std::int32_t kConfigure2d[] = {0x13, 0x19, 0x1a, 0x1b, 0x23, 0x24, 0x25, 0x26, 0x33};
constexpr std::int32_t kConfigure3d[] = {0x1d, 0x13, 0x19, 0x1a, 0x1b, 0x23, 0x24, 0x25, 0x26, 0x33, 0x1e, 0x1f};

// volume = (scale * volume) / 1024, or /2048 while the channel's duck priority is below the
// manager's duck threshold. Division truncates toward zero like the original shifts.
std::int32_t ComputeDuckedVolume(const cIGZSndRegisterSet& source) {
    const std::int32_t scaled =
        source.GetRegisterValue(0x14) * source.GetRegisterValue(0x15);  // volume scale * volume
    std::int32_t volume = scaled / 1024;
    const std::int32_t threshold = RZ::HM()->GetRegisterValue(static_cast<std::int32_t>(kPropDuckPriority));
    if (threshold != 0 && source.GetRegisterValue(static_cast<std::int32_t>(kPropDuckPriority)) < threshold) {
        volume = scaled / 2048;
    }
    return volume;
}

void CopyRegister(cIGZSndRegisterSet& source, cIGZSndRegisterSet& channel, std::int32_t id) {
    channel.SetRegisterValue(id, source.GetRegisterValue(id), true);
}

}  // namespace

// cHitSem::QueryInterface(unsigned int, void**) @ 0x1004c2688
bool cHitSem::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIHitSem) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cHitSem::Release() @ 0x1004c26b8
std::uint32_t cHitSem::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cHitSem::GetName(cIGZString&) const @ 0x1004c26de
bool cHitSem::GetName(cIGZString& outName) const {
    outName.Copy(name_);
    return true;
}

// cHitSem2dDefault::cHitSem2dDefault() @ 0x1004c25bc
cHitSem2dDefault::cHitSem2dDefault() {
    id_ = kHitSem2dDefaultId;
    name_.AssignFromCString("2D default");
}

// cHitSem2dDefault::UpdateVolPan(cIGZSndRegisterSet*, cIGZSndRegisterSet*) const @ 0x1004d1510
bool cHitSem2dDefault::UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    const std::int32_t pan = source->GetRegisterValue(0x16);
    const std::int32_t volume = ComputeDuckedVolume(*source);
    channel->SetRegisterValue(0x16, pan, true);
    channel->SetRegisterValue(0x15, volume, false);
    return true;
}

// cHitSem2dDefault::ConfigureChannel(cIGZSndRegisterSet*, cIGZSndRegisterSet*) const @ 0x1004d15f2
bool cHitSem2dDefault::ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    for (const std::int32_t id : kConfigure2d) CopyRegister(*source, *channel, id);
    return true;
}

// cHitSem3dDefault::cHitSem3dDefault() @ 0x1004c2706
cHitSem3dDefault::cHitSem3dDefault() {
    id_ = kHitSem3dDefaultId;
    name_.AssignFromCString("3D default");
}

// cHitSem3dDefault::UpdateVolPan(cIGZSndRegisterSet*, cIGZSndRegisterSet*) const @ 0x1004d176e
bool cHitSem3dDefault::UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    if (channel == source) return false;
    // position (0x20-0x22) and velocity (0x40-0x42)
    for (const std::int32_t id : {0x20, 0x21, 0x22, 0x40, 0x41, 0x42}) CopyRegister(*source, *channel, id);
    if (source->GetRegisterValue(0x1f) != 0) {  // maximum distance explicitly set
        CopyRegister(*source, *channel, 0x1e);
        CopyRegister(*source, *channel, 0x1f);
    } else {  // derive from the default distance: min = base (or 4), max = min * 500
        std::int32_t base = source->GetRegisterValue(0x38);
        if (base == 0) base = 4;
        channel->SetRegisterValue(0x1e, base, true);
        channel->SetRegisterValue(0x1f, base * 500, true);
    }
    return channel->SetRegisterValue(0x15, ComputeDuckedVolume(*source), false);
}

// cHitSem3dDefault::ConfigureChannel(cIGZSndRegisterSet*, cIGZSndRegisterSet*) const @ 0x1004d19d2
bool cHitSem3dDefault::ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const {
    channel->SetRegisterValue(0x1c, 1, true);  // positional playback on
    for (const std::int32_t id : kConfigure3d) CopyRegister(*source, *channel, id);
    return true;
}
