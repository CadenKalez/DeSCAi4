// cSC4AutomataAttractor — a controller that pulls automata (pedestrians, cars) towards an occupant, e.g. a shop that
// attracts shoppers (controller type / class id 0x6a1628ba; 20 functions at 0x1000f7872..0x1000fbaa6 plus the nested
// tTemplate 0x1000fbe8e, 0x1000fc0a6, 0x1000fc104). Extends cSC4AutomataController (layout up to +0xa4) with the strength
// curve at +0xa8 (copied from the template's curve at +0x128 during Init). Its constructor starts with flag 0x2
// (source data ready).
// tTemplate (size 0x148, base tControllerTemplate): +0xe0 vector<u32> automaton types that are never attracted,
// +0xf8 vector<u32> occupant groups that are attracted, +0x110 std::list<cRZString> (unused by the recovered code),
// +0x128 cSC4ResponseCurve strength by distance (0 .. 1).
// Strength = (1 - distance / radius) * dayMask-gated (template curve at time of day) * (attractor curve at distance); the
// day mask (template +0x60) is tested with the day of the controller time modulo 32.
// Quirks kept: Init with a null template crashes in the original (it is skipped here); HandleEnterState only tracks
// automata when the template limits them (maxAutomata != 0) and AddRefs each tracked one; Write ignores the base
// result; the class id equals the controller type id; SetAutomataGroups silently ignores templates of another type.
#pragma once
#include "sc4/AutomataController.h"
#include "sc4/SimStreamOps.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AutomataAttractor = 0x6a1628ba;
constexpr std::uint32_t kAutomatonIidForFilter = 0xa9b40f05;

struct tAttractorTemplate : tControllerTemplate {
    cSC4ResponseCurve strengthCurve;              // +0x128
};
// Creates the attractor template (cSC4AutomataAttractor::tTemplate::tTemplate @ 0x1000fbe8e; destructors 0x1000fc0a6 / 0x1000fc104).
tControllerTemplate* CreateAttractorTemplate();

class IAutomataAttractorHost {
public:
    virtual ~IAutomataAttractorHost() = default;
    // The occupant answers QueryInterface(0xa9b40f05) (it is an automaton) and that interface's vslot 0x1c0 answers a record
    // whose int at +8 is the automaton's current state id. False when the occupant is not an automaton / has no record.
    virtual bool GetAutomatonStateId(cISC4Occupant* occupant, std::int32_t& stateId) = 0;
};
IAutomataAttractorHost* AutomataAttractorHost();
void SetAutomataAttractorHost(IAutomataAttractorHost* host);

class cSC4AutomataAttractor : public cSC4AutomataController {
public:
    cSC4AutomataAttractor() { flags_ = kCtrlFlagReady; }                    // @ 0x1000fa0b6 (thunk-like 0x1000fa17e)
    ~cSC4AutomataAttractor() override = default;                            // @ 0x1000fb818 (deleting 0x1000fb868)

    bool Init() override;                                                   // @ 0x1000fa188
    bool Write(cIGZOStream& stream) const override;                         // @ 0x1000fa1c4 (thunk 0x1000fa206)
    bool Read(cIGZIStream& stream) override;                                // @ 0x1000fa214 (thunk 0x1000fa25e)
    bool HandleEnterState(IControllerAutomaton* automaton, std::uint32_t state) override;   // @ 0x1000fa26c
    bool HandleExitState(IControllerAutomaton* automaton, std::uint32_t state) override;    // @ 0x1000fa308
    // QueryInterface @ 0x1000fa394 (thunk 0x1000fa39e), AddRef 0x1000fb7ea / Release 0x1000fb7f8 and FinalRelease
    // 0x1000fba4a are the base implementations (thunks 0x1000fba74, 0x1000fba82).
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AutomataAttractor; }   // @ 0x1000fba3e (thunk 0x1000fbaa6)
    std::uint32_t GetControllerType() const override { return kControllerTypeAttractor; }   // @ 0x1000fb8d2

    void SetAutomataGroups(const std::vector<std::uint32_t>& groups) override;   // @ 0x1000fa3ac
    bool TestAutomataFilter(cISC4Occupant* occupant) const;                 // @ 0x1000f7872
    float GetCurrentStrength(const ControllerTime& time, float distance) const;   // @ 0x1000f7984

private:
    cSC4ResponseCurve strength_;   // +0xa8
};

}  // namespace sc4
