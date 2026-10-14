// cIGZSndUpdateTarget — receives periodic ticks. Slot 3 (+0x18). Id 0xaa11a64f (cHitTrackPlayer QI).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndUpdateTarget : public cIGZUnknown {
public:
    virtual void Update(std::int32_t elapsedMs) = 0;  // [3]
};

constexpr std::uint32_t kGZIID_cIGZSndUpdateTarget = 0xaa11a64f;
