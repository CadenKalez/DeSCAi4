// cIGZSndEaRendererEffects — effects-bus control of an EA renderer (interface id 0xab79e31c).
// Slots 3..4 (+0x18, +0x20), from cGZSndSys's calls.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSndEaRendererEffects : public cIGZUnknown {
public:
    virtual bool InitializeEffectsBus(std::int32_t bus, float level, std::int32_t a, std::int32_t b, std::int32_t c) = 0;   // [3]
    virtual bool SetEffectMasterLevel(std::int32_t bus, float level) = 0;                                                   // [4]
};
constexpr std::uint32_t kGZIID_cIGZSndEaRendererEffects = 0xab79e31c;
