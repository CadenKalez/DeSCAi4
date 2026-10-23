// cSC4PortDeveloper — see PortDeveloper.h for the layout, the two cPortLot/cPortTypeConfig support
// classes, the vtable slots the derived developers implement and the list of quirks.
//
// Every call outside the object goes through IPortDeveloperHost (one method per original call, with its
// global and vtable slot); the lots are the recovered cSC4Lot (Lot.h), the occupants/properties/messages
// are the recovered interfaces. The address of the original function is on every definition.
//
// Two functions deviate from the binary on purpose (both would be undefined behaviour otherwise, and the
// header documents them): GetLeastEfficientPort returns the first port lot instead of spinning forever
// when there is more than one (0x1000cdbc1 is `jmp self`), and GetLeastEfficientPort / GetRequiredBudget /
// GetPortUsageValues / IsGrowthTriggered clamp a stage index that the original reads unchecked.

#include "sc4/PortDeveloper.h"

#include <algorithm>
#include <cstring>

#include "rz/cRZMessage2Standard.h"

namespace sc4 {

namespace {

IPortDeveloperHost* g_host = nullptr;

const SC4Percentage kPercentageZero{0.0f};

// The float->int conversion the original uses for the history percentage (0x1000ccbcf..0x1000ccbed):
// y = efficiency * 100.0f + 12582912.0f; MOVD the float's bits to EDX; ADD EDX, 0xb4c00000. Adding the
// negated bit pattern of 12582912.0f (0x4b400000) yields the rounded value in the mantissa, so the result
// is round(efficiency * 100) without a float->int conversion. Kept bit-exact.
std::int32_t RoundPercentage(float value) {
    const float shifted = value * 100.0f + 12582912.0f;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &shifted, sizeof bits);
    return static_cast<std::int32_t>(bits + 0xb4c00000u);
}

// Lot.cpp's concrete lot answers QueryInterface for the cISC4Lot iid, but the port's configuration view
// needs the three calls of IPortLotConfiguration (GetID/GetGrowthStage/IsCompatibleWithZoneType).
IPortLotConfiguration* ConfigurationOf(cSC4Lot* lot) {
    if (!lot) return nullptr;
    return static_cast<IPortLotConfiguration*>(lot->GetLotConfiguration());
}

// The single configuration lookup used by swapLot (cSC4LotConfigurationManager vslot 0x40, borrowed).
IPortLotConfiguration* FindConfiguration(std::uint32_t id) {
    if (!g_host) return nullptr;
    IPortLotConfigurationManagerApi* manager = g_host->LotConfigurationManager();
    return manager ? manager->GetLotConfiguration(id) : nullptr;
}

}  // namespace

IPortDeveloperHost* PortDeveloperHost() { return g_host; }
void SetPortDeveloperHost(IPortDeveloperHost* host) { g_host = host; }

//==================================================================================================
// PortCellRegion (SC4CellRegion<int> + cRZCellMap<bool>)
//==================================================================================================

// The cell map ctor of 0x1000cd332..: rowCount = x1+1-x0, bitCount = z1+1-z0, wordCount =
// (bitCount >> 5) + 1 - (bitCount & 31 == 0), then every word is set to 0xffffffff.
void PortCellRegion::Fill(int x0_, int z0_, int x1_, int z1_) {
    x0 = x0_;
    z0 = z0_;
    x1 = x1_;
    z1 = z1_;
    const std::uint32_t width = static_cast<std::uint32_t>(x1_ + 1 - x0_);
    const std::uint32_t height = static_cast<std::uint32_t>(z1_ + 1 - z0_);
    rowCount = width;
    bitCount = height;
    wordCount = (height >> 5) + 1u - ((height & 0x1fu) == 0 ? 1u : 0u);
    rows.assign(width, std::vector<std::uint32_t>(wordCount, 0xffffffffu));
}

bool PortCellRegion::Test(int x, int z) const {
    const std::uint32_t dx = static_cast<std::uint32_t>(x - x0);
    const std::uint32_t dz = static_cast<std::uint32_t>(z - z0);
    if (dx >= rowCount || dz >= bitCount || dx >= rows.size()) return false;
    return ((rows[dx][dz >> 5] >> (dz & 0x1f)) & 1u) != 0;
}

//==================================================================================================
// cPortLot
//==================================================================================================

// @ 0x1000cdcda: release the cached lot.
cPortLot::~cPortLot() {
    if (lot_) {
        lot_->Release();
        lot_ = nullptr;
    }
}

// @ 0x1000cc3e4. The original AddRef's SL::spLotManager around the lookup, takes a reference on the lot
// and clears the stale flag. A null manager leaves the cached pointer in place (the original would fault).
cSC4Lot* cPortLot::GetLot() {
    if (lotStale_) {
        IPortLotManagerApi* lotManager = g_host ? g_host->LotManager() : nullptr;
        if (lotManager) {
            lotManager->AddRef();
            cSC4Lot* lot = lotManager->GetLot(x_, z_, true);
            lot_ = lot;
            if (lot) lot->AddRef();
            lotManager->Release();
            lotStale_ = false;
        }
    }
    return lot_;
}

// @ 0x1000ccf24. The stage's capacity is read before the cached lot is refreshed; the lot state (2 =
// occupied) suppresses growth. The capacity is compared as float: capacity * 0.96 <= trips.
bool cPortLot::IsGrowthTriggered(cSC4PortDeveloper* owner) {
    if (!owner || !config_) return false;
    if (static_cast<std::uint32_t>(stage_) + 1u >= config_->StageCount()) return false;
    const std::uint32_t trips = owner->GetTripsForPort(*this);
    const std::uint32_t stage = static_cast<std::uint32_t>(stage_);
    // The original indexes stages[stage] unchecked (stage+1 < stageCount bounds it for a well-formed
    // configuration); clamp when the exemplar is inconsistent.
    const std::uint32_t capacity =
        stage < config_->Stages().size() ? config_->Stages()[stage].capacity : 0u;
    cSC4Lot* lot = GetLot();
    if (!lot) return false;
    const bool occupied = lot->GetState() == static_cast<std::uint8_t>(kLotStateOccupied);
    if (occupied) return false;
    return static_cast<float>(capacity) * 0.96f <= static_cast<float>(trips);
}

//==================================================================================================
// cPortTypeConfig
//==================================================================================================

// The ctor stores at 0x1000cd707: vptr, +0x08 = 0, +0x0c = -1, +0x10/+0x14 = 0, the two vectors empty,
// +0x30 = 2.
cPortTypeConfig::cPortTypeConfig() = default;
cPortTypeConfig::~cPortTypeConfig() = default;

// @ 0x1000cd79e. The stage count comes from property 0xca271780 (read with GetValUint32); the four stage
// arrays must all exist before any rule is built; the threshold/size/per-cell grid are read afterwards.
// The original reads `stageCount` entries from each array without checking them (the port clamps), and it
// returns true exactly when the four arrays were present.
bool cPortTypeConfig::LoadConfiguration(int typeIndex, cISCPropertyHolder* holder) {
    typeIndex_ = typeIndex;
    if (!holder) return false;

    std::uint32_t stageCount = 0;
    const bool hasStageCount = cSCPropertyHelp::GetPropertyValue(holder, 0xca271780, stageCount);
    bool arraysPresent = false;
    if (hasStageCount) {
        stageCount_ = stageCount;
        std::vector<std::uint32_t> lotConfigurationIds;
        std::vector<std::int64_t> budgets;
        std::vector<std::uint32_t> capacities;
        std::vector<std::int64_t> costs;
        arraysPresent = cSCPropertyHelp::GetPropertyValue(holder, 0xca271781, lotConfigurationIds) &&
                        cSCPropertyHelp::GetPropertyValue(holder, 0xca271783, budgets) &&
                        cSCPropertyHelp::GetPropertyValue(holder, 0xca271784, capacities) &&
                        cSCPropertyHelp::GetPropertyValue(holder, 0xca271786, costs);
        if (arraysPresent) {
            std::size_t count = stageCount_;
            count = std::min(count, lotConfigurationIds.size());
            count = std::min(count, budgets.size());
            count = std::min(count, capacities.size());
            count = std::min(count, costs.size());
            stages_.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                cPortGrowthRule rule;
                rule.stage = static_cast<std::int32_t>(i);
                rule.lotConfigurationId = static_cast<std::int32_t>(lotConfigurationIds[i]);
                rule.budget = budgets[i];
                rule.capacity = capacities[i];
                rule.cost = costs[i];
                stages_.push_back(rule);
            }
        }
    }

    float threshold = 0.0f;
    if (cSCPropertyHelp::GetPropertyValue(holder, 0xca271785, threshold)) {
        threshold_ = static_cast<float>(threshold * 0.01);   // the original multiplies in double
    }
    cSCPropertyHelp::GetPropertyValue(holder, 0xca271787, sizeClass_);

    std::uint8_t rows = 0;
    std::uint8_t columns = 0;
    cSCPropertyHelp::GetPropertyValue(holder, 0xca271788, rows);
    cSCPropertyHelp::GetPropertyValue(holder, 0xca271789, columns);
    std::vector<float> cells;
    if (rows != 0 && columns != 0 &&
        cSCPropertyHelp::GetPropertyValue(holder, 0xca271800, cells)) {
        if (static_cast<std::size_t>(rows) * columns == cells.size()) {
            grid_.assign(rows, std::vector<float>(columns, 0.0f));
            for (std::size_t r = 0; r < rows; ++r) {
                for (std::size_t c = 0; c < columns; ++c) {
                    grid_[r][c] = cells[r * columns + c];
                }
            }
        }
    }
    return arraysPresent;
}

//==================================================================================================
// construction / COM
//==================================================================================================

// @ 0x1000cbe6a. The three vptrs are set by the compiler; the body clears +0x18..+0x54, sets +0x64 to -1
// and installs the list sentinel. The identity fields (+0x20/+0x58/+0x5c/+0x60/+0x64) are left to the
// derived constructor.
cSC4PortDeveloper::cSC4PortDeveloper() {
    tunablesLoaded_ = false;
    initialized_ = false;
    reserved1a_ = 0;
    refCount_ = 0;
    portDeveloperId_ = 0;
    portTypeCount_ = 0;
    messageServer_ = nullptr;
    zoneManager_ = nullptr;
    lotManager_ = nullptr;
    city_ = nullptr;
    budget_ = nullptr;
    reserved50_ = 0;
    reserved54_ = 0;
    zoneType_ = 0;
    reserved5a_ = 0;
    historyCategory_ = 0;
    advisorEventId_ = 0;
    buildingGroup_ = 0xffffffffu;   // the original's `mov dword [this+0x64], -1`
}

// @ 0x1000cbf1c. Frees the two vectors' buffers and the list's nodes; the elements are the ones Shutdown
// deleted (the original calls operator delete on the buffers and std::list::clear). The member destructors
// do exactly that, so the body is empty by design.
cSC4PortDeveloper::~cSC4PortDeveloper() = default;

// @ 0x1000cc4fe. The ordering of the comparisons is the original's (the iid is compared against 0x452294aa
// first and the serializer last).
bool cSC4PortDeveloper::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kPortGZIIDMessageTarget2) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == kPortGZIIDSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kPortGZIIDPortDeveloper || iid == 1) {
        *out = static_cast<cISC4PortDeveloper*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// @ 0x1000cc44e.
std::uint32_t cSC4PortDeveloper::AddRef() { return ++refCount_; }

// @ 0x1000cc478. Below 2 the object is destroyed through FinalRelease; the count is not decremented.
std::uint32_t cSC4PortDeveloper::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

// @ 0x1000cc4d4: Shutdown (through the vtable), then the deleting destructor.
void cSC4PortDeveloper::FinalRelease() {
    Shutdown();
    delete this;
}

// @ 0x1000cbf9e. readTunableVars, the simulator agent registration, the four services and the message
// server with its three notifications. The original dereferences a missing global; the port reports false.
bool cSC4PortDeveloper::Init() {
    if (initialized_) return true;
    initialized_ = true;
    readTunableVars();
    IPortDeveloperHost* host = g_host;
    if (!host) return false;
    if (IPortSimulatorApi* simulator = host->Simulator()) {
        simulator->AddAgent(static_cast<cIGZMessageTarget2*>(this), kPortSimulatorInterval,
                            kPortSimulatorName, kPortSimulatorFlags);
    }
    if (!city_) {
        city_ = host->City();
        if (!city_) return false;
        city_->AddRef();
    }
    if (!zoneManager_) {
        zoneManager_ = host->ZoneManager();
        if (!zoneManager_) return false;
        zoneManager_->AddRef();
    }
    if (!lotManager_) {
        lotManager_ = host->LotManager();
        if (!lotManager_) return false;
        lotManager_->AddRef();
    }
    if (!budget_) {
        budget_ = host->Budget();
        if (!budget_) return false;
        budget_->AddRef();
    }
    if (!messageServer_) {
        messageServer_ = host->MessageServer();
        if (!messageServer_) return false;
        messageServer_->AddRef();
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messageServer_->AddNotification(target, kMessageLotStateChanged);
        messageServer_->AddNotification(target, kMessagePortPostCityInit);
        messageServer_->AddNotification(target, kMessagePortBuildingPlaced);
    }
    return true;
}

// @ 0x1000cc128. The exemplar is keyed by the port developer id (+0x20, zero instance = no exemplar);
// property 0x8a270fc3 holds the port type id array. The flag at +0x18 is only set when +0x20 was non-zero.
void cSC4PortDeveloper::readTunableVars() {
    if (tunablesLoaded_ || portDeveloperId_ == 0) return;
    if (IPortDeveloperHost* host = g_host) {
        if (IPortResourceApi* resources = host->ResourceManager()) {
            cISCPropertyHolder* holder = resources->LoadExemplar(portDeveloperId_);
            if (holder) {
                std::vector<std::uint32_t> ids;
                if (cSCPropertyHelp::GetPropertyValue(holder, kPortTypeListProperty, ids)) {
                    portTypeCount_ = static_cast<int>(ids.size());
                    portTypeIds_.assign(ids.begin(), ids.end());
                }
                holder->Release();
                for (int i = 0; i < portTypeCount_; ++i) loadLotConfiguration(i);
            }
        }
    }
    tunablesLoaded_ = true;
}

// @ 0x1000cc26e. Removes the three notifications, deletes the port lots and the configurations, releases
// the four services (budget, lot manager, zone manager, city) and removes the simulator agent through the
// *global* city's simulator.
bool cSC4PortDeveloper::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;
    if (messageServer_) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messageServer_->RemoveNotification(target, kMessageLotStateChanged);
        messageServer_->RemoveNotification(target, kMessagePortPostCityInit);
        messageServer_->RemoveNotification(target, kMessagePortBuildingPlaced);
        messageServer_->Release();
        messageServer_ = nullptr;
    }
    for (cPortLot* port : portLots_) {
        port->GetLot();   // the original refreshes the cached lot first (discarding it)
        delete port;
    }
    portLots_.clear();
    for (cPortTypeConfig* config : configurations_) delete config;   // the original's deleting dtor call
    configurations_.clear();
    if (budget_) {
        budget_->Release();
        budget_ = nullptr;
    }
    if (lotManager_) {
        lotManager_->Release();
        lotManager_ = nullptr;
    }
    if (zoneManager_) {
        zoneManager_->Release();
        zoneManager_ = nullptr;
    }
    if (city_) {
        city_->Release();
        city_ = nullptr;
    }
    if (g_host) {
        if (IPortCityApi* city = g_host->City()) {
            if (IPortSimulatorApi* simulator = city->GetSimulator()) {
                simulator->RemoveAgent(static_cast<cIGZMessageTarget2*>(this));
            }
        }
    }
    return true;
}

// @ 0x1000ccddc.
std::int64_t cSC4PortDeveloper::GetRequiredBudget() {
    std::int64_t total = 0;
    for (cPortLot* port : portLots_) {
        cPortTypeConfig* config = port->Config();
        if (!config) continue;
        const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
        if (stage >= config->Stages().size()) continue;
        total += config->Stages()[stage].budget;
    }
    return total;
}

// @ 0x1000cce10.
float cSC4PortDeveloper::GetAvgEfficiency() {
    if (portLots_.empty()) return 1.0f;
    float sum = 0.0f;
    for (cPortLot* port : portLots_) sum += port->Efficiency();
    return sum / static_cast<float>(portLots_.size());
}

// @ 0x1000cce78. usage = the port's trips (its virtual GetTripsForPort), capacity = the stage's capacity;
// usage is capped at capacity. Not found (or no lot) leaves both zero and returns false.
bool cSC4PortDeveloper::GetPortUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                                           std::uint32_t& capacity) {
    bool found = false;
    capacity = 0;
    usage = 0;
    if (lot) {
        for (cPortLot* port : portLots_) {
            if (port->GetLot() != lot) continue;
            usage = GetTripsForPort(*port);
            cPortTypeConfig* config = port->Config();
            if (config) {
                const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
                if (stage < config->Stages().size()) capacity = config->Stages()[stage].capacity;
            }
            found = true;
            break;
        }
    }
    if (capacity < usage) usage = capacity;
    return found;
}

// @ 0x1000ccfaa. cRZMessage2Standard(0x89efa536, data1 = category, data2 = value), sent to SL::spMS2.
void cSC4PortDeveloper::RecordHistoryData(std::uint32_t category, std::uint32_t value) {
    if (!messageServer_) return;
    cRZMessage2Standard* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kMessagePortHistory);
    message->SetData1(category);
    message->SetData2(value);
    messageServer_->MessageSend(message);
    message->Release();
}

// @ 0x1000cd04e. Every triggered port advances one stage through swapLot; the budget for the new stage is
// withdrawn even when the stage counter cannot move (the original checks stageCount twice).
void cSC4PortDeveloper::GrowPortsToNextStage() {
    for (cPortLot* port : portLots_) {
        if (!port->IsGrowthTriggered(this)) continue;
        cPortTypeConfig* config = port->Config();
        if (!config) continue;
        const std::uint32_t next = static_cast<std::uint32_t>(port->Stage()) + 1u;
        if (next >= config->StageCount() || next >= config->Stages().size()) continue;
        const cPortGrowthRule rule = config->Stages()[next];
        if (!swapLot(*port, static_cast<std::uint32_t>(rule.lotConfigurationId))) continue;
        if (static_cast<std::uint32_t>(port->Stage()) + 1u < config->StageCount()) {
            port->SetStage(port->Stage() + 1);
        }
        if (budget_) budget_->WithdrawFunds(rule.cost);
        OnStageChange(*port);
        UpdateBudget();
    }
}

// @ 0x1000cd116. Demolish the port's lot and rebuild it with the next configuration: the old footprint is
// reused (x0/z0, x1+1-x0 by z1+1-z0, the old facing), the configuration is assigned, then the lot developer
// constructs the default building. Returns ConstructDefaultLot's result.
bool cSC4PortDeveloper::swapLot(cPortLot& port, std::uint32_t lotConfigurationId) {
    cSC4Lot* lot = port.GetLot();
    if (!lot) return false;
    IPortLotConfiguration* config = FindConfiguration(lotConfigurationId);
    if (!config) return false;
    IPortLotDeveloperApi* developer = nullptr;
    if (g_host) {
        if (IPortBuildingDevelopmentApi* buildingDevelopment = g_host->BuildingDevelopment()) {
            developer = buildingDevelopment->GetLotDeveloper();
        }
    }
    if (!developer) return false;
    developer->AddRef();

    port.SetIsBeingRebuilt(true);
    const int facing = lot->GetFacing();
    std::int32_t rect[4] = {0, 0, 0, 0};
    lot->GetBoundingRect(rect);
    const int x = rect[0];
    const int z = rect[1];
    const int sizeX = (1 - rect[0]) + rect[2];   // the original masks both sizes with 0xff
    const int sizeZ = (1 - rect[1]) + rect[3];
    developer->StartDemolishLot(lot);
    developer->EndDemolishLot(lot);
    lot->Release();
    port.SetCachedLot(nullptr);

    cSC4Lot* newLot = nullptr;
    bool created = false;
    if (IPortLotManagerApi* lotManager = g_host ? g_host->LotManager() : nullptr) {
        created = lotManager->CreateLot(x, z, sizeX & 0xff, sizeZ & 0xff, facing, &newLot);
    }
    bool constructed = false;
    if (created && newLot) {
        port.SetCachedLot(newLot);
        newLot->SetLotConfiguration(config);
        newLot->UpdateZoneType();
        constructed = developer->ConstructDefaultLot(newLot, config, 0, 1);
    }
    developer->Release();
    return constructed;
}

// @ 0x1000cd28a. Take the port's demand back, de-zone the lot's footprint through the zone manager, unlink
// and delete the port record and refresh the budget.
void cSC4PortDeveloper::removeLot(cPortLot* port) {
    if (!port) return;
    cSC4Lot* lot = port->GetLot();
    if (lot) {
        if (g_host) {
            if (IPortDemandApi* demand = g_host->Demand()) {
                const SC4Percentage oldUsage{port->Usage()};
                demand->UpdateOccupantEffects(lot->GetBuildingType(false), oldUsage, kPercentageZero);
            }
        }
    }
    port->SetUsage(0.0f);
    if (lot) {
        std::int32_t rect[4] = {0, 0, 0, 0};
        lot->GetBoundingRect(rect);
        PortCellRegion region;
        region.Fill(rect[0], rect[1], rect[2], rect[3]);
        if (IPortZoneManagerApi* zoneManager = g_host ? g_host->ZoneManager() : nullptr) {
            zoneManager->ClearZone(region);
        }
    }
    portLots_.remove(port);
    delete port;
    UpdateBudget();
}

// @ 0x1000cd5fc. The port whose cached lot has the same city cell (the original compares the location, not
// the pointer).
cPortLot* cSC4PortDeveloper::findPortLot(cSC4Lot* lot) {
    if (!lot) return nullptr;
    int x = 0;
    int z = 0;
    lot->GetLocation(x, z);
    for (cPortLot* port : portLots_) {
        cSC4Lot* cached = port->GetLot();
        if (!cached) continue;
        int px = 0;
        int pz = 0;
        cached->GetLocation(px, pz);
        if (px == x && pz == z) return port;
    }
    return nullptr;
}

// @ 0x1000cd68a. One exemplar per port type id (the key's instance), loaded as a cPortTypeConfig.
bool cSC4PortDeveloper::loadLotConfiguration(int index) {
    if (!g_host) return false;
    if (index < 0 || static_cast<std::size_t>(index) >= portTypeIds_.size()) return false;
    IPortResourceApi* resources = g_host->ResourceManager();
    if (!resources) return false;
    cISCPropertyHolder* holder = resources->LoadExemplar(portTypeIds_[index]);
    if (!holder) return false;
    cPortTypeConfig* config = new cPortTypeConfig();
    config->LoadConfiguration(index, holder);
    configurations_.push_back(config);
    holder->Release();
    return true;
}

// What the derived constructors write into the base object (0x1000ca01e.. and the airport factory at
// 0x1000c8034): the ini exemplar instance, the zone type, the history category, the advisor event and the
// occupant group of the port building.
void cSC4PortDeveloper::SetIdentity(std::uint32_t portDeveloperId, std::uint16_t zoneType,
                                    std::uint32_t historyCategory, std::uint32_t advisorEventId,
                                    std::uint32_t buildingGroup) {
    portDeveloperId_ = portDeveloperId;
    zoneType_ = zoneType;
    historyCategory_ = historyCategory;
    advisorEventId_ = advisorEventId;
    buildingGroup_ = buildingGroup;
}

// The vtable call at 0x1000cdb42 is a tail call to GetNumPorts(1 << size, 0x1f); kept virtual.
int cSC4PortDeveloper::GetNumPortsBySize(int size) {
    return GetNumPorts(1 << (size & 0x1f), 0x1f);
}

// @ 0x1000cdb62. sizeMask selects the configuration's size class (+0x30), stageMask the port's stage (+8).
int cSC4PortDeveloper::GetNumPorts(int sizeMask, int stageMask) {
    int count = 0;
    for (cPortLot* port : portLots_) {
        cPortTypeConfig* config = port->Config();
        if (!config) continue;
        const std::uint32_t sizeClass = config->SizeClass() & 0x1f;
        if (((static_cast<std::uint32_t>(sizeMask) >> sizeClass) & 1u) == 0) continue;
        const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage()) & 0x1f;
        if (((static_cast<std::uint32_t>(stageMask) >> stage) & 1u) != 0) ++count;
    }
    return count;
}

// @ 0x1000cdb98. Takes the *first* port lot: the original's loop never advances and jumps to itself when
// the list has more than one element (0x1000cdbc1 is `jmp 0x1000cdbc1`), so the "least efficient" search
// never happens. The efficiency is read before the cached lot is refreshed.
bool cSC4PortDeveloper::GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) {
    if (portLots_.empty()) return false;
    cPortLot* port = portLots_.front();
    efficiency.value = port->Efficiency();
    cSC4Lot* lot = port->GetLot();
    if (!lot) return false;   // the original would call through the null lot
    lot->GetLocation(location.x, location.z);
    return true;
}

// @ 0x1000cdbf6. Every port lot is refreshed and handed out with a reference.
void cSC4PortDeveloper::GetAllPortLots(std::vector<cSC4Lot*>& out) {
    for (cPortLot* port : portLots_) {
        cSC4Lot* lot = port->GetLot();
        if (!lot) continue;   // the original would call AddRef through the null pointer
        lot->AddRef();
        out.push_back(lot);
    }
}

// @ 0x1000cdc74.
bool cSC4PortDeveloper::GetPortLot(const cSC4Lot* lot, cPortLot*& out) {
    out = nullptr;
    for (cPortLot* port : portLots_) {
        if (port->GetLot() == lot) {
            out = port;
            return true;
        }
    }
    return false;
}

// @ 0x1000cddea: city->GetAdvisorSystem(), AddRef, PushEvent(event, 0), Release.
void cSC4PortDeveloper::SendAdvisorMessage(std::uint32_t event) {
    if (!g_host) return;
    IPortCityApi* city = g_host->City();
    if (!city) return;
    IPortAdvisorSystemApi* advisorSystem = city->GetAdvisorSystem();
    if (!advisorSystem) return;   // the original calls through the null pointer
    advisorSystem->AddRef();
    advisorSystem->PushEvent(static_cast<int>(event), nullptr);
    advisorSystem->Release();
}

// @ 0x1000cc7ba. Four message types: the monthly tick (virtual DoMonthlyTick then doMonthlySimulation), the
// post-city-init refresh, the placed-building message and the lot state change. Everything else is ignored
// (the derived developers intercept their own types before forwarding to this function).
bool cSC4PortDeveloper::DoMessage(cIGZMessage2* message) {
    if (!message) return true;
    const std::uint32_t type = message->GetType();
    if (type == kMessagePortMonthly) {
        DoMonthlyTick();
        doMonthlySimulation();
        return true;
    }
    if (type == kMessagePortPostCityInit) {
        for (cPortLot* port : portLots_) port->GetLot();
        return true;
    }
    // The payload accessors live on cIGZMessage2Standard; every message these notifications carry is a
    // cRZMessage2Standard in the original (the code calls the slots unconditionally).
    cIGZMessage2Standard* standard = static_cast<cIGZMessage2Standard*>(message);
    if (type == kMessagePortBuildingPlaced) {
        void* raw = standard->GetVoid1();
        if (!raw) return true;   // the original dereferences the null pointer at 0x1000cc89f
        cISC4Occupant* occupant = static_cast<cISC4Occupant*>(raw);
        occupant->AddRef();
        cISCPropertyHolder* holder = occupant->AsPropertyHolder();
        // A port building has no 0x29244db property (the original faults on a null holder).
        const bool excluded = holder && holder->HasProperty(kPortExcludedProperty);
        if (!excluded && occupant->GetType() == kPortBuildingType &&
            occupant->IsOccupantGroup(buildingGroup_)) {
            if (IPortLotManagerApi* lotManager = g_host ? g_host->LotManager() : nullptr) {
                cSC4Lot* lot = lotManager->GetOccupantLot(occupant);
                if (lot) DoLotInserted(lot);
            }
        }
        occupant->Release();
        return true;
    }
    if (type == kMessageLotStateChanged) {
        const std::uint64_t oldState = standard->GetData1();   // never read by doLotStateChanged
        const std::uint64_t newState = standard->GetData2();
        // The original QueryInterfaces the payload for cISC4Lot (0xe818fced) and drops it on failure; the
        // recovered lot is the concrete cSC4Lot, so the host resolves the payload object.
        cSC4Lot* lot = g_host ? g_host->MessageLot(standard->GetIGZUnknown()) : nullptr;
        if (!lot) return true;
        lot->AddRef();
        doLotStateChanged(lot, static_cast<int>(oldState), static_cast<int>(newState));
        lot->Release();
        return true;
    }
    return true;
}

// @ 0x1000cc564. Version 1: developer id, zone type (uint16), port count (uint16), then per port the stage
// (uint32), the configuration's type index (uint16) and the two int16 cell coordinates.
bool cSC4PortDeveloper::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    stream.SetUint32(portDeveloperId_);
    stream.SetUint16(zoneType_);
    const auto count = static_cast<std::uint16_t>(portLots_.size());
    stream.SetUint16(count);
    for (const cPortLot* port : portLots_) {
        stream.SetUint32(static_cast<std::uint32_t>(port->Stage()));
        stream.SetUint16(static_cast<std::uint16_t>(port->Config() ? port->Config()->TypeIndex() : 0));
        stream.SetSint16(port->X());
        stream.SetSint16(port->Z());
    }
    return true;
}

// @ 0x1000cc62a. Reads the header, loads the tunables when they are not loaded yet, then the port records
// (which start with the "cached lot is stale" flag set). The configuration vector index is not checked.
bool cSC4PortDeveloper::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    stream.GetUint32(portDeveloperId_);
    std::uint16_t zoneType = 0;
    stream.GetUint16(zoneType);
    // The compiled code stores a zero-extended uint32, clearing the padding at +0x5a as well.
    zoneType_ = zoneType;
    reserved5a_ = 0;
    std::uint16_t count = 0;
    stream.GetUint16(count);
    if (!tunablesLoaded_) readTunableVars();
    for (std::uint16_t i = 0; i < count; ++i) {
        cPortLot* port = new cPortLot();
        port->SetLotIsStale(true);
        port->SetStage(0);
        port->SetConfig(nullptr);
        port->SetCell(0, 0);
        port->SetEfficiency(1.0f);
        port->SetIsBeingRebuilt(false);
        port->SetUsage(0.0f);
        port->SetTrips(0);
        port->SetCachedLot(nullptr);
        std::uint32_t stage = 0;
        stream.GetUint32(stage);
        port->SetStage(static_cast<std::int32_t>(stage));
        std::uint16_t configIndex = 0;
        stream.GetUint16(configIndex);
        // The original indexes the configuration vector unchecked (0x1000cc6e6).
        port->SetConfig(configIndex < configurations_.size() ? configurations_[configIndex] : nullptr);
        std::int16_t x = 0;
        std::int16_t z = 0;
        stream.GetSint16(x);
        stream.GetSint16(z);
        port->SetCell(x, z);
        portLots_.push_front(port);
    }
    return true;
}

// @ 0x1000cc950. The monthly pass: the funding ratio funds/required, then per port the efficiency from the
// stage capacity and the trips, the demand usage update, the condition (2 = complaint) and the one
// advisor-upgrade message the pass is allowed to send; finally the average efficiency is recorded.
void cSC4PortDeveloper::doMonthlySimulation() {
    if (portLots_.empty()) return;
    const std::int64_t funding = GetAllocatedFunding();
    float allocation = 0.0f;
    {
        std::int64_t required = 0;
        for (cPortLot* port : portLots_) {
            cPortTypeConfig* config = port->Config();
            if (!config) continue;
            const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
            if (stage < config->Stages().size()) required += config->Stages()[stage].budget;
        }
        allocation = static_cast<float>(funding) / static_cast<float>(required);
    }
    bool upgradeMessageSent = false;
    for (cPortLot* port : portLots_) {
        cSC4Lot* lot = port->GetLot();
        if (!lot) continue;   // the original would dereference the null lot
        const std::uint8_t state = lot->GetState();
        const std::uint8_t condition = lot->GetCondition();
        if (condition == 1) continue;   // 1 = under construction / condemned: skip the economics
        const std::uint32_t stateBefore = state;
        const std::uint32_t trips = GetTripsForPort(*port);
        float efficiency = 1.0f;
        if (trips != 0) {
            cPortTypeConfig* config = port->Config();
            const std::uint32_t stage = static_cast<std::uint32_t>(port->Stage());
            const std::uint32_t capacity =
                (config && stage < config->Stages().size()) ? config->Stages()[stage].capacity : 0u;
            const float threshold = static_cast<float>(capacity) * allocation;
            efficiency = 0.0f;
            if (threshold > 0.0f && static_cast<float>(trips) <= threshold) {
                efficiency = 1.0f - static_cast<float>(trips) / threshold;
            }
        }
        port->SetEfficiency(efficiency);

        std::uint32_t usage = 0;
        std::uint32_t capacity = 0;
        GetPortUsageValues(lot, usage, capacity);
        const float ratio = static_cast<float>(usage) / static_cast<float>(capacity);
        if (g_host) {
            if (IPortDemandApi* demand = g_host->Demand()) {
                const SC4Percentage oldUsage{port->Usage()};
                const SC4Percentage newUsage{ratio};
                demand->UpdateOccupantEffects(lot->GetBuildingType(false), oldUsage, newUsage);
            }
        }
        port->SetUsage(ratio);

        // The original's first test is `threshold >= efficiency` (0x1000ccb33: UCOMISS threshold,
        // efficiency / JC skips to the funding test); the second is `funding <= 0`.
        const bool underperforming = port->Config() && efficiency <= port->Config()->Threshold();
        const std::uint32_t days = static_cast<std::uint32_t>(lot->GetDaysSinceLastStateChange());
        if (0x5a < days && (underperforming || funding <= 0)) {
            lot->SetCondition(2);
        } else {
            lot->SetCondition(0);
        }
        // states 1 (under construction), 3 (livable) and 4 (abandoned) may ask for an upgrade; only one
        // message per pass.
        if (stateBefore < 5 && ((0x1a >> stateBefore) & 1u) != 0 && !upgradeMessageSent &&
            port->IsGrowthTriggered(this)) {
            SendAdvisorUpgradeMessage(*port);
            upgradeMessageSent = true;
        }
    }
    RecordHistoryData(historyCategory_, static_cast<std::uint32_t>(RoundPercentage(GetAvgEfficiency())));
}

// @ 0x1000ccc0e. A lot that entered state 5 (reserved) with a port configuration compatible with this
// developer's zone: remove the port record unless it is being rebuilt, otherwise clear that flag. The first
// state argument is part of the signature but never read.
void cSC4PortDeveloper::doLotStateChanged(cSC4Lot* lot, int oldState, int newState) {
    (void)oldState;
    if (!lot || newState != static_cast<int>(kLotStateReserved)) return;
    IPortLotConfiguration* config = ConfigurationOf(lot);
    if (!config || !config->IsCompatibleWithZoneType(zoneType_)) return;
    cPortLot* port = findPortLot(lot);
    if (!port) return;
    if (!port->IsBeingRebuilt()) {
        removeLot(port);
        return;
    }
    port->SetIsBeingRebuilt(false);
}

// @ 0x1000ccc72. A newly placed port building: create the record when its lot configuration is the first
// stage of one of the loaded port types, then let the derived developer react.
void cSC4PortDeveloper::DoLotInserted(cSC4Lot* lot) {
    if (!lot) return;
    IPortLotConfiguration* config = ConfigurationOf(lot);
    if (!config || config->GetGrowthStage() != 1) return;
    cPortLot* port = new cPortLot();
    port->SetLotIsStale(false);
    port->SetStage(0);
    port->SetConfig(nullptr);
    port->SetCell(0, 0);
    port->SetEfficiency(1.0f);
    port->SetIsBeingRebuilt(false);
    port->SetUsage(0.0f);
    port->SetTrips(0);
    int x = 0;
    int z = 0;
    lot->GetLocation(x, z);
    port->SetCachedLot(lot);
    port->SetCell(static_cast<std::int16_t>(x), static_cast<std::int16_t>(z));
    lot->AddRef();
    const std::uint32_t configurationId = config->GetID();
    for (cPortTypeConfig* typeConfig : configurations_) {
        if (typeConfig->StageCount() == 0 || typeConfig->Stages().empty()) continue;
        if (static_cast<std::uint32_t>(typeConfig->Stages()[0].lotConfigurationId) != configurationId) {
            continue;
        }
        port->SetConfig(typeConfig);
        portLots_.push_front(port);
        UpdateBudget();
        OnNewLotCreated(*port);
        return;
    }
    delete port;   // not a port configuration
}

// @ 0x1000cde2e.
}  // namespace sc4
