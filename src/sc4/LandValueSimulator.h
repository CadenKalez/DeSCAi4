// cSC4LandValueSimulator (36 functions, 0x1000c4f1c..0x1000c7494, class id 0xe990c18e, interfaces 0xe7781d8c /
// 0x452294aa message target / 0xe4fda3d4 serializable). It rebuilds the city's land value map one stage per
// idle slice, each stage walking the tract grid with a resumable cursor under a time budget:
//   0 reset cursor        1 land mask (tract has land ? tunable d1 : 0) into the scratch grid
//   2 distance effect on the scratch grid, altitude range, reset      3 altitude statistics: min/max, mean and a
//     32-unit histogram of the zoned land tracts (repeated over every land tract when nothing is zoned)
//   5 slope map (float grid, tract size 8)         4 add the altitude effect to the intrinsic map
//   6 copy intrinsic -> scratch     7 one growth developer per step: scratch += desirability map * weight
//   8 copy scratch -> total map and average     9 finished (waits for the end-of-cycle message when dirty)
// Layout: +0x18 initialised, +0x1c refcount, +0x40 intrinsic map (id 0x89d5bb39), +0x48 total map (0x89d5bb51),
// +0x50 scratch (0x89d5bb60), +0x58 slope map (float, 0x49d5bb6f), +0x60 dirty, +0x68 stage, +0x70/+0x78 cursors,
// +0x80 sum, +0x84 count, +0x88 "all land" retry flag, +0x90 histogram, +0xa8 histogram shift (5), +0xac..+0xb8
// altitude effect setup, +0xc8 average, +0xcc mean altitude, +0xd0 tract size and tunables d1..d6, +0xd8 altitude
// curve, +0xe0 developer curve, +0xe8 developer weights, +0x100/+0x104 altitude range, +0x108 mode altitude.
// Quirks kept: TractHasLand scans (size+1)^2 cells (inclusive bounds) while TractIsZoned scans size^2; the
// timer is polled before every cell. The service globals (city, terrain, zone manager, history, simulator,
// building development simulator, cSC4MapUtilities::AddDistanceEffect) are reached through ILandValueHost.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/SimGridOps.h"
#include "sc4/cSC4ResponseCurve.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4LandValueSimulator = 0xe990c18e;
constexpr std::uint32_t kGZIID_cISC4LandValueSimulator = 0xe7781d8c;
constexpr std::uint32_t kLandValueHistoryAverage = 0x6a663ac8;
constexpr std::uint32_t kMessageLandValueEndOfCycle = 0x87cc75b3;

// Resumable walk over a tract rectangle (the 0x20-byte cursor record at +0x70 / +0x78).
struct TractCursor {
    int state = 0;                      // 0 fresh, 1 running, 2 finished
    int x = 0, z = 0;
    int step = 1;
    int minX = 0, minZ = INT32_MIN, maxX = INT32_MAX, maxZ = INT32_MAX;
    void Reset();                       // x = z = 0; finished at once when the range excludes the origin
    void SetBounds(int countX, int countZ);   // [0, count-1], clamped at 0 for empty grids
    bool Next(int& outX, int& outZ);    // false once the walk is finished
};

class ILandValueTerrain {
public:
    virtual ~ILandValueTerrain() = default;
    virtual float MinAltitude() = 0;                     // vslot 0xb8
    virtual float MaxAltitude() = 0;                     // vslot 0xc0
    virtual float AltitudeAt(float x, float z) = 0;      // vslot 0x60
    virtual bool CellHasLand(int x, int z) = 0;          // vslot 0x120
};

class ILandValueHost {
public:
    virtual ~ILandValueHost() = default;
    virtual void AddIdleClient(cIGZMessageTarget2* target, int kind, const char* name, int priority) = 0;
    virtual void RemoveIdleClient(cIGZMessageTarget2* target) = 0;
    virtual void AddNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
    virtual void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
    virtual void PostMessage(std::uint32_t message) = 0;                       // MessagePost(msg, 0)
    virtual bool AcquireServices() = 0;                                        // city, zone manager, terrain
    virtual const cISCPropertyHolder* SimulatorExemplar() = 0;                 // {0x6534284a, 0xe7e2c2db, 0xe7e2c8d8}; null if missing
    virtual void ReleaseServices() = 0;
    virtual ILandValueTerrain* Terrain() = 0;
    virtual int CellZoneType(int x, int z) = 0;                                // zone manager vslot 0x60
    virtual void CreateHistory(std::uint32_t historyId, int kind) = 0;         // history warehouse vslot 0x68
    virtual void AddHistory(std::uint32_t historyId, std::int64_t value) = 0;  // history warehouse vslot 0x60
    virtual const void* GrowthDeveloperForType(std::uint32_t developerType) = 0;
    virtual cSC4SimGrid<std::int16_t>* DesirabilityMap(const void* developer) = 0;
    virtual float AverageSlope(int x0, int z0, int x1, int z1) = 0;            // building development sim
    virtual void AddDistanceEffect(std::vector<std::vector<std::uint8_t>>& grid, int countX, int countZ,
                                   float inner, float outer) = 0;
    virtual std::uint64_t BeginSlice(std::uint32_t milliseconds) = 0;          // cSC4DynamicLimitTimer
    virtual bool SliceExpired(std::uint64_t slice) = 0;
};

class cISC4LandValueSimulator : public cIGZUnknown {
public:
    virtual int GetLandValueType(int x, int z) const = 0;
    virtual std::uint8_t GetLandValue(int x, int z) const = 0;
    virtual cSC4SimGrid<std::uint8_t>* GetIntrinsicLandValueMap() const = 0;
    virtual cSC4SimGrid<std::uint8_t>* GetTotalLandValueMap() const = 0;
    virtual cSC4SimGrid<float>* GetSlopeMap() const = 0;
    virtual std::uint8_t GetAverageLandValue() const = 0;
    virtual float GetModeAltitude() const = 0;
};

class cSC4LandValueSimulator final : public cISC4LandValueSimulator,
                                     public cIGZMessageTarget2,
                                     public cIGZSerializable {
public:
    explicit cSC4LandValueSimulator(ILandValueHost* host) : host_(host) {}
    ~cSC4LandValueSimulator() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init();
    bool Shutdown();
    void ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar);
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    bool DoMessage(cIGZMessage2* message) override;
    void DoIdle(std::uint32_t budgetMilliseconds);
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4LandValueSimulator; }

    bool TractIsZoned(int tractX, int tractZ);
    bool TractHasLand(int tractX, int tractZ);
    void SetupAltitudeEffect();
    std::uint32_t LookupAltitudeEffect(float altitude);
    void DoEndOfSimulationCycle() { host_->PostMessage(kMessageLandValueEndOfCycle); }

    int GetLandValueType(int x, int z) const override;
    std::uint8_t GetLandValue(int x, int z) const override { return total_->GetCellValue(x, z); }
    cSC4SimGrid<std::uint8_t>* GetIntrinsicLandValueMap() const override { return intrinsic_; }
    cSC4SimGrid<std::uint8_t>* GetTotalLandValueMap() const override { return total_; }
    cSC4SimGrid<float>* GetSlopeMap() const override { return slope_; }
    std::uint8_t GetAverageLandValue() const override { return average_; }
    float GetModeAltitude() const override { return modeAltitude_; }

    // state for tests / the engine
    bool IsInitialised() const { return initialised_; }
    bool IsDirty() const { return dirty_; }
    int Stage() const { return stage_; }
    void SetTractSize(std::uint8_t size) { tractSize_ = size; }
    std::uint8_t tractSize() const { return tractSize_; }
    void SetAltitudeCurve(const std::vector<SC4Point>& points);

private:
    cSC4SimGrid<std::uint8_t>* MakeGrid(std::uint32_t id);

    ILandValueHost* host_;
    bool initialised_ = false;
    std::uint32_t refCount_ = 0;
    bool dirty_ = false;
    int stage_ = 0;
    TractCursor cursorA_, cursorB_;
    int sum_ = 0, count_ = 0;
    bool allLand_ = false;
    std::vector<int> histogram_;
    int histogramShift_ = 5;
    float effectPivot_ = 0, effectLowSpan_ = 0, effectHighSpan_ = 0;
    int effectScale_ = 0;
    std::map<const void*, float>::iterator developerCursor_;
    std::uint8_t average_ = 0;
    float meanAltitude_ = 0;
    std::uint8_t tractSize_ = 2;
    std::uint8_t d1_ = 0, d2_ = 0, d3_ = 0, d4_ = 0, d5_ = 0, d6_ = 0;
    std::unique_ptr<cSC4ResponseCurve> altitudeCurve_, developerCurve_;
    std::map<const void*, float> developerWeights_;
    float minAltitude_ = 0, maxAltitude_ = 0, modeAltitude_ = 0;
    cSC4SimGrid<std::uint8_t>* intrinsic_ = nullptr;
    cSC4SimGrid<std::uint8_t>* total_ = nullptr;
    cSC4SimGrid<std::uint8_t>* scratch_ = nullptr;
    cSC4SimGrid<float>* slope_ = nullptr;
};

}  // namespace sc4
