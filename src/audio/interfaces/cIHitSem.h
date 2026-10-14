// cIHitSem — "sound emanation model": decides how a sound's logical registers become the
// registers of the channel that actually plays it (volume/pan for 2D, positional data for 3D).
// Slots 3..6 (byte offsets +0x18..+0x30); cHitSem implements GetId/GetName.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"

class cIHitSem : public cIGZUnknown {
public:
    virtual std::uint32_t GetId() const = 0;                       // [3]
    virtual bool GetName(cIGZString& outName) const = 0;           // [4] copies the name, true
    // [5] Derives the playback volume/pan registers of `channel` from `source`. false if no work.
    virtual bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const = 0;
    // [6] Copies the registers that configure `channel` from `source`.
    virtual bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const = 0;
};

constexpr std::uint32_t kGZIID_cIHitSem = 0xddcf3bd7;
