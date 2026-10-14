// cIFreshDimensionPlayer — primary base of cFreshDimensionPlayer. Slots 3..4 (+0x18..+0x20); the class
// adds destructors at 5-6 and its own virtuals after them.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIFreshPlayer.h"

class cIFreshDimensionPlayer : public cIGZUnknown {
public:
    virtual cIFreshPlayer* AsIFreshPlayer() = 0;                             // [3] returns this+8
    virtual bool SetScoreEnabled(std::uint32_t scoreId, bool enabled) = 0;   // [4]
};

constexpr std::uint32_t kGZIID_cIFreshDimensionPlayer = 0xddda72f1;
