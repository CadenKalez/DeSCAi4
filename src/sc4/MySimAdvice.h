// cSC4MySimAdvice — advice about one MySim (class id 0x6a9335de; 7 functions: DoLink 0x10079eb12, destructors
// 0x1007aad50/0x1007aad5a and their thunks, GetGZCLSID 0x1007aad88/0x1007aadd8). It only overrides DoLink: before the
// base class runs the link it tells the advice's MySim (looked up by the advisor id) to react to the link click.
// Quirk kept: the original does not check the lookup result (a missing MySim dereferences null); the seam call is skipped
// instead when the host answers no MySim.
#pragma once
#include "sc4/Advice.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4MySimAdvice = 0x6a9335de;

class IMySimAdviceHost {
public:
    virtual ~IMySimAdviceHost() = default;
    // SL::spCity vslot 0x288 (MySim manager) -> vslot 0x38(simId) -> the sim object (the secondary interface pointer
    // minus 0x10), then its vslot 0x2d8 (called before the link is run). Answers false when no such sim exists.
    virtual bool NotifySimOfLink(int simId) = 0;
};
IMySimAdviceHost* MySimAdviceHost();
void SetMySimAdviceHost(IMySimAdviceHost* host);

class cSC4MySimAdvice : public cSC4Advice {
public:
    cSC4MySimAdvice() = default;
    ~cSC4MySimAdvice() override = default;   // @ 0x1007aad50 (deleting 0x1007aad5a, thunks 0x1007aad94/0x1007aada2)
    bool DoLink(const cIGZString& link) override;   // @ 0x10079eb12
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4MySimAdvice; }   // @ 0x1007aad88 (thunk 0x1007aadd8)
};

}  // namespace sc4
