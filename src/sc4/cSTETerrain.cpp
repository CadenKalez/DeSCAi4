#include "sc4/cSTETerrain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZPersistRecord.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kGroupTerrain = 0xe98f9525;
constexpr std::uint32_t kTypeMisc = 0x8a91e7e3, kInstanceMisc = 3;
constexpr std::uint32_t kTypeAltitudes = 0xa9dd6ff4, kInstanceAltitudes = 1;
constexpr std::uint32_t kTypeVertexProps = 0x8a91e7e0, kInstanceVertexProps = 2;

// cSC4AutoLoadStream / cSC4AutoSaveStream: a record opened for the duration of a scope with a
// leading u16 version.
class AutoLoad {
public:
    AutoLoad(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance)
        : segment_(segment) {
        if (!segment_) return;
        if (!segment_->OpenRecord({type, group, instance}, &record_, 1) || !record_) { record_ = nullptr; return; }
        void* raw = nullptr;
        if (!record_->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw)) return;
        stream_ = static_cast<cIGZPersistDBSerialRecord*>(raw);
        stream_->GetFieldUint16(version_);
    }
    ~AutoLoad() {
        if (stream_) stream_->Release();
        if (record_) {
            segment_->CloseRecord(record_);
            record_->Release();
        }
    }
    bool IsOpen() const { return stream_ != nullptr; }
    cIGZPersistDBSerialRecord& Stream() { return *stream_; }
    std::uint16_t Version() const { return version_; }

private:
    cIGZPersistDBSegment* segment_;
    cIGZPersistDBRecord* record_ = nullptr;
    cIGZPersistDBSerialRecord* stream_ = nullptr;
    std::uint16_t version_ = 0;
};

class AutoSave {
public:
    AutoSave(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
             std::uint16_t version)
        : segment_(segment) {
        if (!segment_) return;
        cGZPersistResourceKey key{type, group, instance};
        bool ok = segment_->TestForRecord(key) ? segment_->OpenRecord(key, &record_, 3)
                                               : segment_->CreateNewRecord(key, &record_);
        if (!ok || !record_) { record_ = nullptr; return; }
        void* raw = nullptr;
        if (!record_->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw)) return;
        stream_ = static_cast<cIGZPersistDBSerialRecord*>(raw);
        stream_->SetSize(0);
        stream_->SeekAbsolute(0);
        stream_->SetFieldUint16(version);
    }
    ~AutoSave() {
        if (stream_) stream_->Release();
        if (record_) {
            segment_->CloseRecord(record_);
            record_->Release();
        }
    }
    bool IsOpen() const { return stream_ != nullptr; }
    cIGZPersistDBSerialRecord& Stream() { return *stream_; }

private:
    cIGZPersistDBSegment* segment_;
    cIGZPersistDBRecord* record_ = nullptr;
    cIGZPersistDBSerialRecord* stream_ = nullptr;
};
}  // namespace

bool cSTETerrain::Init(std::uint32_t cellsX, std::uint32_t cellsZ, const TerrainTuning& tuning) {
    tuning_ = tuning;
    cellsX_ = cellsX;
    cellsZ_ = cellsZ;
    lastCellX_ = cellsX - 1;
    lastCellZ_ = cellsZ - 1;
    vertsX_ = cellsX + 1;
    vertsZ_ = cellsZ + 1;
    totalVerts_ = vertsX_ * vertsZ_;
    widthMeters_ = static_cast<float>(cellsX) * kTerrainCellWidth;
    depthMeters_ = static_cast<float>(cellsZ) * kTerrainCellWidth;
    const auto vx = static_cast<std::int32_t>(vertsX_);
    const std::int32_t offsets[8] = {-1, ~vx, -vx, 1 - vx, 1, vx + 1, vx, vx - 1};
    std::copy(offsets, offsets + 8, neighborOffsets_);
    gridRect_ = {0, 0, static_cast<std::int32_t>(cellsX), static_cast<std::int32_t>(cellsZ)};   // +0x58
    vertices_.assign(totalVerts_, Vertex{});
    waterDepth_.assign(totalVerts_, 0.0f);        // cSTESurfaceWater vertex +0x0
    sediment_.assign(totalVerts_, 0.0f);          // cSTESurfaceWater vertex +0x1c
    tempWaterLevels_.assign(totalVerts_, 0.0f);   // +0x100 (allocated in the original's Init)
    waterVertexCount_ = 0;                        // +0x108
    initialized_ = true;
    return true;
}

std::uint32_t cSTETerrain::FloorCell(float coordinate) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(coordinate * 0.0625f)));
}

std::uint32_t cSTETerrain::ClampCell(float coordinate, std::uint32_t limit) const {
    return std::min(FloorCell(coordinate), limit);
}

std::uint8_t cSTETerrain::GetNeighbors(std::int32_t x, std::int32_t z) const {
    std::uint8_t mask = 0x7c;
    if (0 < x) mask = 0xff;
    if (cellsX_ <= static_cast<std::uint32_t>(x)) mask &= 0xc7;
    if (z < 1) mask &= 0xf1;
    if (cellsZ_ <= static_cast<std::uint32_t>(z)) mask &= 0x1f;
    return mask;
}

// ------------------------------------------------------------------------------- altitude

float cSTETerrain::GetAltitude(float x, float z) const {
    if (!initialized_) return 0.0f;
    std::uint32_t cx = ClampCell(x, lastCellX_), cz = ClampCell(z, lastCellZ_);
    const std::uint32_t vx = vertsX_;
    const float h00 = vertices_[cz * vx + cx].altitude;
    const float fx = x - static_cast<float>(cx) * 16.0f;
    const float fz = z - static_cast<float>(cz) * 16.0f;
    const std::uint32_t rowNext = (cz + 1) * vx;
    std::uint32_t mid = cz * vx + 1;
    if (fx <= fz) mid = rowNext;
    const float lo = fx <= fz ? fx : fz;
    const float hi = fz < fx ? fx : fz;
    const float hMid = vertices_[mid + cx].altitude;
    const float h11 = vertices_[rowNext + 1 + cx].altitude;
    return ((hMid - h00) * hi + (h11 - hMid) * lo) * 0.0625f + h00;
}

float cSTETerrain::GetAltitude(std::int32_t index) const {
    return static_cast<std::uint32_t>(index) < totalVerts_ ? vertices_[static_cast<std::size_t>(index)].altitude : 0.0f;
}

float cSTETerrain::GetAltitudeAtVertex(std::int32_t x, std::int32_t z) const {
    if (static_cast<std::uint32_t>(x) < vertsX_ && static_cast<std::uint32_t>(z) < vertsZ_)
        return vertices_[vertsX_ * static_cast<std::uint32_t>(z) + static_cast<std::uint32_t>(x)].altitude;
    return 0.0f;
}

float cSTETerrain::GetAltitudeAtNearestGrid(float x, float z) const {
    if (!initialized_) return 0.0f;
    const auto gx = static_cast<std::int32_t>(std::nearbyintf(x * 0.0625f));
    const auto gz = static_cast<std::int32_t>(std::nearbyintf(z * 0.0625f));
    return vertices_[static_cast<std::uint32_t>(gx + gz * static_cast<std::int32_t>(vertsX_))].altitude;
}

void cSTETerrain::GetCellCornerAltitudes(std::uint32_t cx, std::uint32_t cz, float out[4]) const {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    const std::uint32_t i = cz * vertsX_ + cx;
    out[0] = vertices_[i].altitude;
    out[1] = vertices_[i + vertsX_].altitude;
    out[2] = vertices_[i + 1 + vertsX_].altitude;
    out[3] = vertices_[i + 1].altitude;
}

void cSTETerrain::GetAltitudes(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        float* p = out + (z - r.top) * rowStride;
        for (std::int32_t x = r.left; x <= r.right; ++x, p += colStride)
            *p = vertices_[static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(x)].altitude;
    }
}

void cSTETerrain::FindMinAndMaxHeights() {
    if (vertices_.empty()) return;
    minHeight_ = maxHeight_ = vertices_[0].altitude;
    for (std::uint32_t i = 1; i < totalVerts_; ++i) {
        float h = vertices_[i].altitude;
        if (h < minHeight_) minHeight_ = h;
        else if (maxHeight_ < h) maxHeight_ = h;
    }
}

void cSTETerrain::FindMinAndMaxHeights(const TerrainRect& r, float& lo, float& hi) const {
    lo = 3.4028235e+38f;
    hi = -3.4028235e+38f;
    for (std::int32_t z = r.top; z <= r.bottom; ++z)
        for (std::int32_t x = r.left; x <= r.right; ++x) {
            float h = vertices_[static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(x)].altitude;
            if (h < lo) lo = h;
            if (hi < h) hi = h;
        }
}

void cSTETerrain::SetAltitude(std::int32_t index, float altitude) {
    Vertex& v = vertices_[static_cast<std::size_t>(index)];
    if (v.flags & kVertexLocked) return;
    float& depth = waterDepth_[static_cast<std::size_t>(index)];
    float surface = v.altitude + depth;
    v.altitude = altitude;
    if (0.0f < depth || altitude < tuning_.seaLevel) {
        // flooded vertices keep their water surface; dry ones that dip under the sea fill to it
        float reference = 0.0f < depth ? surface : tuning_.seaLevel;
        depth = reference - altitude;
    }
}

void cSTETerrain::SetAltitude(const TerrainRect& r, float altitude, bool recomputeWater) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z)
        for (std::int32_t x = r.left; x <= r.right; ++x)
            SetAltitude(static_cast<std::int32_t>(vertsX_) * z + x, altitude);
    if (recomputeWater) RecomputeWaterLevel(r);
}

void cSTETerrain::LockAltitude(const TerrainRect& r, bool lock) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z)
        for (std::int32_t x = r.left; x <= r.right; ++x) {
            std::uint16_t& f = Flags(static_cast<std::int32_t>(vertsX_) * z + x);
            f = lock ? static_cast<std::uint16_t>(f | kVertexLocked) : static_cast<std::uint16_t>(f & ~kVertexLocked);
        }
}

// --------------------------------------------------------------------------------- normals

void cSTETerrain::GetFaceNormal(float x, float z, TerrainVector3& out) const {
    if (!(0.0f <= x && 0.0f <= z && x < widthMeters_ && z < depthMeters_)) return;
    const std::uint32_t cx = FloorCell(x), cz = FloorCell(z);
    const std::uint32_t vx = vertsX_;
    const float h00 = vertices_[cz * vx + cx].altitude;
    const float diag = vertices_[(cz + 1) * vx + 1 + cx].altitude - h00;
    const float fx = x - static_cast<float>(cx) * 16.0f, fz = z - static_cast<float>(cz) * 16.0f;
    float nx, nz;
    if (fx <= fz) {
        float up = (vertices_[(cz + 1) * vx + cx].altitude - h00) * 16.0f;
        nx = up - diag * 16.0f;
        nz = diag * 0.0f - up;
    } else {
        float side = (vertices_[cz * vx + cx + 1].altitude - h00) * 16.0f;
        nx = diag * 0.0f - side;
        nz = side - diag * 16.0f;
    }
    float inv = 1.0f / std::sqrt(nz * nz + nx * nx + 65536.0f);
    out.x = nx * inv;
    out.y = inv * 256.0f;
    out.z = nz * inv;
}

void cSTETerrain::GetLerpedNormal(float x, float z, TerrainVector3& out) const {
    if (!initialized_) return;
    const float gx = x * 0.0625f, gz = z * 0.0625f;
    const std::uint32_t cx = FloorCell(x), cz = FloorCell(z);
    const float fx = gx - static_cast<float>(cx), fz = gz - static_cast<float>(cz);
    const std::uint32_t vx = vertsX_, i00 = cz * vx + cx;
    const Vertex& a = vertices_[i00];
    const Vertex& b = vertices_[i00 + 1];
    const Vertex& c = vertices_[i00 + vx];
    const Vertex& d = vertices_[i00 + vx + 1];
    float n[3];
    for (int k = 0; k < 3; ++k) {
        float top = (b.normal[k] - a.normal[k]) * fx + a.normal[k];
        float bottom = (d.normal[k] - c.normal[k]) * fx + c.normal[k];
        n[k] = (bottom - top) * fz + top;
    }
    float inv = 1.0f / std::sqrt(n[2] * n[2] + n[1] * n[1] + n[0] * n[0]);
    out = {n[0] * inv, n[1] * inv, n[2] * inv};
}

void cSTETerrain::GetVertexNormal(std::uint32_t x, std::uint32_t z, TerrainVector3& out) const {
    if (x < vertsX_ && z < vertsZ_) {
        const Vertex& v = vertices_[vertsX_ * z + x];
        out = {v.normal[0], v.normal[1], v.normal[2]};
    }
}

// ------------------------------------------------------------------------------------ water

bool cSTETerrain::SetSeaLevel(float level) {
    if (!(tuning_.minSeaLevel <= level && level <= tuning_.maxSeaLevel)) return false;
    tuning_.seaLevel = level;
    RecomputeWaterLevel({0, 0, static_cast<std::int32_t>(cellsX_), static_cast<std::int32_t>(cellsZ_)});
    return true;
}

void cSTETerrain::RecomputeWaterLevel(const TerrainRect& r) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z)
        for (std::int32_t x = r.left; x <= r.right; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(x);
            const float altitude = vertices_[i].altitude;
            waterDepth_[i] = altitude < tuning_.seaLevel ? tuning_.seaLevel - altitude : 0.0f;
        }
}

float cSTETerrain::GetWaterDepthAtVertex(std::int32_t index) const {
    return static_cast<std::uint32_t>(index) < totalVerts_ ? waterDepth_[static_cast<std::size_t>(index)] : 0.0f;
}

float cSTETerrain::GetWaterDepthAtVertex(std::int32_t x, std::int32_t z) const {
    if (static_cast<std::uint32_t>(x) < vertsX_ && static_cast<std::uint32_t>(z) < vertsZ_)
        return waterDepth_[vertsX_ * static_cast<std::uint32_t>(z) + static_cast<std::uint32_t>(x)];
    return 0.0f;
}

float cSTETerrain::GetWaterLevel(std::int32_t index) const {
    if (static_cast<std::uint32_t>(index) < totalVerts_)
        return vertices_[static_cast<std::size_t>(index)].altitude + waterDepth_[static_cast<std::size_t>(index)];
    return 0.0f;
}

float cSTETerrain::GetWaterLevel(float x, float z) const {
    if (!initialized_) return 0.0f;
    const std::size_t i = FloorCell(z) * vertsX_ + FloorCell(x);
    return vertices_[i].altitude + waterDepth_[i];
}

float cSTETerrain::GetLerpedWaterLevel(float x, float z) const {
    if (!initialized_) return 0.0f;
    const float gx = x * 0.0625f, gz = z * 0.0625f;
    const std::uint32_t cx = FloorCell(x), cz = FloorCell(z), vx = vertsX_;
    const std::size_t i = cz * vx + cx;
    auto level = [&](std::size_t k) { return vertices_[k].altitude + waterDepth_[k]; };
    const float fx = gx - static_cast<float>(cx), fz = gz - static_cast<float>(cz);
    const float v00 = level(i), v01 = level(i + vx);
    const float top = (level(i + 1) - v00) * fx + v00;
    const float bottom = (level(i + 1 + vx) - v01) * fx + v01;
    return (bottom - top) * fz + top;
}

float cSTETerrain::GetMaxSurfaceLevel(std::uint32_t cx, std::uint32_t cz) const {
    if (cx >= cellsX_ || cz >= cellsZ_) return 0.0f;
    const std::uint32_t vx = vertsX_, i = cz * vx + cx;
    auto surface = [&](std::uint32_t k) {
        float depth = waterDepth_[k];
        return (0.0f <= depth ? depth : 0.0f) + vertices_[k].altitude;
    };
    float best = surface(i);
    for (std::uint32_t k : {i + vx, i + vx + 1, i + 1}) best = std::max(best, surface(k));
    return best;
}

void cSTETerrain::GetWaterDepths(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        float* p = out + (z - r.top) * rowStride;
        for (std::int32_t x = r.left; x <= r.right; ++x, p += colStride)
            *p = waterDepth_[static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(x)];
    }
}

bool cSTETerrain::IsSeaVertex(std::int32_t index) const {
    return vertices_[static_cast<std::size_t>(index)].altitude < tuning_.seaLevel;
}
bool cSTETerrain::IsSeaVertex(std::uint32_t x, std::uint32_t z) const {
    return x < vertsX_ && z < vertsZ_ && vertices_[vertsX_ * z + x].altitude < tuning_.seaLevel;
}
bool cSTETerrain::IsWaterVertex(std::int32_t index) const { return 0.0f < waterDepth_[static_cast<std::size_t>(index)]; }
bool cSTETerrain::IsWaterVertex(std::uint32_t x, std::uint32_t z) const {
    return x < vertsX_ && z < vertsZ_ && 0.0f < waterDepth_[vertsX_ * z + x];
}
bool cSTETerrain::IsLandVertex(std::int32_t index) const { return waterDepth_[static_cast<std::size_t>(index)] <= 0.0f; }
bool cSTETerrain::IsLandVertex(std::uint32_t x, std::uint32_t z) const {
    return x < vertsX_ && z < vertsZ_ && waterDepth_[vertsX_ * z + x] <= 0.0f;
}

bool cSTETerrain::IsShoreVertex(std::int32_t index) const {
    const float sea = tuning_.seaLevel;
    if (!(sea < vertices_[static_cast<std::size_t>(index)].altitude)) return false;
    const std::uint32_t z = static_cast<std::uint32_t>(index) / vertsX_, x = static_cast<std::uint32_t>(index) % vertsX_;
    const std::uint8_t mask = GetNeighbors(static_cast<std::int32_t>(x), static_cast<std::int32_t>(z));
    for (int dir = 0; dir < 8; dir += 2) {
        if (!((mask >> dir) & 1)) continue;
        if (vertices_[static_cast<std::size_t>(neighborOffsets_[dir] + index)].altitude < sea) return true;
    }
    return false;
}

bool cSTETerrain::CellHasWater(std::uint32_t cx, std::uint32_t cz) const {
    if (cx >= cellsX_ || cz >= cellsZ_) return false;
    const std::uint32_t i = cz * vertsX_ + cx;
    // true unless all four corners are dry-or-... exactly as the original: any corner with depth <= 0
    // short-circuits to true, and when all of the first three are wet the last decides.
    if (waterDepth_[i] <= 0.0f && waterDepth_[i + 1] <= 0.0f && waterDepth_[i + vertsX_] <= 0.0f)
        return 0.0f < waterDepth_[i + vertsX_ + 1];
    return true;
}

bool cSTETerrain::CellHasLand(std::uint32_t cx, std::uint32_t cz) const {
    if (cx >= cellsX_ || cz >= cellsZ_) return false;
    const std::uint32_t i = cz * vertsX_ + cx;
    if (0.0f < waterDepth_[i] && 0.0f < waterDepth_[i + 1] && 0.0f < waterDepth_[i + vertsX_])
        return waterDepth_[i + vertsX_ + 1] <= 0.0f;
    return true;
}

// ------------------------------------------------------------------------- cell flags

std::uint16_t cSTETerrain::GetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz) const {
    if (cx >= cellsX_ || cz >= cellsZ_) return 0;
    std::uint16_t f = vertices_[cz * vertsX_ + cx].flags;
    return static_cast<std::uint16_t>(((f >> 10) & 8) | ((f >> 7) & 4) | ((f >> 5) & 3));
}

bool cSTETerrain::SetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz, std::uint32_t flags) {
    if (cx >= cellsX_ || cz >= cellsZ_) return false;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    const std::uint16_t old = f;
    f = static_cast<std::uint16_t>(((flags & 3) << 5) | ((flags & 4) << 7) | (old & 0xfd9f));
    FlipExternalTriangulation(cx, cz, ((flags & 8) >> 3) != 0);
    CellChanged(cx, cz);
    return f != old;
}

void cSTETerrain::HideTerrainCell(std::uint32_t cx, std::uint32_t cz, bool hide) {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    f = hide ? static_cast<std::uint16_t>(f | 0x60) : static_cast<std::uint16_t>(f & 0xff9f);
    CellChanged(cx, cz);
}

void cSTETerrain::HideEvenTriangle(std::uint32_t cx, std::uint32_t cz, bool hide) {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    f = hide ? static_cast<std::uint16_t>(f | 0x20) : static_cast<std::uint16_t>(f & 0xffdf);
    CellChanged(cx, cz);
}

void cSTETerrain::HideOddTriangle(std::uint32_t cx, std::uint32_t cz, bool hide) {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    f = hide ? static_cast<std::uint16_t>(f | 0x40) : static_cast<std::uint16_t>(f & 0xffbf);
    CellChanged(cx, cz);
}

bool cSTETerrain::IsEvenTriangleHidden(std::uint32_t cx, std::uint32_t cz) const {
    return cx < cellsX_ && cz < cellsZ_ && (vertices_[cz * vertsX_ + cx].flags & 0x20) != 0;
}
bool cSTETerrain::IsOddTriangleHidden(std::uint32_t cx, std::uint32_t cz) const {
    return cx < cellsX_ && cz < cellsZ_ && (vertices_[cz * vertsX_ + cx].flags & 0x40) != 0;
}
bool cSTETerrain::IsWholeCellHidden(std::uint32_t cx, std::uint32_t cz) const {
    return cx < cellsX_ && cz < cellsZ_ && (vertices_[cz * vertsX_ + cx].flags & 0x60) == 0x60;
}
bool cSTETerrain::IsTriangulationFlipped(std::uint32_t cx, std::uint32_t cz) const {
    return cx < cellsX_ && cz < cellsZ_ && (vertices_[cz * vertsX_ + cx].flags & 0x3000) != 0;
}

bool cSTETerrain::IsPointHidden(float x, float z) const {
    const std::uint32_t cx = ClampCell(x, lastCellX_), cz = ClampCell(z, lastCellZ_);
    const float fx = x - static_cast<float>(cx) * 16.0f, fz = z - static_cast<float>(cz) * 16.0f;
    const std::uint16_t f = vertices_[cz * vertsX_ + cx].flags;
    bool odd = fz < fx;
    if (f & 0x3000) odd = 16.0f < fx + fz;
    return (f & (odd ? 0x40 : 0x20)) != 0;
}

void cSTETerrain::FlipInternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip) {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    const std::uint16_t n = flip ? static_cast<std::uint16_t>(f | 0x1000) : static_cast<std::uint16_t>(f & 0xefff);
    if (n != f) {
        f = n;
        CellChanged(cx, cz);
    }
}

void cSTETerrain::FlipExternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip) {
    if (cx >= cellsX_ || cz >= cellsZ_) return;
    std::uint16_t& f = vertices_[cz * vertsX_ + cx].flags;
    const std::uint16_t n = flip ? static_cast<std::uint16_t>(f | 0x2000) : static_cast<std::uint16_t>(f & 0xdfff);
    if (n != f) {
        f = n;
        CellChanged(cx, cz);
    }
}

// ------------------------------------------------------------------------------ persistence

bool cSTETerrain::LoadMiscTerrainData(cIGZPersistDBSegment* segment) {
    AutoLoad in(segment, kTypeMisc, kGroupTerrain, kInstanceMisc);
    if (!in.IsOpen()) return false;
    cIGZPersistDBSerialRecord& s = in.Stream();
    s.GetFieldUint32(vertsX_);
    s.GetFieldUint32(vertsZ_);
    s.GetFieldSint32(terrainType_);
    s.GetFieldFloat32(minHeight_);
    s.GetFieldFloat32(maxHeight_);
    s.GetFieldFloat32(maxNormalHeight_);
    if (in.Version() < 2 && terrainType_ > 0) terrainType_ = 0;
    totalVerts_ = vertsX_ * vertsZ_;
    return true;
}

bool cSTETerrain::LoadAltitudes(cIGZPersistDBSegment* segment) {
    AutoLoad in(segment, kTypeAltitudes, kGroupTerrain, kInstanceAltitudes);
    if (!in.IsOpen() || in.Version() != 2) return false;
    if (vertices_.size() != totalVerts_) vertices_.assign(totalVerts_, Vertex{});
    if (waterDepth_.size() != totalVerts_) waterDepth_.assign(totalVerts_, 0.0f);
    for (std::uint32_t i = 0; i < totalVerts_; ++i) in.Stream().GetFieldFloat32(vertices_[i].altitude);
    return true;
}

bool cSTETerrain::LoadVertexProperties(cIGZPersistDBSegment* segment) {
    AutoLoad in(segment, kTypeVertexProps, kGroupTerrain, kInstanceVertexProps);
    if (!in.IsOpen() || in.Version() != 1 || vertices_.empty()) return false;
    for (std::uint32_t i = 0; i < totalVerts_; ++i) in.Stream().GetFieldUint16(vertices_[i].flags);
    return true;
}

bool cSTETerrain::Load(cIGZPersistDBSegment* segment) {
    loadOk_ = false;
    if (LoadMiscTerrainData(segment) && LoadAltitudes(segment)) loadOk_ = LoadVertexProperties(segment);
    return true;   // (sic) the original always reports success; LastLoadSucceeded() has the truth
}

bool cSTETerrain::SaveMiscTerrainData(cIGZPersistDBSegment* segment) const {
    AutoSave out(segment, kTypeMisc, kGroupTerrain, kInstanceMisc, 2);
    if (!out.IsOpen()) return false;
    cIGZPersistDBSerialRecord& s = out.Stream();
    s.SetFieldUint32(vertsX_);
    s.SetFieldUint32(vertsZ_);
    s.SetFieldSint32(terrainType_);
    s.SetFieldFloat32(minHeight_);
    s.SetFieldFloat32(maxHeight_);
    s.SetFieldFloat32(maxNormalHeight_);
    return true;
}

bool cSTETerrain::SaveAltitudes(cIGZPersistDBSegment* segment) const {
    AutoSave out(segment, kTypeAltitudes, kGroupTerrain, kInstanceAltitudes, 2);
    if (!out.IsOpen()) return false;
    for (std::uint32_t i = 0; i < totalVerts_; ++i) out.Stream().SetFieldFloat32(vertices_[i].altitude);
    return true;
}

bool cSTETerrain::SaveVertexProperties(cIGZPersistDBSegment* segment) const {
    AutoSave out(segment, kTypeVertexProps, kGroupTerrain, kInstanceVertexProps, 1);
    if (!out.IsOpen()) return false;
    for (std::uint32_t i = 0; i < totalVerts_; ++i) out.Stream().SetFieldUint16(vertices_[i].flags);
    return true;
}

bool cSTETerrain::Save(cIGZPersistDBSegment* segment) const {
    SaveMiscTerrainData(segment);
    SaveAltitudes(segment);
    SaveVertexProperties(segment);
    for (int edge = 0; edge < 4; ++edge) SaveEdgeAltitudes(edge, segment);   // 0x10073f17a
    return true;
}


// ==================================================================================== identity

namespace {
// SL::spMS2 / spLightingManager (the seams this file talks to).
ITerrainHost*& HostSlot() {
    static ITerrainHost* host = nullptr;
    return host;
}
// sbDisplayPlumbingInfo: a class-wide display flag (cSTETerrain::sbDisplayPlumbingInfo).
bool& PlumbingDisplayFlag() {
    static bool flag = false;
    return flag;
}
// The original's round-to-nearest helper: the (v + 1.5 * 2^23) - 2^23 sequence Ghidra inlines in
// GetSedimentValue (0x1007394dc); ties go to even, as nearbyintf does.
std::int32_t RoundToNearestInt(float v) { return static_cast<std::int32_t>(std::nearbyintf(v)); }
}  // namespace

ITerrainHost* TerrainHost() { return HostSlot(); }
void SetTerrainHost(ITerrainHost* host) { HostSlot() = host; }

bool cSTETerrain::QueryInterface(std::uint32_t iid, void** out) {
    // 0x10073aa92: the original answers with this, this + 0x8 (cIGZMessageTarget2) or this + 0x10
    // (the persist interface). The port has no multiple inheritance, so every accepted id gets the
    // one object; the identity check is what callers rely on.
    if (iid != 0xab953253 && iid != 0xc70bd37e && iid != 0xe4fda3d4 && iid != 0x452294aa && iid != 1) return false;
    if (out) *out = this;
    AddRef();
    return true;
}

std::uint32_t cSTETerrain::AddRef() { return ++refCount_; }

std::uint32_t cSTETerrain::Release() {
    if (refCount_ <= 1) {
        delete this;   // the original calls its deleting destructor (vslot 0x2e0)
        return 0;
    }
    return --refCount_;
}

// ==================================================================================== messages

void cSTETerrain::PostMessageA(std::uint32_t type, std::uint64_t a, std::uint64_t b, std::uint64_t c,
                               std::uint64_t d) {
    ITerrainHost* host = TerrainHost();
    cIGZMessageServer2* server = host ? host->Messages() : nullptr;
    if (!server) return;
    cIGZMessage2Standard* message = host->NewMessage();
    if (!message) return;
    message->SetType(type);
    message->SetData1(a);
    message->SetData2(b);
    message->SetData3(c);
    message->SetData4(d);
    server->MessagePost(message, false);
    message->Release();
}

void cSTETerrain::PostMessageToTarget(std::uint32_t type, cIGZMessageTarget2* target) {
    ITerrainHost* host = TerrainHost();
    cIGZMessageServer2* server = host ? host->Messages() : nullptr;
    if (!server || !target) return;
    cIGZMessage2Standard* message = host->NewMessage();
    if (!message) return;
    message->SetType(type);
    server->GeneralMessagePostToTarget(message, target);
    message->Release();
}

void cSTETerrain::BeginLongTerrainOperation() { PostMessageA(0xea3b0af1, 0, 0, 0, 0); }
void cSTETerrain::EndLongTerrainOperation() { PostMessageA(0x6a3b0afd, 0, 0, 0, 0); }

// ==================================================================================== display

bool cSTETerrain::GetPlumbingInfoDisplayState() const { return PlumbingDisplayFlag(); }
void cSTETerrain::SetPlumbingInfoDisplayState(bool on) { PlumbingDisplayFlag() = on; }

void cSTETerrain::SetWaterSurfaceDisplayState(bool on) {
    if (displayWater_ == on) return;
    displayWater_ = on;
    ITerrainHost* host = TerrainHost();
    cIGZMessageServer2* server = host ? host->Messages() : nullptr;
    if (!server) return;
    cIGZMessage2Standard* message = host->NewMessage();
    if (!message) return;
    message->SetType(0x6ab97795);
    message->SetData1(on ? 1u : 0u);
    server->MessagePost(message, false);
    message->Release();
}

void cSTETerrain::StartAccumulatingTextureUpdates() {
    if (accumulating_) return;
    accumulating_ = true;
    accumulateRect_ = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};   // +0xe4
}

void cSTETerrain::StopAccumulatingTextureUpdates() {
    if (!accumulating_) return;
    accumulating_ = false;
    // The original re-textures the accumulated rectangle and redisplays it here; both calls belong
    // to the surface-texture pipeline (to-do row TERRAIN-3), so the rectangle is only reset here.
    accumulateRect_ = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};
}

// ==================================================================================== counts

int cSTETerrain::NumberOfWaterVertices(const TerrainRect& r) const {
    int count = 0;
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i)
            count += 0.0f < waterDepth_[i] ? 1 : 0;
    }
    return count;
}

bool cSTETerrain::AreSeaVertices(const TerrainRect& r) const {
    if (static_cast<std::uint32_t>(r.left) >= vertsX_) return false;
    if (static_cast<std::uint32_t>(r.top) >= vertsZ_) return false;
    if (vertsX_ <= static_cast<std::uint32_t>(r.right)) return false;
    if (static_cast<std::uint32_t>(r.bottom) >= vertsZ_) return false;
    if (r.bottom < r.top) return true;   // (sic) an empty range counts as "all sea"
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i) {
            const float altitude = vertices_[i].altitude;
            if (tuning_.seaLevel <= altitude) return false;
        }
    }
    return true;
}

float cSTETerrain::GetAltitudeCheckSum(const TerrainRect& r) const {
    if (static_cast<std::uint32_t>(r.left) >= vertsX_) return 0.0f;
    if (static_cast<std::uint32_t>(r.top) >= vertsZ_) return 0.0f;
    if (static_cast<std::uint32_t>(r.right) >= vertsX_) return 0.0f;
    if (static_cast<std::uint32_t>(r.bottom) >= vertsZ_) return 0.0f;
    if (r.bottom < r.top) return 0.0f;
    float sum = 0.0f;
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i) sum += vertices_[i].altitude;
    }
    return sum;
}

// ==================================================================================== editing

void cSTETerrain::LowerVertex(std::uint32_t index, float amount) {
    Vertex& v = vertices_[index];
    if (v.flags & kVertexLocked) return;
    float altitude = v.altitude - amount;
    v.altitude = altitude;
    if (altitude < kMinTerrainHeight) {
        v.altitude = kMinTerrainHeight;
        altitude = kMinTerrainHeight;
    }
    // The original rewrites the surface-water depth unconditionally, so a vertex raised above the sea
    // keeps a negative depth instead of drying out (kept on purpose).
    waterDepth_[index] = tuning_.seaLevel - altitude;
}

void cSTETerrain::RaiseVertex(std::uint32_t index, float amount) {
    Vertex& v = vertices_[index];
    if (v.flags & kVertexLocked) return;
    float altitude = amount + v.altitude;
    v.altitude = altitude;
    if (kMaxTerrainHeight < altitude) {
        v.altitude = kMaxTerrainHeight;
        altitude = kMaxTerrainHeight;
    }
    waterDepth_[index] = tuning_.seaLevel - altitude;   // negative above the sea (see LowerVertex)
}

void cSTETerrain::SetAltitudeAtVertex(std::int32_t x, std::int32_t z, float altitude) {
    const std::uint32_t index = vertsX_ * static_cast<std::uint32_t>(z) + static_cast<std::uint32_t>(x);
    const float current = vertices_[index].altitude;
    if (current < altitude) {
        RaiseVertex(index, altitude - current);
    } else if (altitude < current) {
        LowerVertex(index, current - altitude);
    }
}

void cSTETerrain::ClearVertexPropertyFlags(std::uint16_t mask) {
    for (std::uint32_t i = 0; i < totalVerts_; ++i) vertices_[i].flags &= static_cast<std::uint16_t>(~mask);
}

void cSTETerrain::ClearVertexPropertyFlags(const TerrainRect& r, std::uint16_t mask) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i)
            vertices_[i].flags &= static_cast<std::uint16_t>(~mask);
    }
}

void cSTETerrain::ClearVertexCliffFlipTriangulationFlags(const TerrainRect& r) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i) {
            if (vertices_[i].flags & kVertexCliffFlip) {
                FlipInternalTriangulation(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z), false);
                vertices_[i].flags &= static_cast<std::uint16_t>(~kVertexCliffFlip);
            }
        }
    }
}

bool cSTETerrain::IsVertexWithMark1(std::int32_t index) const {
    return (vertices_[static_cast<std::size_t>(index)].flags & kVertexMark1) != 0;
}

bool cSTETerrain::IsLandAndNotMark1(std::int32_t index) const {
    const Vertex& v = vertices_[static_cast<std::size_t>(index)];
    if (v.flags & kVertexMark1) return false;
    return tuning_.seaLevel < v.altitude;   // strictly above the sea (IsLandVertex is <=)
}

bool cSTETerrain::IsVertexWithMark3(std::int32_t index) const {
    return (vertices_[static_cast<std::size_t>(index)].flags & kVertexMark3) != 0;
}

bool cSTETerrain::IsLandAndNotMark3(std::int32_t index) const {
    const Vertex& v = vertices_[static_cast<std::size_t>(index)];
    if (v.flags & kVertexMark3) return false;
    return tuning_.seaLevel < v.altitude;
}

void cSTETerrain::PadRectangle(TerrainRect& r, int amount) const {
    r.left -= amount;
    r.top -= amount;
    r.right += amount;
    r.bottom += amount;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (static_cast<std::uint32_t>(r.right) > cellsX_) r.right = static_cast<std::int32_t>(cellsX_);
    if (static_cast<std::uint32_t>(r.bottom) > cellsZ_) r.bottom = static_cast<std::int32_t>(cellsZ_);
}

void cSTETerrain::CreateVertRectAroundVertex(TerrainRect& r, int x, int z, int radius) const {
    r.left = x - radius;
    if (r.left < 0) r.left = 0;
    std::uint32_t right = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(radius);
    r.right = static_cast<std::int32_t>(cellsX_ < right ? cellsX_ : right);
    r.top = z - radius;
    if (r.top < 0) r.top = 0;
    std::uint32_t bottom = static_cast<std::uint32_t>(z) + static_cast<std::uint32_t>(radius);
    r.bottom = static_cast<std::int32_t>(cellsZ_ < bottom ? cellsZ_ : bottom);
}

// ==================================================================================== water

void cSTETerrain::SaveWaterLevelsInTempArray(TerrainRect& r) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        const std::size_t row = static_cast<std::size_t>(vertsX_) * static_cast<std::uint32_t>(z);
        for (std::int32_t x = r.left; x <= r.right; ++x) {
            const std::size_t i = row + static_cast<std::uint32_t>(x);
            tempWaterLevels_[i] = waterDepth_[i];
        }
    }
}

void cSTETerrain::RestoreWaterLevelsFromTempArray(TerrainRect& r) {
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        const std::size_t row = static_cast<std::size_t>(vertsX_) * static_cast<std::uint32_t>(z);
        for (std::int32_t x = r.left; x <= r.right; ++x) {
            const std::size_t i = row + static_cast<std::uint32_t>(x);
            waterDepth_[i] = tempWaterLevels_[i];
        }
    }
}

float cSTETerrain::GetSedimentValue(float x, float z) const {
    if (!LocationIsInBounds(x, z)) return 0.0f;
    // The original rounds (coordinate / 16 - 0.5) to the nearest integer grid line.
    const std::int32_t gx = RoundToNearestInt(x * 0.0625f - 0.5f);
    const std::int32_t gz = RoundToNearestInt(z * 0.0625f - 0.5f);
    const std::size_t i = static_cast<std::size_t>(gz) * vertsX_ + static_cast<std::size_t>(gx);
    return i < sediment_.size() ? sediment_[i] : 0.0f;   // cSTESurfaceWater vertex +0x1c
}

std::int32_t cSTETerrain::GetNeighborWithLowestWaterLevel(std::int32_t index) const {
    const std::uint32_t z = static_cast<std::uint32_t>(index) / vertsX_;
    const std::uint32_t x = static_cast<std::uint32_t>(index) % vertsX_;
    const std::uint8_t mask = GetNeighbors(static_cast<std::int32_t>(x), static_cast<std::int32_t>(z));
    const float level = vertices_[static_cast<std::size_t>(index)].altitude + waterDepth_[static_cast<std::size_t>(index)];
    std::int32_t best = -1;
    float bestDrop = 0.0f;
    // Straight neighbours first: directions 0, 2 and 4 only (the original skips 6).
    for (std::int32_t dir = 0; dir < 6; dir += 2) {
        if (!((mask >> dir) & 1)) continue;
        const std::size_t n = static_cast<std::size_t>(neighborOffsets_[dir] + index);
        const float drop = level - (vertices_[n].altitude + waterDepth_[n]);
        if (bestDrop < drop) {
            best = dir;
            bestDrop = drop;
        }
    }
    // Then the diagonals, 1, 3 and 5 (skipping 7), compared against sqrt(2) times the best straight drop.
    static const float kRoot2 = 1.4142135f;
    bestDrop *= kRoot2;
    for (std::int32_t dir = 1; dir < 6; dir += 2) {
        if (!((mask >> dir) & 1)) continue;
        const std::size_t n = static_cast<std::size_t>(neighborOffsets_[dir] + index);
        const float drop = level - (vertices_[n].altitude + waterDepth_[n]);
        if (bestDrop < drop) {
            best = dir;
            bestDrop = drop;
        }
    }
    return best;
}

// ==================================================================================== surfaces

void cSTETerrain::ClearSurfaceTextures(const TerrainRect& r) {
    // The three surface-texture layer keys live at vertex +0x16, +0x1a and +0x1e (u16 each); the
    // blend weights at +0x18/+0x1c/+0x20 are left alone.
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        std::size_t i = static_cast<std::size_t>(z) * vertsX_ + static_cast<std::size_t>(r.left);
        for (std::int32_t x = r.left; x <= r.right; ++x, ++i) {
            for (int layer = 0; layer < 3; ++layer) {
                std::uint16_t zero = 0;
                std::memcpy(&vertices_[i].surface[6 + layer * 4], &zero, sizeof zero);
            }
        }
    }
}

void cSTETerrain::UpdateMoistureValues(const TerrainRect& r) {
    // 0x100733d98: `if (+0x140 != null) (+0x140)->vslot(0x80)()`. +0x140 is the city's land-value /
    // moisture simulator (spCity slot 0x230, acquired in Init); that simulator is unrecovered, so the
    // call is a no-op until the seam exists (to-do row TERRAIN-3).
    (void)r;
}

// ============================================================================ brushed regions

void cSTETerrain::ComputeBoundsOfMarkedRegion(TerrainRect& r, std::uint16_t mask) const {
    r = {0x7fffffff, 0x7fffffff, -0x7fffffff - 1, -0x7fffffff - 1};
    for (std::uint32_t z = 0; z < vertsZ_; ++z) {
        std::size_t i = static_cast<std::size_t>(vertsX_) * z;
        for (std::uint32_t x = 0; x < vertsX_; ++x, ++i) {
            if ((vertices_[i].flags & mask) == 0) continue;
            if (static_cast<std::int32_t>(x) < r.left) r.left = static_cast<std::int32_t>(x);
            if (r.right < static_cast<std::int32_t>(x)) r.right = static_cast<std::int32_t>(x);
            if (static_cast<std::int32_t>(z) < r.top) r.top = static_cast<std::int32_t>(z);
            if (r.bottom < static_cast<std::int32_t>(z)) r.bottom = static_cast<std::int32_t>(z);
        }
    }
}

void cSTETerrain::ComputeRectAroundPt(TerrainPoint out[4], float x, float z, float width, float height,
                                      float angle) {
    const float sinAngle = std::sin(angle);
    const float cosAngle = std::cos(angle);
    const float halfW = width * -0.5f;
    const float halfH = height * -0.5f;
    // Corners in the original's order: (-w/2,-h/2), (-w/2,+h/2), (+w/2,+h/2), (+w/2,-h/2).
    const float dx[4] = {halfW, halfW, -halfW, -halfW};
    const float dz[4] = {halfH, -halfH, -halfH, halfH};
    for (int i = 0; i < 4; ++i) {
        const float cornerX = dx[i], cornerZ = dz[i];
        out[i].x = (cosAngle * cornerX - sinAngle * cornerZ) + x;
        out[i].z = (cornerZ * cosAngle + cornerX * sinAngle) + z;
    }
}

void cSTETerrain::ComputeSpansForRectangle(const TerrainPoint* quad, int* spanMin, int* spanMax,
                                           int& firstRow, int& lastRow) const {
    // Row range: the four quad points' z/16, clamped. The original tracks the maximum of only the
    // first three points for its "negative" test (kept quirk), then clamps the fourth-point maximum
    // with an *unsigned* compare, which turns an all-negative quad into "every row".
    std::int32_t minRow = 0x7fffffff, maxRow = -0x7fffffff - 1, maxOfFirstThree = -0x7fffffff - 1;
    for (int k = 0; k < 4; ++k) {
        const std::int32_t row = static_cast<std::int32_t>(quad[k].z * 0.0625f);
        if (maxRow < row) maxRow = row;
        if (row < minRow) minRow = row;
        if (k != 3) maxOfFirstThree = maxRow;
    }
    if (maxOfFirstThree < 0) minRow = 0;
    if (static_cast<std::uint32_t>(maxRow) > cellsZ_) maxRow = static_cast<std::int32_t>(cellsZ_);
    firstRow = minRow;
    lastRow = maxRow;

    for (std::int32_t z = minRow; z <= maxRow; ++z) {
        spanMin[z] = 0x7fffffff;
        spanMax[z] = -0x7fffffff - 1;
        const float rowZ = static_cast<float>(z) * 16.0f;
        std::int32_t rowMax = -0x7fffffff - 1;
        for (int edge = 1; edge != 5; ++edge) {
            const float z0 = quad[edge - 1].z;
            const int next = edge == 4 ? 0 : edge;
            const float z1 = quad[next].z;
            if (!((z0 <= rowZ && rowZ < z1) || (rowZ < z0 && z1 <= rowZ))) continue;
            float x0 = quad[edge - 1].x;
            const float x1 = quad[next].x;
            if (x0 != x1) {
                // (sic) the intersection is truncated to an integer *before* being divided by 16.
                x0 = static_cast<float>(static_cast<std::int32_t>((rowZ - z0) * ((x1 - x0) / (z1 - z0)) + x0));
            }
            const std::int32_t column = static_cast<std::int32_t>(x0 * 0.0625f);
            if (column < spanMin[z]) {
                spanMin[z] = column;
                rowMax = spanMax[z];
            }
            if (rowMax < column) {
                spanMax[z] = column;
                rowMax = column;
            }
        }
        if (spanMin[z] < 0) spanMin[z] = 0;
        std::uint32_t right = static_cast<std::uint32_t>(rowMax + 1);
        spanMax[z] = static_cast<std::int32_t>(right);
        if (static_cast<std::uint32_t>(cellsX_) < right) spanMax[z] = static_cast<std::int32_t>(cellsX_);
    }
}

bool cSTETerrain::ComputeBooleanFnForRect(const TerrainPoint* quad, bool (cSTETerrain::*fn)(int) const,
                                          float fraction) const {
    int firstRow = 0, lastRow = 0;
    int spanMin[260];
    int spanMax[260];
    ComputeSpansForRectangle(quad, spanMin, spanMax, firstRow, lastRow);
    int tested = 0;
    int hits = 0;
    for (std::int32_t z = firstRow; z <= lastRow; ++z) {
        const std::int32_t rowStart = static_cast<std::int32_t>(vertsX_) * z;
        for (std::int32_t x = spanMin[z]; x <= spanMax[z]; ++x) {
            ++tested;
            if ((this->*fn)(rowStart + x)) ++hits;
        }
    }
    return static_cast<int>(static_cast<float>(tested) * fraction) <= hits;
}

// ==================================================================================== brushes

std::map<std::uint32_t, TerrainBrushDescription>& cSTETerrain::BrushDescriptions() {
    static std::map<std::uint32_t, TerrainBrushDescription> map;   // sBrushDescriptionMap
    return map;
}

void cSTETerrain::ReleaseBrushDescriptions() { BrushDescriptions().clear(); }

float cSTETerrain::BrushRadius(std::uint32_t brushType) const {
    float radius = 32.0f;                                        // 0x42000000: the default radius
    const auto& map = BrushDescriptions();
    const auto it = map.find(brushType);
    if (it != map.end()) {
        // The original reads radius[zoom] out of bounds for zoom > 7; the port clamps.
        radius = it->second.radius[zoom_ < 8 ? zoom_ : 7];
    }
    return radius;
}

std::int64_t cSTETerrain::GetCostPerBrushApplication(std::uint32_t brushType) const {
    const auto& map = BrushDescriptions();
    const auto it = map.find(brushType);
    if (it != map.end()) return static_cast<std::int64_t>(it->second.cost[zoom_ < 5 ? zoom_ : 4]);
    return 32;                                                   // 0x20: the default cost
}

bool cSTETerrain::GetUIElementsForBrush(std::uint32_t brushType, TerrainBrushUIElements& out) const {
    const auto& map = BrushDescriptions();
    const auto it = map.find(brushType);
    if (it == map.end()) return false;
    out = it->second.ui;
    return true;
}

bool cSTETerrain::GetBrushExemplarID(std::int32_t brushType, std::uint32_t& exemplarID) const {
    // The table at 0x100a29bf0 (one exemplar id per cISTETerrain::eBrushTypes value).
    static const std::uint32_t kExemplarIDForBrushType[8] = {0xea312ef9, 0xca313035, 0xea312f77, 0x2a312f97,
                                                             0xea313bb9, 0x8a31301c, 0x2a31304e, 0};
    exemplarID = kExemplarIDForBrushType[brushType & 7];
    return brushType != 7;   // eBrushType 7 (none) has no exemplar
}

void cSTETerrain::InitBrushParameters() {
    // Eight brush phases in [0, 2*pi) (+0x248..+0x264), drawn from the class's sRand.
    ITerrainHost* host = TerrainHost();
    if (!host) return;
    for (float& phase : brushPhase_) phase = static_cast<float>(host->RandomDoubleRangeUniform(0.0, 6.2831854820251465));
}

// ==================================================================================== lighting

void cSTETerrain::DoLighting(const TerrainRect& r) {
    ITerrainHost* host = TerrainHost();
    if (!host) return;
    for (std::int32_t z = r.top; z <= r.bottom; ++z) {
        const std::size_t i = static_cast<std::size_t>(vertsX_) * static_cast<std::uint32_t>(z) +
                              static_cast<std::uint32_t>(r.left);
        host->ApplyTerrainLightingRow(z, r.left, (1 - r.left) + r.right, &vertices_[i].normal[0], 0x24,
                                      vertices_[i].surface, 0x24);
    }
}

// =============================================================================== persistence (streams)

void cSTETerrain::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    if (ITerrainHost* host = TerrainHost()) host->WriteGZSerializable(stream, view_ ? view_->Serializable() : nullptr);
    cIGZPersistDBSegment* segment = nullptr;
    void* raw = nullptr;
    if (stream.QueryInterface(kGZIID_cISC4DBSegmentOStream, &raw) && raw) {
        auto* out = static_cast<cISC4DBSegmentOStream*>(raw);
        cISC4DBSegment* dbSegment = out->GetSegment();     // vslot 0xc8
        out->Release();
        if (dbSegment) segment = dbSegment->AsIGZPersistDBSegment();   // vslot 0x18
    }
    Save(segment);
}

bool cSTETerrain::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (2 < version) return false;
    // The view (CLSID 0xc9b84e10, cSTETerrainView3D) is read back into +0x110 here. The port has no
    // adapter from the loaded object to ITerrainView yet (to-do row TERRAIN-VIEW-3), so the view is
    // skipped and the rest of the payload still loads.
    cIGZPersistDBSegment* segment = nullptr;
    void* raw = nullptr;
    if (ITerrainHost* host = TerrainHost()) {
        void* viewObject = nullptr;
        (void)host->ReadGZSerializable(stream, 0xc9b84e10, &viewObject);   // kCLSID_cSTETerrainView3D
    }
    if (stream.QueryInterface(kGZIID_cISC4DBSegmentIStream, &raw) && raw) {
        auto* in = static_cast<cISC4DBSegmentIStream*>(raw);
        cISC4DBSegment* dbSegment = in->GetSegment();
        in->Release();
        if (dbSegment) segment = dbSegment->AsIGZPersistDBSegment();
    }
    Load(segment);
    return true;
}

// =============================================================================== persistence (edge subfiles)

bool cSTETerrain::SaveEdgeAltitudes(int edge, cIGZPersistDBSegment* segment) const {
    // The four edge subfiles share the altitude type id and differ in the instance (edge + 100).
    AutoSave out(segment, kTypeAltitudes, kGroupTerrain, static_cast<std::uint32_t>(edge + 100), 1);
    if (!out.IsOpen()) return false;
    cIGZPersistDBSerialRecord& s = out.Stream();
    if ((edge | 2) == 2) {
        // West (0) and east (2): one column of vertices, stored bottom to top.
        const std::uint32_t rowStride = vertsX_;
        std::uint32_t index = 0;
        std::uint32_t last = rowStride * cellsZ_;
        if (edge == 2) {
            index = cellsX_;
            last += index;
        }
        s.SetFieldUint32(vertsZ_);
        for (; index <= last; index += rowStride) s.SetFieldFloat32(vertices_[index].altitude);
    } else {
        // North (1) and south (3): one row of vertices, left to right.
        std::uint32_t first = 0;
        std::uint32_t last = cellsX_;
        if (edge == 3) {
            first = cellsZ_ * vertsX_;
            last += first;
        }
        s.SetFieldUint32(vertsX_);
        for (std::uint32_t index = first; index <= last; ++index) s.SetFieldFloat32(vertices_[index].altitude);
    }
    return true;
}

bool cSTETerrain::LoadEdgeAltitudes(int edge, cIGZPersistDBSegment* segment, std::vector<float>& out) const {
    AutoLoad in(segment, kTypeAltitudes, kGroupTerrain, static_cast<std::uint32_t>(edge + 100));
    if (!in.IsOpen() || in.Version() != 1) return false;
    std::uint32_t count = 0;
    in.Stream().GetFieldUint32(count);
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) in.Stream().GetFieldFloat32(out[i]);
    return true;
}

void cSTETerrain::SaveObliterated(cIGZPersistDBSegment* segment) {
    ClearVertexPropertyFlags(gridRect_, 0xffe7);
    Save(segment);
}

void cSTETerrain::LoadObliterated(cIGZPersistDBSegment* segment) { Load(segment); }

}  // namespace sc4
