// cSC4MyStreetSim - a MySim that lives on the street (class id 0x0bf606a5): the sims the player meets while driving round
// the city. A cSC4MySim whose constructor stamps the street bitmap id 0x0bfa331e and the "no track" handle -1 and keeps one
// extra interface (iid 0x0bf61276, +0x430) with a cRZString at +0x440. Init also builds the Lua table of the sim when the base
// has not. Recovered from SimCity 4 Deluxe Edition 1.2.1 (0x1006b9ae4..0x1006bbbcc).
//
// PARTIAL: the trip behaviour (DoMessage 0x1006b9ee4, update_trip_variables 0x1006b9fea, the simulate_* overrides and
// AttachAutomaton / DetachAutomaton) is not recovered; the street sim loads, saves and answers its interfaces, and the
// base class's per-day behaviour applies.
#pragma once
#include "sc4/MySim.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4MyStreetSim = 0x0bf606a5;
constexpr std::uint32_t kGZIID_cISC4MyStreetSim = 0x0bf61276;
constexpr std::uint32_t kMyStreetSimBitmapId = 0x0bfa331e;

class cSC4MyStreetSim : public cSC4MySim {
public:
    cSC4MyStreetSim();                                              // 0x1006b9ae4
    ~cSC4MyStreetSim() override = default;                          // 0x1006b9bec
    bool QueryInterface(std::uint32_t iid, void** out) override;    // 0x1006b9cba
    bool Init(int advisorType) override;                            // 0x1006b9df2
    bool Shutdown() override;                                       // 0x1006b9e34
    bool Write(cIGZOStream& stream) const override;                 // 0x1006b9e4c
    bool Read(cIGZIStream& stream) override;                        // 0x1006b9e98
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4MyStreetSim; }   // 0x1006bbbcc

private:
    cRZString extra_;   // +0x440 (the original holds a ref-counted string)
};

}  // namespace sc4
