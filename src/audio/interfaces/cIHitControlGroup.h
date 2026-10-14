// cIHitControlGroup — a numbered group of tracks that share a volume. Slots 3..4 (+0x18..+0x20);
// cHitControlGroup adds the destructors at 5-6 and its own virtuals after them.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSnd.h"

class cIHitControlGroup : public cIGZUnknown {
public:
    virtual void AddTrack(std::int32_t trackId) = 0;  // [3]
    virtual cIGZSnd* AsIGZSnd() = 0;                  // [4] returns this+8
};

constexpr std::uint32_t kGZIID_cIHitControlGroup = 0xfb1c0050;
