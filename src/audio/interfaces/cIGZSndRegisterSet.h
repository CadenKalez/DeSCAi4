// cIGZSndRegisterSet — read/write access to a sound's registers (vtable group of cHitChannel at
// offset -24 / cHitTrackPlayer). Slots 3..8, byte offsets +0x18..+0x40.
// `id` may be a legacy register number (< 0x100, translated through nsHitReg) or a property id.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndRegisterSet : public cIGZUnknown {
public:
    // [3] +0x18. `suppressNotification`: when false the owner is told the property changed
    // (cHitTrackPlayer::ClearRegisterVal forwards exactly this flag).
    virtual bool SetRegisterValue(std::int32_t id, std::int32_t value, bool suppressNotification) = 0;
    // [4] +0x20
    virtual std::int32_t GetRegisterValue(std::int32_t id) const = 0;
    // [5] +0x28
    virtual void ClearRegisterValue(std::int32_t id, bool suppressNotification) = 0;
    // [6] +0x30
    virtual bool HasRegisterValue(std::int32_t id) const = 0;
    // [7] +0x38
    virtual bool SetRegisterFloat(std::uint32_t id, float value, bool suppressNotification) = 0;
    // [8] +0x40
    virtual float GetRegisterFloat(std::uint32_t id) const = 0;
};

constexpr std::uint32_t kGZIID_cIGZSndRegisterSet = 0xfd584467;  // cHitTrackPlayer::QueryInterface -> sub-object at +8
