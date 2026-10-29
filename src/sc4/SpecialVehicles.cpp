#include "sc4/SpecialVehicles.h"

#include "gz/StreamOps.h"

namespace sc4 {

bool cSC4CarjackedVehicle::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    return cSC4Vehicle::Write(stream);
}

bool cSC4CarjackedVehicle::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    return version == 1 && cSC4Vehicle::Read(stream);
}

bool cSC4GetawayVan::Write(cIGZOStream& stream) const {
    stream.SetUint16(3);
    const bool ok = cSC4Vehicle::Write(stream);
    if (ok) {
        stream << false;             // the bool, always false
        stream.SetSint32(0);
    }
    return ok;
}

bool cSC4GetawayVan::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 3 || !cSC4Vehicle::Read(stream)) return false;
    bool flag = false;
    stream >> flag;
    std::int32_t value = 0;
    stream.GetSint32(value);
    return true;
}

bool cSC4MayorLimo::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    return cSC4Vehicle::Write(stream);
}

bool cSC4MayorLimo::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    return version == 1 && cSC4Vehicle::Read(stream);
}

}  // namespace sc4
