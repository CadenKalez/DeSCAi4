#include "sc4/Ambulance.h"

namespace sc4 {

// 0x1006d5eb6: version 1, then the emergency vehicle's record.
bool cSC4Ambulance::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    return cSC4EmergencyVehicle::Write(stream);
}

// 0x1006d5f6a: only version 1 is accepted.
bool cSC4Ambulance::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    return version == 1 && cSC4EmergencyVehicle::Read(stream);
}

}  // namespace sc4
