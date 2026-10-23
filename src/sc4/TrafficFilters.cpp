#include "sc4/TrafficFilters.h"

namespace sc4 {

namespace {
// QI the occupant for its network interface; the caller Releases the result.
ITrafficNetworkOccupant* NetworkInterface(cISC4Occupant* occupant) {
    void* raw = nullptr;
    if (!occupant->QueryInterface(kGZIID_cISC4NetworkOccupant, &raw)) return nullptr;
    return static_cast<ITrafficNetworkOccupant*>(raw);
}

bool HasTrafficNetworkFlags(cISC4Occupant* occupant) {
    if (occupant->GetType() != kTrafficFilterOccupantNetwork) return false;
    ITrafficNetworkOccupant* network = NetworkInterface(occupant);
    if (!network) return false;
    const bool result = network->HasAnyNetworkFlag(kTrafficFilterNetworkFlags);
    network->Release();
    return result;
}
}  // namespace

bool cTrafficNetworkFilter_NetworkMap::IsOccupantIncluded(cISC4Occupant* occupant) { return HasTrafficNetworkFlags(occupant); }
bool cTrafficNetworkFilter_PlayerDrive::IsOccupantIncluded(cISC4Occupant* occupant) { return HasTrafficNetworkFlags(occupant); }

bool cSC4TrafficQueryOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    const std::uint32_t type = occupant->GetType();
    if (type == kTrafficFilterOccupantBuilding) return true;
    if (type != kTrafficFilterOccupantNetwork) return false;
    ITrafficNetworkOccupant* network = NetworkInterface(occupant);
    if (!network) return false;
    bool result = false;
    for (std::uint32_t networkType : {0u, 1u, 2u, 3u, 6u, 7u, 8u, 9u, 0xcu, 10u, 0xbu}) {
        if (network->IsOfType(networkType)) { result = true; break; }
    }
    network->Release();
    return result;
}

}  // namespace sc4
