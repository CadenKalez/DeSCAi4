#include "sc4/EffectMaps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sc4 {

namespace {
ITerrainMapSource* g_source = nullptr;

// floor() the way the original does it: round(x - 0.5) with ties to even through the float-add trick, then take
// the next integer when x is exactly that integer.
std::int32_t FloorTrick(float x) {
    const float biased = x - 0.5f + 12582912.0f;
    std::int32_t bits;
    std::memcpy(&bits, &biased, sizeof bits);
    const std::int32_t whole = bits - 0x4b400000;
    const std::int32_t next = bits - 0x4b3fffff;
    return x == static_cast<float>(next) ? next : whole;
}

bool Intersect(SC4CellRect& rect, const SC4CellRect& other) {
    const std::int32_t x0 = std::max(rect.x0, other.x0), z0 = std::max(rect.z0, other.z0);
    const std::int32_t x1 = std::min(rect.x1, other.x1), z1 = std::min(rect.z1, other.z1);
    if (x0 > x1 || z0 > z1) return false;
    rect = {x0, z0, x1, z1};
    return true;
}
}  // namespace

ITerrainMapSource* TerrainMapSource() { return g_source; }
void SetTerrainMapSource(ITerrainMapSource* source) { g_source = source; }

bool cSC4EffectMaps::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4EffectMaps || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC4EffectMaps::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4EffectMaps::Init() {
    if (!maps_.empty() || !g_source) return true;
    waterLevel_ = g_source->WaterLevel();
    cellsX_ = g_source->CellsX();
    cellsZ_ = g_source->CellsZ();
    const float cell = g_source->CellSize();
    cellSize_ = static_cast<int>(static_cast<long>(cell));
    vertsX_ = cellsX_ + 1;
    vertsZ_ = cellsZ_ + 1;
    inverseCellSize_ = 1.0f / static_cast<float>(static_cast<std::uint32_t>(static_cast<long>(cell)));
    mapRect_ = {0, 0, cellsX_, cellsZ_};
    maps_.resize(1);
    maps_[0].assign(static_cast<std::size_t>(vertsX_) * vertsZ_, 0.0f);
    return true;
}

bool cSC4EffectMaps::Shutdown() {
    maps_.clear();
    return true;
}

// Height of a vertex including the painted offset: above the water surface the offset is added, below it the
// vertex is only lifted to (water level + offset - 50).
float cSC4EffectMaps::MapVertexHeight(unsigned, unsigned x, unsigned z) const {
    float height = g_source ? g_source->VertexHeight(static_cast<int>(x), static_cast<int>(z)) : 0.0f;
    float offset = maps_[0][static_cast<std::size_t>(z) * vertsX_ + x];
    if (waterLevel_ <= height) return height + offset;
    offset = offset + waterLevel_ - 50.0f;
    return height <= offset ? offset : height;
}

// Bilinear interpolation of the four surrounding vertices.
float cSC4EffectMaps::MapLerpedHeight(unsigned map, float x, float z) const {
    const float fx = x * inverseCellSize_, fz = z * inverseCellSize_;
    const std::int32_t ix = FloorTrick(fx), iz = FloorTrick(fz);
    const float tx = fx - static_cast<float>(ix), tz = fz - static_cast<float>(iz);
    const float h00 = MapVertexHeight(map, ix, iz), h01 = MapVertexHeight(map, ix, iz + 1);
    const float h10 = MapVertexHeight(map, ix + 1, iz), h11 = MapVertexHeight(map, ix + 1, iz + 1);
    const float low = h00 + (h01 - h00) * tz;
    const float high = h10 + (h11 - h10) * tz;
    return low + (high - low) * tx;
}

// Surface normal from the bilinear slopes of the four surrounding vertices. (The original blends twelve samples
// whose per-call coordinates the decompile lost; this uses the four-vertex form, so the function is partial.)
void cSC4EffectMaps::MapLerpedNormal(unsigned map, float x, float z, float out[3]) const {
    const float fx = x * inverseCellSize_, fz = z * inverseCellSize_;
    const std::int32_t ix = FloorTrick(fx), iz = FloorTrick(fz);
    const float tx = fx - static_cast<float>(ix), tz = fz - static_cast<float>(iz);
    const float a = MapVertexHeight(map, ix, iz), b = MapVertexHeight(map, ix, iz + 1);
    const float c = MapVertexHeight(map, ix + 1, iz), d = MapVertexHeight(map, ix + 1, iz + 1);
    const float half = inverseCellSize_ * 0.5f;
    const float slopeZ = (((d - c) - (b - a)) * tx + (b - a)) * half;
    const float slopeX = ((c - a) + ((d - b) - (c - a)) * tz) * half;
    const float inv = 1.0f / std::sqrt(slopeX * slopeX + slopeZ * slopeZ + 1.0f);
    out[0] = -(slopeZ * inv);
    out[1] = inv;
    out[2] = -(inv * slopeX);
}

bool cSC4EffectMaps::ClipToMap(SC4CellRect& rect) const { return Intersect(rect, mapRect_); }

// Adds value / (spread + 1) to every cell of the rectangle and of `spread` surrounding rings.
void cSC4EffectMaps::AddRectToMap(unsigned map, SC4CellRect rect, float value, unsigned spread) {
    if (map - 1 >= maps_.size()) return;
    for (unsigned ring = 0; ring != spread + 1; ++ring) {
        if (ClipToMap(rect)) {
            for (int z = rect.z0; z <= rect.z1; ++z)
                for (int x = rect.x0; x <= rect.x1; ++x)
                    maps_[map - 1][static_cast<std::size_t>(vertsX_) * z + x] += value / static_cast<float>(spread + 1);
        }
        --rect.x0; --rect.z0; ++rect.x1; ++rect.z1;
    }
    dirty_ = true;
}

// Raises cells to at least `value`, the value falling off geometrically over the surrounding rings.
void cSC4EffectMaps::SetValueAboveTerrain(unsigned map, SC4CellRect rect, float value, unsigned spread) {
    if (map - 1 >= maps_.size()) return;
    const float falloff = 1.0f - 1.0f / (static_cast<float>(spread) + 1.0f);
    unsigned ring = 0;
    bool more;
    do {
        if (ClipToMap(rect)) {
            for (int z = rect.z0; z <= rect.z1; ++z)
                for (int x = rect.x0; x <= rect.x1; ++x) {
                    float& cell = maps_[map - 1][static_cast<std::size_t>(vertsX_) * z + x];
                    if (cell <= value && value != cell) cell = value;
                }
        }
        --rect.x0; --rect.z0; ++rect.x1; ++rect.z1;
        value = value * falloff;
        more = ring != spread;
        ++ring;
    } while (more);
    dirty_ = true;
}

void cSC4EffectMaps::ResetValueToTerrain(unsigned map, SC4CellRect rect) {
    if (map - 1 >= maps_.size()) return;
    if (ClipToMap(rect)) {
        for (int z = rect.z0; z <= rect.z1; ++z)
            for (int x = rect.x0; x <= rect.x1; ++x) maps_[map - 1][static_cast<std::size_t>(vertsX_) * z + x] = 0.0f;
    }
    dirty_ = true;
}

void cSC4EffectMaps::ClearMaps() {
    if (!maps_.empty()) std::fill(maps_[0].begin(), maps_[0].end(), 0.0f);
}

}  // namespace sc4
