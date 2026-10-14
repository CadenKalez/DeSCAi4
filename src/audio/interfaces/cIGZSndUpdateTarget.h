// cIGZSndUpdateTarget — receives periodic ticks. Slot 3 (+0x18). Id 0x5b34f0af (cHitTrackPlayer QI -> +0x28).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndUpdateTarget : public cIGZUnknown {
public:
    virtual void Update(std::int32_t elapsedMs) = 0;  // [3]
};

constexpr std::uint32_t kGZIID_cIGZSndUpdateTarget = 0x5b34f0af;
