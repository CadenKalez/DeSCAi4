// cSC4RewardAdvice — the advice that announces a reward building (class id 0xaa371c32; 8 functions: Init 0x10046ce4e,
// GetTrigger 0x10046ce7c, destructors 0x10046d28c/0x10046d296 + thunks, GetGZCLSID 0x10046d2c4/0x10046d314).
// Init runs the base Init and then turns on flag 0x1000 (reward). GetTrigger evaluates the row's "condition" function:
//   * a string result is the reason text (loaded from the string resources when it is a "text@..." key, kept raw
//     otherwise) and clears the two low state bits;
//   * 1 fires the advice (once: state bit 4 remembers it) and attaches an advice subject with the building's two
//     location words; the state becomes 6 either way;
//   * 0 resets the state to 1;
//   * anything else leaves the state alone.
// The per-guid state (a 32-bit word and the reason text) lives in the civic building simulator and is read at the
// start and written back at the end of every evaluation; it defaults to state 1 with an empty text.
// Quirks kept: a failing condition call only formats an error text that is thrown away; the result is "fired and the
// advice has text" (!flag 0x80), never the raw condition.
#pragma once
#include "sc4/Advice.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4RewardAdvice = 0xaa371c32;
constexpr std::uint32_t kAdviceFlagReward = 0x1000;
constexpr std::uint32_t kRewardStateFired = 0x4;   // bit 2 of the state byte pattern 6

class IRewardAdviceHost {
public:
    virtual ~IRewardAdviceHost() = default;
    // SL::spCivicBuildingSim vslot 0x30(guid): the stored state record {u32 state; cRZString text}, or false when none.
    virtual bool GetConditionState(std::uint32_t guid, std::uint32_t& state, cRZString& text) = 0;
    // vslot 0x38(guid, record*): stores the record.
    virtual void SetConditionState(std::uint32_t guid, std::uint32_t state, const cRZString& text) = 0;
    // SL::spBuildingDevelopmentSim vslot 0x140(guid): the building's record, whose words at +0x40 / +0x48 become the
    // subject's data words. Answers false (words 0) when there is none.
    virtual bool GetBuildingWords(std::uint32_t guid, std::uint32_t& word0, std::uint32_t& word1) = 0;
    // GZCOM()->CreateInstance(0x6a26d049, 0x6a26d050): a new advice subject (counted reference).
    virtual cISC4AdviceSubject* CreateSubject() = 0;
};
IRewardAdviceHost* RewardAdviceHost();
void SetRewardAdviceHost(IRewardAdviceHost* host);

class cSC4RewardAdvice : public cSC4Advice {
public:
    cSC4RewardAdvice() = default;
    ~cSC4RewardAdvice() override = default;   // @ 0x10046d28c (deleting 0x10046d296, thunks 0x10046d2d0/0x10046d2de)
    bool Init(int adviceId) override;          // @ 0x10046ce4e
    bool GetTrigger() const override;          // @ 0x10046ce7c
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4RewardAdvice; }   // @ 0x10046d2c4 (thunk 0x10046d314)
};

}  // namespace sc4
