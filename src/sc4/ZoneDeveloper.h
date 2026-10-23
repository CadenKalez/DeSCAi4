// cSC4ZoneDeveloper — the zoning/growth developer: it turns a zoned region into lots, removes lots
// when a zone is cleared, and answers the lot queries the lot manager, the growth developer and the
// simulator use.
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (0x100128098..0x1001334c0, class id 0x498f9b01).
// It is one COM object with four views:
//   +0x00 cISC4ZoneDeveloper        iid 0xe818ed2b   (and iid 1)
//   +0x08 cISC4LotManager           iid 0x681905bd   (queried by cSC4City from the zone developer)
//   +0x10 cIGZMessageTarget2        iid 0x452294aa
//   +0x18 cIGZSerializable          iid 0xe4fda3d4
// One reference count at +0x24 is shared by the four views (the three secondary AddRef/Release
// thunks adjust their `this` back to the primary before touching it).
//
// Original layout (0x1c0 bytes):
//   +0x00/+0x08/+0x10/+0x18 the four vptrs
//   +0x20 initialised byte, +0x21 "loaded from stream" byte, +0x24 reference count
//   +0x28 cIGZMessageServer2, +0x30 cIGZRenderer, +0x38 cSC4BuildingDevelopmentSimulator,
//   +0x40 cISC4City, +0x48/+0x4c city cell counts, +0x50 cISC4OccupantManager, +0x58 cISC4Terrain,
//   +0x60 cISC4TerrainView, +0x68 cISC4TrafficSimulator, +0x70 cISC4ZoneManager,
//   +0x78 rail sub-network (type 0xb), +0x80 road sub-network (type 3), +0x88 street sub-network (type 0),
//   +0x90 cSC4LotDeveloper
//   +0x98 std::vector<std::vector<cISC4Lot*>> lot map, indexed [x][z]; every cell of a multi-cell lot
//         points at the same lot
//   +0xb0 std::list<cISC4Lot*> all lots (a cSC4SafeList in the original), +0xe0 its count
//   +0xc8 std::list<cSC4SafeListIterator<cISC4Lot*>*> live iterators
//   +0xe8/+0xf0 unused 16 bytes (never read outside the constructor)
//   +0xf8 cSC4SimGrid<uint16_t> maximum-capacity grid (tract size 4, instance id 0x69d5c456)
//   +0x100 std::map<ZoneType, map<WealthType, map<DemandType, set<pair<u8,u8>>>>>
//   +0x118 int[5] parcellization tuning (2, 2, 1, 3, 6)
//   +0x12c scratch byte saved/restored by Parcellize, +0x12d lot changes enabled,
//   +0x12e extend as streets, +0x12f swap lot orientation, +0x130 examine boundaries first
//   +0x134 current zone type, +0x138 current SC4CellRegion, +0x140 highlight origin,
//   +0x144 unknown int, +0x148 int[4] boundary cost, +0x158 int[4] boundary condition,
//   +0x168 lot orientation, +0x16c alternate orientation, +0x170 renderer highlight scale
//   +0x178 map pending lot placements (cell -> {{x,z}, facing}), filled by CreateLotIfPossible when
//         lot changes are deferred
//   +0x190 map pending lot deletions (cell -> {{x,z}, zone type}), filled by DeleteLot in the same mode
//   +0x1a8 map<int, BoundaryLotTextureRule> boundary lot texture table
//
// Quirk kept: CreateLot takes *two* references (one for the caller, one for the developer's lot
// list) and Release deletes the object as soon as the count drops below 2 (0x100132d39, 0x100131282).
//
// The neighbours this class reaches through the SL::sp* globals (city, occupant manager, terrain,
// traffic/zone/network managers, building development, lot developer, renderer, message server) are
// reached through IZoneDeveloperHost, one method per original call with its vtable slot. cSC4Lot is
// used directly (recovered in Lot.h); mapping the calls to its slots is noted in the .cpp.
//
// Not recovered here (see the PARTIAL/SKIPPED sections of notes/ledger_pending/cSC4ZoneDeveloper.txt):
// the parcellization pipeline (OnZone, Parcellize, SubdivideTheZone, ...), the street drawing helpers,
// CanCreatePloppedLot/CheckLotTerrainConstraints (need DecodeNetworkConnectionObject and the surface
// water), the highlight texture update (needs the 3D renderer) and the plop cheat commands.
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZAutoRefCount.h"
#include "sc4/Lot.h"
#include "sc4/Occupant.h"

struct cS3DVector3;   // gz/StreamOps.h

namespace sc4 {

template <class T>
class cSC4SimGrid;
class cSC4LotConfiguration;

// The three cell types the original uses (SC4Point<int>, SC4Rect<int>, SC4CellRegion<int>). They are
// re-declared here because the class is the first recovered code that needs the int cell variants;
// SC4CellRegion carries the same bounds + cRZCellMap bit map layout as cSC4LandfillZone's region.
template <typename T>
struct SC4Point {
    T x = 0;
    T z = 0;
    friend bool operator==(const SC4Point& a, const SC4Point& b) { return a.x == b.x && a.z == b.z; }
};
template <typename T>
struct SC4Rect {
    T x0 = 0, z0 = 0, x1 = 0, z1 = 0;
};
template <typename T>
class SC4CellRegion {
public:
    SC4Rect<T> bounds;
    std::uint32_t rowCount = 0, bitCount = 0, wordCount = 0;
    std::vector<std::vector<std::uint32_t>> rows;

    bool Test(T x, T z) const {
        const std::uint32_t dx = static_cast<std::uint32_t>(x - bounds.x0);
        const std::uint32_t dz = static_cast<std::uint32_t>(z - bounds.z0);
        if (dx >= rowCount || dz >= bitCount || dx >= rows.size()) return false;
        return ((rows[dx][dz >> 5] >> (dz & 0x1f)) & 1u) != 0;
    }
};

// ---------------------------------------------------------------------------------------------------
// Services the class reaches through the globals / city components. Each is a small view with the
// original vtable slots; the host owns the objects and may return the same singleton to every class.
// ---------------------------------------------------------------------------------------------------
class IZoneDeveloperService {
public:
    virtual ~IZoneDeveloperService() = default;
    virtual void AddRef() {}      // original cIGZUnknown slot 0x08
    virtual void Release() {}     // original cIGZUnknown slot 0x10
    virtual void Init() {}        // the network managers' slot 0x18 (Init), when DoMessage attaches one
    virtual void Shutdown() {}    // original cIGZUnknown slot 0x20 where the owner calls it
};

class IZoneDeveloperCityApi : public IZoneDeveloperService {
public:
    virtual int CellCountX() = 0;                                              // city vslot 0x2e8
    virtual int CellCountZ() = 0;                                              // city vslot 0x2f0
    virtual void PositionToCell(float x, float z, int& cellX, int& cellZ) = 0;  // city vslot 0x2f8
    virtual bool LocationIsInBounds(float x, float z) = 0;                      // city vslot 0x310
};

class IZoneDeveloperTerrainApi : public IZoneDeveloperService {
public:
    virtual bool IsCellWater(int x, int z) = 0;              // terrain vslot 0x110
    virtual float Altitude(float x, float z) = 0;            // terrain vslot 0x60
    virtual float CellAltitude(int x, int z) = 0;            // terrain vslot 0x70
};

class IZoneDeveloperZoneManagerApi : public IZoneDeveloperService {
public:
    virtual int ZoneTypeAt(int x, int z) = 0;                // zone manager vslot 0x60
    virtual bool IsZoneTypeValid(int zoneType) = 0;          // zone manager vslot 0x68
};

// One of the three network sub-managers the zone developer keeps (street type 0, road type 3,
// rail/neighbour type 0xb).
class IZoneDeveloperNetworkApi : public IZoneDeveloperService {
public:
    virtual bool HasFlag(std::uint32_t flag) = 0;                                   // vslot 0x40
    virtual bool GetCellFlags(int x, int z, std::uint32_t flag, std::uint32_t& out) = 0;  // vslot 0x48
    virtual std::uint32_t GetTypeFlags() = 0;                                       // vslot 0x60
    virtual std::uint32_t GetConnectionFlags() = 0;                                 // vslot 0x130
    virtual bool IsConnected(std::uint32_t direction, int networkType) = 0;         // vslot 0x178
    virtual void ClearHighlight() = 0;                                              // vslot 0x28
};

class IZoneDeveloperTerrainViewApi : public IZoneDeveloperService {
public:
    virtual void ClearHighlight() = 0;                       // terrain view vslot 0x48
};

class IZoneDeveloperLotDeveloperApi : public IZoneDeveloperService {
public:
    virtual void RemoveLot(cSC4Lot* lot) = 0;                // lot developer vslot 0x58
};

// The object returned by cSC4BuildingDevelopmentSimulator vslot 0x120; the three calls the zone
// developer makes on it.
class IZoneDeveloperBuildingDevelopmentApi : public IZoneDeveloperService {
public:
    virtual void ForgetLot(cSC4Lot* lot) = 0;                          // vslot 0x108
    virtual bool GetLotUnitId(cSC4Lot* lot, std::uint32_t& unitId) = 0;  // vslot 0x148 + unit vslot 0x18
    virtual void AddLot(cSC4Lot* lot) = 0;                             // vslot 0x158
};

// cISC4NetworkOccupant, as far as this class uses it (QI iid 0xa821ef94).
class IZoneDeveloperNetworkOccupant : public cIGZUnknown {
public:
    virtual bool HasNetworkFlags(std::uint32_t flags) = 0;                 // vslot 0x48
    virtual std::uint32_t GetNetworkFlags() = 0;                           // vslot 0x60
    virtual std::uint32_t GetConnectionFlags() = 0;                        // vslot 0x130
    virtual bool IsConnected(std::uint32_t direction, int networkType) = 0;  // vslot 0x178
};

class IZoneDeveloperOccupantManagerApi : public IZoneDeveloperService {
public:
    // occupant manager vslot 0x140: occupants whose cell rectangle contains (x, z), optionally filtered
    // by occupant type id (0 = any); the returned pointers carry a reference the caller releases.
    virtual void ListOccupants(std::vector<cISC4Occupant*>& out, int x, int z, std::uint32_t typeId,
                               int maxCount) = 0;
    virtual void RemoveOccupant(std::uint32_t occupantId, int flags, int reason) = 0;   // vslot 0xa8
};

// cSC4SafeListIterator<cISC4Lot*>: only the one call CreateLot makes on the iterator the traffic
// simulator hands out (vslot 0xb0). The list itself is not recovered (see the ledger).
class IZoneDeveloperSafeListApi : public IZoneDeveloperService {
public:
    virtual void AddAfter(cSC4Lot* lot) = 0;                 // cSC4SafeListIterator::AddAfter
};

class IZoneDeveloperLotIterator;   // cSC4SafeListIterator<cISC4Lot*>, not recovered

// Everything outside the object. One method per original call; the global/slot is in the comment.
class IZoneDeveloperHost {
public:
    virtual ~IZoneDeveloperHost() = default;

    virtual cIGZMessageServer2* MessageServer() = 0;                     // SL::spMS2
    // SL::spRenderer->vslot 0x50()->+0x14 (the highlight texture scale); 0 when absent.
    virtual int RendererHighlightScale() = 0;
    virtual IZoneDeveloperService* Renderer() = 0;                       // SL::spRenderer
    virtual IZoneDeveloperLotDeveloperApi* LotDeveloper() = 0;           // SL::spLotDeveloper
    virtual IZoneDeveloperBuildingDevelopmentApi* BuildingDevelopment() = 0;  // SL::spBuildingDevelopmentSim->0x120
    virtual IZoneDeveloperCityApi* City() = 0;                           // SL::spCity
    virtual IZoneDeveloperOccupantManagerApi* OccupantManager() = 0;     // SL::spOccupantManager
    virtual IZoneDeveloperTerrainApi* Terrain() = 0;                     // SL::spTerrain
    virtual IZoneDeveloperTerrainViewApi* TerrainView() = 0;             // SL::spTerrainView
    virtual IZoneDeveloperService* TrafficSim() = 0;                     // SL::spTrafficSim
    virtual IZoneDeveloperZoneManagerApi* ZoneManager() = 0;             // SL::spZoneManager
    // SL::spNetworkManager->vslot 0x28(type, 1): index 0 = type 0, 1 = type 3, 2 = type 0xb.
    virtual IZoneDeveloperNetworkApi* NetworkSubnet(int index) = 0;
    // SL::spTrafficSim->vslot 0xb0: the live lot-list iterator, or null.
    virtual IZoneDeveloperSafeListApi* SafeListIterator() = 0;

    // SL::spCCM (cheat code manager) and SL::spSC4->vslot 0xa8.
    virtual bool CheatCodesEnabled() = 0;                                 // CCM != null && app 0xa8
    virtual void RegisterCheatCode(const char* text, std::uint32_t id, cIGZMessageTarget2* target) = 0;
    virtual void UnregisterCheatCode(std::uint32_t id) = 0;

    virtual std::uint32_t RandomBelow(std::uint32_t range) = 0;           // cRZRandom::RandomUint32UniformBelow(sRandom, range)
    virtual void WriteLot(cIGZOStream& stream, cSC4Lot* lot) = 0;         // SC4WriteGZSerializable
    virtual cSC4Lot* ReadLot(cIGZIStream& stream) = 0;                    // SC4ReadGZSerializable(0xe818fced), referenced
};
IZoneDeveloperHost* ZoneDeveloperHost();
void SetZoneDeveloperHost(IZoneDeveloperHost* host);

// ---------------------------------------------------------------------------------------------------
// The interfaces of the four COM views (copied slot for slot from the original vtables).
// ---------------------------------------------------------------------------------------------------
class cISC4ZoneDeveloper : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                              // [3]  0x18
    virtual bool Shutdown() = 0;                                                          // [4]  0x20
    virtual void HighlightParcels(const SC4CellRegion<int>& region, int zoneType,
                                  const SC4Point<int>* origin, SC4CellRegion<int>** out) = 0;  // [5] 0x28
    virtual void DoParcellization(const SC4CellRegion<int>& region, int zoneType, bool arg) = 0;  // [6] 0x30
    virtual void SetOptions(bool swapOrientation, bool extendAsStreets, bool examineBoundaries) = 0;  // [7] 0x38
    virtual bool ExistsNetworkOfType(int x, int z, std::uint32_t flags) = 0;               // [8]  0x40
    virtual bool IsCellWater(int x, int z) = 0;                                            // [9]  0x48
    virtual ~cISC4ZoneDeveloper() = default;                                               // [10]/[11] 0x50/0x58
    virtual bool DoMessage(cIGZMessage2* message) = 0;                                     // [12] 0x60
    virtual bool Write(cIGZOStream& stream) const = 0;                                     // [13] 0x68
    virtual bool Read(cIGZIStream& stream) = 0;                                            // [14] 0x70
    virtual std::uint32_t GetGZCLSID() const = 0;                                          // [15] 0x78
    virtual cSC4Lot* GetLot(int x, int z, bool exactCell) = 0;                             // [16] 0x80
    virtual cSC4Lot* GetLot(const cS3DVector3& location) = 0;                              // [17] 0x88
    virtual int GetLotsInCellRect(std::list<cRZAutoRefCount<cSC4Lot>>& out, int x0, int z0, int x1,
                                  int z1, bool unique) = 0;                                // [18] 0x90
    virtual int GetLotsSurroundingLot(const cSC4Lot* lot, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                      int edgeMask) = 0;                                   // [19] 0x98
    virtual int GetLotsSurroundingLot(int x, int z, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                      int edgeMask) = 0;                                   // [20] 0xa0
    virtual int GetLotsSurroundingRect(const SC4Rect<int>& rect,
                                       std::list<cRZAutoRefCount<cSC4Lot>>& out, int edgeMask) = 0;  // [21] 0xa8
    virtual void GetVerticesAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const = 0;  // [22] 0xb0
    virtual bool GetNeighborCellsAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const = 0;  // [23] 0xb8
    virtual cSC4Lot* GetRandomLot() = 0;                                                   // [24] 0xc0
    virtual cSC4Lot* GetOccupantLot(cISC4Occupant* occupant) = 0;                          // [25] 0xc8
    virtual IZoneDeveloperLotIterator* GetSafeLotIterator() = 0;                           // [26] 0xd0
    virtual int GetLotCount() = 0;                                                          // [27] 0xd8
    virtual float GetLotSavvyTerrainVertexAltitude(int x, int z, const cSC4Lot* lot, bool* lotFound) const = 0;  // [28] 0xe0
    virtual float GetLotSavvyTerrainAltitude(float x, float z, bool* lotFound) const = 0;  // [29] 0xe8
    virtual float GetLotSavvyCellAltitude(int x, int z, bool* lotFound) const = 0;         // [30] 0xf0
    virtual int CanCreateLot(int x, int z, int sizeX, int sizeZ, bool param5, bool param6,
                             cSC4LotConfiguration* configuration) = 0;                      // [31] 0xf8
    virtual int CanCreatePloppedLot(SC4Rect<int> rect, int rotation,
                                    cSC4LotConfiguration* configuration) = 0;               // [32] 0x100
    virtual bool CreateLotIfPossible(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out,
                                     bool param7, bool param8,
                                     cSC4LotConfiguration* configuration) = 0;              // [33] 0x108
    virtual bool CreateLot(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out) = 0;  // [34] 0x110
    virtual void DeleteLot(cSC4Lot* lot) = 0;                                              // [35] 0x118
    virtual void DeleteLots(int x0, int z0, int x1, int z1) = 0;                            // [36] 0x120
    virtual void GetLotVertexAltitudeAdjustmentRange(int x, int z, float& minAltitude,
                                                     float& maxAltitude) = 0;               // [37] 0x128
    virtual cSC4SimGrid<std::uint16_t>* GetMaximumCapacityGrid() const = 0;                 // [38] 0x130
};

class cISC4LotManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                              // [3]  0x18
    virtual bool Shutdown() = 0;                                                          // [4]  0x20
    virtual cSC4Lot* GetLot(int x, int z, bool exactCell) = 0;                             // [5]  0x28
    virtual cSC4Lot* GetLot(const cS3DVector3& location) = 0;                              // [6]  0x30
    virtual int GetLotsInCellRect(std::list<cRZAutoRefCount<cSC4Lot>>& out, int x0, int z0, int x1,
                                  int z1, bool unique) = 0;                                // [7]  0x38
    virtual int GetLotsSurroundingLot(const cSC4Lot* lot, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                      int edgeMask) = 0;                                   // [8]  0x40
    virtual int GetLotsSurroundingLot(int x, int z, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                                      int edgeMask) = 0;                                   // [9]  0x48
    virtual int GetLotsSurroundingRect(const SC4Rect<int>& rect,
                                       std::list<cRZAutoRefCount<cSC4Lot>>& out, int edgeMask) = 0;  // [10] 0x50
    virtual void GetVerticesAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const = 0;  // [11] 0x58
    virtual bool GetNeighborCellsAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const = 0;  // [12] 0x60
    virtual cSC4Lot* GetRandomLot() = 0;                                                   // [13] 0x68
    virtual cSC4Lot* GetOccupantLot(cISC4Occupant* occupant) = 0;                          // [14] 0x70
    virtual IZoneDeveloperLotIterator* GetSafeLotIterator() = 0;                           // [15] 0x78
    virtual int GetLotCount() = 0;                                                          // [16] 0x80
    virtual float GetLotSavvyTerrainVertexAltitude(int x, int z, const cSC4Lot* lot, bool* lotFound) const = 0;  // [17] 0x88
    virtual float GetLotSavvyTerrainAltitude(float x, float z, bool* lotFound) const = 0;  // [18] 0x90
    virtual float GetLotSavvyCellAltitude(int x, int z, bool* lotFound) const = 0;         // [19] 0x98
    virtual int CanCreateLot(int x, int z, int sizeX, int sizeZ, bool param5, bool param6,
                             cSC4LotConfiguration* configuration) = 0;                      // [20] 0xa0
    virtual int CanCreatePloppedLot(SC4Rect<int> rect, int rotation,
                                    cSC4LotConfiguration* configuration) = 0;               // [21] 0xa8
    virtual bool CreateLotIfPossible(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out,
                                     bool param7, bool param8,
                                     cSC4LotConfiguration* configuration) = 0;              // [22] 0xb0
    virtual bool CreateLot(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out) = 0;  // [23] 0xb8
    virtual void DeleteLot(cSC4Lot* lot) = 0;                                              // [24] 0xc0
    virtual void DeleteLots(int x0, int z0, int x1, int z1) = 0;                            // [25] 0xc8
    virtual void GetLotVertexAltitudeAdjustmentRange(int x, int z, float& minAltitude,
                                                     float& maxAltitude) = 0;               // [26] 0xd0
    virtual cSC4SimGrid<std::uint16_t>* GetMaximumCapacityGrid() const = 0;                 // [27] 0xd8
};

// cISC4LotDeveloper::BoundaryType: four 2-bit side flags packed as ints, keyed into the texture map.
struct BoundaryType {
    int sides[4] = {0, 0, 0, 0};
};

// cSC4ZoneDeveloper::BoundaryCondition.
enum BoundaryCondition : int {
    kBoundaryNone = 0,        // open cell: cost +1
    kBoundarySoft = 1,        // a street/rail border: cost +30..400
    kBoundaryBlocked = 2,     // water, a lot or a building occupant: cost +5
};

class cSC4ZoneDeveloper final : public cISC4ZoneDeveloper,
                                public cISC4LotManager,
                                public cIGZMessageTarget2,
                                public cIGZSerializable {
public:
    cSC4ZoneDeveloper();
    ~cSC4ZoneDeveloper() override;

    // cIGZUnknown (one shared count at +0x24)
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    // cISC4ZoneDeveloper / cISC4LotManager
    bool Init() override;
    bool Shutdown() override;
    void HighlightParcels(const SC4CellRegion<int>& region, int zoneType, const SC4Point<int>* origin,
                          SC4CellRegion<int>** out) override;
    void DoParcellization(const SC4CellRegion<int>& region, int zoneType, bool arg) override;
    void SetOptions(bool swapOrientation, bool extendAsStreets, bool examineBoundaries) override;
    bool ExistsNetworkOfType(int x, int z, std::uint32_t flags) override;
    bool IsCellWater(int x, int z) override;
    bool DoMessage(cIGZMessage2* message) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kZoneDeveloperClassId; }
    cSC4Lot* GetLot(int x, int z, bool exactCell) override;
    cSC4Lot* GetLot(const cS3DVector3& location) override;
    int GetLotsInCellRect(std::list<cRZAutoRefCount<cSC4Lot>>& out, int x0, int z0, int x1, int z1,
                          bool unique) override;
    int GetLotsSurroundingLot(const cSC4Lot* lot, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                              int edgeMask) override;
    int GetLotsSurroundingLot(int x, int z, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                              int edgeMask) override;
    int GetLotsSurroundingRect(const SC4Rect<int>& rect, std::list<cRZAutoRefCount<cSC4Lot>>& out,
                               int edgeMask) override;
    void GetVerticesAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const override;
    bool GetNeighborCellsAtEdgeOfLot(const SC4Rect<int>& rect, int edge, SC4Rect<int>& out) const override;
    cSC4Lot* GetRandomLot() override;
    cSC4Lot* GetOccupantLot(cISC4Occupant* occupant) override;
    IZoneDeveloperLotIterator* GetSafeLotIterator() override;
    int GetLotCount() override { return lotCount_; }
    float GetLotSavvyTerrainVertexAltitude(int x, int z, const cSC4Lot* lot, bool* lotFound) const override;
    float GetLotSavvyTerrainAltitude(float x, float z, bool* lotFound) const override;
    float GetLotSavvyCellAltitude(int x, int z, bool* lotFound) const override;
    int CanCreateLot(int x, int z, int sizeX, int sizeZ, bool param5, bool param6,
                     cSC4LotConfiguration* configuration) override;
    int CanCreatePloppedLot(SC4Rect<int> rect, int rotation,
                            cSC4LotConfiguration* configuration) override;
    bool CreateLotIfPossible(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out,
                             bool param7, bool param8, cSC4LotConfiguration* configuration) override;
    bool CreateLot(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot*& out) override;
    void DeleteLot(cSC4Lot* lot) override;
    void DeleteLots(int x0, int z0, int x1, int z1) override;
    void GetLotVertexAltitudeAdjustmentRange(int x, int z, float& minAltitude,
                                             float& maxAltitude) override;
    cSC4SimGrid<std::uint16_t>* GetMaximumCapacityGrid() const override { return capacityGrid_; }

    // Non-virtual helpers / accessors of the original.
    void InitBoundaryRules();                                                // @ 0x100128b94
    IZoneDeveloperNetworkOccupant* GetNetwork(int x, int z, std::uint32_t flags);  // @ 0x100130604
    bool ExistsConnectionOfType(int x, int z, std::uint32_t flags, int direction,
                                int networkType);                            // @ 0x100130274
    int CanCreateLotOnCell(int x, int z, bool skipZoneCheck, bool allowWater);  // @ 0x1001307bc
    bool DoesCellHaveLot(int x, int z);                                      // @ 0x100130354
    void ExamineCell(int x, int z, BoundaryCondition& boundary, int& cost);  // @ 0x1001303ea
    void OnZone(const SC4CellRegion<int>* region, SC4CellRegion<int>** out);  // @ 0x10012b160 (SKIPPED)
    void OnDezone(const SC4CellRegion<int>* region, bool arg);                // @ 0x10012b976 (SKIPPED)
    void UpdateHighlightTextures(const SC4CellRegion<int>* region);           // @ 0x100130994 (SKIPPED)
    void ClearLotMap();                                                      // @ 0x1001294de
    void ClearHighlight();                                                   // @ 0x10012b0aa
    void DeferOccupantOperations(bool defer);                                // @ 0x10012bdac
    void GetBoundaryLotTexture(const BoundaryType& boundary, std::uint32_t& texture,
                               std::uint8_t& shape, bool& flag) const;       // @ 0x100130e66
    bool IsInitialized() const { return initialized_; }
    bool CheatCodesWereRegistered() const;
    int RendererHighlightScale() const { return highlightScale_; }
    void* MessageServerForTest() const { return messageServer_; }
    bool ExtendAsStreets() const { return extendAsStreets_; }
    bool SwapOrientation() const { return swapOrientation_; }
    bool ExamineBoundaries() const { return examineBoundaries_; }

    // Original field offsets, asserted by the test.
    static constexpr std::uint32_t kOffsetLotMap = 0x98;
    static constexpr std::uint32_t kOffsetLots = 0xb0;
    static constexpr std::uint32_t kOffsetCapacityGrid = 0xf8;
    static constexpr std::uint32_t kOffsetBoundaryRules = 0x1a8;
    static constexpr std::uint32_t kClassSize = 0x1c0;
    static constexpr std::uint32_t kZoneDeveloperClassId = 0x498f9b01;

private:
    struct SC4PointSintCompare {
        bool operator()(const SC4Point<int>& a, const SC4Point<int>& b) const {
            return a.x != b.x ? a.x < b.x : a.z < b.z;
        }
    };
    struct BoundaryLotTextureRule {
        std::uint32_t texture = 0;   // +0
        std::uint16_t shape = 0;     // +4 (the getter exposes only its low byte)
        bool flag = false;           // +6
    };
    using PendingLot = std::pair<SC4Point<int>, int>;       // origin + facing
    using PendingDelete = std::pair<SC4Point<int>, int>;    // origin + zone type
    using BoundaryRuleMap = std::map<int, BoundaryLotTextureRule>;
    using DemandMap = std::map<int, std::map<int, std::map<int, std::set<std::pair<std::uint8_t, std::uint8_t>>>>>;

    // +0x20
    bool initialized_ = false;
    bool loaded_ = false;
    std::uint32_t refCount_ = 0;                 // +0x24
    cIGZMessageServer2* messageServer_ = nullptr;   // +0x28
    IZoneDeveloperService* renderer_ = nullptr;     // +0x30
    IZoneDeveloperBuildingDevelopmentApi* buildingDevelopment_ = nullptr;  // +0x38
    IZoneDeveloperCityApi* city_ = nullptr;          // +0x40
    int cellsX_ = 0;                                 // +0x48
    int cellsZ_ = 0;                                 // +0x4c
    IZoneDeveloperOccupantManagerApi* occupantManager_ = nullptr;   // +0x50
    IZoneDeveloperTerrainApi* terrain_ = nullptr;                   // +0x58
    IZoneDeveloperTerrainViewApi* terrainView_ = nullptr;           // +0x60
    IZoneDeveloperService* trafficSim_ = nullptr;                   // +0x68
    IZoneDeveloperZoneManagerApi* zoneManager_ = nullptr;           // +0x70
    IZoneDeveloperNetworkApi* railNetwork_ = nullptr;               // +0x78
    IZoneDeveloperNetworkApi* roadNetwork_ = nullptr;               // +0x80
    IZoneDeveloperNetworkApi* streetNetwork_ = nullptr;             // +0x88
    IZoneDeveloperLotDeveloperApi* lotDeveloper_ = nullptr;         // +0x90
    std::vector<std::vector<cSC4Lot*>> lotMap_;                     // +0x98
    std::list<cSC4Lot*> lots_;                                      // +0xb0
    std::list<void*> lotIterators_;                                 // +0xc8
    int lotCount_ = 0;                                              // +0xe0
    [[maybe_unused]] std::uint64_t reservedE8_ = 0;                                  // +0xe8
    [[maybe_unused]] std::uint64_t reservedF0_ = 0;                                  // +0xf0
    cSC4SimGrid<std::uint16_t>* capacityGrid_ = nullptr;            // +0xf8
    DemandMap demandMap_;                                           // +0x100
    [[maybe_unused]] int parcellizationSizes_[5] = {2, 2, 1, 3, 6};                  // +0x118
    [[maybe_unused]] std::uint8_t scratch12c_ = 0;                                   // +0x12c
    std::uint8_t lotChangesEnabled_ = 1;                            // +0x12d
    std::uint8_t extendAsStreets_ = 1;                              // +0x12e
    std::uint8_t swapOrientation_ = 0;                              // +0x12f
    std::uint8_t examineBoundaries_ = 0;                            // +0x130
    int currentZoneType_ = 0;                                       // +0x134
    SC4CellRegion<int>* currentRegion_ = nullptr;                   // +0x138
    SC4Point<int>* highlightOrigin_ = nullptr;                      // +0x140
    [[maybe_unused]] int unknown144_ = 0;                                            // +0x144
    [[maybe_unused]] int boundaryCost_[4] = {0, 0, 0, 0};                            // +0x148
    [[maybe_unused]] int boundary_[4] = {0, 0, 0, 0};                                // +0x158
    [[maybe_unused]] int lotOrientation_ = 0;                                        // +0x168
    [[maybe_unused]] int lotOrientationAlt_ = 0;                                     // +0x16c
    int highlightScale_ = 0;                                        // +0x170
    std::map<SC4Point<int>, PendingLot, SC4PointSintCompare> pendingLots_;      // +0x178
    std::map<SC4Point<int>, PendingDelete, SC4PointSintCompare> pendingDeletes_;  // +0x190
    BoundaryRuleMap boundaryRules_;                                 // +0x1a8
};

}  // namespace sc4
