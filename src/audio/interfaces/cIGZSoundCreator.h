// cIGZSoundCreator — factory that can build a sound for an id (registered with the sound
// manager). Slot 3 (+0x18); interface id 0x3af9866f.
#pragma once
#include <cstdint>

#include "gz/cIGZVariant.h"

class cIGZSoundCreator : public cIGZUnknown {
public:
    // [3] +0x18: builds the sound `id` and returns interface `iid` in `out` (AddRef'd).
    virtual bool CreateSound(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) = 0;
};
constexpr std::uint32_t kGZIID_cIGZSoundCreator = 0x3af9866f;
