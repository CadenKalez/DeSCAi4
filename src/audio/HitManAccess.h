// Access to the global Hit manager (RZ::HM() in the original). The Sem models only need its
// register file, so this is the narrow view used until cHitMan itself is recovered.
#pragma once
#include <cstdint>

namespace RZ {

class cHitManRegisterView {
public:
    virtual ~cHitManRegisterView() = default;
    virtual std::int32_t GetRegisterValue(std::int32_t id) const = 0;  // cIHitMan slot 31 (+0xf8)
};

// Supplied by the application (the original's RZ::HM() @ 0x100127ee4).
cHitManRegisterView* HM();

}  // namespace RZ
