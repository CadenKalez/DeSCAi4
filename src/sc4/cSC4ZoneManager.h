// cSC4ZoneManager — the zone grid and zoning statistics. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtable 0x100a7b740, 39 functions; class id 0x298f9b2d, iid 0x67652433).
//
// The zone grid is a tract-size-1 int8 grid (class 0x49b9e603) holding the zone type per city cell.
// Zone types 0..15: 0 none, 1..3 residential (low/medium/high), 4..6 commercial, 7..9 industrial,
// 0xb airport, 0xc seaport, 0xd spaceport, 0xe landfill, 0xf plopped; IsRCIZone is the mask 0x3fe
// (types 1..9). Per type it keeps the cell count, the undeveloped-cell count, minimum/maximum zone
// side, drag colour, display texture instance and the per-zone-type development cost tunables
// (exemplar {type 0x6534284a, group 0xe7e2c2db, instance 0xe9482490}, properties 0xe94825b2..b9).
//
// Not recovered yet (pending in the ledger): PlaceZone (the core zoning transaction) and the
// message handler that calls it for zoned-building removal (0x99ef1142 / 0xc7729698).
#pragma once
#include <cstdint>
#include <map>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZRandom.h"
#include "sc4/CommonIIDs.h"
#include "sc4/SCProperty.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

constexpr std::uint32_t kZoneManagerClassId = 0x298f9b2d;
constexpr std::uint32_t kGZIID_cISC4ZoneManager = 0x67652433;
constexpr int kZoneTypeCount = 16;

enum ZoneType : int {
    kZoneNone = 0, kZoneResidentialLow = 1, kZoneResidentialMedium = 2, kZoneResidentialHigh = 3,
    kZoneCommercialLow = 4, kZoneCommercialMedium = 5, kZoneCommercialHigh = 6, kZoneIndustrialAgriculture = 7,
    kZoneIndustrialMedium = 8, kZoneIndustrialHigh = 9, kZoneMilitary = 10, kZoneAirport = 11,
    kZoneSeaport = 12, kZoneSpaceport = 13, kZoneLandfill = 14, kZonePlopped = 15,
};

class IZoneManagerHost {
public:
    virtual ~IZoneManagerHost() = default;
    virtual int CityCellsX() = 0;
    virtual int CityCellsZ() = 0;
    virtual cISCPropertyHolder* GetTunables(const cGZPersistResourceKey& key) = 0;   // null when absent
    virtual void RegisterNotifications() {}                                           // occupant removed (0x99ef1142)
    virtual void UnregisterNotifications() {}
    // The five services the manager references in its stream, in stream order and with the
    // interface ids it reads them as: city 0x66ba4db5, occupant manager 0xc6bb8334, terrain
    // 0xc70bd37e, pollution simulator 0x877181ec, budget simulator 0x6772bd23.
    virtual cIGZUnknown* GetServiceForStream(int /*index*/) { return nullptr; }
    virtual void OnServiceLoaded(int /*index*/, cIGZUnknown* /*object*/) {}
    // the building development simulator re-lights lots when the zone overlay alpha changes
    virtual void OnZoneDisplayAlphaChanged() {}
};

// The zone manager's own interface (class id 0x298f9b2d, iid 0x67652433). The recovered port only needs
// the COM identity; the concrete methods are called directly, so the interface carries no extra slots.
class cISC4ZoneManager : public cIGZUnknown {
public:
    virtual ~cISC4ZoneManager() = default;
};

class cSC4ZoneManager : public cISC4ZoneManager, public cIGZMessageTarget2, public cIGZSerializable {
public:
    struct ZoneDevelopmentResult {
        std::int8_t zoneType = 0;
        std::int8_t result = 0;
        friend bool operator<(const ZoneDevelopmentResult& a, const ZoneDevelopmentResult& b) {
            return a.zoneType != b.zoneType ? a.zoneType < b.zoneType : a.result < b.result;
        }
    };

    explicit cSC4ZoneManager(IZoneManagerHost* host);
    ~cSC4ZoneManager() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoMessage(cIGZMessage2* message) override;   // unrecovered; the port does not route messages here

    bool Init();
    bool Shutdown();

    cSC4SimGrid<std::int8_t>* GetZoneGrid() const { return zoneGrid_.get(); }
    std::int32_t GetZoneCount(int zoneType) const { return zoneCount_[zoneType]; }
    std::int32_t GetUndevelopedTileCount() const { return undevelopedTotal_; }
    std::int32_t GetUndevelopedTileCount(int zoneType) const { return undevelopedByType_[zoneType]; }
    std::uint32_t GetDevelopmentFailureCount(int zoneType, int result) const;
    std::int32_t GetAbandonedTileCount() const { return abandonedTotal_; }
    int GetZoneType(int x, int z) const { return zoneGrid_->GetTractValue(x, z); }
    static constexpr std::uint32_t kStreamServiceIid[5] = {0x66ba4db5, 0xc6bb8334, 0xc70bd37e, 0x877181ec, 0x6772bd23};
    static bool IsRCIZone(int zoneType) { return static_cast<unsigned>(zoneType) < 0x11 && ((0x3feu >> zoneType) & 1u) != 0; }
    bool GetTextureForZone(int zoneType, cGZPersistResourceKey& key) const;
    std::uint32_t GetZoneDragColor(int zoneType) const { return dragColor_[zoneType]; }
    std::uint8_t GetZoneDisplayAlpha() const { return displayAlpha_; }
    std::uint8_t GetDefaultZoneDisplayAlpha() const { return defaultDisplayAlpha_; }
    void SetZoneDisplayAlpha(std::uint8_t alpha);
    std::int32_t GetMinZoneSize(int zoneType) const { return minSize_[zoneType]; }
    std::int32_t GetMaxZoneSize(int zoneType) const { return maxSize_[zoneType]; }

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kZoneManagerClassId; }

    // Exposed for tests / callers that adjust statistics when zones change.
    void AdjustZoneCounts(int fromType, int toType, std::int32_t cells) {
        zoneCount_[fromType] -= cells;
        zoneCount_[toType] += cells;
    }
    void RecordDevelopmentFailure(int zoneType, int result) { ++failures_[{static_cast<std::int8_t>(zoneType), static_cast<std::int8_t>(result)}]; }

private:
    void ReadSimulatorTunableValues();

    IZoneManagerHost* host_;
    std::uint32_t refCount_ = 0;               // COM refcount (shared by the three sub-objects)
    bool initialized_ = false;                 // +0x18
    bool countsLoaded_ = false;                // +0x20 (set by Read: Init keeps the loaded counters)
    cRZRandom random_{0xffffffffu};            // +0x58
    cRZAutoRefCount<cSC4SimGrid<std::int8_t>> zoneGrid_;   // +0x70
    std::int32_t zoneCount_[kZoneTypeCount] = {};          // +0x78
    std::int32_t undevelopedByType_[kZoneTypeCount] = {};  // +0xb8
    std::int32_t secondCounts_[kZoneTypeCount] = {};       // +0xf8 (initialised like the undeveloped counts)
    std::int32_t undevelopedTotal_ = 0;                    // +0x138
    std::int32_t counter13c_ = 0, abandonedTotal_ = 0, counter144_ = 0;   // +0x13c, +0x140, +0x144
    std::int32_t minSize_[kZoneTypeCount] = {};            // +0x148
    std::int32_t maxSize_[kZoneTypeCount] = {};            // +0x188
    float developmentCost_[kZoneTypeCount] = {};           // +0x1c8
    std::int64_t zoneParameterA_[kZoneTypeCount] = {};     // +0x208
    std::int64_t zoneParameterB_[kZoneTypeCount] = {};     // +0x288
    std::uint32_t textureInstance_[kZoneTypeCount] = {};   // +0x308
    std::uint32_t dragColor_[kZoneTypeCount] = {};         // +0x348
    std::uint8_t displayAlpha_ = 0xff;                     // +0x388
    std::uint8_t defaultDisplayAlpha_ = 0xff;              // +0x389
    std::map<ZoneDevelopmentResult, std::uint32_t> failures_;      // +0x390
    std::map<ZoneDevelopmentResult, std::uint32_t> successes_;     // +0x3a8
};

}  // namespace sc4
