// cIGZSndCompletionNotification — completion callbacks and remaining-time estimate.
// Slots 3..5 (+0x18..+0x28), from cGZSnd's third sub-vtable.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndCompletionFunctionObject;  // not recovered yet

class cIGZSndCompletionNotification : public cIGZUnknown {
public:
    virtual bool SetCompletionNotification(cIGZSndCompletionFunctionObject* callback) = 0;  // [3]
    virtual bool StopCompletionNotification() = 0;                                          // [4]
    virtual std::uint32_t EstimateRemainingPlayTime() = 0;                                  // [5] cGZSnd: 0
};
