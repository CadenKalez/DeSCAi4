// cIGZSndRenderer — a platform sound renderer registered with the sound system. Interface id
// 0xc9d25f29. Slots 3..7 (+0x18..+0x38) from cGZSndSysMac's secondary vtable (+8). A renderer
// may also expose cIGZSnd3DListener (0x406077d2), cIGZSnd3DListenerEx (0x5b30e7b3) and
// cIGZSndEaRendererEffects (0xab79e31c) through QueryInterface.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndRenderer : public cIGZUnknown {
public:
    virtual bool SetMasterVolume(std::int32_t volume) = 0;     // [3] +0x18
    virtual std::int32_t GetMasterVolume() = 0;                // [4] +0x20
    virtual bool Pause(bool pause) = 0;                        // [5] +0x28
    virtual bool Unpause() = 0;                                // [6] +0x30
    virtual bool IsPaused() const = 0;                         // [7] +0x38
};
constexpr std::uint32_t kGZIID_cIGZSndRenderer = 0xc9d25f29;
