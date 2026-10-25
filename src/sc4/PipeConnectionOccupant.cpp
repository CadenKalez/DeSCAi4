#include "sc4/PipeConnectionOccupant.h"

namespace sc4 {

namespace {
tCustomGeometryInfo sCustomGeometryInfo = {};   // PARTIAL: the piece tables are not recovered
}

bool cSC4PipeConnectionOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZCLSID_cSC4PipeConnectionOccupant) {
        *out = static_cast<cSC4PipeConnectionOccupant*>(this);
        AddRef();
        return true;
    }
    return cSC4UndergroundConnectionOccupant::QueryInterface(iid, out);
}

bool cSC4PipeConnectionOccupant::Init() {
    type_ = kPipeConnectionOccupantType;
    return cSC4UndergroundConnectionOccupant::Init(kGZCLSID_cSC4PipeConnectionOccupant, &sCustomGeometryInfo);
}

}  // namespace sc4
