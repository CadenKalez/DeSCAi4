// cSC4AdviceNeghborDeal [sic] — the advice shown for a neighbour deal (class id 0x6a3848db, interface 0xea3848ba which is
// answered by the primary pointer; 18 functions at 0x1004411ee..0x1004415ce). It is cSC4Advice plus one field, the deal type
// (+0x140), read from the advice row's "nd_type". Stream record: u16 version 2, the base record, s32 deal type.
// Two virtuals were appended to the base vtable: GetDealType (0x120) and SetDealType (0x128).
// Quirks kept: Init answers false when the row cannot be read again, even though the base Init already ran; Clone copies the
// deal type through a QueryInterface(0xea3848ba) of the fresh clone; Read of any other version fails without reading.
#pragma once
#include "sc4/Advice.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AdviceNeghborDeal = 0x6a3848db;
constexpr std::uint32_t kGZIID_cISC4AdviceNeighborDeal = 0xea3848ba;

class cSC4AdviceNeghborDeal : public cSC4Advice {
public:
    cSC4AdviceNeghborDeal() = default;       // inlined into the class factory in the original
    ~cSC4AdviceNeghborDeal() override = default;   // @ 0x1004411ee (deleting 0x10044125e, thunks 0x100441224/0x1004412b8)

    bool Init(int adviceId) override;                              // @ 0x1004412c6
    bool Shutdown() override { return cSC4Advice::Shutdown(); }    // @ 0x1004413c2
    bool Clone(cISC4Advice** out) override;                        // @ 0x1004413cc
    bool Write(cIGZOStream& stream) const override;                // @ 0x100441460 (thunk 0x1004414aa)
    bool Read(cIGZIStream& stream) override;                       // @ 0x1004414b8 (thunk 0x10044151c)
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10044152a (thunks 0x100441548, 0x10044156a)
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AdviceNeghborDeal; }   // @ 0x1004415c2 (thunk 0x1004415ce)

    virtual int GetDealType() const { return dealType_; }          // @ 0x10044158c (vslot 0x120)
    virtual void SetDealType(int type) { dealType_ = type; }       // @ 0x100441598 (vslot 0x128)

private:
    int dealType_ = 0;   // +0x140
};

}  // namespace sc4
