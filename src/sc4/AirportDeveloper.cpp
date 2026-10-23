// cSC4AirportDeveloper — see AirportDeveloper.h for the RTTI, the four vtables, the identity, the department
// budget and the deviations. The eight port-type virtuals the base leaves pure are implemented here; the
// airport-specific trip computation and department budget are the parts that make this developer concrete.
//
// The original constructor has no symbol of its own: GZCOM_CreatecSC4AirportDeveloper (0x1000c8034) allocates
// the 0xb8 bytes and inlines the identity writes, so the constructor below is the recovered body of that code.

#include "sc4/AirportDeveloper.h"

#include <cstddef>
#include <list>
#include <vector>

namespace sc4 {

namespace {

// The host installed through SetAirportDeveloperHost; the base's PortDeveloperHost() stays the one the base
// object uses. The two wrap the same SL::sp* objects in production.
IAirportDeveloperHost* g_airportHost = nullptr;

}  // namespace

IAirportDeveloperHost* AirportDeveloperHost() { return g_airportHost; }
void SetAirportDeveloperHost(IAirportDeveloperHost* host) { g_airportHost = host; }

//==================================================================================================
// construction / COM
//==================================================================================================

// @ GZCOM_CreatecSC4AirportDeveloper 0x1000c8034 (inlined). The original zeroes the base's +0x50 department
// slot and stores the identity; the zeroed slot is this port's department_ member.
cSC4AirportDeveloper::cSC4AirportDeveloper() {
    SetIdentity(kAirportDeveloperId, kAirportZoneType, kAirportHistoryCategory, kAirportAdvisorEventId,
                kAirportBuildingGroup);
    department_ = nullptr;
}

// @ 0x1000c80de (complete object) / 0x1000c817a (deleting). The original only resets the four vptrs (the
// compiler emits that here) and runs the base destructor; the department is released by Shutdown, not here.
cSC4AirportDeveloper::~cSC4AirportDeveloper() = default;

// @ 0x1000c8430. The airport iid answers with the interface subobject at +0x00 and counts the reference
// through the primary AddRef; every other iid is the base's (which answers from +0x08/+0x10/+0x18).
bool cSC4AirportDeveloper::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cISC4AirportDeveloper) {
        *out = static_cast<cISC4AirportDeveloper*>(this);
        AddRef();
        return true;
    }
    return cSC4PortDeveloper::QueryInterface(iid, out);
}

// @ 0x1000c84c4 / 0x1000c84f8. One shared count, owned by the base object at +0x08.
std::uint32_t cSC4AirportDeveloper::AddRef() { return cSC4PortDeveloper::AddRef(); }
std::uint32_t cSC4AirportDeveloper::Release() { return cSC4PortDeveloper::Release(); }

// @ 0x1000c852c.
void cSC4AirportDeveloper::FinalRelease() { cSC4PortDeveloper::FinalRelease(); }

// @ 0x1000c8e5c.
std::uint32_t cSC4AirportDeveloper::GetSimulatorType() const { return kAirportSimulatorType; }

// @ 0x1000c8e8e.
std::uint32_t cSC4AirportDeveloper::GetGZCLSID() const { return kGZCLSID_cSC4AirportDeveloper; }

// @ 0x1000c8e84: the concrete port implementation (the cSC4PortDeveloper subobject at +0x08).
cISC4PortDeveloper* cSC4AirportDeveloper::AsISC4PortDeveloper() {
    return static_cast<cISC4PortDeveloper*>(static_cast<cSC4PortDeveloper*>(this));
}

// The base overrides, in the original's order of work (each is a tail call to the base in the binary).
// @ 0x1000c872a, 0x1000c87f0, 0x1000c8e68, 0x1000c8e76.
float cSC4AirportDeveloper::GetAvgEfficiency() { return cSC4PortDeveloper::GetAvgEfficiency(); }
bool cSC4AirportDeveloper::GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) {
    return cSC4PortDeveloper::GetLeastEfficientPort(efficiency, location);
}
void cSC4AirportDeveloper::GetAllPortLots(std::vector<cSC4Lot*>& out) {
    cSC4PortDeveloper::GetAllPortLots(out);
}
int cSC4AirportDeveloper::GetNumPorts(int sizeMask, int stageMask) {
    return cSC4PortDeveloper::GetNumPorts(sizeMask, stageMask);
}

//==================================================================================================
// Init / Shutdown and the department budget
//==================================================================================================

// The line item the department's funding percentage applies to. Borrowed (the department owns it).
cSC4LineItem* cSC4AirportDeveloper::DepartmentLineItem() {
    return department_ ? department_->GetLineItem(kAirportDepartmentLineItem) : nullptr;
}

// current/full expenses of the airport line item: the share of the required budget that is actually funded.
// The original reads both values even when the full expenses are zero; 1.0 means "no scaling".
float cSC4AirportDeveloper::FundingRatio() {
    if (cSC4LineItem* line = DepartmentLineItem()) {
        const std::int64_t current = line->GetCurrentExpenses();
        const std::int64_t full = line->GetFullExpenses();
        if (full > 0) return static_cast<float>(current) / static_cast<float>(full);
    }
    return 1.0f;
}

// @ 0x1000c81fc. Base Init first (tunables, simulator agent, services, notifications), then the airport
// department budget, then the six advisor notifications. The original checks only the base's initialised flag
// and returns success when it is already set.
bool cSC4AirportDeveloper::Init() {
    if (IsInitialized()) return true;
    if (!cSC4PortDeveloper::Init()) return false;
    IAirportDeveloperHost* host = AirportDeveloperHost();
    if (!host) return false;
    cSC4BudgetSimulator* budget = host->BudgetSimulator();
    if (!budget) return false;

    cSC4DepartmentBudget* department = budget->GetDepartmentBudget(kAirportAdvisorEventId);
    if (department) {
        department->AddRef();
    } else {
        department = budget->CreateDepartmentBudget(kAirportAdvisorEventId, kAirportDepartmentGroup);
        if (!department) return false;
        department->AddRef();
        department->SetDepartmentName(kAirportDepartmentNameGroup, kAirportDepartmentNameInstance);
        department->SetMaxAllowedFundingPercentage(kAirportMaxAllowedFundingPercentage);
        department->CreateLineItem(kAirportDepartmentLineItem, false);
        if (cSC4LineItem* line = department->GetLineItem(kAirportDepartmentLineItem)) {
            line->SetDisplayFlag(1, false);
        }
    }
    department_ = department;

    if (cIGZMessageServer2* server = MessageServerForTest()) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        for (std::uint32_t type : kAirportAdvisorMessages) server->AddNotification(target, type);
    }
    return true;
}

// @ 0x1000c8362. Remove the six airport notifications, let the base shut down, then release the department.
// The original reads the base's initialised flag and returns success when it is clear.
bool cSC4AirportDeveloper::Shutdown() {
    if (!IsInitialized()) return true;
    if (cIGZMessageServer2* server = MessageServerForTest()) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        for (std::uint32_t type : kAirportAdvisorMessages) server->RemoveNotification(target, type);
    }
    const bool result = cSC4PortDeveloper::Shutdown();
    if (department_) {
        department_->Release();
        department_ = nullptr;
    }
    return result;
}

// @ 0x1000c8808. The line item's full expenses are the airport's required monthly budget. The original takes
// the reference on the borrowed item around the call.
void cSC4AirportDeveloper::UpdateBudget() {
    cSC4LineItem* line = DepartmentLineItem();
    if (!line) return;
    line->AddRef();
    line->SetFullExpenses(GetRequiredBudget());
    line->Release();
}

// @ 0x1000c8880: the line item's current expenses.
std::int64_t cSC4AirportDeveloper::GetAllocatedFunding() {
    cSC4LineItem* line = DepartmentLineItem();
    return line ? line->GetCurrentExpenses() : 0;
}

//==================================================================================================
// airport queries
//==================================================================================================

// @ 0x1000c87c2. Size 0 returns the whole port list (the base +0x78 list size); any other size goes through
// GetNumPortsBySize, which turns it into GetNumPorts(1 << size, 0x1f).
int cSC4AirportDeveloper::GetNumAirports(int size) {
    if (size != 0) return GetNumPortsBySize(size);
    return static_cast<int>(PortLotCount());
}

// @ 0x1000c87dc.
std::uint32_t cSC4AirportDeveloper::GetTripsForPort(cPortLot& port) {
    return static_cast<std::uint32_t>(port.Trips());
}

// @ 0x1000c895e: every port's trips, summed through the virtual GetTripsForPort.
int cSC4AirportDeveloper::GetNumAirportTrips() {
    int total = 0;
    for (cPortLot* port : PortLots()) total += static_cast<int>(GetTripsForPort(*port));
    return total;
}

// @ 0x1000c88c0. The base's usage/capacity, then the capacity scaled by current/full expenses and the usage
// clamped to it (the original's UCOMISS/JBE pair).
bool cSC4AirportDeveloper::GetAirportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                                                 std::uint32_t& capacity) {
    if (!cSC4PortDeveloper::GetPortUsageValues(lot, usage, capacity)) return false;
    if (cSC4LineItem* line = DepartmentLineItem()) {
        const std::int64_t current = line->GetCurrentExpenses();
        const std::int64_t full = line->GetFullExpenses();
        if (full > 0) {
            const float scaled = static_cast<float>(capacity) *
                                 (static_cast<float>(current) / static_cast<float>(full));
            capacity = static_cast<std::uint32_t>(static_cast<std::int64_t>(scaled));
        }
    }
    if (usage > capacity) usage = capacity;
    return true;
}

// @ 0x1000c89a2. The stage's budget (cPortGrowthRule +0x08, not the construction cost at +0x18) scaled by the
// funding ratio. The original indexes the stage array unchecked; this port clamps the index.
std::int64_t cSC4AirportDeveloper::GetMaintenanceCost(const cSC4Lot* lot) {
    std::int64_t cost = 0;
    cPortLot* port = nullptr;
    if (GetPortLot(lot, port) && port) {
        const cPortTypeConfig* config = port->Config();
        const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
        if (config && stage < config->Stages().size()) cost = config->Stages()[stage].budget;
    }
    if (cSC4LineItem* line = DepartmentLineItem()) {
        const std::int64_t current = line->GetCurrentExpenses();
        const std::int64_t full = line->GetFullExpenses();
        if (full > 0) {
            cost = static_cast<std::int64_t>(static_cast<float>(cost) *
                                             (static_cast<float>(current) / static_cast<float>(full)));
        }
    }
    return cost;
}

//==================================================================================================
// the port-lot stage machine (the six advisor messages)
//==================================================================================================

// @ 0x1000c8742 / 0x1000c874e: the airport reacts to neither a stage change nor a new lot.
void cSC4AirportDeveloper::OnStageChange(cPortLot& port) { (void)port; }
void cSC4AirportDeveloper::OnNewLotCreated(cPortLot& port) { (void)port; }

// @ 0x1000c875a. One advisor event per size class: the first id while the port is at stage 0, the second for
// every later stage. A configuration with another size class sends event 0 (the original's uninitialised tail).
void cSC4AirportDeveloper::SendAdvisorUpgradeMessage(cPortLot& port) {
    const cPortTypeConfig* config = port.Config();
    const std::uint8_t size = config ? config->SizeClass() : 0;
    const bool firstStage = port.Stage() == 0;
    std::uint32_t event = 0;
    switch (size) {
        case 1: event = firstStage ? kAirportMessageSize1First : kAirportMessageSize1Later; break;
        case 2: event = firstStage ? kAirportMessageSize2First : kAirportMessageSize2Later; break;
        case 3: event = firstStage ? kAirportMessageSize3First : kAirportMessageSize3Later; break;
        default: break;
    }
    SendAdvisorMessage(event);
}

//==================================================================================================
// DoMonthlyTick: the airport's share of the office traffic
//==================================================================================================

// @ 0x1000c8a46. For every subnetwork the traffic simulator reports office occupants on, find the airports
// whose lots touch it and split those occupants between them in proportion to each port's current-stage
// capacity scaled by the funding ratio. The original builds a manual array of list sentinels indexed by the
// subnetwork id; std::vector<std::list<cPortLot*>> is the same structure.
void cSC4AirportDeveloper::DoMonthlyTick() {
    if (PortLots().empty()) return;
    IAirportDeveloperHost* host = AirportDeveloperHost();
    if (!host) return;
    cSC4TrafficSimulator* traffic = host->TrafficSimulator();
    if (!traffic) return;

    // The two office developer types whose occupants an airport serves.
    std::vector<std::uint32_t> officeMedium;
    std::vector<std::uint32_t> officeHigh;
    traffic->GetOccupantCountForAllSubnetworks(kAirportOfficeMediumType, officeMedium);
    traffic->GetOccupantCountForAllSubnetworks(kAirportOfficeHighType, officeHigh);

    std::vector<std::list<cPortLot*>> portsBySubnetwork(officeMedium.size());
    std::vector<std::uint32_t> subnetworks;
    for (cPortLot* port : PortLots()) {
        port->SetTrips(0);
        subnetworks.clear();
        cSC4Lot* lot = port->GetLot();
        if (lot) {
            // The original hands the simulator the cISC4Lot*; the recovered simulator reads the lot's
            // property holder (property 0x2a4f9c74 holds the subnetwork ids).
            traffic->GetSubnetworksForLot(&lot->Properties(), subnetworks);
        }
        for (std::uint32_t id : subnetworks) {
            // The original writes the bucket array unchecked; an id past the reported subnetwork count is
            // dropped here instead.
            if (id < portsBySubnetwork.size()) portsBySubnetwork[id].push_back(port);
        }
    }

    const float ratio = FundingRatio();

    for (std::size_t i = 0; i < portsBySubnetwork.size(); ++i) {
        std::list<cPortLot*>& bucket = portsBySubnetwork[i];
        if (bucket.empty()) continue;

        // Total current-stage capacity of the airports on this subnetwork.
        std::uint32_t capacityTotal = 0;
        for (cPortLot* port : bucket) {
            const cPortTypeConfig* config = port->Config();
            if (!config) continue;
            const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
            if (stage < config->Stages().size()) capacityTotal += config->Stages()[stage].capacity;
        }
        const std::uint32_t occupants =
            (i < officeMedium.size() ? officeMedium[i] : 0u) +
            (i < officeHigh.size() ? officeHigh[i] : 0u);
        const float scaledTotal =
            static_cast<float>(static_cast<std::int32_t>(static_cast<float>(capacityTotal) * ratio));
        // Funding 0 would divide by zero in the original (inf, then an INT_MIN trip count); keep the trips at 0.
        if (!(scaledTotal > 0.0f)) continue;

        for (cPortLot* port : bucket) {
            const cPortTypeConfig* config = port->Config();
            if (!config) continue;
            const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
            if (stage >= config->Stages().size()) continue;
            const float scaled = static_cast<float>(static_cast<std::uint32_t>(
                static_cast<std::int64_t>(static_cast<float>(config->Stages()[stage].capacity) * ratio)));
            const float share = scaled / scaledTotal * static_cast<float>(occupants);
            port->SetTrips(port->Trips() + static_cast<std::int32_t>(share));
        }
    }
}

//==================================================================================================
// DoMessage / streams
//==================================================================================================

// @ 0x1000c8544. The six advisor ids grow the ports and are consumed; everything else is the base's
// (monthly tick, post-city-init, placed building, lot state).
bool cSC4AirportDeveloper::DoMessage(cIGZMessage2* message) {
    if (!message) return true;
    const std::uint32_t type = message->GetType();
    switch (type) {
        case kAirportMessageSize1First:
        case kAirportMessageSize1Later:
        case kAirportMessageSize2First:
        case kAirportMessageSize2Later:
        case kAirportMessageSize3First:
        case kAirportMessageSize3Later:
            GrowPortsToNextStage();
            return true;
        default:
            return cSC4PortDeveloper::DoMessage(message);
    }
}

// @ 0x1000c85d0. The airport format starts with its own version (2) before the base's version-1 payload.
bool cSC4AirportDeveloper::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    return cSC4PortDeveloper::Write(stream);
}

// @ 0x1000c8656. A wrong airport version is rejected before the base's header is read.
bool cSC4AirportDeveloper::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    return cSC4PortDeveloper::Read(stream);
}

}  // namespace sc4
