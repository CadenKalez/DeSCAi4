// cSC4AircraftManager - the automata manager of the aircraft (class id / interface 0xc98f9d4a, object 0xb40 bytes). A
// cSC4AutomataManager that keeps one automaton per occupant that launches aircraft (airports, helipads: the property
// 0xabb90e58 / 0xbfa15a5 select the kind) and the list of runway markers the planes taxi between. Recovered from SimCity 4
// Deluxe Edition 1.2.1 (0x10050044c..0x10050234e).
//
// Saved state (record version 5): the base manager, then the count and the (occupant, automaton) pairs of the airport map
// at +0xb08, then the count and the runway markers (three floats and a byte) of the list at +0xb20. Versions below 5 are the
// base manager's own legacy record.
//
// PARTIAL: HandleOccupantInserted / HandleOccupantRemoved (which create and destroy the airport's aircraft automata through
// the manager's CreateAutomaton and the property tables at 0x100a23650..), AddAutomaton, Dispatch and GetRunwayList are not
// recovered; the manager loads, saves and answers its interfaces with an empty fleet.
#pragma once
#include <list>
#include <map>

#include "sc4/AutomataManager.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AircraftManager = 0xc98f9d4a;
constexpr std::uint32_t kAircraftManagerFallbackGroup = 0xc96e75dc;   // the aircraft class id
constexpr std::uint32_t kAircraftManagerDefaultGroup = 0x4003;
constexpr std::uint32_t kAircraftManagerExemplarGroup = 0x18;
constexpr std::uint16_t kAircraftManagerRecordVersion = 5;
constexpr std::uint32_t kAircraftIIDOccupant = 0x26bb767e;
constexpr std::uint32_t kAircraftIIDAutomaton = 0xa9b40f05;

struct AircraftRunwayMarker {
    float x = 0, y = 0, z = 0;   // +0x10 / +0x14 / +0x18 of the list node
    std::uint8_t flag = 0;       // +0x1c
};

class cSC4AircraftManager : public cSC4AutomataManager {
public:
    cSC4AircraftManager();                                    // 0x10050044c
    ~cSC4AircraftManager() override;                          // 0x100502244

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100501216
    bool Init() override;                                     // 0x1005004fe
    bool Shutdown() override;                                 // 0x100500590
    bool Write(cIGZOStream& stream) const override;           // 0x100500680
    bool Read(cIGZIStream& stream) override;                  // 0x1005007c2
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AircraftManager; }   // 0x10050234e

    std::size_t AirportCount() const { return airports_.size(); }
    const std::list<AircraftRunwayMarker>& RunwayMarkers() const { return runways_; }

private:
    std::map<cIGZUnknown*, cIGZUnknown*> airports_;   // +0xb08 occupant -> automaton (both held)
    std::list<AircraftRunwayMarker> runways_;         // +0xb20
};

}  // namespace sc4
