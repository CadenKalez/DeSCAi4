// cSTETerrain — the city terrain model: a (cells+1)^2 vertex grid of 16 m cells with altitude,
// water depth, vertex normals and per-vertex property flags. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtable 0x100aea6c0): the data model, point/cell queries, lighting-free setters
// and the three save/load subfiles. The brush, erosion, cliff, texture and flood code is not
// recovered yet (see notes/ledger.csv).
//
// Vertex (36 bytes): +0 altitude, +4/+8/+0xc normal, +0x10..0x21 texture/colour data, +0x22 u16
// property flags:
//   0x0020 even triangle hidden   0x0040 odd triangle hidden   (0x60 = whole cell hidden)
//   0x0200 cell flag 4            0x1000 internal triangulation flipped
//   0x2000 external triangulation flipped / cell flag 8         0x4000 altitude locked
// GetTerrainCellFlags packs them as bit0-1 = (flags>>5)&3, bit2 = (flags>>7)&4, bit3 = (flags>>10)&8.
//
// Water depth lives in the surface-water object in the original (cSTESurfaceWater, same 36-byte
// stride); the model keeps the depth array itself and hands it out through WaterDepth(). A vertex
// is "land" when its water depth is <= 0, "water" when > 0 and "sea" when its altitude is below
// the sea level.
//
// Saved subfiles (group 0xe98f9525): 0x8a91e7e3/3 {u32 vertsX, u32 vertsZ, s32 terrainType,
// f32 minHeight, f32 maxHeight, f32 maxNormalHeight} (version 2), 0xa9dd6ff4/1 {f32 altitude per
// vertex, row-major} (version 2), 0x8a91e7e0/2 {u16 flags per vertex} (version 1).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "gz/cIGZPersistDBSegment.h"

namespace sc4 {

struct TerrainRect {                // SC4Rect<int>: inclusive vertex bounds
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

struct TerrainVector3 { float x = 0, y = 0, z = 0; };

struct TerrainTuning {              // statics read from the terrain ini / exemplars at startup
    float seaLevel = 250.0f;
    float minSeaLevel = -10.1f, maxSeaLevel = 200.1f;
    float seaRaiseLowerAmount = 20.0f;
    float maxTerrainHeight = 200.0f;
};

constexpr float kTerrainCellWidth = 16.0f;
constexpr std::uint16_t kVertexHiddenEven = 0x0020, kVertexHiddenOdd = 0x0040, kVertexHiddenWhole = 0x0060;
constexpr std::uint16_t kVertexFlipInternal = 0x1000, kVertexFlipExternal = 0x2000, kVertexLocked = 0x4000;

class cSTETerrain {
public:
    struct Vertex {
        float altitude = 0;
        float normal[3] = {0, 0, 0};
        std::uint8_t surface[0x12] = {};
        std::uint16_t flags = 0;
    };
    static_assert(sizeof(Vertex) == 0x24);

    cSTETerrain() = default;

    // Lays out the grid for a city of cellsX x cellsZ cells (zeroed vertices, sea-level water).
    bool Init(std::uint32_t cellsX, std::uint32_t cellsZ, const TerrainTuning& tuning = {});
    bool IsInitialized() const { return initialized_; }
    // Called by view code that caches triangle indices: (cellZ, cellX) of a cell whose flags changed.
    void SetCellChangedCallback(std::function<void(std::uint32_t, std::uint32_t)> cb) { onCellChanged_ = std::move(cb); }

    // ---- dimensions -------------------------------------------------------------------------
    static float CellWidth() { return kTerrainCellWidth; }
    std::uint32_t CellCountX() const { return cellsX_; }
    std::uint32_t CellCountZ() const { return cellsZ_; }
    std::uint32_t VertexCountX() const { return vertsX_; }
    std::uint32_t VertexCountZ() const { return vertsZ_; }
    std::uint32_t VertexCount() const { return totalVerts_; }
    std::int32_t GetVertIndex(std::int32_t x, std::int32_t z) const { return z * static_cast<std::int32_t>(vertsX_) + x; }
    void GetVertex(std::int32_t index, std::int32_t& x, std::int32_t& z) const {
        z = static_cast<std::int32_t>(static_cast<std::uint32_t>(index) / vertsX_);
        x = static_cast<std::int32_t>(static_cast<std::uint32_t>(index) % vertsX_);
    }
    bool LocationIsInBounds(float x, float z) const { return x >= 0 && z >= 0 && x <= widthMeters_ && z <= depthMeters_; }
    std::int32_t GetTerrainGridIndex(float x, float z) const {
        return static_cast<std::int32_t>(x * 0.0625f) + static_cast<std::int32_t>(z * 0.0625f) * static_cast<std::int32_t>(vertsX_);
    }
    // Bit mask of the existing neighbours of cell/vertex (x, z): bit i = direction i of the table
    // E, NE, N... as the original packs them (0x7c edge pattern when x <= 0, etc.).
    std::uint8_t GetNeighbors(std::int32_t x, std::int32_t z) const;
    float MinHeight() const { return minHeight_; }
    float MaxHeight() const { return maxHeight_; }
    float MaxNormalTerrainHeight() const { return maxNormalHeight_; }
    std::int32_t TerrainType() const { return terrainType_; }
    void SetTerrainTypeRaw(std::int32_t type) { terrainType_ = type; }

    // ---- altitude ---------------------------------------------------------------------------
    float GetAltitude(float x, float z) const;                       // triangulated interpolation
    float GetAltitude(std::int32_t index) const;
    float GetAltitudeAtVertex(std::int32_t x, std::int32_t z) const;
    float GetAltitudeAtNearestGrid(float x, float z) const;
    void GetCellCornerAltitudes(std::uint32_t cx, std::uint32_t cz, float out[4]) const;
    void GetAltitudes(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const;
    void FindMinAndMaxHeights();                                      // whole grid
    void FindMinAndMaxHeights(const TerrainRect& r, float& lo, float& hi) const;
    void SetAltitude(std::int32_t index, float altitude);             // honours the lock flag
    void SetAltitude(const TerrainRect& r, float altitude, bool recomputeWater);
    void LockAltitude(const TerrainRect& r, bool lock);                // sets/clears the 0x4000 flag

    // ---- normals ----------------------------------------------------------------------------
    void GetFaceNormal(float x, float z, TerrainVector3& out) const;
    void GetLerpedNormal(float x, float z, TerrainVector3& out) const;
    void GetVertexNormal(std::uint32_t x, std::uint32_t z, TerrainVector3& out) const;

    // ---- water / sea ------------------------------------------------------------------------
    float GetSeaLevel() const { return tuning_.seaLevel; }
    bool SetSeaLevel(float level);                                    // range checked, resets the water
    void ChangeSeaLevel(bool raise) { SetSeaLevel(tuning_.seaLevel + (raise ? tuning_.seaRaiseLowerAmount : -tuning_.seaRaiseLowerAmount)); }
    float GetWaterDepthAtVertex(std::int32_t index) const;
    float GetWaterDepthAtVertex(std::int32_t x, std::int32_t z) const;
    float GetWaterLevel(std::int32_t index) const;                    // altitude + depth
    float GetWaterLevel(float x, float z) const;
    float GetLerpedWaterLevel(float x, float z) const;
    float GetMaxSurfaceLevel(std::uint32_t cx, std::uint32_t cz) const;
    void GetWaterDepths(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const;
    void RecomputeWaterLevel(const TerrainRect& r);                   // depth = sea - altitude where below
    bool IsSeaVertex(std::int32_t index) const;
    bool IsSeaVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsWaterVertex(std::int32_t index) const;
    bool IsWaterVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsLandVertex(std::int32_t index) const;
    bool IsLandVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsShoreVertex(std::int32_t index) const;
    bool CellHasWater(std::uint32_t cx, std::uint32_t cz) const;
    bool CellHasLand(std::uint32_t cx, std::uint32_t cz) const;
    std::vector<float>& WaterDepth() { return waterDepth_; }
    const std::vector<float>& WaterDepth() const { return waterDepth_; }

    // ---- cell flags / triangulation ---------------------------------------------------------
    std::uint16_t GetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz) const;
    bool SetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz, std::uint32_t flags);
    void HideTerrainCell(std::uint32_t cx, std::uint32_t cz, bool hide);
    void HideEvenTriangle(std::uint32_t cx, std::uint32_t cz, bool hide);
    void HideOddTriangle(std::uint32_t cx, std::uint32_t cz, bool hide);
    bool IsEvenTriangleHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsOddTriangleHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsWholeCellHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsPointHidden(float x, float z) const;
    bool IsTriangulationFlipped(std::uint32_t cx, std::uint32_t cz) const;
    void FlipInternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip);
    void FlipExternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip);

    // ---- persistence ------------------------------------------------------------------------
    bool Load(cIGZPersistDBSegment* segment);
    bool Save(cIGZPersistDBSegment* segment) const;
    bool LoadMiscTerrainData(cIGZPersistDBSegment* segment);
    bool LoadAltitudes(cIGZPersistDBSegment* segment);
    bool LoadVertexProperties(cIGZPersistDBSegment* segment);
    bool SaveMiscTerrainData(cIGZPersistDBSegment* segment) const;
    bool SaveAltitudes(cIGZPersistDBSegment* segment) const;
    bool SaveVertexProperties(cIGZPersistDBSegment* segment) const;
    bool LastLoadSucceeded() const { return loadOk_; }

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    std::vector<Vertex>& Vertices() { return vertices_; }

private:
    std::uint32_t ClampCell(float coordinate, std::uint32_t limit) const;
    static std::uint32_t FloorCell(float coordinate);
    void CellChanged(std::uint32_t cx, std::uint32_t cz) { if (onCellChanged_) onCellChanged_(cz, cx); }
    std::uint16_t& Flags(std::int32_t index) { return vertices_[static_cast<std::size_t>(index)].flags; }
    std::uint16_t Flags(std::int32_t index) const { return vertices_[static_cast<std::size_t>(index)].flags; }

    bool initialized_ = false;
    TerrainTuning tuning_;
    std::uint32_t cellsX_ = 0, cellsZ_ = 0, lastCellX_ = 0, lastCellZ_ = 0;
    std::uint32_t vertsX_ = 0, vertsZ_ = 0, totalVerts_ = 0;
    float widthMeters_ = 0, depthMeters_ = 0;
    std::int32_t terrainType_ = 0;
    float minHeight_ = 0, maxHeight_ = 0, maxNormalHeight_ = 0;
    bool loadOk_ = false;
    std::int32_t neighborOffsets_[8] = {};
    std::vector<Vertex> vertices_;
    std::vector<float> waterDepth_;
    std::function<void(std::uint32_t, std::uint32_t)> onCellChanged_;
};

}  // namespace sc4
