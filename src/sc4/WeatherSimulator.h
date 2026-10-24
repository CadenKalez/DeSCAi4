// cSC4WeatherSimulator — the city's weather: cloud moisture, rain, temperature, wind and the seasons
// (42 functions + thunks, 0x1000e320e..0x1000e7138, class id 0x2990c1bc, object size 0x1e0).
//
// The object is a GZCOM server with four views: the primary cISC4WeatherSimulator (iid 0xc6c66245, also
// answered for iid 1), the legacy message target cIGZMessageTarget (+8, iid 0xe98b2f57), the message
// target cIGZMessageTarget2 (+0x10, iid 0x452294aa) and cIGZSerializable (+0x18, iid 0xe4fda3d4).
// It is registered with the simulator for the day (interval 4), week (5) and month (6) messages and for
// 0x26d31ec1 ("city started", which runs the catch-up DoCompleteSimulation).
//
// Two grids drive everything. The weather grid has one cell per 16 m (cellWidthX/Z = 16, inverse 0.0625)
// and cellCountX/Z = city size / 16 cells. +0x98 is the temperature grid (a byte per cell: 4.25 steps per
// degree over -15..45 C), +0xb0 the "sea moderation" flags (water within seaModerationRadius cells),
// +0x138 the published moisture/cloud grid (0..255, 0xff = water), +0x150 the manual moisture additions.
//
// Monthly work (DoMonthlySimulation) picks the month's target temperature and humidity from two cosine
// curves, picks a new target wind (a random speed 2..4 m/s and the direction of the city edge with the
// least water, jittered by a gaussian angle) and, on the first month, finds the water areas (via the
// 0x1000e629a helper) to mark the sea-moderation cells. Daily work (0x66956814) interpolates the current
// wind from the previous month's target to this month's. Weekly work (DoWeeklySimulation) walks
// cellsPerUpdate random cells (RandomIterator at +0xe0), recomputes their temperature and then rebuilds
// the moisture grid over the whole city (SimulateMoisture); when the walk wraps it posts 0x6716a4e6.
//
// Original layout used here: +0x20 initialized, +0x24 refcount, +0x28 loaded from a stream,
// +0x29 post-city-init done, +0x30 city, +0x38 simulator, +0x48 terrain, +0x50/+0x54 cell counts,
// +0x58/+0x5c cell widths (16), +0x60/+0x64 their inverses, +0x68/+0x6c world size, +0x70/+0x74 standard
// city cells per weather cell, +0x78 update divisor (8), +0x7c second divisor (8), +0x80 temperature write
// radius (1), +0x84 last published season (-1), +0x88 max temperature (24), +0x8c seasonal amplitude (0.5),
// +0x90 second amplitude (0.5), +0x94 current temperature (20), +0x98 temperature grid, +0xb0 sea
// moderation, +0xc8 water area list, +0xe0 RandomIterator, +0x120 cells per weekly update, +0x124 sea
// moderation radius (6 cells), +0x128 coast warming (0.95), +0x12c altitude lapse (0.0035), +0x130 wind
// cooling (0.25), +0x138 moisture grid, +0x150 manual moisture, +0x168 humidity, +0x16c base humidity
// (0.5), +0x170 humidity amplitude (0.5), +0x174 humidity scale (0.4), +0x178 wind/rain factor (128),
// +0x17c extra moisture at peaks (0.12), +0x180 peak threshold (4.0), +0x184/+0x188 two unused tunables
// (64/15), +0x18c base wind speed (3), +0x190 base wind direction (0,1), +0x198 current wind speed,
// +0x19c current wind direction, +0x1a4 previous month's wind speed, +0x1a8 its direction, +0x1b0 this
// month's wind speed, +0x1b4 its direction, +0x1c0 the random generator (seeded 0xffffffff), +0x1d8 the
// month the ambient values were computed for (-1), +0x1dc the wind angle spread (0.25).
//
// Quirks kept: CellToStandardCityCell computes *both* outputs from its first argument (0x1000e48ba);
// GetHumidity ignores its position and the *base* humidity, returning (1 + base) / 2 (0x1000e4b5a);
// GetWindAtCell ignores the cell and tail-calls GetAmbientWind (0x1000e4bc4); SetManualMoistureAddition
// stores the low byte of a rounded 0..325 value, so additions above 200 wrap (0x1000e4a74);
// WriteTemperatureValue averages in place, so overlapping writes depend on the sweep order (0x1000e6e82);
// the constructor writes only the low half of the current wind direction at +0x19c (the port zeroes the
// other half so saves are deterministic); GetCurrentSeasonMonth shifts southern-hemisphere months with
// (m + 5) % 12 + 1 (0x1000e491c).
#pragma once
#include "sc4/CommonIIDs.h"
#include <cstdint>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/cGZMessageServer.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZRandom.h"
#include "sc4/RandomIterator.h"
#include "sc4/SCProperty.h"

class cIGZMessageServer2;

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4WeatherSimulator = 0x2990c1bc;
constexpr std::uint32_t kGZIID_cISC4WeatherSimulator = 0xc6c66245;
constexpr std::uint32_t kGZIID_cIGZMessageTargetLegacy = 0xe98b2f57;

// Messages the simulator sends / receives.
constexpr std::uint32_t kWeatherMessageCityStarted = 0x26d31ec1;   // received: run DoCompleteSimulation
constexpr std::uint32_t kWeatherMessageDay = 0x66956814;            // received: daily wind interpolation
constexpr std::uint32_t kWeatherMessageWeek = 0x66956815;           // received: DoWeeklySimulation
constexpr std::uint32_t kWeatherMessageMonth = 0x66956816;          // received: DoMonthlySimulation
constexpr std::uint32_t kWeatherMessageWeeklyPassDone = 0x6716a4e6; // posted when the weekly walk wraps
constexpr std::uint32_t kWeatherMessageSeasonChanged = 0x6716a4e5;  // posted with the new season in data 1
constexpr std::uint32_t kWeatherMessageMoistureUpdated = 0x09b6d58e; // posted at the end of SimulateMoisture

// The simulator's own exemplar: {0x6534284a, 0x7a4a8458, 0x1a2fdb6b}, filter 0xa52160f5.
constexpr cGZPersistResourceKey kWeatherTunablesKey{0x6534284a, 0x7a4a8458, 0x1a2fdb6b};
constexpr std::uint32_t kWeatherTunablesFilter = 0xa52160f5;

// Tunable property ids (0x1000e3b30).
constexpr std::uint32_t kWeatherPropUpdateDivisor = 0xa7607d70;       // +0x78, int
constexpr std::uint32_t kWeatherPropSecondDivisor = 0xa7607d71;       // +0x7c, int
constexpr std::uint32_t kWeatherPropTemperatureRadius = 0xa7607d72;   // +0x80, int
constexpr std::uint32_t kWeatherPropMaxTemperature = 0xa7607d73;      // +0x88, float
constexpr std::uint32_t kWeatherPropSeasonalAmplitude = 0xa7607d74;   // +0x8c, float [0,1)
constexpr std::uint32_t kWeatherPropTemperatureScale = 0xa7607d75;    // +0x90, float
constexpr std::uint32_t kWeatherPropSeaRadius = 0xa7607d76;           // +0x124, int
constexpr std::uint32_t kWeatherPropCoastWarming = 0xa7607d77;        // +0x128, float (>= 0)
constexpr std::uint32_t kWeatherPropAltitudeLapse = 0xa7607d78;       // +0x12c, float
constexpr std::uint32_t kWeatherPropWindCooling = 0xa7607d79;         // +0x130, float
constexpr std::uint32_t kWeatherPropBaseWindSpeed = 0xa7607d86;       // +0x18c, float
constexpr std::uint32_t kWeatherPropBaseWindDirX = 0xa7607d87;        // +0x190, float
constexpr std::uint32_t kWeatherPropBaseWindDirZ = 0xa7607d88;        // +0x194, float
constexpr std::uint32_t kWeatherPropBaseHumidity = 0xa7607d7a;        // +0x16c, float
constexpr std::uint32_t kWeatherPropHumidityAmplitude = 0xa7607d7b;   // +0x170, float [0,1)
constexpr std::uint32_t kWeatherPropHumidityScale = 0xa7607d7c;       // +0x174, float
constexpr std::uint32_t kWeatherPropMoistureScalar = 0xa7607d7d;      // +0x178, float (stored as * 255)
constexpr std::uint32_t kWeatherPropPeakExtraMoisture = 0xa7607d7e;   // +0x17c, float
constexpr std::uint32_t kWeatherPropPeakThreshold = 0xa7607d7f;       // +0x180, float
constexpr std::uint32_t kWeatherPropUnused1 = 0xa7607d80;             // +0x184, u32
constexpr std::uint32_t kWeatherPropUnused2 = 0xa7607d81;             // +0x188, u32
constexpr std::uint32_t kWeatherPropWindAngleDegrees = 0xa7607d89;    // +0x1dc, tan(deg / 2) / 2

// A rectangle in weather cells; unlike SC4Rect<int> the right/bottom edges are *exclusive* (the weekly
// UpdateMoisture caller passes (0, 0, cellCountX, cellCountZ) and the class clamps them against the grid).
struct WeatherRect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// ---------------------------------------------------------------------------------------------------
// Seams (unrecovered neighbours; each method mirrors one original call)
// ---------------------------------------------------------------------------------------------------

// SL::spTerrain (cISTETerrain). Slots from the calls this class makes.
class IWeatherTerrain {
public:
    virtual ~IWeatherTerrain() = default;
    virtual int CellCountX() = 0;                                            // +0x38
    virtual int CellCountZ() = 0;                                            // +0x40
    // Fills `out` with the altitudes of the vertices of [x0..x1] x [z0..z1] (both inclusive, row-major,
    // `stepX`/`stepZ` cells apart).
    virtual void GetAltitudes(int x0, int z0, int x1, int z1, float* out, int stepX, int stepZ) = 0;  // +0x78
    virtual float AltitudeAtVertex(int x, int z) = 0;                        // +0x70
    virtual bool CellHasWater(int x, int z) = 0;                             // +0x110
    virtual void VertexNormal(int x, int z, float out[3]) = 0;               // +0xe0
    virtual float SeaLevel() = 0;                                            // +0xe8 (water-area scanner)
};

// The services the original reaches through SL::sp* globals and the city/simulator vtable slots.
class IWeatherHost {
public:
    virtual ~IWeatherHost() = default;
    // ---- framework ----
    virtual cISCPropertyHolder* SimulatorTunables(const cGZPersistResourceKey& key,
                                                  std::uint32_t filter) = 0;   // spRM 0x18 -> resource 0x30
    // ---- SL::spCity ----
    virtual float CitySizeX() = 0;                                            // vslot 0x2c8 (metres)
    virtual float CitySizeZ() = 0;                                            // vslot 0x2d0
    virtual float CityCellSizeX() = 0;                                        // vslot 0x2d8 (metres)
    virtual float CityCellSizeZ() = 0;                                        // vslot 0x2e0
    virtual int CityHemisphere() = 0;                                         // vslot 0xf0 (1 = northern)
    // ---- SL::spSimulator ----
    virtual int SimulatorMonth() = 0;                                         // vslot 0x30 -> date vslot 0xb8
    virtual void RegisterAgent(cIGZMessageTarget2* target, int interval, const char* name) = 0;  // 0x98
    virtual void UnregisterAgent(cIGZMessageTarget2* target) = 0;             // 0xa8
    // ---- message server ----
    virtual void AddNotification(cIGZMessageTarget2* target, std::uint32_t type) = 0;     // 0x28
    virtual void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) = 0;  // 0x30
    virtual void PostMessage(cIGZMessage2* message) = 0;                                   // 0x20
    // ---- weather data ----
    virtual void MoistureValueRange(std::uint8_t& low, std::uint8_t& high) = 0;   // spSC4 0x118 -> 0xa8
    virtual IWeatherTerrain* Terrain() = 0;
    // The water-area scanner of the same translation unit (MakeWaterAreaList @ 0x1000e629a).
    virtual void MakeWaterAreaList(IWeatherTerrain& terrain, std::vector<cRZRect>& out, int minWidth,
                                   int minDepth, int cellsX, int cellsZ, int cellWidth, int cellDepth,
                                   const cRZRect* area) = 0;
};

IWeatherHost* WeatherHost();
void SetWeatherHost(IWeatherHost* host);   // not owning; null disables Init / Shutdown

// ---------------------------------------------------------------------------------------------------

class cISC4WeatherSimulator : public cIGZUnknown {};

class cSC4WeatherSimulator final : public cISC4WeatherSimulator,
                                   public cIGZMessageTarget,
                                   public cIGZMessageTarget2,
                                   public cIGZSerializable {
public:
    // The port's constructor takes the host seam where the original read the SL::sp* globals.
    explicit cSC4WeatherSimulator(IWeatherHost* host);
    ~cSC4WeatherSimulator();

    // cIGZUnknown / cIGZSerializable
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Write(cIGZOStream& stream) const override;   // version 2
    bool Read(cIGZIStream& stream) override;          // accepts versions 0..2
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4WeatherSimulator; }

    // cIGZMessageTarget / cIGZMessageTarget2
    bool DoMessage(cGZMessage& message) override;
    bool DoMessage(cIGZMessage2* message) override;

    // cISC4WeatherSimulator / cIZGUnknown-derived lifecycle
    bool Init();
    bool Shutdown();

    // ---- geometry -------------------------------------------------------------------------------
    float CellWidthX() const { return cellWidthX_; }        // +0x58
    float CellWidthZ() const { return cellWidthZ_; }        // +0x5c
    int CellCountX() const { return cellCountX_; }          // +0x50
    int CellCountZ() const { return cellCountZ_; }          // +0x54
    void PositionToCell(float x, float z, int& cellX, int& cellZ) const;                  // 0x1000e487a
    void CellToPosition(int cellX, int cellZ, float& x, float& z) const;                  // 0x1000e4896
    // Quirk: the second output is computed from cellX again (0x1000e48ba).
    void CellToStandardCityCell(int cellX, int cellZ, int& stdX, int& stdZ) const;
    void StandardCityCellToCell(int stdX, int stdZ, int& cellX, int& cellZ) const;        // 0x1000e48d0

    // ---- seasons / ambient ----------------------------------------------------------------------
    int GetCurrentSeason();          // (GetCurrentSeasonMonth() - 1) / 3, 0..3  @ 0x1000e48f8
    int GetCurrentSeasonMonth();     // 1..12, shifted by six months in the southern hemisphere @ 0x1000e491c
    void DoPostCityInit();           // 0x1000e3fcc
    void DoCompleteSimulation();     // 0x1000e3ff0
    void GetNewCurrentAmbientValues(int month);   // 0x1000e6b66
    float GetHumidity(float x, float z);          // 0x1000e4b5a
    float GetAmbientWind(float* direction);       // 0x1000e4b80 (returns the speed)
    float GetWindAtCell(int cellX, int cellZ, float* direction);   // 0x1000e4bc4 (ambient only)
    float CurrentTemperature() const { return temperature_; }     // +0x94
    float CurrentHumidity() const { return humidity_; }           // +0x168
    float CurrentWindSpeed() const { return windSpeed_; }         // +0x198
    float WindDirectionX() const { return windDirection_.x; }     // +0x19c
    float WindDirectionZ() const { return windDirection_.y; }     // +0x1a0
    int TargetWindMonth() const { return ambientMonth_; }         // +0x1d8

    // ---- moisture / cloud -----------------------------------------------------------------------
    std::uint8_t GetMoistureValue(float x, float z);                  // 0x1000e497a
    void GetMoistureValueMapPtr(std::vector<std::vector<std::uint8_t>>*& out);   // 0x1000e4a52 (+0x138)
    std::uint32_t GetMoistureType(float x, float z);                   // 0x1000e4a62 (value >> 4)
    void SetManualMoistureAddition(float x, float z, float x2, float z2, float value);   // 0x1000e4a74
    void UpdateMoisture(const WeatherRect& rect);                     // 0x1000e623e
    void SimulateMoisture(const WeatherRect& rect);                   // 0x1000e51ee (PARTIAL)

    // ---- simulation steps -----------------------------------------------------------------------
    void GenerateMoistureVariation();                    // 0x1000e3e1c
    void ReadSimulatorTunableValues();                   // 0x1000e3b30
    bool DoWeeklySimulation(int week);                   // 0x1000e4dd2 (returns true when the walk wrapped)
    void DoMonthlySimulation(int monthNumber);           // 0x1000e4f16
    void SimulateCellTemperature(int cellX, int cellZ);  // 0x1000e4ffa
    void WriteTemperatureValue(int cellX, int cellZ, int value);   // 0x1000e6e82
    void PostMessageServerMessage(std::uint32_t type, void* void1, std::uint32_t data2,
                                  std::uint32_t data3);  // 0x1000e5122
    void RemoveWaterThatIsNotAtCityEdge(std::vector<cRZRect>& areas);   // 0x1000e6866
    void CalculateSeaTemperatureModeration(const std::vector<cRZRect>& areas,
                                           std::vector<std::vector<bool>>& moderation);  // 0x1000e6994

    // ---- state accessors (tests / hook-up) ------------------------------------------------------
    bool IsInitialized() const { return initialized_; }             // +0x20
    std::uint32_t RefCount() const { return refCount_; }            // +0x24
    bool LoadedFromStream() const { return loadedFromStream_; }     // +0x28
    int CellsPerUpdate() const { return cellsPerUpdate_; }           // +0x120
    int LastPublishedSeason() const { return lastPublishedSeason_; } // +0x84
    float CurrentTemperatureValue() const { return temperature_; }
    float CurrentHumidityValue() const { return humidity_; }
    float CurrentWindSpeedValue() const { return windSpeed_; }
    cS3DVector2 CurrentWindDirection() const { return windDirection_; }        // +0x19c
    cS3DVector2 TargetWindDirection() const { return targetWindDirection_; }   // +0x1b4
    float TargetWindSpeed() const { return targetWindSpeed_; }                 // +0x1b0
    cS3DVector2 BaseWindDirection() const { return baseWindDirection_; }       // +0x190
    float BaseWindSpeed() const { return baseWindSpeed_; }                     // +0x18c
    std::vector<std::vector<std::uint8_t>>& TemperatureMap() { return temperatureMap_; }   // +0x98
    std::vector<std::vector<std::uint8_t>>& MoistureMap() { return moistureMap_; }         // +0x138
    std::vector<std::vector<std::uint8_t>>& ManualMoistureMap() { return manualMoisture_; }  // +0x150
    std::vector<std::vector<bool>>& SeaModeration() { return seaModeration_; }             // +0xb0
    std::vector<cRZRect>& WaterAreas() { return waterAreas_; }                             // +0xc8
    RandomIterator& CellWalk() { return cellWalk_; }                                       // +0xe0
    cRZRandom& Random() { return random_; }                                                // +0x1c0
    float SeaModerationRadius() const { return static_cast<float>(seaModerationRadius_); }
    float CoastWarming() const { return coastWarming_; }
    float AltitudeLapse() const { return altitudeLapse_; }
    float WindCooling() const { return windCooling_; }
    float MoistureScalar() const { return moistureScalar_; }          // +0x178
    float PeakExtraMoisture() const { return peakExtraMoisture_; }    // +0x17c
    float PeakThreshold() const { return peakThreshold_; }            // +0x180
    float WindAngleSpread() const { return windAngleSpread_; }        // +0x1dc
    int UpdateDivisor() const { return updateDivisor_; }              // +0x78
    int SecondDivisor() const { return secondDivisor_; }              // +0x7c
    int TemperatureWriteRadius() const { return temperatureWriteRadius_; }   // +0x80
    int StandardCellsPerCellX() const { return standardCellsPerCellX_; }     // +0x70
    int StandardCellsPerCellZ() const { return standardCellsPerCellZ_; }     // +0x74
    float WorldWidth() const { return worldWidth_; }                  // +0x68
    float WorldDepth() const { return worldDepth_; }                  // +0x6c
    float CellWidthInverseX() const { return inverseCellWidthX_; }    // +0x60
    float CellWidthInverseZ() const { return inverseCellWidthZ_; }    // +0x64
    float MaxTemperature() const { return maxTemperature_; }          // +0x88
    float SeasonalAmplitude() const { return seasonalAmplitude_; }    // +0x8c
    float TemperatureScale() const { return temperatureScale_; }      // +0x90
    float BaseHumidity() const { return baseHumidity_; }              // +0x16c
    float HumidityAmplitude() const { return humidityAmplitude_; }    // +0x170
    float HumidityScale() const { return humidityScale_; }            // +0x174
    int AmbientMonth() const { return ambientMonth_; }                // +0x1d8
    cS3DVector2 PreviousWindDirection() const { return previousWindDirection_; }  // +0x1a8
    float PreviousWindSpeed() const { return previousWindSpeed_; }                // +0x1a4

    // ---- original layout accessors used by the test / hook-up --------------------------------
    void SetCellWalkRect(int minX, int minZ, int maxX, int maxZ) { cellWalk_.SetRect(minX, minZ, maxX, maxZ); }
    void SetCellsPerUpdate(int count) { cellsPerUpdate_ = count; }
    void SetWindAngleSpread(float spread) { windAngleSpread_ = spread; }
    void SetTemperatureWriteRadius(int radius) { temperatureWriteRadius_ = radius; }
    void SetAirTemperatureForTest(float t) { temperature_ = t; }
    void SetHumidityForTest(float h) { humidity_ = h; }
    IWeatherHost* Host() const { return host_; }
    IWeatherTerrain* Terrain() const { return terrain_; }

private:
    void SetGridSize(int cellsX, int cellsZ);
    static int Clamp255(int value) { return value < 0 ? 0 : (value > 255 ? 255 : value); }

    std::vector<std::vector<std::uint8_t>> CreateByteGrid(int cellsX, int cellsZ, std::uint8_t value) const;

    IWeatherHost* host_;                              // the seam (SL::sp* globals)
    std::uint32_t refCount_ = 0;                      // +0x24
    bool initialized_ = false;                        // +0x20
    bool loadedFromStream_ = false;                   // +0x28
    bool postCityInitDone_ = false;                   // +0x29
    cIGZMessageTarget2* messageTarget_ = nullptr;     // the +0x10 view handed to the simulator / server
    IWeatherTerrain* terrain_ = nullptr;              // +0x48 (SL::spTerrain)

    int cellCountX_ = 0;                              // +0x50
    int cellCountZ_ = 0;                              // +0x54
    float cellWidthX_ = 16.0f;                        // +0x58
    float cellWidthZ_ = 16.0f;                        // +0x5c
    float inverseCellWidthX_ = 0.0625f;               // +0x60
    float inverseCellWidthZ_ = 0.0625f;               // +0x64
    float worldWidth_ = 0.0f;                         // +0x68
    float worldDepth_ = 0.0f;                         // +0x6c
    int standardCellsPerCellX_ = 1;                   // +0x70
    int standardCellsPerCellZ_ = 1;                   // +0x74
    int updateDivisor_ = 8;                           // +0x78
    int secondDivisor_ = 8;                           // +0x7c
    int temperatureWriteRadius_ = 1;                  // +0x80
    int lastPublishedSeason_ = -1;                    // +0x84
    float maxTemperature_ = 24.0f;                    // +0x88
    float seasonalAmplitude_ = 0.5f;                  // +0x8c
    float temperatureScale_ = 0.5f;                   // +0x90
    float temperature_ = 20.0f;                       // +0x94
    std::vector<std::vector<std::uint8_t>> temperatureMap_;   // +0x98
    std::vector<std::vector<bool>> seaModeration_;            // +0xb0
    std::vector<cRZRect> waterAreas_;                         // +0xc8
    RandomIterator cellWalk_;                                 // +0xe0
    int cellsPerUpdate_ = 1;                                  // +0x120
    int seaModerationRadius_ = 6;                             // +0x124
    float coastWarming_ = 0.95f;                              // +0x128
    float altitudeLapse_ = 0.0035f;                           // +0x12c (0x3b656042)
    float windCooling_ = 0.25f;                               // +0x130
    std::vector<std::vector<std::uint8_t>> moistureMap_;       // +0x138
    std::vector<std::vector<std::uint8_t>> manualMoisture_;    // +0x150
    float humidity_ = 0.5f;                                   // +0x168
    float baseHumidity_ = 0.5f;                               // +0x16c
    float humidityAmplitude_ = 0.5f;                          // +0x170
    float humidityScale_ = 0.4f;                              // +0x174
    float moistureScalar_ = 128.0f;                           // +0x178
    float peakExtraMoisture_ = 0.12f;                         // +0x17c
    float peakThreshold_ = 4.0f;                              // +0x180
    std::uint32_t unused1_ = 64;                              // +0x184
    std::uint32_t unused2_ = 15;                              // +0x188
    float baseWindSpeed_ = 3.0f;                              // +0x18c
    cS3DVector2 baseWindDirection_{0.0f, 1.0f};               // +0x190
    float windSpeed_ = 3.0f;                                  // +0x198
    cS3DVector2 windDirection_{1.0f, 0.0f};                   // +0x19c (the +0x1a0 half starts zeroed)
    float previousWindSpeed_ = 3.0f;                          // +0x1a4
    cS3DVector2 previousWindDirection_{1.0f, 0.0f};           // +0x1a8
    float targetWindSpeed_ = 3.0f;                            // +0x1b0
    cS3DVector2 targetWindDirection_{1.0f, 0.0f};             // +0x1b4
    cRZRandom random_{0xffffffffu};                           // +0x1c0
    int ambientMonth_ = -1;                                   // +0x1d8
    float windAngleSpread_ = 0.25f;                           // +0x1dc
};

}  // namespace sc4
