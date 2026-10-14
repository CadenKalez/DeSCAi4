// cIGZSndViewUpdateTarget — told when the listener/view changed. Slot 3 (+0x18).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndViewUpdateTarget : public cIGZUnknown {
public:
    virtual void UpdateView() = 0;  // [3]
};

constexpr std::uint32_t kGZIID_cIGZSndViewUpdateTarget = 0xaa11a64f;  // cHitTrackPlayer QI -> +0x20
