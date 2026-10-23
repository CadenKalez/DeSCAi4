// cSC4SeaportDeveloper — the seaport developer (port developer for zone type 12). It derives
// cISC4SeaportDeveloper (the seaport-facing interface) and cSC4PortDeveloper (the recovered port machinery):
// the base owns the cPortLot records, the per-port-type growth configurations and the monthly simulation,
// while this class adds
//   * the seaport budget department and its single "seaport" line item (id 1),
//   * the sea-access map: a per-cell byte map built from the terrain's water cells and flood-filled from the
//     four city borders, so a port knows whether it touches open sea (bits 1/2/4/8 = north/west/east/south),
//   * the trip-capacity feed to the traffic simulator (capacity scaled by the department's funding ratio),
//   * the terrain flattening of a new lot from the port type's per-vertex height grid,
//   * the two extra messages (grow the ports, the water map is stale) and the monthly water-access check that
//     flags the "no sea access" prop state and raises the advisor event.
//
// Recovered from Sim City 4 Deluxe Edition 1.2.1 (0x1000ca01e..0x1000cb5e4, class id 0xea230a28, simulator
// type 0xea3957ad). Derivation confirmed from the Itanium RTTI (cSC4SeaportDeveloper : cISC4SeaportDeveloper,
// cSC4PortDeveloper) and the four vtables of the object:
//   +0x00 cISC4SeaportDeveloper   vtable 0x100a6faa0  (0x100aff638 is its pure interface table)
//   +0x08 cISC4PortDeveloper      vtable 0x100a6fba8  (the cSC4PortDeveloper subobject)
//   +0x10 cIGZMessageTarget2      vtable 0x100a6fc78
//   +0x18 cIGZSerializable        vtable 0x100a6fcb8
// so that the cSC4PortDeveloper base lands at +0x08 and its members keep the offsets the base header records.
//
// Primary vtable (0x100a6faa0), in declaration order:
//   [ 0] 0x00 QueryInterface   [ 1] 0x08 AddRef            [ 2] 0x10 Release
//   [ 3] 0x18 Init             [ 4] 0x20 Shutdown          [ 5] 0x28 GetAvgEfficiency
//   [ 6] 0x30 GetSimulatorType [ 7] 0x38 GetLeastEfficientPort [ 8] 0x40 GetAllPortLots
//   [ 9] 0x48 GetNumPorts
//   -- cISC4SeaportDeveloper's own seven --
//   [10] 0x50 AsISC4PortDeveloper        [11] 0x58 GetNumSeaports
//   [12] 0x60 GetSeaportUsageValues      [13] 0x68 GetNumSeaportTrips
//   [14] 0x70 GetMaintenanceCost         [15] 0x78 CityHasSea
//   [16] 0x80 GetCellSeaAccess
//   -- the concrete class's own --
//   [17] 0x88 ~cSC4SeaportDeveloper      [18] 0x90 deleting dtor
//   [19] 0x98 FinalRelease               [20] 0xa0 Write
//   [21] 0xa8 Read                       [22] 0xb0 GetGZCLSID
//   [23] 0xb8 DoMessage                  [24] 0xc0 GetTripsForPort
//   [25] 0xc8 OnStageChange              [26] 0xd0 OnNewLotCreated
//   [27] 0xd8 SendAdvisorUpgradeMessage  [28] 0xe0 DoMonthlyTick
//   [29] 0xe8 UpdateBudget               [30] 0xf0 GetAllocatedFunding
// The secondary tables (0x100a6fba8.., offset-to-top -8 / -0x10 / -0x18) hold the adjustor thunks to the
// same functions. GetNumSeaports returns the port list's size (base +0x78) and AsISC4PortDeveloper the base
// subobject at +0x08, exactly like the binary.
//
// Original layout (0xe0 bytes, the size the GZCOM factory allocates):
//   +0x00..+0xb7 see PortDeveloper.h (the base object at +0x08; its +0x50 slot, never read by the base, holds
//         the *department budget* pointer that this class caches; see the note below)
//   +0xb8 bool "water access has to be re-checked this month" (set by OnNewLotCreated and by message
//         0x6a3b0afd, cleared by DoMonthlyTick after the check), +0xb9 bool "rebuild the water map in the
//         next monthly tick" (never set by the recovered code), +0xba bool city has open sea
//   +0xc0 std::vector<uint8_t> sea-access map: width * height bytes, [z * width + x]; 0x80 = the terrain cell
//         has water, the low bits are the borders the connected water region reaches (1 north, 2 west,
//         4 east, 8 south)
//   +0xd8 int map width (terrain CellCountX), +0xdc int map height (terrain CellCountZ)
//
// Supporting classes recovered in this translation unit:
//   cSC4LotPropFilter (0x28 bytes in the original, vtable 0x100aff6d8, with the cIGZSerializable view at
//   +8): the transient occupant filter GetLotProps hands to the occupant manager — props (type 0xa823821e)
//   whose world position is inside the lot's box and which carry a given property. +0x14 float minX,
//   +0x18 minZ, +0x1c maxX, +0x20 maxZ, +0x24 uint32 property id (0 = any prop).
//
// Quirks kept:
//  * OnNewLotCreated flattens the lot's terrain with the *world position* of each cell centre truncated to an
//    int as the terrain vertex index (0x1000cabe5 / 0x1000cac90: `cvttss2si` of CellCenterToPosition's
//    output, passed straight to cSTETerrain::SetAltitudeAtVertex). With 16 m cells the index is 16 * cell + 8,
//    so the height grid lands on the wrong vertices; kept bit-for-bit (the test asserts the arguments).
//  * The grid is applied only when rows * columns == (x1 - x0) * (z1 - z0) (0x1000cab65), i.e. exactly the
//    lot's (sizeX - 1) x (sizeZ - 1) interior cells, and the loops run x0..x1-1 / z0..z1-1; the four lot
//    facings rotate the grid (0: grid[x][z], 1: grid[zR][xR], 2: grid[xR][zR], 3: grid[z][x]).
//  * GetLotProps builds the filter's box from the *cell centres* of (x0, z0) and (x1 + 1, z1 + 1), not from
//    the lot's corners (0x1000cb31b: `incl %esi / incl %edx`), while the occupant manager query uses the
//    inclusive cells (x0, z0)..(x1, z1).
//  * GetMaintenanceCost reads the stage's *budget* (+0x08 of cPortGrowthRule), not its cost (+0x18)
//    (0x1000cb4f2: `[stages] + 8 + stage * 0x20`), then scales it by the line item's funding ratio.
//  * GetAllocatedFunding and the four funding-ratio users call GetLineItem(1) without a null check
//    (0x1000cb1be would fault before the department exists); this port returns 0 / ratio 1 instead.
//  * BuildWaterAccessMap returns without touching the object when SL::spTerrain is null (0x1000ca2a2), and
//    only *starts* a fill when the border water state changes (the `bVar11` run detection); the resulting
//    marks are the same as filling from every border water cell.
//  * DoMonthlyTick's water check only runs when +0xb8 is set, and sends advisor event 0x4a490d33 once for the
//    whole pass when at least one port lost its sea access (0x1000cb0xx).
//  * Init returns true when the object was already initialised, without re-reading the budget or rebuilding
//    the map (0x1000ca1a8); Shutdown skips everything when it is not initialised and leaves the department
//    pointer released.
//  * The seaport line item is created with SetDisplayFlag(1, false) (0x1000ca0xx: `vslot 0x90(1, 0)`), and
//    the department's max allowed funding percentage is 1.2 (kfMaxAllowedFundingPercentage, 0x100a16ee0).
//
// Every call outside the base goes through ISeaportHost (one method per original call, with the original
// global / vtable slot in the comment). The base's own host is IPortDeveloperHost, so a host object normally
// implements both (the test's Fixture does).
//
// Base seam that is not sufficient: the original caches the seaport department budget in the base's +0x50
// slot, which the recovered cSC4PortDeveloper keeps private (two "never read" uint32 fields). This port
// therefore looks the department up through ISeaportHost::BudgetSimulator() on each use (the budget simulator
// owns it, so the pointer is stable); Init still AddRefs it and Shutdown releases it so the reference count
// matches the original. Everything else the base exposes through SetIdentity / MutablePortLots /
// MutableConfigurations / GetRequiredBudget / SendAdvisorMessage / GrowPortsToNextStage /
// GetPortUsageValues / GetPortLot is used as is.
#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "sc4/OccupantManager.h"
#include "sc4/PortDeveloper.h"
#include "sc4/PropOccupant.h"

namespace sc4 {

class cSC4SeaportDeveloper;

constexpr std::uint32_t kSeaportDeveloperIID = 0xca22fe3e;        // cISC4SeaportDeveloper
constexpr std::uint32_t kSeaportDeveloperClassId = 0xea230a28;    // cGZCLSID (== GetGZCLSID)
constexpr std::uint32_t kSeaportSimulatorType = 0xea3957ad;       // GetSimulatorType
// identity written by the constructor (the ini exemplar instance and the four SetIdentity fields)
constexpr std::uint32_t kSeaportPortDeveloperId = 0xea29ad92;
constexpr std::uint16_t kSeaportZoneType = 12;
constexpr std::uint32_t kSeaportHistoryCategory = 0xea3e904a;
constexpr std::uint32_t kSeaportAdvisorEvent = 0x4a4155ab;
constexpr std::uint32_t kSeaportBuildingGroup = 0x1509;
// the budget department / line item Init sets up
constexpr std::uint32_t kSeaportDepartmentId = 0x4a4155ab;
constexpr std::uint32_t kSeaportDepartmentGroup = 0xaa369059;
constexpr std::uint32_t kSeaportDepartmentNameGroup = 0x6a3ff01c;      // kBudgetGroupNameTable
constexpr std::uint32_t kSeaportDepartmentNameInstance = 0xca493af3;
constexpr std::uint32_t kSeaportLineItemId = 1;
constexpr float kSeaportMaxFundingPercentage = 1.2f;                   // kfMaxAllowedFundingPercentage
// messages (registered in Init, removed in Shutdown)
constexpr std::uint32_t kSeaportMessageGrowPorts = 0x8a394dc4;         // -> GrowPortsToNextStage
constexpr std::uint32_t kSeaportMessageWaterMapDirty = 0x6a3b0afd;     // -> BuildWaterAccessMap
// advisor events
constexpr std::uint32_t kSeaportAdvisorUpgrade = 0x8a394dc4;           // SendAdvisorUpgradeMessage
constexpr std::uint32_t kSeaportAdvisorNoSeaAccess = 0x4a490d33;       // DoMonthlyTick
// the prop property DoMonthlyTick flips to state 1 when a port has no sea access
constexpr std::uint32_t kSeaportWaterAccessProperty = 0xc98204b8;
constexpr std::uint32_t kSeaportTravelType = 3;                        // cISC4TrafficSimulator travel type
// sea-access map
constexpr std::uint8_t kSeaportWaterBit = 0x80;
constexpr std::uint8_t kSeaportBorderNorth = 1;   // z == 0
constexpr std::uint8_t kSeaportBorderWest = 2;    // x == 0
constexpr std::uint8_t kSeaportBorderEast = 4;    // x == width - 1
constexpr std::uint8_t kSeaportBorderSouth = 8;   // z == height - 1
constexpr std::uint8_t kSeaportBorderMask = 0x0f;

// cSC4LotPropFilter: the transient occupant filter cSC4SeaportDeveloper::GetLotProps builds (vtable
// 0x100aff6d8, 0x28 bytes). Its ctor is inlined at its only call site; the two destructor symbols are at
// 0x1000cb6c6 / 0x1000cb6d0. The original derives cSC4OccupantFilter (which also implements
// cIGZSerializable); this port derives the recovered cSC4OccupantFilter, so the extra vptr is present and the
// four fields start at +0x18 instead of +0x14 (documented deviation, the object is never serialised).
class cSC4LotPropFilter : public cSC4OccupantFilter {
public:
    cSC4LotPropFilter() = default;
    cSC4LotPropFilter(float minX, float minZ, float maxX, float maxZ, std::uint32_t property);
    ~cSC4LotPropFilter() override = default;

    // @ 0x1000cb624. Props (type 0xa823821e) inside the box, carrying `property` when it is non-zero.
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;

    float MinX() const { return minX_; }
    float MinZ() const { return minZ_; }
    float MaxX() const { return maxX_; }
    float MaxZ() const { return maxZ_; }
    std::uint32_t Property() const { return property_; }

    static constexpr std::uint32_t kOriginalClassSize = 0x28;

private:
    float minX_ = 0.0f;             // +0x18 (original +0x14)
    float minZ_ = 0.0f;             // +0x1c (original +0x18)
    float maxX_ = 0.0f;             // +0x20 (original +0x1c)
    float maxZ_ = 0.0f;             // +0x24 (original +0x20)
    std::uint32_t property_ = 0;    // +0x28 (original +0x24)
};

// ---------------------------------------------------------------------------------------------------
// Seams. Each interface is one original object narrowed to the calls this class makes; the original global
// and vtable slot are in the comment. The host owns the objects (the originals are singletons / city-owned).
// ---------------------------------------------------------------------------------------------------
class ISeaportTerrainApi {
public:
    virtual ~ISeaportTerrainApi() = default;
    virtual void AddRef() = 0;                                                  // cIGZUnknown 0x08
    virtual void Release() = 0;                                                 // cIGZUnknown 0x10
    virtual std::int32_t CellCountX() = 0;                                      // cSTETerrain vslot 0x38
    virtual std::int32_t CellCountZ() = 0;                                      // cSTETerrain vslot 0x40
    virtual bool CellHasWater(std::uint32_t x, std::uint32_t z) = 0;            // vslot 0x110
    virtual void SetAltitudeAtVertex(std::int32_t x, std::int32_t z, float altitude) = 0;   // vslot 0x190
};

class ISeaportTrafficApi {
public:
    virtual ~ISeaportTrafficApi() = default;
    virtual void AddRef() = 0;                                                  // cIGZUnknown 0x08
    virtual void Release() = 0;                                                 // cIGZUnknown 0x10
    // cISC4TrafficSimulator vslot 0xa8 GetTrafficArrived(cISCPropertyHolder*, travelType)
    virtual std::uint32_t GetTrafficArrived(cISCPropertyHolder* source, std::uint32_t travelType) = 0;
    // vslot 0x98 SetMaxTripCapacity(cISCPropertyHolder*, travelType, capacity)
    virtual void SetMaxTripCapacity(cISCPropertyHolder* source, std::uint32_t travelType,
                                    std::uint32_t capacity) = 0;
};

// The city as the seaport uses it: the base's IPortCityApi plus the two calls the base does not model.
class ISeaportCityApi : public IPortCityApi {
public:
    virtual ISeaportTrafficApi* GetTrafficSimulator() = 0;                      // cSC4City vslot 0x228
    virtual void CellCenterToPosition(std::int32_t cellX, std::int32_t cellZ, float& x, float& z) = 0;  // 0x308
};

// cSC4LineItem narrowed to the calls the seaport budget code makes (vtable 0x100a88f48).
class ISeaportLineItemApi {
public:
    virtual ~ISeaportLineItemApi() = default;
    virtual void AddRef() = 0;                                       // 0x08
    virtual void Release() = 0;                                      // 0x10
    virtual std::int64_t GetFullExpenses() const = 0;                // 0x50
    virtual std::int64_t GetCurrentExpenses() const = 0;             // 0x58
    virtual void SetFullExpenses(std::int64_t value) = 0;            // 0x60
    virtual void SetDisplayFlag(std::uint32_t flag, bool on) = 0;    // 0x90
};

// cSC4DepartmentBudget narrowed to the calls the seaport makes (vtable 0x100a88dc0).
class ISeaportDepartmentBudgetApi {
public:
    virtual ~ISeaportDepartmentBudgetApi() = default;
    virtual void AddRef() = 0;                                                            // 0x08
    virtual void Release() = 0;                                                           // 0x10
    virtual void SetDepartmentName(std::uint32_t group, std::uint32_t instance) = 0;      // 0x28
    virtual void SetMaxAllowedFundingPercentage(const SC4Percentage& percentage) = 0;     // 0x68
    virtual ISeaportLineItemApi* CreateLineItem(std::uint32_t id, bool localFunding) = 0; // 0x88
    virtual ISeaportLineItemApi* GetLineItem(std::uint32_t id) = 0;                       // 0xa0
};

// cSC4BudgetSimulator narrowed to the two department lookups (vtable 0x100a87130).
class ISeaportBudgetSimApi {
public:
    virtual ~ISeaportBudgetSimApi() = default;
    virtual ISeaportDepartmentBudgetApi* CreateDepartmentBudget(std::uint32_t id, std::uint32_t group) = 0;  // 0xc8
    virtual ISeaportDepartmentBudgetApi* GetDepartmentBudget(std::uint32_t id) = 0;                          // 0xd8
};

// cISC4PropOccupant (iid 0xe9793a65) narrowed to SetState (cSC4PropOccupant vslot 0x58).
class ISeaportPropOccupantApi {
public:
    virtual ~ISeaportPropOccupantApi() = default;
    virtual void AddRef() = 0;                                  // 0x08
    virtual void Release() = 0;                                 // 0x10
    virtual void SetState(std::uint32_t state) = 0;             // 0x58
};

// cSC4OccupantManager narrowed to the two calls GetLotProps makes (vtable 0x10024..).
class ISeaportOccupantManagerApi {
public:
    virtual ~ISeaportOccupantManagerApi() = default;
    // vslot 0x148: GetOccupantsByStandardCityCells(list, a = {x0, z0}, b = {x1, z1}, filter, maxCount)
    virtual void GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b,
                                                 cISC4OccupantFilter* filter,
                                                 std::uint32_t maxCount) = 0;
    virtual void ReleaseOccupantList(OccupantList& list) = 0;    // vslot 0x180
};

// The seaport's services. IPortDeveloperHost is the base's; the six extra methods are this class's calls.
class ISeaportHost : public IPortDeveloperHost {
public:
    virtual ISeaportCityApi* SeaportCity() = 0;                        // base +0x40 (cISC4City)
    virtual ISeaportBudgetSimApi* BudgetSimulator() = 0;               // base +0x48 (cISC4BudgetSimulator)
    virtual ISeaportTrafficApi* TrafficSimulator() = 0;                // SL::spTrafficSim
    virtual ISeaportTerrainApi* Terrain() = 0;                         // SL::spTerrain
    virtual ISeaportOccupantManagerApi* OccupantManager() = 0;         // SL::spOccupantManager
    // QI 0xe9793a65 on the occupant (0x1000cb489); the returned view is AddRef'd, the caller releases it.
    virtual ISeaportPropOccupantApi* AsPropOccupant(cISC4Occupant* occupant) = 0;
};
ISeaportHost* SeaportHost();
void SetSeaportHost(ISeaportHost* host);

// ---------------------------------------------------------------------------------------------------
// cISC4SeaportDeveloper: the primary COM view. The first ten virtuals come from cISC4PortDeveloper.
// ---------------------------------------------------------------------------------------------------
class cISC4SeaportDeveloper : public cISC4PortDeveloper {
public:
    virtual cISC4PortDeveloper* AsISC4PortDeveloper() = 0;                   // [10] 0x50
    virtual int GetNumSeaports() = 0;                                        // [11] 0x58
    virtual bool GetSeaportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                                       std::uint32_t& capacity) = 0;         // [12] 0x60
    virtual int GetNumSeaportTrips() = 0;                                    // [13] 0x68
    virtual std::int64_t GetMaintenanceCost(const cSC4Lot* lot) = 0;         // [14] 0x70
    virtual bool CityHasSea() = 0;                                           // [15] 0x78
    virtual std::uint8_t GetCellSeaAccess(std::uint32_t x, std::uint32_t z) = 0;   // [16] 0x80

protected:
    ~cISC4SeaportDeveloper() = default;
};

// ---------------------------------------------------------------------------------------------------
// The object.
// ---------------------------------------------------------------------------------------------------
class cSC4SeaportDeveloper : public cISC4SeaportDeveloper, public cSC4PortDeveloper {
public:
    cSC4SeaportDeveloper();                                          // @ 0x1000ca01e
    ~cSC4SeaportDeveloper() override;                                 // @ 0x1000ca0cc (0x88/0x90)

    // cIGZUnknown: one shared count, held by the cSC4PortDeveloper subobject at +0x08.
    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x1000ca6a6
    std::uint32_t AddRef() override;                                  // @ 0x1000ca73a
    std::uint32_t Release() override;                                 // @ 0x1000ca76e
    void FinalRelease() override;                                     // @ 0x1000ca7a2

    // cISC4PortDeveloper
    bool Init() override;                                             // @ 0x1000ca194
    bool Shutdown() override;                                         // @ 0x1000ca624
    float GetAvgEfficiency() override;                                // @ 0x1000ca9aa
    std::uint32_t GetSimulatorType() const override { return kSeaportSimulatorType; }   // @ 0x1000cb596
    bool GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) override;  // @ 0x1000caeee
    void GetAllPortLots(std::vector<cSC4Lot*>& out) override;         // @ 0x1000cb5a2
    int GetNumPorts(int sizeMask, int stageMask) override;            // @ 0x1000cb5b0

    // cISC4SeaportDeveloper
    cISC4PortDeveloper* AsISC4PortDeveloper() override;               // @ 0x1000cb5be
    int GetNumSeaports() override;                                    // @ 0x1000cb5c8
    bool GetSeaportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                               std::uint32_t& capacity) override;     // @ 0x1000cb1fe
    int GetNumSeaportTrips() override;                                // @ 0x1000cb4ae
    std::int64_t GetMaintenanceCost(const cSC4Lot* lot) override;     // @ 0x1000cb4f2
    bool CityHasSea() override;                                       // @ 0x1000cb29c
    std::uint8_t GetCellSeaAccess(std::uint32_t x, std::uint32_t z) override;   // @ 0x1000cb2a8

    // cIGZSerializable / cIGZMessageTarget2
    bool Write(cIGZOStream& stream) const override;                   // @ 0x1000ca82c
    bool Read(cIGZIStream& stream) override;                          // @ 0x1000ca8b2
    std::uint32_t GetGZCLSID() const override { return kSeaportDeveloperClassId; }   // @ 0x1000cb5d4
    bool DoMessage(cIGZMessage2* message) override;                   // @ 0x1000ca7ba

    // The seven pure slots of cSC4PortDeveloper (+ GetSimulatorType above).
    std::uint32_t GetTripsForPort(cPortLot& port) override;           // @ 0x1000cae76
    void OnStageChange(cPortLot& port) override;                      // @ 0x1000caa8e
    void OnNewLotCreated(cPortLot& port) override;                    // @ 0x1000caaa6
    void SendAdvisorUpgradeMessage(cPortLot& port) override;          // @ 0x1000ca986
    void DoMonthlyTick() override;                                    // @ 0x1000caf06
    void UpdateBudget() override;                                     // @ 0x1000cb146
    std::int64_t GetAllocatedFunding() override;                      // @ 0x1000cb1be

    // Non-virtual helpers of the original (public so the test can drive them directly).
    void BuildWaterAccessMap();                                       // @ 0x1000ca290
    void UpdateTripCapacity(cPortLot& port);                          // @ 0x1000ca9c2
    bool CalculateWaterAccessible(cPortLot& port);                    // @ 0x1000cafdc
    void SetLotPropsState(const cSC4Lot* lot, std::uint32_t property, std::int32_t state);  // @ 0x1000cb058
    void GetLotProps(const cSC4Lot* lot, OccupantList& out, std::uint32_t property);        // @ 0x1000cb2c2

    // State / accessors (the test and the file's docs use them).
    bool WaterAccessCheckPending() const { return waterAccessCheckPending_; }   // +0xb8
    bool ForceWaterMapRebuild() const { return forceWaterMapRebuild_; }         // +0xb9
    std::int32_t WaterMapWidth() const { return waterMapWidth_; }               // +0xd8
    std::int32_t WaterMapHeight() const { return waterMapHeight_; }             // +0xdc
    const std::vector<std::uint8_t>& WaterMap() const { return waterAccess_; }  // +0xc0
    void SetForceWaterMapRebuild(bool value) { forceWaterMapRebuild_ = value; }
    void SetWaterAccessCheckPending(bool value) { waterAccessCheckPending_ = value; }

    static constexpr std::uint32_t kOffsetWaterAccessCheckPending = 0xb8;
    static constexpr std::uint32_t kOffsetForceWaterMapRebuild = 0xb9;
    static constexpr std::uint32_t kOffsetCityHasSea = 0xba;
    static constexpr std::uint32_t kOffsetWaterMap = 0xc0;
    static constexpr std::uint32_t kOffsetWaterMapWidth = 0xd8;
    static constexpr std::uint32_t kOffsetWaterMapHeight = 0xdc;
    static constexpr std::uint32_t kClassSize = 0xe0;

private:
    // The department's seaport line item (GetLineItem(1)), or null when there is no department yet.
    ISeaportLineItemApi* BudgetLineItem();
    // The funding ratio current / full expenses of the line item, 1.0 when it is unknown.
    float FundingRatio();

    bool waterAccessCheckPending_ = false;    // +0xb8
    bool forceWaterMapRebuild_ = false;       // +0xb9
    bool cityHasSea_ = false;                 // +0xba
    std::uint8_t reservedBB_ = 0;             // +0xbb
    std::vector<std::uint8_t> waterAccess_;   // +0xc0
    std::int32_t waterMapWidth_ = 0;          // +0xd8
    std::int32_t waterMapHeight_ = 0;         // +0xdc
};
static_assert(sizeof(cSC4SeaportDeveloper) == 0xe0, "cSC4SeaportDeveloper must stay 0xe0 bytes");

}  // namespace sc4
