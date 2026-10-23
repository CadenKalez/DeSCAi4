// cSC4SeaportDeveloper — see SeaportDeveloper.h for the layout, the seaport interface, cSC4LotPropFilter,
// the seam interfaces and the list of quirks. Every function carries the address of the original.
//
// The class is the concrete port developer for zone type 12: it plugs the seaport identity and the seaport
// budget department into the recovered cSC4PortDeveloper (PortDeveloper.cpp), feeds the traffic simulator
// with the trip capacity of every port lot, and owns the sea-access map the monthly water check uses.

#include "sc4/SeaportDeveloper.h"

#include <cstring>
#include <utility>

namespace sc4 {

namespace {

ISeaportHost* g_host = nullptr;

// The water bit and the four border marks (see the header). A connected water region is OR-ed with the mark
// of every city border it touches.
constexpr std::uint8_t kWater = kSeaportWaterBit;

// The sea-access byte of a cell, or 0 outside the map. The original indexes the vector unchecked
// (0x1000cafdc / 0x1000cb2a8); this port clamps so a stale lot rectangle cannot read past it.
std::uint8_t WaterByteAt(const std::vector<std::uint8_t>& map, std::int32_t width, std::int32_t height,
                         std::int32_t x, std::int32_t z) {
    if (x < 0 || z < 0 || x >= width || z >= height) return 0;
    const std::size_t index = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(x);
    return index < map.size() ? map[index] : 0;
}

// cSC4MapUtilities::FloodFill @ 0x1000d832c, narrowed to the four-connected water fill BuildWaterAccessMap
// runs: visit every water cell reachable from (x, z) that does not carry `mask` yet and OR `mask` into it.
// The original is a scanline fill driven by the AMFillRead / AMFillWrite / AMFillTest callbacks
// (0x1000cb45b / 0x1000cb466 / 0x1000cb485) whose test is `((mask | 0x80) & byte) == 0x80` and whose write is
// `byte |= mask`; a four-connected flood fill marks exactly the same cells.
void FloodFillWater(std::vector<std::uint8_t>& map, std::int32_t width, std::int32_t height,
                    std::int32_t startX, std::int32_t startZ, std::uint8_t mask) {
    auto visitedOrNotWater = [&](std::int32_t x, std::int32_t z) {
        return ((mask | kWater) & WaterByteAt(map, width, height, x, z)) != kWater;
    };
    if (visitedOrNotWater(startX, startZ)) return;
    std::vector<std::pair<std::int32_t, std::int32_t>> pending;
    pending.emplace_back(startX, startZ);
    while (!pending.empty()) {
        const auto [x, z] = pending.back();
        pending.pop_back();
        if (visitedOrNotWater(x, z)) continue;
        map[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)] |= mask;
        pending.emplace_back(x + 1, z);
        pending.emplace_back(x - 1, z);
        pending.emplace_back(x, z + 1);
        pending.emplace_back(x, z - 1);
    }
}

// The traffic simulator's property-holder argument: the lot's cISC4Lot::AsPropertyHolder view
// (lot vslot 0x18). The recovered cSC4Lot exposes its cSCPropertyHolder member.
cISCPropertyHolder* LotPropertyHolder(cSC4Lot* lot) {
    return lot ? static_cast<cISCPropertyHolder*>(&lot->Properties()) : nullptr;
}

}  // namespace

ISeaportHost* SeaportHost() { return g_host; }
void SetSeaportHost(ISeaportHost* host) { g_host = host; }

//==================================================================================================
// cSC4LotPropFilter
//==================================================================================================

// The original ctor is inlined at its only call site (GetLotProps): operator new(0x28), the
// cSC4OccupantFilter ctor, the two vptrs, +0x24 = 0, then the fields.
cSC4LotPropFilter::cSC4LotPropFilter(float minX, float minZ, float maxX, float maxZ, std::uint32_t property)
    : minX_(minX), minZ_(minZ), maxX_(maxX), maxZ_(maxZ), property_(property) {}

// @ 0x1000cb624. Props only (type 0xa823821e); the position must be inside the box; a non-zero property id
// additionally requires the prop's property holder to carry it. The original calls GetPosition with a
// three-float buffer and reads [0] / [2].
bool cSC4LotPropFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (!occupant) return false;
    if (occupant->GetType() != kOccupantTypeProp) return false;
    float position[3] = {0.0f, 0.0f, 0.0f};
    occupant->GetPosition(position);
    if (position[0] < minX_ || position[2] < minZ_) return false;
    if (maxX_ < position[0] || maxZ_ < position[2]) return false;
    if (property_ != 0) {
        cISCPropertyHolder* holder = occupant->AsPropertyHolder();
        if (!holder || !holder->HasProperty(property_)) return false;
    }
    return true;
}

//==================================================================================================
// construction / COM
//==================================================================================================

// @ 0x1000ca01e. The base's ctor runs first (it installs its own three vptrs), then this ctor installs the
// four seaport vptrs, clears +0xb8..+0xba and the water map, and writes the seaport identity.
cSC4SeaportDeveloper::cSC4SeaportDeveloper() {
    waterAccessCheckPending_ = false;
    forceWaterMapRebuild_ = false;
    cityHasSea_ = false;
    reservedBB_ = 0;
    waterAccess_.clear();
    waterMapWidth_ = 0;
    waterMapHeight_ = 0;
    SetIdentity(kSeaportPortDeveloperId, kSeaportZoneType, kSeaportHistoryCategory, kSeaportAdvisorEvent,
                kSeaportBuildingGroup);
}

// @ 0x1000ca0cc. Frees the sea-access map's buffer (the original deletes +0xc0 explicitly) and then runs the
// base destructor. The department budget reference is dropped by Shutdown, not here (see the header for where
// this port keeps that pointer).
cSC4SeaportDeveloper::~cSC4SeaportDeveloper() = default;

// @ 0x1000ca6a6. The seaport iid returns the primary view; everything else is the base's. The original
// compares the seaport iid first for every one of the four views' thunks.
bool cSC4SeaportDeveloper::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kSeaportDeveloperIID) {
        if (!out) return false;
        *out = static_cast<cISC4SeaportDeveloper*>(this);
        AddRef();
        return true;
    }
    return cSC4PortDeveloper::QueryInterface(iid, out);
}

// @ 0x1000ca73a.
std::uint32_t cSC4SeaportDeveloper::AddRef() { return cSC4PortDeveloper::AddRef(); }

// @ 0x1000ca76e.
std::uint32_t cSC4SeaportDeveloper::Release() { return cSC4PortDeveloper::Release(); }

// @ 0x1000ca7a2. The original forwards to the base slot, so Shutdown / the deleting destructor still run
// through the seaport's overrides.
void cSC4SeaportDeveloper::FinalRelease() { cSC4PortDeveloper::FinalRelease(); }

// @ 0x1000ca194. The base's Init first (tunables, simulator agent, services, the three messages), then the
// seaport department: get or create it, name it, allow 120% funding, create the single line item and hide it
// from the budget window's flag list; then AddRef it, register the two seaport messages and build the map.
bool cSC4SeaportDeveloper::Init() {
    if (IsInitialized()) return true;
    if (!cSC4PortDeveloper::Init()) return false;
    ISeaportHost* host = g_host;
    if (!host) return false;

    ISeaportDepartmentBudgetApi* department = nullptr;
    if (ISeaportBudgetSimApi* budget = host->BudgetSimulator()) {
        department = budget->GetDepartmentBudget(kSeaportDepartmentId);
        if (!department) {
            department = budget->CreateDepartmentBudget(kSeaportDepartmentId, kSeaportDepartmentGroup);
            if (department) {
                department->SetDepartmentName(kSeaportDepartmentNameGroup, kSeaportDepartmentNameInstance);
                const SC4Percentage maxFunding{kSeaportMaxFundingPercentage};
                department->SetMaxAllowedFundingPercentage(maxFunding);
                // `vslot 0x90(1, 0)`: display flag 1 off for the line item.
                if (ISeaportLineItemApi* lineItem = department->CreateLineItem(kSeaportLineItemId, false)) {
                    lineItem->SetDisplayFlag(1, false);
                }
            }
        }
        // The original dereferences the department unconditionally (0x1000ca248); a host without a budget
        // simulator leaves the seaport without a department, which every user tolerates.
        if (department) department->AddRef();
    }
    if (cIGZMessageServer2* messages = host->MessageServer()) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(static_cast<cSC4PortDeveloper*>(this));
        messages->AddNotification(target, kSeaportMessageGrowPorts);
        messages->AddNotification(target, kSeaportMessageWaterMapDirty);
    }
    BuildWaterAccessMap();
    return true;
}

// @ 0x1000ca624. Remove the two seaport messages while the message server is still the one the base holds,
// run the base's Shutdown (which removes the base's three and drops the services), then release the
// department budget reference Init took.
bool cSC4SeaportDeveloper::Shutdown() {
    if (!IsInitialized()) return true;
    ISeaportHost* host = g_host;
    if (host) {
        if (cIGZMessageServer2* messages = host->MessageServer()) {
            cIGZMessageTarget2* target =
                static_cast<cIGZMessageTarget2*>(static_cast<cSC4PortDeveloper*>(this));
            messages->RemoveNotification(target, kSeaportMessageGrowPorts);
            messages->RemoveNotification(target, kSeaportMessageWaterMapDirty);
        }
    }
    const bool result = cSC4PortDeveloper::Shutdown();
    if (host) {
        if (ISeaportBudgetSimApi* budget = host->BudgetSimulator()) {
            if (ISeaportDepartmentBudgetApi* department = budget->GetDepartmentBudget(kSeaportDepartmentId)) {
                department->Release();
            }
        }
    }
    return result;
}

//==================================================================================================
// the base's forwarding slots
//==================================================================================================

// @ 0x1000ca9aa.
float cSC4SeaportDeveloper::GetAvgEfficiency() { return cSC4PortDeveloper::GetAvgEfficiency(); }

// @ 0x1000caeee.
bool cSC4SeaportDeveloper::GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) {
    return cSC4PortDeveloper::GetLeastEfficientPort(efficiency, location);
}

// @ 0x1000cb5a2.
void cSC4SeaportDeveloper::GetAllPortLots(std::vector<cSC4Lot*>& out) {
    cSC4PortDeveloper::GetAllPortLots(out);
}

// @ 0x1000cb5b0.
int cSC4SeaportDeveloper::GetNumPorts(int sizeMask, int stageMask) {
    return cSC4PortDeveloper::GetNumPorts(sizeMask, stageMask);
}

// @ 0x1000cb5be: the cISC4PortDeveloper view of the object, i.e. the base subobject at +0x08.
cISC4PortDeveloper* cSC4SeaportDeveloper::AsISC4PortDeveloper() {
    return static_cast<cISC4PortDeveloper*>(static_cast<cSC4PortDeveloper*>(this));
}

// @ 0x1000cb5c8: the port list's size lives at base +0x78, which is this+0x80 in the object.
int cSC4SeaportDeveloper::GetNumSeaports() { return static_cast<int>(PortLotCount()); }

// @ 0x1000cb29c.
bool cSC4SeaportDeveloper::CityHasSea() { return cityHasSea_; }

// @ 0x1000cb2a8. The raw sea-access byte of a cell (all bits); the original indexes the map unchecked.
std::uint8_t cSC4SeaportDeveloper::GetCellSeaAccess(std::uint32_t x, std::uint32_t z) {
    if (x > 0x7fffffffu || z > 0x7fffffffu) return 0;
    return WaterByteAt(waterAccess_, waterMapWidth_, waterMapHeight_, static_cast<std::int32_t>(x),
                       static_cast<std::int32_t>(z));
}

// @ 0x1000ca82c. Version 2 of the base's record: the base's version-1 payload follows the seaport version.
bool cSC4SeaportDeveloper::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    return cSC4PortDeveloper::Write(stream);
}

// @ 0x1000ca8b2. Anything but version 2 is rejected before the base sees the stream.
bool cSC4SeaportDeveloper::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    return cSC4PortDeveloper::Read(stream);
}

// @ 0x1000ca7ba. Two seaport messages are intercepted; everything else (the monthly tick, the placed
// building, the lot state change and the post-city-init refresh) goes to the base.
bool cSC4SeaportDeveloper::DoMessage(cIGZMessage2* message) {
    if (!message) return true;
    const std::uint32_t type = message->GetType();
    if (type == kSeaportMessageWaterMapDirty) {
        BuildWaterAccessMap();
        waterAccessCheckPending_ = true;
        return true;
    }
    if (type == kSeaportMessageGrowPorts) {
        cSC4PortDeveloper::GrowPortsToNextStage();
        return true;
    }
    return cSC4PortDeveloper::DoMessage(message);
}

//==================================================================================================
// budget
//==================================================================================================

// The seaport's line item in the seaport department (base +0x50 in the original; looked up through the host
// here, see the header). Null when the department or the item does not exist yet.
ISeaportLineItemApi* cSC4SeaportDeveloper::BudgetLineItem() {
    ISeaportHost* host = g_host;
    if (!host) return nullptr;
    ISeaportBudgetSimApi* budget = host->BudgetSimulator();
    if (!budget) return nullptr;
    ISeaportDepartmentBudgetApi* department = budget->GetDepartmentBudget(kSeaportDepartmentId);
    return department ? department->GetLineItem(kSeaportLineItemId) : nullptr;
}

// The fraction the department actually funds: the line item's current expenses over its full expenses
// (0x1000ca9d9: `current / full` while full > 0, 1.0 otherwise).
float cSC4SeaportDeveloper::FundingRatio() {
    ISeaportLineItemApi* lineItem = BudgetLineItem();
    if (!lineItem) return 1.0f;
    const std::int64_t full = lineItem->GetFullExpenses();
    const std::int64_t current = lineItem->GetCurrentExpenses();
    if (full <= 0) return 1.0f;
    return static_cast<float>(current) / static_cast<float>(full);
}

// @ 0x1000cb146. The department's seaport line item's full expenses become the base's required budget.
void cSC4SeaportDeveloper::UpdateBudget() {
    ISeaportLineItemApi* lineItem = BudgetLineItem();
    if (!lineItem) return;   // the original would call through the null pointer
    lineItem->AddRef();
    lineItem->SetFullExpenses(cSC4PortDeveloper::GetRequiredBudget());
    lineItem->Release();
}

// @ 0x1000cb1be. The line item's current expenses are what the city allocated to the seaports.
std::int64_t cSC4SeaportDeveloper::GetAllocatedFunding() {
    ISeaportLineItemApi* lineItem = BudgetLineItem();
    if (!lineItem) return 0;   // the original would call through the null pointer
    return lineItem->GetCurrentExpenses();
}

// @ 0x1000ca9c2. Hand the traffic simulator the port's trip capacity for travel type 3, scaled by the
// funding ratio: the capacity is the port's stage capacity, the scale the department's current/full
// expenses. The capacity is truncated to an integer after the multiply (0x1000caa8c: `cvttss2si`).
void cSC4SeaportDeveloper::UpdateTripCapacity(cPortLot& port) {
    const float ratio = FundingRatio();
    cSC4Lot* lot = port.GetLot();
    if (!lot) return;   // the original would call through the null lot
    std::int64_t capacity = 0;
    if (cPortTypeConfig* config = port.Config()) {
        const std::uint32_t stage = static_cast<std::uint32_t>(port.Stage());
        if (stage < config->Stages().size()) capacity = config->Stages()[stage].capacity;
    }
    ISeaportHost* host = g_host;
    if (!host) return;
    ISeaportTrafficApi* traffic = host->TrafficSimulator();
    if (!traffic) return;
    const float scaled = static_cast<float>(capacity) * ratio;
    traffic->SetMaxTripCapacity(LotPropertyHolder(lot), kSeaportTravelType,
                                static_cast<std::uint32_t>(static_cast<std::int64_t>(scaled)));
}

//==================================================================================================
// the port-lot callbacks
//==================================================================================================

// @ 0x1000cae76. Trips the traffic simulator recorded for the port's lot on travel type 3
// (city->GetTrafficSimulator() then GetTrafficArrived(lot->AsPropertyHolder(), 3)).
std::uint32_t cSC4SeaportDeveloper::GetTripsForPort(cPortLot& port) {
    ISeaportHost* host = g_host;
    if (!host) return 0;
    ISeaportCityApi* city = host->SeaportCity();
    if (!city) return 0;
    ISeaportTrafficApi* traffic = city->GetTrafficSimulator();
    if (!traffic) return 0;
    traffic->AddRef();
    cSC4Lot* lot = port.GetLot();
    const std::uint32_t trips = traffic->GetTrafficArrived(LotPropertyHolder(lot), kSeaportTravelType);
    traffic->Release();
    return trips;
}

// @ 0x1000caa8e.
void cSC4SeaportDeveloper::OnStageChange(cPortLot& port) { UpdateTripCapacity(port); }

// @ 0x1000caaa6. Refresh the port's trip capacity, mark the monthly water check as pending, and — when the
// port type has a per-vertex height grid — flatten the new lot's terrain to it, rotated by the lot's facing.
void cSC4SeaportDeveloper::OnNewLotCreated(cPortLot& port) {
    UpdateTripCapacity(port);
    waterAccessCheckPending_ = true;

    ISeaportHost* host = g_host;
    if (!host) return;
    ISeaportTerrainApi* terrain = host->Terrain();
    cPortTypeConfig* config = port.Config();
    if (!terrain || !config) return;
    const std::vector<std::vector<float>>& grid = config->Grid();
    if (grid.empty()) return;

    terrain->AddRef();
    cSC4Lot* lot = port.GetLot();
    if (lot) {
        std::int32_t rect[4] = {0, 0, 0, 0};
        lot->GetBoundingRect(rect);
        const std::int64_t x0 = rect[0], z0 = rect[1], x1 = rect[2], z1 = rect[3];
        // The grid must cover exactly the lot's interior cells (0x1000cab65).
        const std::int64_t rows = static_cast<std::int64_t>(grid.size());
        const std::int64_t columns = grid.empty() ? 0 : static_cast<std::int64_t>(grid[0].size());
        if (rows * columns == (x1 - x0) * (z1 - z0)) {
            const int facing = lot->GetFacing();
            ISeaportCityApi* city = host->SeaportCity();
            for (std::int32_t z = rect[1]; z < rect[3]; ++z) {
                for (std::int32_t x = rect[0]; x < rect[2]; ++x) {
                    std::int64_t row = 0;
                    std::int64_t column = 0;
                    switch (facing) {
                        case 0:   // grid[x - x0][z - z0]
                            row = x - x0;
                            column = z - z0;
                            break;
                        case 1:   // grid[z1 - 1 - z][x1 - 1 - x]
                            row = (z1 - 1) - z;
                            column = (x1 - 1) - x;
                            break;
                        case 2:   // grid[x1 - 1 - x][z1 - 1 - z]
                            row = (x1 - 1) - x;
                            column = (z1 - 1) - z;
                            break;
                        case 3:   // grid[z - z0][x - x0]
                            row = z - z0;
                            column = x - x0;
                            break;
                        default:
                            continue;
                    }
                    if (row < 0 || column < 0 || row >= rows ||
                        column >= static_cast<std::int64_t>(grid[static_cast<std::size_t>(row)].size())) {
                        continue;
                    }
                    const float altitude = grid[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                    // The original converts the cell centre's world position to int and uses it as the
                    // terrain vertex index (0x1000cabe5 / 0x1000cac90); with 16 m cells that is
                    // 16 * cell + 8, i.e. the height lands on the wrong vertex. Kept as is.
                    float worldX = 0.0f;
                    float worldZ = 0.0f;
                    if (city) city->CellCenterToPosition(x, z, worldX, worldZ);
                    terrain->SetAltitudeAtVertex(static_cast<std::int32_t>(worldX),
                                                static_cast<std::int32_t>(worldZ), altitude);
                }
            }
        }
    }
    terrain->Release();
}

// @ 0x1000ca986: advisor event 0x8a394dc4 (also the "grow the ports" message id).
void cSC4SeaportDeveloper::SendAdvisorUpgradeMessage(cPortLot& port) {
    (void)port;   // the argument is part of the pure slot, the original ignores it
    SendAdvisorMessage(kSeaportAdvisorUpgrade);
}

//==================================================================================================
// the sea-access map
//==================================================================================================

// @ 0x1000ca290. Build the per-cell water map from the terrain, then mark every water region with the city
// borders it reaches (1 north, 8 south, 2 west, 4 east) and remember whether the city has open sea at all.
void cSC4SeaportDeveloper::BuildWaterAccessMap() {
    ISeaportHost* host = g_host;
    ISeaportTerrainApi* terrain = host ? host->Terrain() : nullptr;
    if (!terrain) return;   // 0x1000ca2a2: without a terrain the map is left alone

    const std::int32_t width = terrain->CellCountX();
    const std::int32_t height = terrain->CellCountZ();
    waterMapWidth_ = width;
    waterMapHeight_ = height;
    const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    waterAccess_.resize(cells);
    for (std::int32_t z = 0; z < height; ++z) {
        for (std::int32_t x = 0; x < width; ++x) {
            waterAccess_[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)] = terrain->CellHasWater(x, z) ? kWater : 0;
        }
    }
    cityHasSea_ = false;
    if (width <= 0 || height <= 0) {
        forceWaterMapRebuild_ = false;
        return;
    }
    // The original starts one fill per run of border water cells (the `bVar11` state in 0x1000ca3d0);
    // filling from every border water cell marks the same regions.
    auto fillFrom = [&](std::int32_t x, std::int32_t z, std::uint8_t mask) {
        if ((WaterByteAt(waterAccess_, width, height, x, z) & kWater) == 0) return;
        cityHasSea_ = true;
        FloodFillWater(waterAccess_, width, height, x, z, mask);
    };
    for (std::int32_t x = 0; x < width; ++x) fillFrom(x, 0, kSeaportBorderNorth);
    for (std::int32_t x = 0; x < width; ++x) fillFrom(x, height - 1, kSeaportBorderSouth);
    for (std::int32_t z = 0; z < height; ++z) fillFrom(0, z, kSeaportBorderWest);
    for (std::int32_t z = 0; z < height; ++z) fillFrom(width - 1, z, kSeaportBorderEast);
    forceWaterMapRebuild_ = false;
}

// @ 0x1000cafdc. True when any cell of the port's lot footprint carries one of the four border marks.
bool cSC4SeaportDeveloper::CalculateWaterAccessible(cPortLot& port) {
    cSC4Lot* lot = port.GetLot();
    if (!lot) return false;   // the original would read through the null lot
    std::int32_t rect[4] = {0, 0, 0, 0};
    lot->GetBoundingRect(rect);
    for (std::int32_t z = rect[1]; z <= rect[3]; ++z) {
        for (std::int32_t x = rect[0]; x <= rect[2]; ++x) {
            if ((WaterByteAt(waterAccess_, waterMapWidth_, waterMapHeight_, x, z) & kSeaportBorderMask) != 0) {
                return true;
            }
        }
    }
    return false;
}

// @ 0x1000caf06. The seaport's monthly pass: rebuild the map when asked, refresh every port's trip capacity,
// and — when a port was added or the map changed — flag the ports without sea access and raise the advisor
// event once for the whole city.
void cSC4SeaportDeveloper::DoMonthlyTick() {
    if (forceWaterMapRebuild_) BuildWaterAccessMap();
    for (cPortLot* port : MutablePortLots()) UpdateTripCapacity(*port);
    if (!waterAccessCheckPending_) return;
    waterAccessCheckPending_ = false;
    bool anyWithoutSeaAccess = false;
    for (cPortLot* port : MutablePortLots()) {
        if (CalculateWaterAccessible(*port)) continue;
        SetLotPropsState(port->GetLot(), kSeaportWaterAccessProperty, 1);
        anyWithoutSeaAccess = true;
    }
    if (anyWithoutSeaAccess) SendAdvisorMessage(kSeaportAdvisorNoSeaAccess);
}

//==================================================================================================
// the lot's prop occupants
//==================================================================================================

// @ 0x1000cb2c2. Build the cSC4LotPropFilter for the lot's box and ask the occupant manager for the props in
// the lot's cells that carry `property`. The box runs from the cell centre of (x0, z0) to the cell centre of
// (x1 + 1, z1 + 1) (the original's two `incl`s), while the cell rectangle is the lot's own inclusive one.
void cSC4SeaportDeveloper::GetLotProps(const cSC4Lot* lot, OccupantList& out, std::uint32_t property) {
    if (!lot) return;   // the original would read through the null lot
    std::int32_t rect[4] = {0, 0, 0, 0};
    lot->GetBoundingRect(rect);
    ISeaportHost* host = g_host;
    if (!host) return;
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
    if (ISeaportCityApi* city = host->SeaportCity()) {
        city->CellCenterToPosition(rect[0], rect[1], minX, minZ);
        city->CellCenterToPosition(rect[2] + 1, rect[3] + 1, maxX, maxZ);
    }
    cSC4LotPropFilter* filter = new cSC4LotPropFilter(minX, minZ, maxX, maxZ, property);
    filter->AddRef();
    const int low[2] = {rect[0], rect[1]};
    const int high[2] = {rect[2], rect[3]};
    if (ISeaportOccupantManagerApi* manager = host->OccupantManager()) {
        manager->GetOccupantsByStandardCityCells(out, low, high, filter, 0xffffffffu);
    }
    filter->Release();
}

// @ 0x1000cb058. Put every prop of the lot carrying `property` into state `state` (the seaport uses this to
// mark the props of a port without sea access).
void cSC4SeaportDeveloper::SetLotPropsState(const cSC4Lot* lot, std::uint32_t property, std::int32_t state) {
    OccupantList props;
    GetLotProps(lot, props, property);
    ISeaportHost* host = g_host;
    for (cISC4Occupant* occupant : props) {
        ISeaportPropOccupantApi* prop = host ? host->AsPropOccupant(occupant) : nullptr;
        if (!prop) continue;
        prop->SetState(static_cast<std::uint32_t>(state));
        prop->Release();
    }
    if (host) {
        if (ISeaportOccupantManagerApi* manager = host->OccupantManager()) manager->ReleaseOccupantList(props);
    }
    props.clear();
}

//==================================================================================================
// the seaport interface queries
//==================================================================================================

// @ 0x1000cb1fe. The base's usage / capacity for the lot, with the capacity scaled by the funding ratio;
// the usage is then capped at the scaled capacity.
bool cSC4SeaportDeveloper::GetSeaportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                                                 std::uint32_t& capacity) {
    if (!cSC4PortDeveloper::GetPortUsageValues(lot, usage, capacity)) return false;
    if (ISeaportLineItemApi* lineItem = BudgetLineItem()) {
        const std::int64_t full = lineItem->GetFullExpenses();
        const std::int64_t current = lineItem->GetCurrentExpenses();
        if (0 < full) {
            capacity = static_cast<std::uint32_t>(static_cast<std::int64_t>(
                static_cast<float>(capacity) *
                (static_cast<float>(current) / static_cast<float>(full))));
        }
    }
    if (capacity < usage) usage = capacity;
    return true;
}

// @ 0x1000cb4ae. The sum of the virtual GetTripsForPort over every port lot.
int cSC4SeaportDeveloper::GetNumSeaportTrips() {
    int total = 0;
    for (cPortLot* port : MutablePortLots()) total += static_cast<int>(GetTripsForPort(*port));
    return total;
}

// @ 0x1000cb4f2. The port's stage *budget* (+0x08 of the growth rule, not the cost) scaled by the funding
// ratio; 0 for a lot that is not a port.
std::int64_t cSC4SeaportDeveloper::GetMaintenanceCost(const cSC4Lot* lot) {
    std::int64_t cost = 0;
    cPortLot* port = nullptr;
    if (cSC4PortDeveloper::GetPortLot(lot, port) && port) {
        if (cPortTypeConfig* config = port->Config()) {
            const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
            if (stage < config->Stages().size()) cost = config->Stages()[stage].budget;
        }
    }
    if (ISeaportLineItemApi* lineItem = BudgetLineItem()) {
        const std::int64_t full = lineItem->GetFullExpenses();
        const std::int64_t current = lineItem->GetCurrentExpenses();
        if (0 < full) cost = static_cast<std::int64_t>(static_cast<float>(cost) *
                                                       (static_cast<float>(current) / static_cast<float>(full)));
    }
    return cost;
}

}  // namespace sc4
