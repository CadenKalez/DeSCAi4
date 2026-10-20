// cSC4Demolition — the demolition service (33 functions with thunks, 0x1000eaf70..0x1000ee64a, class id 0x26dde2ea,
// message target 0x452294aa, serializable 0xe4fda3d4). Layout: +0x1c initialized, +0x20/+0x24 city cells x/z,
// +0x28 default occupant filter, +0x30 city, +0x38 building development simulator, +0x40 occupant manager,
// +0x48 terrain, +0x50 current region.
//
// Recovered here: start up / shut down, the tuning exemplars, the one-version save record, the per-type occupant
// demolition switch (DemolishOccupant), the lot batch entry (DemolishLots: bounding union + cell mask), the network /
// pole follow-up pass (UpdateNetworks), the demolishable occupant queries and PostMessage. The region sweep
// (DemolishRegion), terrain levelling (ModifyTerrainHeight) and the visual effects stay unrecovered.
#pragma once
#include <cstdint>
#include <list>
#include <set>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4Demolition = 0x26dde2ea;
constexpr std::uint32_t kMessageTuningReloaded = 0xc53d10aa, kMessageCityInit = 0x26d31ec1, kMessageDemolishRequest = 0xa9ded179;
constexpr std::uint32_t kMessageOccupantDemolished = 0xaa7a014d;

// Demolition options (the `flags` argument)
constexpr std::uint32_t kDemolishSkipProtected = 0x100;       // occupants with removal flag 0x40 are left alone
constexpr std::uint32_t kDemolishLotsOnly = 0x2000;           // set by DemolishLots: only the lots' own cells count

// Occupant type ids switched on by DemolishOccupant
constexpr std::uint32_t kDemolishTypeBuilding = 0x278128a0, kDemolishTypeProp = 0xa823821e, kDemolishTypeNetwork = 0xc772bf98;
constexpr std::uint32_t kDemolishTypeSpecial = 0x096e763c, kDemolishTypeFoundation = 0x68fd0c69, kDemolishTypeFlora = 0x74758926;
constexpr std::uint32_t kDemolishTypePole = 0x2890d4de;

constexpr std::uint32_t kPropertyModelSmallMedium = 0x6a8a09d2, kPropertyModelMediumLarge = 0x6a8a09d3;
constexpr std::uint32_t kPropertyGroupSmallMedium = 0x6a8a09d4, kPropertyGroupMediumLarge = 0x6a8a09d5;
constexpr std::uint32_t kPropertyDemolishLotTileCost = 0x2ab845e7;

struct DemolitionCellRect { int x0 = 0, z0 = 0, x1 = 0, z1 = 0; };   // inclusive cells

// The occupant as demolition sees it.
class IDemolishable {
public:
    virtual ~IDemolishable() = default;
    virtual std::uint32_t Type() = 0;                       // vslot 0x38
    virtual bool HasRemovalFlag(std::uint32_t flag) = 0;    // vslot 0x100 (0x40 protected, 1 "has network", ...)
    virtual std::uint32_t Flags() = 0;                      // vslot 0x108 (bit 0: network piece needing an update pass)
    virtual void Remove() = 0;                              // vslot 0x20
};

// A lot as demolition sees it.
class IDemolishableLot {
public:
    virtual ~IDemolishableLot() = default;
    virtual DemolitionCellRect Bounds() = 0;                // vslot 0x70
    virtual bool IsPlopped() = 0;                           // vslot 0x138
    virtual std::vector<IDemolishable*> Occupants() = 0;    // vslot 0x188
};

// Everything the service reaches through the service locator.
class IDemolitionHost {
public:
    virtual ~IDemolitionHost() = default;
    virtual void AddNotification(std::uint32_t message) = 0;
    virtual void RemoveNotification(std::uint32_t message) = 0;
    virtual void PostOccupantMessage(std::uint32_t message, IDemolishable* occupant, bool sendNow) = 0;
    virtual cISCPropertyHolder* TuningExemplar(std::uint32_t group, std::uint32_t instance) = 0;   // type 0x6534284a
    virtual void CityCells(int& x, int& z) = 0;                        // city vslot 0x2e8 / 0x2f0
    virtual bool OccupantAllowedByFilter(IDemolishable* occupant) = 0; // default occupant filter slot 0x18
    virtual void ToCellBounds(const DemolitionCellRect& rect, DemolitionCellRect& out) = 0;   // city vslot 0x300 both corners
    virtual std::vector<IDemolishable*> OccupantsInRect(const DemolitionCellRect& cells, bool useFilter) = 0;   // manager 0x148
    virtual bool RemoveOccupant(IDemolishable* occupant, bool refund, bool deferred) = 0;    // manager vslot 0xa8
    virtual void ReleaseOccupantList(std::vector<IDemolishable*>& list) = 0;                 // manager vslot 0x180
    // per-type services
    virtual bool PropManagerAvailable() = 0;
    virtual bool PropManagerRemove(IDemolishable* prop) = 0;           // prop manager vslot 0xe8
    virtual bool NetworkCost(IDemolishable* network, std::int64_t& cost) = 0;      // QI 0xa821ef94, vslot 0x1f8
    virtual bool BuildingCost(IDemolishable* building, std::int64_t& cost) = 0;    // city vslot 0x1a8 object, vslot 0x48
    virtual bool FloraCost(IDemolishable* flora, std::int64_t& cost) = 0;          // city vslot 0x1d8 object, vslot 0xb0
    virtual bool PoleCost(IDemolishable* pole, std::int64_t& cost) = 0;            // QI 0xa8c86142, vslot 0x50
    virtual std::vector<IDemolishable*> PoleConnections(IDemolishable* pole) = 0;  // slots 0x20 / 0x38
    virtual void PoleDetach(IDemolishable* pole) = 0;                              // slot 0x30 with 1
    virtual bool IsPlainOccupant(IDemolishable* occupant) = 0;                     // connection's own slot 0x20 == 0
    virtual void NotifyNeighborsNetworkChange(IDemolishable* network) = 0;         // neighbors simulator vslot 0x40
    virtual void UpdateNetworkManager(const std::vector<IDemolishable*>& networks) = 0;   // network manager vslot 0x30
    virtual bool Sweep(bool commit, const DemolitionCellRect& region, const std::vector<std::vector<bool>>* mask,
                       int privilege, std::uint32_t flags, bool arg, std::int64_t* cost) = 0;   // DemolishRegion
};

class cSC4Demolition {
public:
    explicit cSC4Demolition(IDemolitionHost* host) : host_(host) {}

    bool Init();
    bool Shutdown();
    bool Write(cIGZOStream& stream) const;
    bool Read(cIGZIStream& stream);
    std::uint32_t GetGZCLSID() const { return kGZCLSID_cSC4Demolition; }
    void SetDefaultOccupantFilter(void*) {}
    // returns true when `message` was one of ours
    bool DoMessage(std::uint32_t type);

    void ReadProperties();
    float SmallMediumModelThreshold() const { return smallMediumModel_; }
    float MediumLargeModelThreshold() const { return mediumLargeModel_; }
    float SmallMediumGroupThreshold() const { return smallMediumGroup_; }
    float MediumLargeGroupThreshold() const { return mediumLargeGroup_; }
    std::int32_t CostToDemolishDestroyedLotTile() const { return lotTileCost_; }
    int CityCellsX() const { return cellsX_; }
    int CityCellsZ() const { return cellsZ_; }

    bool DemolishOccupant(bool commit, IDemolishable* occupant, std::uint32_t flags, std::int64_t* cost, bool skipNetworks,
                          std::vector<IDemolishable*>* released);
    bool DemolishLots(bool commit, const std::list<IDemolishableLot*>& lots, int privilege, std::uint32_t flags, bool arg,
                      std::int64_t* cost);
    void UpdateNetworks(const std::set<IDemolishable*>& occupants);
    std::vector<IDemolishable*> GetDemolishableLotOccupants(IDemolishableLot* lot);
    std::vector<IDemolishable*> GetDemolishableRectOccupants(const DemolitionCellRect& rect);
    void PostMessage(std::uint32_t message, IDemolishable* occupant, bool sendNow);

private:
    IDemolitionHost* host_;
    bool initialized_ = false;
    int cellsX_ = 0, cellsZ_ = 0;
    float smallMediumModel_ = 0, mediumLargeModel_ = 0, smallMediumGroup_ = 0, mediumLargeGroup_ = 0;
    std::int32_t lotTileCost_ = 0;
};

}  // namespace sc4
