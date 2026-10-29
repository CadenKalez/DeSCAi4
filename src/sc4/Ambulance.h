// cSC4Ambulance — the ambulance (vtable 0x100ae3970): a cSC4EmergencyVehicle that answers dispatch type 7 with a radius of 80 and
// writes a version-1 record around the emergency vehicle's. Class id 0x6c1acde1.
#pragma once
#include "sc4/EmergencyVehicle.h"

namespace sc4 {

constexpr std::uint32_t kAmbulanceGZCLSID = 0x6c1acde1;
constexpr std::uint32_t kAmbulanceDispatchType = 7;
constexpr float kAmbulanceEffectiveRadius = 80.0f;   // 0x42a00000

class cSC4Ambulance : public cSC4EmergencyVehicle {
public:
    using cSC4EmergencyVehicle::cSC4EmergencyVehicle;                          // @ 0x1006d5e3c
    float GetEffectiveRadius() const override { return kAmbulanceEffectiveRadius; }   // 0x640 @ 0x1006d5ea8
    bool Write(cIGZOStream& stream) const override;                            // 0x240 @ 0x1006d5eb6
    bool Read(cIGZIStream& stream) override;                                   // 0x248 @ 0x1006d5f6a
    std::uint32_t GetGZCLSID() const override { return kAmbulanceGZCLSID; }    // 0x250 @ 0x1006d619c
    std::uint32_t GetDispatchType() const override { return kAmbulanceDispatchType; }   // 0x638 @ 0x1006d61b4
};

}  // namespace sc4
