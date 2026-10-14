// Access to the global Hit manager (RZ::HM() in the original). The Sem models only need its
// register file, so this is the narrow view used until cHitMan itself is recovered.
#pragma once
#include <cstdint>

namespace RZ {

class cHitManRegisterView {
public:
    virtual ~cHitManRegisterView() = default;
    virtual std::int32_t GetRegisterValue(std::int32_t id) const = 0;  // cIHitMan group-2 slot 31 (+0xf8)
    // cIHitMan group-2 slot 26 (+0xd0): pseudo-random value in [0, range) from the engine's RNG.
    // The original always passes 0 as the second argument.
    virtual std::int32_t Random(std::int32_t range, std::int32_t unused) = 0;
};

// Supplied by the application (the original's RZ::HM() @ 0x100127ee4).
cHitManRegisterView* HM();

}  // namespace RZ
