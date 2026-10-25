// cSC4FlammabilitySimulator — how easily each cell of the city burns, and where random fires
// start. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a6f440, 35 functions;
// class id 0xe990c045, interfaces 0x452294aa (message target), 0xe4fda3d4 (serializable),
// 0x48f41617).
//
// Two tract grids (uint8): `rawFlammability_` (0x49d5b964) holds the flammability of whatever stands
// on each cell, `flammability_` (0x49d5b953, returned by GetFlammabilityGrid) the effective value
// = raw * ordinance/weather factor (x1.1 on cells without water coverage), clamped to 255.
//
// Daily update: the city is swept in a scrambled order by a RandomIterator, `cells / 40`
// cells per daily tick, so a full pass takes 40 days; the end of a pass publishes the average
// flammability of zoned cells (history record 0x2a11cb48, message 0x08f415fe), rebuilds the hotspot
// list (cells over the threshold, merged when within `mergeDistance` Manhattan cells, weight = number
// merged, only weights >= `minHotspotWeight` kept) and may start a random fire: with probability
// `randomFireChancePercent`% on a random lot, otherwise at a hotspot chosen with probability
// curve(weight). Building/prop changes update the cells they cover immediately.
//
// Tunables come from the simulator exemplar {type 0x6534284a, group 0xe7e2c2db, instance 0xa8f41ee3}:
//   0xc9b40128 ordinance/weather multiplier (default 1.2)    0x29b40134 civic-lot factor (1.1)
//   0x29b4013b no-water factor (1.1)   0x29b4013c uint32[3] {threshold, merge distance, min weight}
//   0x29b4013e random fire chance % (0.01)   0x29b4013d firestart curve (hotspot weight -> chance %)
//
// The city services the original reaches through SL::sp* pointers are the IFlammabilityHost seam.
#pragma once
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "rz/cRZRandom.h"
#include "sc4/RandomIterator.h"
#include "sc4/SCProperty.h"
#include "sc4/cSC4ResponseCurve.h"
#include "rz/cRZAutoRefCount.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

constexpr std::uint32_t kFlammabilitySimulatorClassId = 0xe990c045;
constexpr std::uint32_t kGZIID_cISC4FlammabilitySimulator = 0x48f41617;   // the primary interface (also answered for iid 1)
constexpr std::uint32_t kFlammabilityGridInstance = 0x49d5b953;
constexpr std::uint32_t kRawFlammabilityGridInstance = 0x49d5b964;

// Occupant types the simulator cares about.
constexpr std::uint32_t kOccupantTypeBuilding = 0x278128a0;
constexpr std::uint32_t kOccupantTypeCivic = 0xa823821e;     // lot-zoned (see GetOccupantRawFlammability)
constexpr std::uint32_t kOccupantTypeProp = 0x74758926;
// Messages handled (registered with the message server in Init).
constexpr std::uint32_t kFlammabilityMsgOccupantAdded = 0x99ef1142;
constexpr std::uint32_t kFlammabilityMsgOccupantRemoved = 0x99ef1143;
constexpr std::uint32_t kFlammabilityMsgOccupantChanged = 0x99ef1145;
constexpr std::uint32_t kFlammabilityMsgOrdinanceChanged = 0x6716a4e5;
constexpr std::uint32_t kFlammabilityMsgWeatherChanged = 0xc2c09554;
constexpr std::uint32_t kFlammabilityMsgOccupantMoved = 0xe99b3d4d;
constexpr std::uint32_t kMessageDailyTick = 0x66956814;
constexpr std::uint32_t kMessageFlammabilityUpdated = 0x08f415fe;   // posted at the end of a pass
constexpr std::uint32_t kPropertyOccupantFlammability = 0x29244db5;
constexpr std::uint32_t kPropertyNoFlammabilityFlag = 0x029244db;   // exemplar flag: occupant ignored
constexpr std::uint32_t kHistoryFlammability = 0x2a11cb48;
constexpr std::uint32_t kOrdinanceFlameResistance = 0x28f42aa0;

struct SC4Rect { int x0 = 0, z0 = 0, x1 = 0, z1 = 0; };   // inclusive cell rectangle

// What the simulator reads from an occupant.
class ISimOccupant {
public:
    virtual ~ISimOccupant() = default;
    virtual std::uint32_t GetOccupantType() = 0;
    virtual cISCPropertyHolder* GetPropertyHolder() = 0;
    virtual SC4Rect GetBoundingRect() = 0;                       // in cells
    virtual bool HasProperty(std::uint32_t id) = 0;
};

class IFlammabilityHost {
public:
    virtual ~IFlammabilityHost() = default;
    // city
    virtual int CitySizeXInCells() = 0;
    virtual int CitySizeZInCells() = 0;
    virtual bool RandomDisastersBlocked() = 0;                   // city service slot 0x290 -> +0x40
    virtual bool PickRandomLotCell(int& x, int& z) = 0;          // lot manager chain; false when none
    virtual void StartFire(int x, int z, bool onRandomLot, int intensity) = 0;   // disaster manager (fire)
    // other simulators
    virtual float GetOrdinanceFlameResistance() = 0;             // ordinance sim, property 0x28f42aa0
    virtual int GetCurrentWeather() = 0;                         // 2 == rain (multiplier applies)
    virtual bool HasWaterAt(int x, int z) = 0;                   // plumbing simulator
    virtual int GetZoneTypeAt(int x, int z) = 0;                 // zone manager; 0 == unzoned
    virtual int GetLotZoneTypeOfOccupant(ISimOccupant* occupant) = 0;   // lot manager; -1 when none
    // occupants intersecting a cell rectangle that match one of the types
    virtual std::vector<ISimOccupant*> GatherOccupants(const SC4Rect& rect, const std::uint32_t* types, int typeCount) = 0;
    // services
    virtual cISCPropertyHolder* GetSimulatorTunables(const cGZPersistResourceKey& key) = 0;   // null when absent
    virtual void PostFlammabilityMessage() = 0;
    virtual void AddHistoryValue(std::uint32_t historyId, std::uint8_t value) = 0;
    virtual void RegisterSimulator(const char* name) = 0;
    virtual void UnregisterSimulator() = 0;
};

class cSC4FlammabilitySimulator final : public cIGZSerializable {
public:
    struct tHotspot { int x = 0, z = 0, weight = 1; };

    explicit cSC4FlammabilitySimulator(IFlammabilityHost* host);
    ~cSC4FlammabilitySimulator() = default;

    // cIGZUnknown / cIGZSerializable identity (one shared count at +0x1c).
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool Init();
    bool Shutdown();

    // message dispatch (cIGZMessageTarget2::DoMessage)
    void OnMessage(std::uint32_t type, ISimOccupant* occupant);

    cSC4SimGrid<std::uint8_t>* GetFlammabilityGrid() const { return flammability_.get(); }
    bool IsCellFlammable(int x, int z);
    bool GetFlammabilityHotspot(std::uint8_t* value, int* x, int* z);
    std::uint8_t GetAverageFlammability() const { return average_; }

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kFlammabilitySimulatorClassId; }

    // Exposed for tests / the daily update.
    void DoDailySimulation();
    void DoOccupantUpdate(ISimOccupant* occupant, bool increaseOnly);
    std::uint8_t ComputeTractRawFlammability(int tractX, int tractZ);
    std::uint8_t GetOccupantRawFlammability(ISimOccupant* occupant);
    const std::list<tHotspot>& GetHotspots() const { return hotspots_; }
    cRZRandom& Random() { return random_; }
    void SetTuningForTest(float resistanceMultiplier) { factor_ = resistanceMultiplier; }

private:
    void ReadSimulatorTunableValues();
    void EndOfCycle();
    void ComputeHotspotList();
    void TryRandomFirestart();

    IFlammabilityHost* host_;
    bool initialized_ = false;                    // +0x18
    std::uint32_t refCount_ = 0;                  // +0x1c
    cRZRandom random_{0};                         // +0x20
    RandomIterator cellWalk_;                     // +0x38
    int cityCellsX_ = 0, cityCellsZ_ = 0;         // +0x48,+0x4c
    cRZAutoRefCount<cSC4SimGrid<std::uint8_t>> flammability_;      // +0x80
    cRZAutoRefCount<cSC4SimGrid<std::uint8_t>> rawFlammability_;   // +0x88
    std::uint8_t average_ = 0;                    // +0x90
    float factor_ = 1.0f;                         // +0x94 ordinance/weather multiplier
    int sumFlammability_ = 0, zonedCells_ = 0;    // +0x98,+0x9c
    std::vector<tHotspot> candidates_;            // +0xa0 cells over the threshold this pass
    std::list<tHotspot> hotspots_;                // +0xb8 merged list (count at +0xc8)
    // tunables
    float ordinanceRainFactor_ = 1.2f;            // +0xd0
    float civicFactor_ = 1.1f;                    // +0xd4
    float noWaterFactor_ = 1.1f;                  // +0xd8
    int tractSize_ = 1;                           // +0xdc
    int cellsPerDayDivisor_ = 40;                 // +0xe0
    int cellsPerDay_ = 0;                         // +0xe4
    int mergeDistance_ = 8;                       // +0xe8
    int hotspotThreshold_ = 196;                  // +0xec
    int minHotspotWeight_ = 16;                   // +0xf0
    float randomFireChance_ = 0.01f;              // +0xf4
    std::unique_ptr<cSC4ResponseCurve> fireCurve_;   // +0xf8
};

}  // namespace sc4
