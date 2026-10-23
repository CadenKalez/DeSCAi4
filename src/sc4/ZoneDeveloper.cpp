// cSC4ZoneDeveloper — see ZoneDeveloper.h for the layout, the four COM views and the list of quirks.
//
// Every call outside the object goes through IZoneDeveloperHost (one method per original call, with
// its slot); cSC4Lot is used directly through the recovered methods of Lot.h. The comments name the
// original vtable slot of every such call.

#include "sc4/ZoneDeveloper.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iterator>
#include <unordered_set>

#include "gz/StreamOps.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/LotConfiguration.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

namespace {

IZoneDeveloperHost* g_host = nullptr;
bool g_staticInitialized = false;   // mbStaticInitialized @ 0x100b... (set by the first Init)
std::set<const void*> g_cheatRegisteringObjects;   // developers that registered their cheat codes

constexpr std::uint32_t kGZIID_cISC4ZoneDeveloper = 0xe818ed2b;
constexpr std::uint32_t kGZIID_cISC4LotManager = 0x681905bd;
constexpr std::uint32_t kGZIID_cIGZMessageTarget2 = 0x452294aa;
constexpr std::uint32_t kZoneDeveloperIIDSerializable = 0xe4fda3d4;
constexpr std::uint32_t kGZIID_cISC4NetworkOccupant = 0xa821ef94;

// The messages Init subscribes to and DoMessage handles.
constexpr std::uint32_t kZoneMessageOccupantRemoved = 0x99ef1142;
constexpr std::uint32_t kZoneMessageOccupantRemovedSubType = 0xc7729698;
constexpr std::uint32_t kZoneMessageHighLightScale = 0xa6b79602;
constexpr std::uint32_t kZoneMessageCheatCode = 0x230e27ac;
constexpr std::uint32_t kZoneMessageCityInit = 0x26d31ec1;
constexpr std::uint32_t kZoneMessageDeferOccupantOperations = 0x49d968c4;

// Cheat commands registered by Init (strings at 0x100b000d0/d8).
constexpr std::uint32_t kCheatPlopAllLots = 0x8929e68e;
constexpr std::uint32_t kCheatPlopAllBuildings = 0x4ab569c9;
constexpr const char* kCheatCodePlotAllLots = "PlopAllLots";
constexpr const char* kCheatCodePlotAllBuildings = "PlopAllBuildings";

// Network cell flags used by the zoning queries.
constexpr std::uint32_t kNetworkFlagAnyConnection = 0x1f4f;
constexpr std::uint32_t kNetworkFlagStreet = 0x104;
constexpr std::uint32_t kNetworkFlagRail = 0x808;
constexpr std::uint32_t kNetworkFlagRoad = 0x401;
constexpr std::uint32_t kNetworkFlagPowerLine = 0x40;

// The occupant types ExamineCell treats as a blocked boundary (cSC4BuildingOccupant and its
// neighbour-connection occupant).
constexpr std::uint32_t kZoneOccupantTypeBuilding = 0x278128a0;
constexpr std::uint32_t kZoneOccupantTypeBlocking2 = 0x2890d4de;
constexpr std::uint32_t kZoneOccupantTypeNetwork = 0xc772bf98;

// Direction index -> the network occupant's direction flag (DAT_100a18dbc = {1, 2, 3, 0}).
constexpr std::uint32_t kDirectionFlags[4] = {1, 2, 3, 0};

// GetNetwork / GetNetworkOccupant out-object flag: an occupant that already reports this flag is skipped
// when the occupant-removed message is handled (0x80200090).
constexpr std::uint32_t kNetworkOccupantSkipFlag = 0x80200090;

// The magic round-to-nearest sequence of the original cell conversions (0x1001310b4, 0x100131c10):
//   cell = (int)(world * 1/16 + (-0.5 or -0.499) + 12582912.0f) - 12582912, then use base + 1 when the
//   scaled value is exactly the next integer. The net effect is floor(world / 16) for non-negative
//   coordinates; the exact float sequence is kept so the port matches on the boundaries.
int WorldToCell(float world, float halfOffset) {
    const float scaled = world * 0.0625f;
    const float biased = scaled + halfOffset + 12582912.0f;
    const int base = static_cast<int>(biased) - 12582912;
    const int up = base + 1;
    if (std::isnan(scaled) || std::isnan(static_cast<float>(up))) return base;
    return scaled == static_cast<float>(up) ? up : base;
}
int WorldToCell(float world) { return WorldToCell(world, -0.5f); }

int Clamp(int value, int lo, int hi) { return value < lo ? lo : (value > hi ? hi : value); }

bool IsBoundaryLot(const cSC4Lot* lot) {
    return lot != nullptr && (lot->GetFlags() & 1) != 0;   // cISC4Lot vslot 0x60, kLotFlagLocked
}

// The parked-lot lookups read the lot map without a bounds check in the original; the queries here
// keep that contract but never index outside a valid city (callers pass city cells).
cSC4Lot* LotAt(std::vector<std::vector<cSC4Lot*>>& map, int x, int z) {
    if (x < 0 || z < 0 || static_cast<std::size_t>(x) >= map.size()) return nullptr;
    if (static_cast<std::size_t>(z) >= map[static_cast<std::size_t>(x)].size()) return nullptr;
    return map[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)];
}

}  // namespace

IZoneDeveloperHost* ZoneDeveloperHost() { return g_host; }
void SetZoneDeveloperHost(IZoneDeveloperHost* host) { g_host = host; }

//---------------------------------------------------------------------------------------------------------------------
// construction / COM plumbing
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100128098. The original zeroes 0x24..0xaf and initialises every container; the members do the
// same here. The five parcellization tunables (2, 2, 1, 3, 6), the option bytes and the boundary maps
// are set by their in-class initialisers.
cSC4ZoneDeveloper::cSC4ZoneDeveloper() = default;

// @ 0x10012826c (base object destructor), 0x10012836a (complete object destructor) and 0x100128374
// (deleting destructor). The original only releases the lot developer here; every other service is
// released by Shutdown (the destructor assumes it ran).
cSC4ZoneDeveloper::~cSC4ZoneDeveloper() {
    if (lotDeveloper_) {
        lotDeveloper_->Release();   // primary vslot 0x10 of SL::spLotDeveloper
        lotDeveloper_ = nullptr;
    }
}

// @ 0x1001283a2. iid 1 and 0xe818ed2b answer the primary view, 0x681905bd the lot-manager view,
// 0x452294aa the message target and 0xe4fda3d4 the serializable view. The original stores the pointer
// before AddRef; the single count at +0x24 is incremented for every successful QI.
bool cSC4ZoneDeveloper::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    void* result = nullptr;
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISC4ZoneDeveloper) {
        result = static_cast<cISC4ZoneDeveloper*>(this);
    } else if (iid == kGZIID_cISC4LotManager) {
        result = static_cast<cISC4LotManager*>(this);
    } else if (iid == kGZIID_cIGZMessageTarget2) {
        result = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == kZoneDeveloperIIDSerializable) {
        result = static_cast<cIGZSerializable*>(this);
    } else {
        return false;
    }
    *out = result;
    AddRef();
    return true;
}

// @ 0x10012841e (the three secondary AddRef thunks adjust back to this same count).
std::uint32_t cSC4ZoneDeveloper::AddRef() { return ++refCount_; }

// @ 0x100128456. Quirk kept: the object is destroyed as soon as the count drops below 2 (it never
// checks for zero) and the call answers 0 in that case. The three thunks route here with their `this`
// adjusted to the primary object.
std::uint32_t cSC4ZoneDeveloper::Release() {
    if (static_cast<std::int32_t>(refCount_) < 2) {
        Shutdown();      // primary vslot 0x20
        delete this;     // primary vslot 0x58 (the deleting destructor)
        return 0;
    }
    return --refCount_;
}

// @ 0x10012853c. Attaches every service the class uses, sizes the lot map, creates the capacity grid
// and the boundary texture rules, subscribes to the three messages and (when the cheat-code manager
// and the application allow it) registers the two plop cheat codes.
bool cSC4ZoneDeveloper::Init() {
    if (initialized_) return true;
    if (!g_staticInitialized) g_staticInitialized = true;
    highlightScale_ = 0;

    if (!lotDeveloper_) {   // SL::spLotDeveloper
        lotDeveloper_ = g_host ? g_host->LotDeveloper() : nullptr;
        if (!lotDeveloper_) return false;
        lotDeveloper_->AddRef();
    }
    if (!renderer_) {       // SL::spRenderer; its vslot 0x50 object carries the scale at +0x14
        renderer_ = g_host ? g_host->Renderer() : nullptr;
        if (!renderer_) return false;
        renderer_->AddRef();
        highlightScale_ = g_host->RendererHighlightScale();
    }
    if (!buildingDevelopment_) {   // SL::spBuildingDevelopmentSim
        buildingDevelopment_ = g_host ? g_host->BuildingDevelopment() : nullptr;
        if (!buildingDevelopment_) return false;
        buildingDevelopment_->AddRef();
    }
    if (!city_) {                  // SL::spCity
        city_ = g_host ? g_host->City() : nullptr;
        if (!city_) return false;
        city_->AddRef();
        cellsX_ = city_->CellCountX();   // city vslot 0x2e8
        cellsZ_ = city_->CellCountZ();   // city vslot 0x2f0
    }
    if (!occupantManager_) {       // SL::spOccupantManager
        occupantManager_ = g_host ? g_host->OccupantManager() : nullptr;
        if (!occupantManager_) return false;
        occupantManager_->AddRef();
    }
    if (!terrain_) {               // SL::spTerrain
        terrain_ = g_host ? g_host->Terrain() : nullptr;
        if (!terrain_) return false;
        terrain_->AddRef();
    }
    if (!trafficSim_) {            // SL::spTrafficSim
        trafficSim_ = g_host ? g_host->TrafficSim() : nullptr;
        if (!trafficSim_) return false;
        trafficSim_->AddRef();
    }
    if (!zoneManager_) {           // SL::spZoneManager
        zoneManager_ = g_host ? g_host->ZoneManager() : nullptr;
        if (!zoneManager_) return false;
        zoneManager_->AddRef();
    }

    if (lotMap_.empty()) {
        lotMap_.resize(static_cast<std::size_t>(cellsX_));
        for (auto& column : lotMap_) column.assign(static_cast<std::size_t>(cellsZ_), nullptr);
    } else if (cellsX_ > 0) {
        // Re-init: the original walks the existing map and calls cISC4Lot::Init (vslot 0x28) on every
        // non-null cell.
        for (int x = 0; x < cellsX_; ++x)
            for (int z = 0; z < cellsZ_; ++z) {
                cSC4Lot* lot = LotAt(lotMap_, x, z);
                if (lot) lot->Init();
            }
    }

    if (!capacityGrid_) {
        // The original builds a cSC4SimGrid<uint16_t> by hand: refcount 1, initialised, class id
        // 0x49b9e604, tract size 4 and instance id 0x69d5c456.
        capacityGrid_ = new cSC4SimGrid<std::uint16_t>();
        capacityGrid_->AddRef();
        capacityGrid_->Init();
        capacityGrid_->SetTractSize(4);
        capacityGrid_->SetInstanceID(0x69d5c456);
    }

    InitBoundaryRules();

    if (!messageServer_) {   // SL::spMS2
        messageServer_ = g_host ? g_host->MessageServer() : nullptr;
        if (!messageServer_) return false;
        messageServer_->AddRef();
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messageServer_->AddNotification(target, kZoneMessageOccupantRemoved);
        messageServer_->AddNotification(target, kZoneMessageCityInit);
        messageServer_->AddNotification(target, kZoneMessageHighLightScale);
    }

    if (g_host && g_host->CheatCodesEnabled()) {   // SL::spCCM != null && SL::spSC4->vslot 0xa8
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        g_host->RegisterCheatCode(kCheatCodePlotAllLots, kCheatPlopAllLots, target);
        g_host->RegisterCheatCode(kCheatCodePlotAllBuildings, kCheatPlopAllBuildings, target);
        g_cheatRegisteringObjects.insert(this);
    }

    initialized_ = true;
    return true;
}

// @ 0x100128b94. 47 entries keyed by ((s0 * 16 + s1) * 16 + s2) * 16 + s3. Key 0 is the fallback
// rule used by GetBoundaryLotTexture when the exact combination is absent.
void cSC4ZoneDeveloper::InitBoundaryRules() {
    struct Entry {
        int key;
        BoundaryLotTextureRule rule;
    };
    static const Entry kRules[] = {
        {0x0000, {0x3000, 0, false}},   {0x1000, {0x2000, 0, false}},   {0x0100, {0x2000, 1, false}},
        {0x0010, {0x2000, 2, false}},   {0x0001, {0x2000, 3, false}},   {0x2000, {0x2600, 0, false}},
        {0x0200, {0x2600, 1, false}},   {0x0020, {0x2600, 2, false}},   {0x0002, {0x2600, 3, false}},
        {0x1100, {0x2100, 1, false}},   {0x0110, {0x2100, 2, false}},   {0x0011, {0x2100, 3, false}},
        {0x1001, {0x2100, 0, false}},   {0x1200, {0x2400, 1, false}},   {0x0120, {0x2400, 2, false}},
        {0x0012, {0x2400, 3, false}},   {0x2001, {0x2400, 0, false}},   {0x2100, {0x2400, 0x100, false}},
        {0x0210, {0x2400, 0x103, false}}, {0x0021, {0x2400, 0x102, false}}, {0x1002, {0x2400, 0x101, false}},
        {0x1010, {0x2500, 0, false}},   {0x0101, {0x2500, 1, false}},   {0x1020, {0x2900, 2, false}},
        {0x0102, {0x2900, 3, false}},   {0x2010, {0x2900, 0, false}},   {0x0201, {0x2900, 1, false}},
        {0x1110, {0x2200, 1, false}},   {0x0111, {0x2200, 2, false}},   {0x1011, {0x2200, 3, false}},
        {0x1101, {0x2200, 0, false}},   {0x1120, {0x2700, 2, false}},   {0x0112, {0x2700, 3, false}},
        {0x2011, {0x2700, 0, false}},   {0x1201, {0x2700, 1, false}},   {0x1210, {0x2800, 1, false}},
        {0x0121, {0x2800, 2, false}},   {0x1012, {0x2800, 3, false}},   {0x2101, {0x2800, 0, false}},
        {0x2110, {0x2700, 0x100, false}}, {0x0211, {0x2700, 0x103, false}}, {0x1021, {0x2700, 0x102, false}},
        {0x1102, {0x2700, 0x101, false}}, {0x1112, {0x2300, 3, false}}, {0x2111, {0x2300, 0, false}},
        {0x1211, {0x2300, 1, false}},   {0x1121, {0x2300, 2, false}},
    };
    for (const Entry& entry : kRules) boundaryRules_[entry.key] = entry.rule;
}

// @ 0x1001291fe. Unregisters the cheats and the three messages, drops the lot maps and every service.
bool cSC4ZoneDeveloper::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;

    if (g_cheatRegisteringObjects.erase(this) != 0) {
        if (g_host) {
            g_host->UnregisterCheatCode(kCheatPlopAllLots);
            g_host->UnregisterCheatCode(kCheatPlopAllBuildings);
        }
    }

    if (messageServer_) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messageServer_->RemoveNotification(target, kZoneMessageOccupantRemoved);
        messageServer_->RemoveNotification(target, kZoneMessageCityInit);
        messageServer_->RemoveNotification(target, kZoneMessageHighLightScale);
        messageServer_->Release();
        messageServer_ = nullptr;
    }

    boundaryRules_.clear();
    ClearLotMap();
    lotMap_.clear();

    if (railNetwork_) { railNetwork_->Shutdown(); railNetwork_->Release(); railNetwork_ = nullptr; }
    if (roadNetwork_) { roadNetwork_->Shutdown(); roadNetwork_->Release(); roadNetwork_ = nullptr; }
    if (streetNetwork_) { streetNetwork_->Shutdown(); streetNetwork_->Release(); streetNetwork_ = nullptr; }
    if (capacityGrid_) {
        capacityGrid_->Shutdown();
        capacityGrid_->Release();
        capacityGrid_ = nullptr;
    }
    if (zoneManager_) { zoneManager_->Release(); zoneManager_ = nullptr; }
    if (trafficSim_) { trafficSim_->Release(); trafficSim_ = nullptr; }
    if (terrain_) { terrain_->Release(); terrain_ = nullptr; }
    if (terrainView_) { terrainView_->Release(); terrainView_ = nullptr; }
    if (occupantManager_) { occupantManager_->Release(); occupantManager_ = nullptr; }
    if (city_) { city_->Release(); city_ = nullptr; }
    if (buildingDevelopment_) { buildingDevelopment_->Release(); buildingDevelopment_ = nullptr; }
    if (renderer_) { renderer_->Release(); renderer_ = nullptr; }
    if (lotDeveloper_) {
        lotDeveloper_->Release();
        lotDeveloper_ = nullptr;
    }
    return true;
}

// @ 0x1001294de. Deleting a lot clears its whole footprint, so the following cells are already null;
// the loop still reads every cell like the original.
void cSC4ZoneDeveloper::ClearLotMap() {
    for (auto& column : lotMap_)
        for (cSC4Lot*& cell : column) {
            if (cell) {
                cSC4Lot* lot = cell;
                DeleteLot(lot);
            }
        }
}

//---------------------------------------------------------------------------------------------------------------------
// messages
//---------------------------------------------------------------------------------------------------------------------

// @ 0x10012bdac. The message carries the flag in its first data word; the message server posts it to
// the occupant manager (vslot 0x20).
void cSC4ZoneDeveloper::DeferOccupantOperations(bool defer) {
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kZoneMessageDeferOccupantOperations);
    message->SetData1(defer ? 1u : 0u);
    if (messageServer_) messageServer_->MessagePost(message, false);
    message->Release();
}

// @ 0x10012959a. PARTIAL: the message dispatch, the highlight-scale word and the reload walk are
// recovered. The occupant-removed path stops after DeleteLot: CreateLotRemnants (0x100129956) and the
// road/rail connection helpers (0x10012a040, 0x10012a534) are not recovered, and the two plop cheat
// commands (0x10012abe4, 0x10012ad80) are skipped, so those branches leave the city untouched.
bool cSC4ZoneDeveloper::DoMessage(cIGZMessage2* message) {
    if (!message) return false;
    auto* standard = dynamic_cast<cIGZMessage2Standard*>(message);
    if (!standard) return false;
    // The original compares the message type as a signed 32-bit value (`if (type < 0x230e27ac)`),
    // which is why the two negative ids 0x99ef1142 / 0xa6b79602 are handled by the first branch.
    const std::int32_t type = static_cast<std::int32_t>(standard->GetType());

    if (type < static_cast<std::int32_t>(kZoneMessageCheatCode)) {
        if (type == static_cast<std::int32_t>(kZoneMessageOccupantRemoved)) {
            if (standard->GetData3() != kZoneMessageOccupantRemovedSubType) return true;
            auto* occupant = static_cast<cISC4Occupant*>(standard->GetVoid1());
            if (!occupant) return true;
            // QI(0xa821ef94): occupants that already advertise the skip flag are ignored (e.g. network
            // occupants whose own removal owns the lot).
            void* network = nullptr;
            bool skip = false;
            if (occupant->QueryInterface(kGZIID_cISC4NetworkOccupant, &network) && network) {
                auto* view = static_cast<IZoneDeveloperNetworkOccupant*>(network);
                skip = view->HasNetworkFlags(kNetworkOccupantSkipFlag);
                view->Release();
            }
            if (skip) return true;

            float position[3] = {0, 0, 0};
            occupant->GetPosition(position);                       // cISC4Occupant vslot 0x40
            int cellX = 0, cellZ = 0;
            if (!city_) return true;
            city_->PositionToCell(position[0], position[2], cellX, cellZ);   // city vslot 0x2f8
            if (cellX < 0 || cellZ < 0 || cellX >= cellsX_ || cellZ >= cellsZ_) return true;   // original indexes unchecked
            cSC4Lot* lot = lotMap_[static_cast<std::size_t>(cellX)][static_cast<std::size_t>(cellZ)];
            if (lot) {
                std::int32_t rect[4] = {0, 0, 0, 0};
                lot->GetBoundingRect(rect);                         // cISC4Lot vslot 0x70
                const std::uint32_t zoneType = lot->GetZoneType();  // vslot 0xf8
                currentZoneType_ = static_cast<int>(zoneType);
                DeleteLot(lot);                                     // primary vslot 0x118
                if (zoneManager_ && zoneManager_->IsZoneTypeValid(currentZoneType_)) {
                    // CreateLotRemnants(rect.x0, rect.z0, rect.x2 + 1 - rect.x0, rect.z2 + 1 - rect.z0,
                    // lot->GetFacing()) is not recovered (0x100129956); the remnants are left unbuilt.
                }
                currentZoneType_ = 0;
            }
            const std::uint64_t removalKind = standard->GetData4();
            if (removalKind == 1 || removalKind == 2) {
                // ConnectRoadToAnyAdjacentStreets (0x10012a040) is not recovered.
            } else if (removalKind == 3) {
                // ConnectStreetsAcrossRail (0x10012a534) is not recovered.
            }
        } else if (type == static_cast<std::int32_t>(kZoneMessageHighLightScale)) {
            if (void* data = standard->GetVoid2()) highlightScale_ = *static_cast<int*>(data);
        }
    } else if (type == static_cast<std::int32_t>(kZoneMessageCheatCode)) {
        const std::uint64_t command = standard->GetData1();
        if (command == kCheatPlopAllLots) {
            // PlopAllLotTypes (0x10012abe4) is not recovered.
        } else if (command == kCheatPlopAllBuildings) {
            // PlopAllBuildingTypes (0x10012ad80) is not recovered.
        }
    } else if (type == static_cast<std::int32_t>(kZoneMessageCityInit)) {
        if (!terrainView_ && g_host) {
            terrainView_ = g_host->TerrainView();                  // SL::spTerrainView
            if (terrainView_) terrainView_->AddRef();
        }
        if (!roadNetwork_ && g_host) {
            roadNetwork_ = g_host->NetworkSubnet(1);               // network manager, type 3
            if (roadNetwork_) {
                roadNetwork_->AddRef();
                roadNetwork_->Init();                              // network vslot 0x18
            }
        }
        if (!railNetwork_ && g_host) {
            railNetwork_ = g_host->NetworkSubnet(2);               // network manager, type 0xb
            if (railNetwork_) {
                railNetwork_->AddRef();
                railNetwork_->Init();
            }
        }
        if (!streetNetwork_ && g_host) {
            streetNetwork_ = g_host->NetworkSubnet(0);             // network manager, type 0
            if (streetNetwork_) {
                streetNetwork_->AddRef();
                streetNetwork_->Init();
            }
        }
        if (loaded_ && cellsX_ > 0) {
            for (int x = 0; x < cellsX_; ++x) {
                for (int z = 0; z < cellsZ_; ++z) {
                    cSC4Lot* lot = LotAt(lotMap_, x, z);
                    if (!lot) continue;
                    int lotX = 0, lotZ = 0;
                    lot->GetLocation(lotX, lotZ);                  // cISC4Lot vslot 0xa8
                    if (lotX == x && lotZ == z) lot->PostCityInit();  // vslot 0x40
                }
            }
        }
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
// persistence
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100132f32. Version 1, the lot-map dimensions and every non-null cell (multi-cell lots are written
// once per cell, like the original).
bool cSC4ZoneDeveloper::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    const std::uint32_t rows = static_cast<std::uint32_t>(lotMap_.size());
    const std::uint32_t columns = rows != 0 ? static_cast<std::uint32_t>(lotMap_[0].size()) : 0u;
    stream.SetUint32(rows);
    stream.SetUint32(columns);
    for (const auto& column : lotMap_)
        for (cSC4Lot* lot : column)
            if (lot) {
                if (g_host) g_host->WriteLot(stream, lot);   // SC4WriteGZSerializable
                else stream.SetUint32(0);
            }
    return !stream.GetError();
}

// @ 0x100132ffc. Version 1 only; the stream serializer returns a referenced lot for every cell and the
// duplicate pointers get their extra reference released (the original keeps a pointer hash set for
// this).
bool cSC4ZoneDeveloper::Read(cIGZIStream& stream) {
    loaded_ = true;
    std::uint16_t version = 0;
    if (!stream.GetUint16(version) || version != 1) return false;
    std::uint32_t countX = 0, countZ = 0;
    if (!stream.GetUint32(countX)) return false;
    stream.GetUint32(countZ);
    lotMap_.resize(countX);
    std::unordered_set<cSC4Lot*> seen;
    for (std::uint32_t x = 0; x < countX; ++x) {
        lotMap_[x].resize(countZ, nullptr);   // keeps pre-existing cells, like the original resize
        for (std::uint32_t z = 0; z < countZ; ++z) {
            cSC4Lot* lot = g_host ? g_host->ReadLot(stream) : nullptr;
            lotMap_[x][z] = lot;
            if (!lot) continue;
            if (!seen.insert(lot).second) {
                lot->Release();   // the stream handed out a second reference for a duplicate cell
            } else {
                lots_.push_front(lot);
                ++lotCount_;
            }
        }
    }
    return !stream.GetError();
}

//---------------------------------------------------------------------------------------------------------------------
// lot queries
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100131192. `exactCell` additionally requires cISC4Lot::GetLocation (vslot 0xa8) to name the
// queried cell, which rejects the rest of a multi-cell footprint.
cSC4Lot* cSC4ZoneDeveloper::GetLot(int x, int z, bool exactCell) {
    if (x < 0 || z < 0 || x >= cellsX_ || z >= cellsZ_) return nullptr;
    cSC4Lot* lot = LotAt(lotMap_, x, z);
    if (exactCell) {
        if (!lot) return nullptr;
        int lotX = 0, lotZ = 0;
        lot->GetLocation(lotX, lotZ);
        if (lotX != x || lotZ != z) return nullptr;
    }
    return lot;
}

// @ 0x10013122a. City LocationIsInBounds (vslot 0x310) then PositionToCell (vslot 0x2f8).
cSC4Lot* cSC4ZoneDeveloper::GetLot(const cS3DVector3& location) {
    if (!city_ || !city_->LocationIsInBounds(location.x, location.z)) return nullptr;
    int cellX = 0, cellZ = 0;
    city_->PositionToCell(location.x, location.z, cellX, cellZ);
    return GetLot(cellX, cellZ, false);
}

// @ 0x1001310b4. cISC4Occupant::GetPosition (vslot 0x40) then the world-to-cell conversion.
cSC4Lot* cSC4ZoneDeveloper::GetOccupantLot(cISC4Occupant* occupant) {
    if (!occupant) return nullptr;
    float position[3] = {0, 0, 0};
    occupant->GetPosition(position);
    return GetLot(WorldToCell(position[0]), WorldToCell(position[2]), false);
}

// @ 0x100130354. With lot changes enabled: a lot under the cell. While the changes are deferred: a
// pending deletion also counts as "has a lot".
bool cSC4ZoneDeveloper::DoesCellHaveLot(int x, int z) {
    if (lotChangesEnabled_) return GetLot(x, z, false) != nullptr;
    const SC4Point<int> key{x, z};
    if (pendingDeletes_.find(key) != pendingDeletes_.end()) return true;
    return GetLot(x, z, false) != nullptr;
}

// @ 0x100131040. Pulls up to 0x3e6 random cells; returns null when none holds a lot.
cSC4Lot* cSC4ZoneDeveloper::GetRandomLot() {
    if (!g_host) return nullptr;
    for (int attempt = 0; attempt < 0x3e6; ++attempt) {
        const int x = static_cast<int>(g_host->RandomBelow(static_cast<std::uint32_t>(cellsX_)));
        const int z = static_cast<int>(g_host->RandomBelow(static_cast<std::uint32_t>(cellsZ_)));
        if (cSC4Lot* lot = LotAt(lotMap_, x, z)) return lot;
    }
    return nullptr;
}

// @ 0x100130e66. Looks the packed boundary key up and falls back to key 0 (the constructor's default
// rule {0x3000, 0}). The end-node read of the original when key 0 is absent is replaced by zeroes.
void cSC4ZoneDeveloper::GetBoundaryLotTexture(const BoundaryType& boundary, std::uint32_t& texture,
                                              std::uint8_t& shape, bool& flag) const {
    const int key = (((boundary.sides[0] * 16 + boundary.sides[1]) * 16 + boundary.sides[2]) * 16 +
                     boundary.sides[3]);
    auto it = boundaryRules_.find(key);
    if (it == boundaryRules_.end()) it = boundaryRules_.find(0);
    if (it == boundaryRules_.end()) {
        texture = 0;
        shape = 0;
        flag = false;
        return;
    }
    texture = it->second.texture;
    shape = static_cast<std::uint8_t>(it->second.shape);   // GetBoundaryLotTexture truncates the 16-bit rule
    flag = it->second.flag;
}

// @ 0x1001312a8. Clamps the rectangle, clears the list and pushes every lot that touches the cells. In
// the `unique` mode only the cell that names the lot (GetLocation == cell) is accepted; the normal mode
// dedupes a multi-cell lot against its left and upper neighbours.
int cSC4ZoneDeveloper::GetLotsInCellRect(std::list<cRZAutoRefCount<cSC4Lot>>& out, int x0, int z0,
                                         int x1, int z1, bool unique) {
    out.clear();
    const int cx0 = std::max(x0, 0);
    const int cz0 = std::max(z0, 0);
    const int cx1 = std::min(x1, cellsX_ - 1);
    const int cz1 = std::min(z1, cellsZ_ - 1);
    int count = 0;
    if (cx0 > cx1 || cz0 > cz1) return 0;
    if (unique) {
        for (int x = cx0; x <= cx1; ++x) {
            for (int z = cz0; z <= cz1; ++z) {
                cSC4Lot* lot = LotAt(lotMap_, x, z);
                if (!lot) continue;
                int lotX = 0, lotZ = 0;
                lot->GetLocation(lotX, lotZ);
                if (lotX == x && lotZ == z) {
                    out.emplace_back(lot);
                    ++count;
                }
            }
        }
        return count;
    }
    const std::vector<cSC4Lot*>* previousRow = nullptr;
    for (int x = cx0; x <= cx1; ++x) {
        cSC4Lot* previousCellLot = nullptr;
        const std::vector<cSC4Lot*>& row = lotMap_[static_cast<std::size_t>(x)];
        for (int z = cz0; z <= cz1; ++z) {
            cSC4Lot* lot = row[static_cast<std::size_t>(z)];
            const cSC4Lot* above = previousRow ? (*previousRow)[static_cast<std::size_t>(z)] : nullptr;
            if (lot && lot != previousCellLot && (previousRow == nullptr || lot != above)) {
                out.emplace_back(lot);
                ++count;
            }
            previousCellLot = lot;
        }
        previousRow = &row;
    }
    return count;
}

// @ 0x100131800.
int cSC4ZoneDeveloper::GetLotsSurroundingLot(const cSC4Lot* lot, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                             int edgeMask) {
    if (!lot) return 0;
    std::int32_t rect[4] = {0, 0, 0, 0};
    lot->GetBoundingRect(rect);
    const SC4Rect<int> bounds{rect[0], rect[1], rect[2], rect[3]};
    return GetLotsSurroundingRect(bounds, out, edgeMask);
}

// @ 0x100131896.
int cSC4ZoneDeveloper::GetLotsSurroundingLot(int x, int z, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                             int edgeMask) {
    cSC4Lot* lot = GetLot(x, z, false);
    return lot ? GetLotsSurroundingLot(lot, out, edgeMask) : 0;
}

// @ 0x100131514. Walks the one-cell ring around `rect` (top row, left/right cells of the middle rows,
// bottom row), keeps the cells allowed by `edgeMask` (1 north, 2 east, 4 south, 8 west) and pushes the
// distinct overlapping lots.
int cSC4ZoneDeveloper::GetLotsSurroundingRect(const SC4Rect<int>& rect,
                                              std::list<cRZAutoRefCount<cSC4Lot>>& out, int edgeMask) {
    const int left = rect.x0 - 1;
    const int top = rect.z0 - 1;
    const int right = rect.x1 + 1;
    const int bottom = rect.z1 + 1;

    int firstX = left;
    int firstZ = top;
    int lastX = right < cellsX_ ? right : cellsX_ - 1;
    int lastZ = bottom < cellsZ_ ? bottom : cellsZ_ - 1;
    int mask = edgeMask;
    if (rect.x0 < 1) {
        firstX = 0;
        mask &= ~8;
    }
    if (rect.z0 < 1) {
        firstZ = 0;
        mask &= ~1;
    }
    if (right >= cellsX_) mask &= ~2;
    if (bottom >= cellsZ_) mask &= ~4;

    const int ringRight = std::max(right, left);
    const int ringBottom = std::max(bottom, top);
    int count = 0;

    auto consider = [&](int x, int z) {
        if (x > lastX || z > lastZ || x < firstX || z < firstZ) return;
        cSC4Lot* lot = LotAt(lotMap_, x, z);
        if (!lot) return;
        std::int32_t lotRect[4] = {0, 0, 0, 0};
        lot->GetBoundingRect(lotRect);
        const bool overlaps = rect.x0 <= lotRect[2] && lotRect[0] <= rect.x1 &&
                              rect.z0 <= lotRect[3] && lotRect[1] <= rect.z1;
        if (!overlaps) return;
        if (mask != 0xf) {
            if (z == top && (mask & 1) == 0) return;
            if (x == right && (mask & 2) == 0) return;
            if (z == bottom && (mask & 4) == 0) return;
            if (x == left && (mask & 8) == 0) return;
        }
        for (const auto& held : out)
            if (held.get() == lot) return;
        out.emplace_back(lot);
        ++count;
    };

    if (top <= ringBottom && left <= ringRight) {
        for (int x = left; x <= ringRight; ++x) consider(x, top);
        for (int z = top + 1; z < ringBottom; ++z) {
            consider(left, z);
            if (right != left) consider(right, z);
        }
        if (ringBottom != top)
            for (int x = left; x <= ringRight; ++x) consider(x, ringBottom);
    }
    return count;
}

// @ 0x100131946. The four vertices of one side of the lot rectangle (edge 1 north, 2 east, 4 south,
// 8 west); other bits leave the output untouched, like the original.
void cSC4ZoneDeveloper::GetVerticesAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    if ((edge & 1) == 0) {
        if ((edge & 2) == 0) {
            if ((edge & 4) != 0) {
                x0 = rect.x0;
                x1 = rect.x1 + 1;
                z0 = rect.z1 + 1;
                z1 = rect.z1 + 1;
            } else if ((edge & 8) != 0) {
                x0 = rect.x0;
                z0 = rect.z0;
                x1 = rect.x0;
                z1 = rect.z1 + 1;
            } else {
                return;
            }
        } else {
            x0 = rect.x1 + 1;
            z0 = rect.z0;
            x1 = rect.x1 + 1;
            z1 = rect.z1 + 1;
        }
    } else {
        x0 = rect.x0;
        z0 = rect.z0;
        x1 = rect.x1 + 1;
        z1 = rect.z0;
    }
    out = {x0, z0, x1, z1};
}

// @ 0x1001319a6. The cells on the other side of one lot edge, clamped to the city; the function
// answers false when the edge is outside the city (the output is then {0,0,-1,-1}).
bool cSC4ZoneDeveloper::GetNeighborCellsAtEdgeOfLot(const SC4Rect<int>& rect, int edge,
                                                    SC4Rect<int>& out) const {
    out = {0, 0, -1, -1};
    if ((edge & 1) == 0) {
        if ((edge & 2) == 0) {
            if ((edge & 4) == 0) {
                if ((edge & 8) != 0 && rect.x0 > 0) {
                    out = {rect.x0 - 1, rect.z0, rect.x0 - 1, rect.z1};
                }
            } else if (rect.z1 < cellsZ_ - 1) {
                out = {rect.x0, rect.z1 + 1, rect.x1, rect.z1 + 1};
            }
        } else if (rect.x1 < cellsX_ - 1) {
            out = {rect.x1 + 1, rect.z0, rect.x1 + 1, rect.z1};
        }
    } else if (rect.z0 > 0) {
        out = {rect.x0, rect.z0 - 1, rect.x1, rect.z0 - 1};
    }
    return out.x0 <= out.x1;
}

// @ 0x100131a5e. Looks for a boundary lot in the four cells around the vertex (x, z), in the order
// (x-1,z-1), (x-1,z), (x,z-1), (x,z); falls back to the terrain cell altitude (vslot 0x70).
float cSC4ZoneDeveloper::GetLotSavvyTerrainVertexAltitude(int x, int z, const cSC4Lot* exclude,
                                                          bool* lotFound) const {
    if (lotFound) *lotFound = true;
    const cSC4Lot* found = nullptr;
    if (x > 0 && z > 0) {
        const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), x - 1, z - 1);
        if (IsBoundaryLot(lot) && lot != exclude) found = lot;
    }
    if (!found && z > 0 && x < cellsX_) {
        const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), x, z - 1);
        if (IsBoundaryLot(lot) && lot != exclude) found = lot;
    }
    if (!found && x > 0 && z < cellsZ_) {
        const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), x - 1, z);
        if (IsBoundaryLot(lot) && lot != exclude) found = lot;
    }
    if (!found && x < cellsX_ && z < cellsZ_) {
        const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), x, z);
        if (IsBoundaryLot(lot) && lot != exclude) found = lot;
    }
    if (found) return found->GetAltitude();   // cISC4Lot vslot 0x80
    if (lotFound) *lotFound = false;
    return terrain_ ? terrain_->CellAltitude(x, z) : 0.0f;
}

// @ 0x100131c10. Clamps the world point to the city, then answers the boundary lot's altitude or the
// terrain altitude (vslot 0x60).
float cSC4ZoneDeveloper::GetLotSavvyTerrainAltitude(float x, float z, bool* lotFound) const {
    int cellX = WorldToCell(x, -0.499f);
    int cellZ = WorldToCell(z, -0.499f);
    if (static_cast<unsigned>(cellX) >= static_cast<unsigned>(cellsX_)) cellX = Clamp(cellX, 0, cellsX_ - 1);
    if (static_cast<unsigned>(cellZ) >= static_cast<unsigned>(cellsZ_)) cellZ = Clamp(cellZ, 0, cellsZ_ - 1);
    const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), cellX, cellZ);
    if (IsBoundaryLot(lot)) {
        if (lotFound) *lotFound = true;
        return lot->GetAltitude();
    }
    if (lotFound) *lotFound = false;
    return terrain_ ? terrain_->Altitude(x, z) : 0.0f;
}

// @ 0x100131d20. The boundary lot's altitude, or the terrain altitude at the cell centre (vslot 0x60).
float cSC4ZoneDeveloper::GetLotSavvyCellAltitude(int x, int z, bool* lotFound) const {
    const cSC4Lot* lot = LotAt(const_cast<std::vector<std::vector<cSC4Lot*>>&>(lotMap_), x, z);
    if (IsBoundaryLot(lot)) {
        if (lotFound) *lotFound = true;
        return lot->GetAltitude();
    }
    if (lotFound) *lotFound = false;
    return terrain_ ? terrain_->Altitude((static_cast<float>(x) + 0.5f) * 16.0f,
                                         (static_cast<float>(z) + 0.5f) * 16.0f)
                    : 0.0f;
}

// @ 0x10012c1d0. The distinct lots in the 2x2 cell block [x-1..x] x [z-1..z]; a lot beyond its first
// state (cISC4Lot::GetState, vslot 0x108) forces both bounds to 32.0.
void cSC4ZoneDeveloper::GetLotVertexAltitudeAdjustmentRange(int x, int z, float& minAltitude,
                                                            float& maxAltitude) {
    const int x0 = std::max(x - 1, 0);
    const int z0 = std::max(z - 1, 0);
    const int x1 = std::min(x, cellsX_ - 1);
    const int z1 = std::min(z, cellsZ_ - 1);
    minAltitude = FLT_MAX;
    maxAltitude = FLT_MAX;
    if (x1 < x0) return;
    cSC4Lot* found[4] = {nullptr, nullptr, nullptr, nullptr};
    int foundCount = 0;
    for (int cx = x0; cx <= x1; ++cx) {
        for (int cz = z0; cz <= z1; ++cz) {
            cSC4Lot* lot = LotAt(lotMap_, cx, cz);
            if (!lot) continue;
            bool seen = false;
            for (int i = 0; i < foundCount; ++i)
                if (found[i] == lot) seen = true;
            if (seen) continue;
            if (foundCount < 4) found[foundCount++] = lot;
        }
    }
    for (int i = 0; i < foundCount; ++i) {
        if (found[i]->GetState() > 1) {   // cISC4Lot vslot 0x108
            minAltitude = 32.0f;
            maxAltitude = 32.0f;
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
// network / water queries
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100130604. Lists the network occupants of the cell (occupant manager vslot 0x140, type
// 0xc772bf98) and returns the one whose network flag set contains `flags`. The returned pointer is
// borrowed, like the original (the QI reference is released before returning).
IZoneDeveloperNetworkOccupant* cSC4ZoneDeveloper::GetNetwork(int x, int z, std::uint32_t flags) {
    if (!occupantManager_) return nullptr;
    std::vector<cISC4Occupant*> occupants;
    occupantManager_->ListOccupants(occupants, x, z, kZoneOccupantTypeNetwork, -1);
    IZoneDeveloperNetworkOccupant* result = nullptr;
    for (cISC4Occupant* occupant : occupants) {
        if (!occupant) continue;
        void* network = nullptr;
        if (!occupant->QueryInterface(kGZIID_cISC4NetworkOccupant, &network) || !network) continue;
        auto* view = static_cast<IZoneDeveloperNetworkOccupant*>(network);
        const bool hit = view->HasNetworkFlags(flags);
        view->Release();   // the original releases the QI reference even for the chosen occupant
        if (hit) {
            result = view;
            break;
        }
    }
    for (cISC4Occupant* occupant : occupants)
        if (occupant) occupant->Release();
    return result;
}

// @ 0x100130f02. A lot-manager network occupant first; while the lot changes are deferred the road
// (flag 8) and rail (flag 0x800) sub-network cell flags are consulted instead.
bool cSC4ZoneDeveloper::ExistsNetworkOfType(int x, int z, std::uint32_t flags) {
    if (GetNetwork(x, z, flags)) return true;
    if (lotChangesEnabled_) return false;
    IZoneDeveloperNetworkApi* network = nullptr;
    std::uint32_t mask = 0;
    if ((flags & 8) != 0) {
        network = roadNetwork_;
        mask = 8;
    } else if ((flags & 0x800) != 0) {
        network = railNetwork_;
        mask = 0x800;
    } else {
        return false;
    }
    if (!network) return false;
    std::uint32_t cellFlags = 0;
    return network->GetCellFlags(x, z, mask, cellFlags);
}

// @ 0x100130274. Direction 0..3 maps to {1, 2, 3, 0}; a network occupant that reports the connection
// answers directly, else (in the deferred mode) the road and rail cell flags are tested with the
// direction byte.
bool cSC4ZoneDeveloper::ExistsConnectionOfType(int x, int z, std::uint32_t flags, int direction,
                                               int networkType) {
    const std::uint32_t directionFlag =
        static_cast<unsigned>(direction) < 4 ? kDirectionFlags[direction] : 0xffffffffu;
    if (IZoneDeveloperNetworkOccupant* network = GetNetwork(x, z, flags)) {
        if (network->IsConnected(directionFlag, networkType)) return true;
    }
    if ((flags & 0x808) == 0 || lotChangesEnabled_) return false;
    std::uint32_t cellFlags = 0;
    bool found = false;
    if (roadNetwork_) found = roadNetwork_->GetCellFlags(x, z, 8, cellFlags);
    if (!found && railNetwork_) found = railNetwork_->GetCellFlags(x, z, 0x800, cellFlags);
    if (!found) return false;
    return (cellFlags & (0xffu << ((directionFlag & 3) << 3))) != 0;
}

// @ 0x1001308ea. Tail call to the terrain water query (vslot 0x110).
bool cSC4ZoneDeveloper::IsCellWater(int x, int z) { return terrain_ ? terrain_->IsCellWater(x, z) : false; }

// @ 0x1001303ea. Classifies one cell of the parcellization boundary and adds its cost:
//   - a street (0x1f4f) cell: 5 for diagonal/road (0x104), 30 for rail (0x808), 100 for a road (0x401),
//     400 for a power line (0x40), 5 for a crossing (0x2); no other street cell costs anything;
//   - no lot and no water: 5 when a building occupant (0x278128a0 / 0x2890d4de) sits on the cell;
//   - a lot or water: 5.
// An unclassified (0) boundary costs 1.
void cSC4ZoneDeveloper::ExamineCell(int x, int z, BoundaryCondition& boundary, int& cost) {
    auto hasNetwork = [&](std::uint32_t flags) { return ExistsNetworkOfType(x, z, flags); };
    if (hasNetwork(kNetworkFlagAnyConnection)) {
        if (hasNetwork(kNetworkFlagStreet)) {
            boundary = kBoundaryBlocked;
            cost += 5;
        } else if (hasNetwork(kNetworkFlagRail)) {
            if (static_cast<int>(boundary) < 1) boundary = kBoundarySoft;
            cost += 0x1e;
        } else if (hasNetwork(kNetworkFlagRoad)) {
            if (static_cast<int>(boundary) < 1) boundary = kBoundarySoft;
            cost += 100;
        } else if (hasNetwork(kNetworkFlagPowerLine)) {
            if (static_cast<int>(boundary) < 1) boundary = kBoundarySoft;
            cost += 400;
        } else if (hasNetwork(2)) {
            boundary = kBoundaryBlocked;
            cost += 5;
        }
    } else if (!GetLot(x, z, false) && !IsCellWater(x, z)) {
        if (occupantManager_) {
            std::vector<cISC4Occupant*> occupants;
            occupantManager_->ListOccupants(occupants, x, z, 0, -1);
            for (cISC4Occupant* occupant : occupants) {
                if (!occupant) continue;
                const std::uint32_t type = occupant->GetType();   // cISC4Occupant vslot 0x38
                if (type == kZoneOccupantTypeBuilding || type == kZoneOccupantTypeBlocking2) {
                    boundary = kBoundaryBlocked;
                    cost += 5;
                }
            }
            for (cISC4Occupant* occupant : occupants)
                if (occupant) occupant->Release();
        }
    } else {
        boundary = kBoundaryBlocked;
        cost += 5;
    }
    if (boundary == kBoundaryNone) cost += 1;
}

//---------------------------------------------------------------------------------------------------------------------
// lot creation
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1001307bc. 0 out of the city, 1 water (unless allowed), 2 zone/region mismatch, 3 network on the
// cell, 7/8 when a lot already exists.
int cSC4ZoneDeveloper::CanCreateLotOnCell(int x, int z, bool skipZoneCheck, bool allowWater) {
    if (x < 0 || z < 0 || x >= cellsX_ || z >= cellsZ_) return 0;
    if (currentRegion_) {
        const std::uint32_t dx = static_cast<std::uint32_t>(x - currentRegion_->bounds.x0);
        if (dx >= currentRegion_->rowCount) return 0;
        const std::uint32_t dz = static_cast<std::uint32_t>(z - currentRegion_->bounds.z0);
        if (dz >= currentRegion_->bitCount) return 0;
        if (dx >= currentRegion_->rows.size() || (dz >> 5) >= currentRegion_->rows[dx].size()) return 0;
        if (((currentRegion_->rows[dx][dz >> 5] >> (dz & 0x1f)) & 1u) == 0) return 0;
    }
    if (!allowWater && IsCellWater(x, z)) return 1;
    if (!lotChangesEnabled_ && !skipZoneCheck) {
        const int zoneType = zoneManager_ ? zoneManager_->ZoneTypeAt(x, z) : 0;
        if (currentZoneType_ == 0) {
            if (!zoneManager_ || !zoneManager_->IsZoneTypeValid(zoneType)) return 2;
        } else if (zoneType != currentZoneType_) {
            return 2;
        }
    }
    if (ExistsNetworkOfType(x, z, kNetworkFlagAnyConnection)) return 3;
    return 8 - (DoesCellHaveLot(x, z) ? 1 : 0);
}

// @ 0x100131de6. The minimum cell code over the footprint; 8 means "can place".
int cSC4ZoneDeveloper::CanCreateLot(int x, int z, int sizeX, int sizeZ, bool skipZoneCheck,
                                    bool allowWaterAlways, cSC4LotConfiguration* configuration) {
    bool allowWater = true;
    if (!allowWaterAlways) {
        allowWater = configuration != nullptr && configuration->IsCompatibleWithWater();   // vslot 0x70
    }
    int result = 8;
    for (int cx = x; cx < x + sizeX; ++cx) {
        for (int cz = z; cz < z + sizeZ; ++cz) {
            const int cell = CanCreateLotOnCell(cx, cz, skipZoneCheck, allowWater);
            if (cell < result) result = cell;
        }
    }
    return result;
}

// @ 0x100131ecc. PARTIAL/SKIPPED: the plopped-lot path needs DecodeNetworkConnectionObject
// (0x100132a04) and the network occupant edge flags; only the "cannot place" answer is produced.
int cSC4ZoneDeveloper::CanCreatePloppedLot(SC4Rect<int>, int, cSC4LotConfiguration*) { return 8; }

// @ 0x100132b32. In the normal mode the placement is recorded in the pending-lot map (+0x178); in the
// deferred mode it creates the lot, either through CreateLot or (facing == -1) through
// CreateLotToFaceRoad, which is not recovered.
bool cSC4ZoneDeveloper::CreateLotIfPossible(int x, int z, int sizeX, int sizeZ, int facing,
                                            cSC4Lot*& out, bool param7, bool param8,
                                            cSC4LotConfiguration* configuration) {
    out = nullptr;
    if (CanCreateLot(x, z, sizeX, sizeZ, param7, param8, configuration) != 8) return false;
    if (!lotChangesEnabled_) {
        for (int cx = x; cx < x + sizeX; ++cx)
            for (int cz = z; cz < z + sizeZ; ++cz)
                pendingLots_[SC4Point<int>{cx, cz}] = PendingLot{SC4Point<int>{x, z}, facing};
        return true;
    }
    if (facing == -1) {
        // CreateLotToFaceRoad (0x10012cbcc) is not recovered.
        return false;
    }
    CreateLot(x, z, sizeX, sizeZ, facing, out);
    return out != nullptr;
}

// @ 0x100132cde. Allocates the lot, takes the caller's and the lot list's references, calls
// cISC4Lot::Init (vslot 0x28) and SetState(UnderConstruction, 0, 0) (vslot 0x110), fills every cell of
// the footprint and notifies the safe list / building development simulator.
bool cSC4ZoneDeveloper::CreateLot(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out) {
    auto* lot = new cSC4Lot(x, z, sizeX, sizeZ, facing);
    out = lot;
    lot->AddRef();      // the caller's reference
    lot->Init();
    lot->SetState(1, 0, 0);
    int maxX = x - 1 + sizeX;
    int maxZ = z - 1 + sizeZ;
    if (maxX < x) maxX = x;
    if (maxZ < z) maxZ = z;
    lot->AddRef();      // the developer's lot-list reference
    for (int cx = x; cx <= maxX; ++cx)
        for (int cz = z; cz <= maxZ; ++cz)
            if (cx >= 0 && cz >= 0 && cx < cellsX_ && cz < cellsZ_)
                lotMap_[static_cast<std::size_t>(cx)][static_cast<std::size_t>(cz)] = lot;
    if (g_host) {
        if (IZoneDeveloperSafeListApi* iterator = g_host->SafeListIterator()) {
            // The live iterator inserts the lot at its current position; the count is maintained by the
            // iterator, so it is not bumped here (like the original).
            iterator->AddAfter(lot);
        } else {
            lots_.push_front(lot);
            ++lotCount_;
        }
    } else {
        lots_.push_front(lot);
        ++lotCount_;
    }
    if (buildingDevelopment_) buildingDevelopment_->AddLot(lot);   // 0x120 -> 0x158
    return out != nullptr;
}

//---------------------------------------------------------------------------------------------------------------------
// lot deletion
//---------------------------------------------------------------------------------------------------------------------

// @ 0x10012be32. With lot changes enabled: detach from the lot developer, remove the building unit
// occupant, clear the footprint, drop the lot-list reference, let the building development simulator
// forget the lot, shut it down and release the caller's reference. While the changes are deferred the
// footprint is only recorded in the pending-deletion map (+0x190, cell -> {origin, zone type}).
void cSC4ZoneDeveloper::DeleteLot(cSC4Lot* lot) {
    if (!lot) return;
    std::int32_t rawRect[4] = {0, 0, 0, 0};
    lot->GetBoundingRect(rawRect);
    int x0 = rawRect[0], z0 = rawRect[1], x1 = rawRect[2], z1 = rawRect[3];
    if (x1 <= x0) x1 = x0;
    if (z1 < z0) z1 = z0;

    if (lotChangesEnabled_) {
        if (lotDeveloper_) lotDeveloper_->RemoveLot(lot);   // lot developer vslot 0x58
        std::uint32_t unitId = 0;
        if (buildingDevelopment_ && buildingDevelopment_->GetLotUnitId(lot, unitId)) {
            if (occupantManager_) occupantManager_->RemoveOccupant(unitId, 1, 0);
        }
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                if (x >= 0 && z >= 0 && x < cellsX_ && z < cellsZ_)
                    lotMap_[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)] = nullptr;
        // cSC4SafeList::Remove consumes the developer's lot-list reference.
        auto it = std::find(lots_.begin(), lots_.end(), lot);
        if (it != lots_.end()) {
            lots_.erase(it);
            if (lotCount_ > 0) --lotCount_;   // cSC4SafeList::Remove keeps the +0xe0 count
            lot->Release();
        }
        if (lot->GetUnit() != nullptr && buildingDevelopment_) buildingDevelopment_->ForgetLot(lot);   // 0x120 -> 0x108
        lot->Shutdown();    // cISC4Lot vslot 0x38
        lot->Release();     // vslot 0x10: the caller's reference
        return;
    }

    const std::uint32_t zoneType = lot->GetZoneType();
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            pendingDeletes_[SC4Point<int>{x, z}] = PendingDelete{SC4Point<int>{x0, z0},
                                                                 static_cast<int>(zoneType)};
}

// @ 0x10012c130. Collects the lots touching the rectangle and deletes each; the temporary list holds a
// reference, so the last release is the list's.
void cSC4ZoneDeveloper::DeleteLots(int x0, int z0, int x1, int z1) {
    std::list<cRZAutoRefCount<cSC4Lot>> lots;
    if (GetLotsInCellRect(lots, x0, z0, x1, z1, false) != 0) {
        for (const auto& lot : lots) DeleteLot(lot.get());
    }
}

// @ 0x10012b0aa. Clears both pending maps and the highlight of the two road networks and the terrain
// view (vslot 0x28 of each network, 0x48 of the terrain view).
void cSC4ZoneDeveloper::ClearHighlight() {
    pendingLots_.clear();
    pendingDeletes_.clear();
    if (roadNetwork_) roadNetwork_->ClearHighlight();
    if (streetNetwork_) streetNetwork_->ClearHighlight();
    if (terrainView_) terrainView_->ClearHighlight();
}

//---------------------------------------------------------------------------------------------------------------------
// options / zoning entry points
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100133440. The original stores the first argument at +0x12f, the second at +0x12e and the third
// at +0x130 (SetOptions(swap orientation, extend as streets, examine boundaries)).
void cSC4ZoneDeveloper::SetOptions(bool swapOrientation, bool extendAsStreets, bool examineBoundaries) {
    swapOrientation_ = swapOrientation ? 1 : 0;
    extendAsStreets_ = extendAsStreets ? 1 : 0;
    examineBoundaries_ = examineBoundaries ? 1 : 0;
}

// @ 0x100130914. PARTIAL: the flag bookkeeping and the zone-validity test are recovered; OnZone and
// UpdateHighlightTextures are part of the unrecovered parcellization pipeline.
void cSC4ZoneDeveloper::HighlightParcels(const SC4CellRegion<int>& region, int zoneType,
                                         const SC4Point<int>* origin, SC4CellRegion<int>** out) {
    lotChangesEnabled_ = 0;
    currentZoneType_ = zoneType;
    highlightOrigin_ = const_cast<SC4Point<int>*>(origin);
    if (zoneManager_ && zoneManager_->IsZoneTypeValid(zoneType)) {
        ClearHighlight();
        OnZone(&region, out);
        UpdateHighlightTextures(&region);
    }
    lotChangesEnabled_ = 1;
    currentZoneType_ = 0;
    highlightOrigin_ = nullptr;
}

// @ 0x10012b046. PARTIAL for the same reason: both branches call the unrecovered parcellization.
void cSC4ZoneDeveloper::DoParcellization(const SC4CellRegion<int>& region, int zoneType, bool arg) {
    currentZoneType_ = zoneType;
    if (!zoneManager_ || !zoneManager_->IsZoneTypeValid(zoneType)) {
        OnDezone(&region, arg);
    } else {
        ClearHighlight();
        OnZone(&region, nullptr);
    }
    currentZoneType_ = 0;
}

//---------------------------------------------------------------------------------------------------------------------
// parcellization / highlight pipeline (not recovered; see notes/ledger_pending/cSC4ZoneDeveloper.txt)
//---------------------------------------------------------------------------------------------------------------------

void cSC4ZoneDeveloper::OnZone(const SC4CellRegion<int>* region, SC4CellRegion<int>** out) {
    (void)region;
    (void)out;
    // SKIPPED: 0x10012b160 (1987 bytes) runs the whole parcel/lot/street pipeline.
}

void cSC4ZoneDeveloper::OnDezone(const SC4CellRegion<int>* region, bool arg) {
    (void)region;
    (void)arg;
    // SKIPPED: 0x10012b976 (990 bytes) creates lot remnants and drives the demolition utility.
}

void cSC4ZoneDeveloper::UpdateHighlightTextures(const SC4CellRegion<int>* region) {
    (void)region;
    // SKIPPED: 0x100130994 writes the highlight texture instances through the 3D renderer.
}

//---------------------------------------------------------------------------------------------------------------------
// misc accessors that the original defines out of line
//---------------------------------------------------------------------------------------------------------------------

bool cSC4ZoneDeveloper::CheatCodesWereRegistered() const {
    return g_cheatRegisteringObjects.find(this) != g_cheatRegisteringObjects.end();
}

IZoneDeveloperLotIterator* cSC4ZoneDeveloper::GetSafeLotIterator() {
    // SKIPPED: the original returns cSC4SafeList<cISC4Lot*>::GetIterator (0x100133466), an unrecovered
    // class that also keeps the live-iterator list at +0xc8.
    return nullptr;
}

}  // namespace sc4
