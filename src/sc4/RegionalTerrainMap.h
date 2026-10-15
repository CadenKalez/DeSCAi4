// cSC4RegionalTerrainMap — the flat stand-in terrain used by the region view (46 functions,
// 0x100234a46..0x100234e4e). It implements the terrain-map interface for a map where every vertex has the same
// altitude and nothing is water: all queries answer from one altitude value. Normals are always straight up (0, 1, 0).
// Layout: +0 interface vptr, +8 cRZRefCount, +0x14 altitude, +0x18 sea level, +0x1c/+0x20 cell counts,
// +0x24 vertices per row, +0x28 cell width, +0x2c initialized.
// GetAltitudes / GetWaterDepths fill (bottom - top) rows but right - left + 1 columns (the loops are not
// symmetric in the original).
#pragma once
#include <cstddef>
#include <cstdint>

#include "rz/cRZRefCount.h"
#include "sc4/cSTETerrain.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4TerrainMap = 0xab953253;

class cSC4RegionalTerrainMap : public cRZRefCount {
public:
    cSC4RegionalTerrainMap(float altitude, float seaLevel, std::int32_t cellsX, std::int32_t cellsZ, float cellWidth)
        : altitude_(altitude), seaLevel_(seaLevel), cellsX_(cellsX), cellsZ_(cellsZ), vertsPerRow_(cellsX + 1),
          cellWidth_(cellWidth) {}

    bool QueryInterface(std::uint32_t iid, void** out);
    bool Init() { initialized_ = true; return true; }
    bool Shutdown() { initialized_ = false; return true; }
    bool IsInitialized() const { return initialized_; }
    float CellWidth() const { return cellWidth_; }
    std::int32_t CellCountX() const { return cellsX_; }
    std::int32_t CellCountZ() const { return cellsZ_; }
    std::int32_t GetVertIndex(std::int32_t x, std::int32_t z) const { return z * vertsPerRow_ + x; }
    void GetVertex(std::int32_t index, std::int32_t& x, std::int32_t& z) const;
    bool LocationIsInBounds(float x, float z) const;

    float GetAltitude(float, float) const { return altitude_; }
    float GetAltitude(std::int32_t) const { return altitude_; }
    float GetAltitudeAtVertex(std::int32_t, std::int32_t) const { return altitude_; }
    void GetAltitudes(const TerrainRect& rect, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t columnStride) const;
    void GetWaterDepths(const TerrainRect& rect, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t columnStride) const;
    void GetCellCornerAltitudes(std::uint32_t, std::uint32_t, float* corners) const;
    float GetMaxSurfaceLevel(std::uint32_t, std::uint32_t) const { return altitude_; }
    float GetAltitudeAtNearestGrid(float, float) const { return altitude_; }
    std::uint64_t GetAltitudeCheckSum(const TerrainRect&) const { return 0; }
    float MinHeight() const { return altitude_; }
    float MaxHeight() const { return altitude_; }
    float MinAllowedHeight() const { return 0.0f; }
    float MaxAllowedHeight() const { return 1023.0f; }   // 0x447fc000
    std::int32_t GetTerrainGridIndex(float x, float z) const;
    void GetFaceNormal(float, float, TerrainVector3& normal) const { normal = {0.0f, 1.0f, 0.0f}; }
    void GetLerpedNormal(float, float, TerrainVector3& normal) const { normal = {0.0f, 1.0f, 0.0f}; }
    void GetVertexNormal(std::uint32_t, std::uint32_t, TerrainVector3& normal) const { normal = {0.0f, 1.0f, 0.0f}; }
    float GetSeaLevel() const { return seaLevel_; }
    float GetWaterLevel(std::int32_t) const { return 0.0f; }
    float GetWaterLevel(float, float) const { return 0.0f; }
    float GetLerpedWaterLevel(float, float) const { return 0.0f; }
    float GetWaterDepthAtVertex(std::int32_t, std::int32_t) const { return 0.0f; }
    bool CellHasWater(std::uint32_t, std::uint32_t) const { return false; }
    bool CellHasLand(std::uint32_t, std::uint32_t) const { return true; }
    bool IsLandVertex(std::uint32_t, std::uint32_t) const { return true; }
    bool IsWaterVertex(std::uint32_t, std::uint32_t) const { return false; }
    bool IsSeaVertex(std::uint32_t, std::uint32_t) const { return false; }
    bool AreSeaVertices(const TerrainRect&) const { return false; }

private:
    float altitude_;             // +0x14
    float seaLevel_;             // +0x18
    std::int32_t cellsX_;        // +0x1c
    std::int32_t cellsZ_;        // +0x20
    std::int32_t vertsPerRow_;   // +0x24
    float cellWidth_;            // +0x28
    bool initialized_ = false;   // +0x2c
};

}  // namespace sc4
