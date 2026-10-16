// cSC4EffectMaps — per-vertex height offsets painted over the terrain by visual effects (the lava glow, flood
// water, ...): a grid with one float per terrain vertex, the effects add / raise values in rectangles of cells
// and the renderers read terrain height + offset back (bilinearly, plus a normal). 27 functions at
// 0x1005e3436..0x1005fcf70; class iid 0x0a5eaa1a. Layout: +0x18 terrain map, +0x20 water level, +0x28 maps
// (vector of float vectors), +0x40 cells X / Z, +0x48 vertices X / Z, +0x50 cell size, +0x54 1/cell size,
// +0x58 cell rectangle, +0x68 dirty, +0x70 debug mesh.
// Not recovered: the debug mesh drawing (UpdateMapsMesh / DrawTerrainMaps).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZUnknown.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4EffectMaps = 0x0a5eaa1a;

class ITerrainMapSource {
public:
    virtual ~ITerrainMapSource() = default;
    virtual float WaterLevel() = 0;                       // vslot 0xe8
    virtual int CellsX() = 0;                             // vslot 0x38
    virtual int CellsZ() = 0;                             // vslot 0x40
    virtual float CellSize() = 0;                         // vslot 0x30
    virtual float VertexHeight(int x, int z) = 0;         // vslot 0x70
};
ITerrainMapSource* TerrainMapSource();
void SetTerrainMapSource(ITerrainMapSource* source);

class cSC4EffectMaps final : public cIGZUnknown {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool Init();
    bool Shutdown();
    int MapCount() const { return 2; }
    int MapVertsX() const { return vertsX_; }
    int MapVertsZ() const { return vertsZ_; }
    float MapVertexHeight(unsigned map, unsigned x, unsigned z) const;
    float MapLerpedHeight(unsigned map, float x, float z) const;
    void MapLerpedNormal(unsigned map, float x, float z, float out[3]) const;
    void AddRectToMap(unsigned map, SC4CellRect rect, float value, unsigned spread);
    void SetValueAboveTerrain(unsigned map, SC4CellRect rect, float value, unsigned spread);
    void ResetValueToTerrain(unsigned map, SC4CellRect rect);
    void ClearMaps();
    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }
    float MapValue(unsigned map, unsigned x, unsigned z) const { return maps_[map - 1][z * vertsX_ + x]; }

private:
    bool ClipToMap(SC4CellRect& rect) const;
    std::uint32_t refCount_ = 0;
    std::vector<std::vector<float>> maps_;      // +0x28
    float waterLevel_ = 0.0f;                   // +0x20
    int cellsX_ = 0, cellsZ_ = 0;               // +0x40
    int vertsX_ = 0, vertsZ_ = 0;               // +0x48
    int cellSize_ = 0;                          // +0x50
    float inverseCellSize_ = 0.0f;              // +0x54
    SC4CellRect mapRect_;                       // +0x58
    bool dirty_ = false;                        // +0x68
};

}  // namespace sc4
