// cSC4LotDeveloper — builds, lights, levels and demolishes the buildings of zoned lots (124 functions,
// 0x10013eb1e..0x100148eda, class id 0xa990bfe0). This port covers the terrain analysis the developer uses to
// decide whether a lot is buildable (altitude extremes, averages, flatness, slope, levelling), the tunables
// loaded from the simulator exemplar and the development-cost estimate. Construction / demolition,
// foundations, props, base textures and the Test* gates need the lot, occupant and business-unit interfaces and
// stay unrecovered.
//
// Original layout used here: +0x30 city/cell converter, +0x38 terrain leveller, +0x70 terrain, +0x78 vertices
// per row, +0x7c/+0x80 city extent in metres, +0x118 u16 condition threshold, +0x11c/+0x120/+0x124 floats,
// +0x128 flat tolerance, +0x12c float, +0x130/+0x134 u32, +0x138/+0x139 bool, +0x13c level tolerance,
// +0x140 maximum levelling amount, +0x148 development-cost response curve.
#pragma once
#include <cstdint>

#include "sc4/SCProperty.h"
#include "sc4/cSC4ResponseCurve.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4LotDeveloper = 0xa990bfe0;
constexpr std::uint32_t kLotDeveloperTunablesKeyType = 0xe7e2c2db;     // exemplar key {type, group, instance}
constexpr std::uint32_t kLotDeveloperTunablesKeyGroup = 0x6534284a;
constexpr std::uint32_t kLotDeveloperTunablesKeyInstance = 0xe8da7677;

// tunable property ids
constexpr std::uint32_t kLotDevPropConditionThreshold = 0x47e2c400;    // u16 -> +0x118
constexpr std::uint32_t kLotDevPropConditionRatios = 0x47e2c401;       // float[3] -> +0x124, +0x120, +0x11c
constexpr std::uint32_t kLotDevPropFlatTolerance = 0xe900ea24;         // -> +0x128
constexpr std::uint32_t kLotDevPropFloat12c = 0xe900ea25;
constexpr std::uint32_t kLotDevPropU32_130 = 0xe900ea26;
constexpr std::uint32_t kLotDevPropU32_134 = 0x6916516b;
constexpr std::uint32_t kLotDevPropBool138 = 0xe900ea27;
constexpr std::uint32_t kLotDevPropBool139 = 0xe900ea28;
constexpr std::uint32_t kLotDevPropLevelTolerance = 0xe900ea29;        // -> +0x13c
constexpr std::uint32_t kLotDevPropMaxLevelling = 0xe900ea2a;          // -> +0x140
constexpr std::uint32_t kLotDevPropU32_0f0 = 0xe900ea2b;
constexpr std::uint32_t kLotDevPropU32_0ec = 0xe900ea2c;
constexpr std::uint32_t kLotDevPropCostCurve = 0x2a2d7824;             // interleaved x,y floats

constexpr float kLotMaxLevelling = 3.4028235e+38f;                     // "use the tunable" sentinel

struct LotCellRect { int x0 = 0, z0 = 0, x1 = 0, z1 = 0; };            // inclusive cells (SC4Rect<int>)
struct LotMetreRect { float x0 = 0, z0 = 0, x1 = 0, z1 = 0; };         // SC4Rect<float>

struct LotDeveloperTunables {
    std::uint16_t conditionThreshold = 0;   // +0x118
    float ratioC = 0.6f;                    // +0x11c
    float ratioB = 0.4f;                    // +0x120
    float ratioA = 0.2f;                    // +0x124
    float flatTolerance = 0;                // +0x128
    float value12c = 0;                     // +0x12c
    std::uint32_t value130 = 0, value134 = 0;
    bool flag138 = false, flag139 = false;
    float levelTolerance = 0;               // +0x13c
    float maxLevelling = 0;                 // +0x140
    std::uint32_t value0f0 = 0, value0ec = 0;
};

class IBaseLotDeveloperHost {
public:
    virtual ~IBaseLotDeveloperHost() = default;
    // terrain model (this+0x70)
    virtual float VertexHeight(int vertexIndex) = 0;                   // vslot 0x68
    virtual float VertexWaterHeight(int vertexIndex) = 0;              // vslot 0xf0
    virtual float HeightAt(float x, float z) = 0;                      // vslot 0x60
    virtual float WaterHeightAt(float x, float z) = 0;                 // vslot 0xf8
    virtual bool VertexIsLevelled(int vx, int vz) = 0;                 // vslot 0x128
    virtual int VerticesPerRow() = 0;                                  // +0x78
    virtual float CityWidthMetres() = 0;                               // +0x7c
    virtual float CityDepthMetres() = 0;                               // +0x80
    // terrain leveller (this+0x38) vslot 0x48; returns the number of vertices changed
    virtual int LevelVertices(float height, bool a, bool b, int x0, int z0, int x1, int z1, void* lot, int mode,
                              int extra) = 0;
    // city / cell converter (this+0x30) vslot 0x2f8: metres -> cell coordinates
    virtual void CellOfPosition(float x, float z, int& cellX, int& cellZ) = 0;
    // occupant bounding box (occupant vslot 0x50)
    virtual void OccupantBounds(void* occupant, float lo[3], float hi[3]) = 0;
};

class cSC4LotDeveloper {
public:
    explicit cSC4LotDeveloper(IBaseLotDeveloperHost* host) : host_(host) {}

    // tunables (ReadSimulatorTunableValues): reads from the simulator exemplar when it exists
    void ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar);
    LotDeveloperTunables& Tunables() { return tunables_; }
    const LotDeveloperTunables& Tunables() const { return tunables_; }

    float GetTerrainAltitudeByVertex(int vx, int vz, bool includeWater) const;
    float GetLowestOrHighestAreaAltitude(const LotCellRect& area, bool lowest, bool includeWater) const;
    float GetLowestOrHighestAreaAltitude(const LotMetreRect& area, bool lowest, bool includeWater) const;
    float GetAverageAreaAltitude(const LotCellRect& area, bool includeWater) const;
    int LevelAreaInternalVertices(const LotCellRect& area, float height, bool skipLevelled, bool force,
                                  void* lot);
    int LevelBorderingVertices(const LotCellRect& area, float height, void* lot);
    bool AttemptAreaLevelling(const LotCellRect& area, float unusedHeight, bool includeWater, bool flag, float maxSlope,
                              void* lot);
    void GetOccupantCellCoverage(void* occupant, LotCellRect& out) const;
    bool IsAreaDeveloped(const LotCellRect&) const { return false; }
    bool IsAreaFlat(const LotCellRect& area, bool includeWater) const;
    float GetAreaAltitudeRange(const LotCellRect& area, bool includeWater) const;
    float GetAreaSlope(const LotCellRect& area, bool includeWater) const;
    std::int64_t GetLotDevelopmentCost(const LotCellRect& area) const;

private:
    IBaseLotDeveloperHost* host_;
    LotDeveloperTunables tunables_;
    cSC4ResponseCurve developmentCostCurve_;                           // +0x148
};

}  // namespace sc4
