#include "sc4/cSTETerrain.h"

#include <algorithm>
#include <cmath>

#include "gz/cIGZPersistRecord.h"
#include "gz/cIGZPersistDBSerialRecord.h"

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
    vertices_.assign(totalVerts_, Vertex{});
    waterDepth_.assign(totalVerts_, 0.0f);
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
    // (the four SaveEdgeAltitudes subfiles are not recovered yet)
    return true;
}

}  // namespace sc4
