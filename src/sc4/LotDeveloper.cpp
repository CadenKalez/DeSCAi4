#include "sc4/LotDeveloper.h"

#include <cfloat>
#include <cmath>
#include <vector>

#include "gz/StreamOps.h"
#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kGZIID_cISC4BuildingOccupantQueue = 0x87dda3a9;   // the reference iid of the queue entries (0x100140242)
constexpr std::uint32_t kMaxStreamedQueue = 1u << 20;
}  // namespace

// 0x10013eda8 — the class answers its own interface (0x4818e5d2) and cIGZUnknown at +0, the message
// target 2 (0x452294aa) at +8 and the serializable (0xe4fda3d4) at +0x10. The class id 0xa990bfe0 is
// not answered (as in the original).
bool cSC4LotDeveloper::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == kGZIID_cIGZMessageTarget2) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4LotDeveloper) *out = static_cast<cISC4LotDeveloper*>(this);
    else return false;
    AddRef();
    return true;
}

// 0x10013ee38
std::uint32_t cSC4LotDeveloper::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

// The original's DoMessage drives construction/demolition; that stage machine is unrecovered.
bool cSC4LotDeveloper::DoMessage(cIGZMessage2*) {
    return false;
}

// 0x10014009a: version 1, the vertices per row, the city extent, the construction queue (one serializable reference each), the
// position of the building in progress, the stage byte and a flag.
bool cSC4LotDeveloper::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    stream.SetSint32(streamVerticesPerRow_);
    stream.SetFloat32(streamCityWidth_);
    stream.SetFloat32(streamCityDepth_);
    stream.SetUint32(static_cast<std::uint32_t>(constructionQueue_.size()));
    for (const cIGZUnknown* occupant : constructionQueue_) SC4WriteGZSerializable(stream, occupant);
    stream.SetUint32(queueCursor_);
    stream.SetUint8(static_cast<std::uint8_t>(stage_));
    stream << flagF4_;
    return !stream.GetError();
}

// 0x100140242
bool cSC4LotDeveloper::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    stream.GetSint32(streamVerticesPerRow_);
    stream.GetFloat32(streamCityWidth_);
    stream.GetFloat32(streamCityDepth_);
    std::uint32_t count = 0;
    stream.GetUint32(count);
    for (cIGZUnknown* occupant : constructionQueue_) if (occupant) occupant->Release();
    constructionQueue_.clear();
    if (count > kMaxStreamedQueue) return false;
    constructionQueue_.resize(count, nullptr);
    for (cIGZUnknown*& slot : constructionQueue_) {
        void* raw = nullptr;
        SC4ReadGZSerializable(stream, kGZIID_cISC4BuildingOccupantQueue, &raw, false);   // one reference
        slot = static_cast<cIGZUnknown*>(raw);
    }
    stream.GetUint32(queueCursor_);
    std::uint8_t stage = 0;
    stream.GetUint8(stage);
    stage_ = stage;
    stream >> flagF4_;
    return !stream.GetError();
}

void cSC4LotDeveloper::ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar) {
    if (!exemplar) return;
    LotDeveloperTunables& t = tunables_;
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropConditionThreshold, t.conditionThreshold);
    t.ratioA = 0.2f;
    t.ratioB = 0.4f;
    t.ratioC = 0.6f;
    std::vector<float> ratios;
    if (exemplar->HasProperty(kLotDevPropConditionRatios) &&
        cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropConditionRatios, ratios) && ratios.size() >= 3) {
        t.ratioA = ratios[0];
        t.ratioB = ratios[1];
        t.ratioC = ratios[2];
    }
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropFlatTolerance, t.flatTolerance);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropFloat12c, t.value12c);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropU32_130, t.value130);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropU32_134, t.value134);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropBool138, t.flag138);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropBool139, t.flag139);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropLevelTolerance, t.levelTolerance);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropMaxLevelling, t.maxLevelling);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropU32_0f0, t.value0f0);
    cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropU32_0ec, t.value0ec);
    std::vector<float> curve;
    if (exemplar->HasProperty(kLotDevPropCostCurve) &&
        cSCPropertyHelp::GetPropertyValue(exemplar, kLotDevPropCostCurve, curve))
        developmentCostCurve_.SetResponseCurveData(curve.data(), curve.size());
}

float cSC4LotDeveloper::GetTerrainAltitudeByVertex(int vx, int vz, bool includeWater) const {
    const int index = vx + vz * host_->VerticesPerRow();
    const float height = host_->VertexHeight(index);
    if (includeWater) {
        const float water = host_->VertexWaterHeight(index);
        if (height < water) return water;
    }
    return height;
}

// Every vertex of the cell rectangle: the vertex grid is one larger than the cell grid in both directions.
float cSC4LotDeveloper::GetLowestOrHighestAreaAltitude(const LotCellRect& area, bool lowest, bool includeWater) const {
    float best = lowest ? FLT_MAX : -FLT_MAX;
    for (int x = area.x0; x <= area.x1 + 1; ++x) {
        for (int z = area.z0; z <= area.z1 + 1; ++z) {
            const float h = GetTerrainAltitudeByVertex(x, z, includeWater);
            if (lowest ? h < best : best < h) best = h;
        }
    }
    return best;
}

// Metre rectangle: the interior vertices (16 m grid) first, then the four edges at 4 m steps ending exactly on
// the far edge. The rectangle is clamped to [0, cityExtent - 0.1].
float cSC4LotDeveloper::GetLowestOrHighestAreaAltitude(const LotMetreRect& area, bool lowest, bool includeWater) const {
    const float left = area.x0 >= 0.0f ? area.x0 : 0.0f;
    const float top = area.z0 >= 0.0f ? area.z0 : 0.0f;
    const float cityX = host_->CityWidthMetres(), cityZ = host_->CityDepthMetres();
    const float right = area.x1 < cityX ? area.x1 : cityX - 0.1f;
    const float bottom = area.z1 < cityZ ? area.z1 : cityZ - 0.1f;
    float best = lowest ? FLT_MAX : -FLT_MAX;
    auto take = [&](float h) {
        if (lowest ? h < best : best < h) best = h;
    };
    auto sample = [&](float x, float z) {
        float h = host_->HeightAt(x, z);
        if (includeWater) {
            const float water = host_->WaterHeightAt(x, z);
            if (water > h) h = water;
        }
        take(h);
    };
    const int vx0 = static_cast<int>((left + 16.0f) * 0.0625f), vx1 = static_cast<int>((right + 0.0f) * 0.0625f);
    const int vz0 = static_cast<int>((top + 16.0f) * 0.0625f), vz1 = static_cast<int>((bottom + 0.0f) * 0.0625f);
    for (int vx = vx0; vx <= vx1; ++vx)
        for (int vz = vz0; vz <= vz1; ++vz) take(GetTerrainAltitudeByVertex(vx, vz, includeWater));
    // walk one edge in 4 m steps; the step that would reach or pass the far end is replaced by the end itself
    auto walk = [&](float from, float to, auto&& emit) {
        float v = from;
        while (v < to) {
            emit(v);
            const float next = v + 4.0f;
            if (to <= next) {
                emit(to);
                break;
            }
            v = next;
        }
    };
    walk(left, right, [&](float x) { sample(x, top); });
    walk(top, bottom, [&](float z) { sample(right, z); });
    walk(left, right, [&](float x) { sample(x, bottom); });
    walk(top, bottom, [&](float z) { sample(left, z); });
    return best;
}

float cSC4LotDeveloper::GetAverageAreaAltitude(const LotCellRect& area, bool includeWater) const {
    float sum = 0.0f;
    int count = 0;
    for (int x = area.x0; x <= area.x1 + 1; ++x) {
        for (int z = area.z0; z <= area.z1 + 1; ++z) {
            sum += GetTerrainAltitudeByVertex(x, z, includeWater);
            ++count;
        }
    }
    return sum / static_cast<float>(count);   // 0 / 0 = NaN for an empty rectangle, as in the original
}

// Interior vertices only (x0+1..x1, z0+1..z1). A negative height means "average of the area".
int cSC4LotDeveloper::LevelAreaInternalVertices(const LotCellRect& area, float height, bool skipLevelled, bool force,
                                                void* lot) {
    float target = height;
    if (height < 0.0f) target = GetAverageAreaAltitude(area, true);
    int changed = 0;
    for (int x = area.x0 + 1; x <= area.x1; ++x) {
        for (int z = area.z0 + 1; z <= area.z1; ++z) {
            if (skipLevelled && host_->VertexIsLevelled(x, z)) continue;
            const float current = GetTerrainAltitudeByVertex(x, z, true);
            if (force || (current - target > tunables_.levelTolerance)) {
                host_->LevelVertices(target, true, true, x, z, x, z, lot, 5, 0);
                ++changed;
            }
        }
    }
    return changed;
}

int cSC4LotDeveloper::LevelBorderingVertices(const LotCellRect& area, float height, void* lot) {
    return host_->LevelVertices(height, true, true, area.x0, area.z0, area.x1 + 1, area.z1 + 1, lot, 5, 0);
}

bool cSC4LotDeveloper::AttemptAreaLevelling(const LotCellRect& area, float /*unusedHeight*/, bool includeWater,
                                            bool flag, float maxSlope, void* lot) {
    const float target = GetAverageAreaAltitude(area, includeWater);
    if (maxSlope == kLotMaxLevelling) maxSlope = tunables_.maxLevelling;
    if (maxSlope < 1000.0f && maxSlope < GetAreaSlope(area, includeWater)) return false;
    return host_->LevelVertices(target, true, flag, area.x0, area.z0, area.x1 + 1, area.z1 + 1, lot, 5, 0) != 0;
}

void cSC4LotDeveloper::GetOccupantCellCoverage(void* occupant, LotCellRect& out) const {
    float lo[3] = {}, hi[3] = {};
    host_->OccupantBounds(occupant, lo, hi);
    lo[0] += 0.05f;
    lo[2] += 0.05f;
    hi[0] -= 0.05f;
    hi[2] -= 0.05f;
    host_->CellOfPosition(lo[0], lo[2], out.x0, out.z0);
    host_->CellOfPosition(hi[0], hi[2], out.x1, out.z1);
}

bool cSC4LotDeveloper::IsAreaFlat(const LotCellRect& area, bool includeWater) const {
    const float reference = GetTerrainAltitudeByVertex(area.x0, area.z0, true);   // always with water, as written
    for (int x = area.x0; x <= area.x1 + 1; ++x) {
        for (int z = area.z0; z <= area.z1 + 1; ++z) {
            const float h = GetTerrainAltitudeByVertex(x, z, includeWater);
            if (std::fabs(h - reference) > tunables_.flatTolerance) return false;
        }
    }
    return true;
}

float cSC4LotDeveloper::GetAreaAltitudeRange(const LotCellRect& area, bool includeWater) const {
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (int x = area.x0; x <= area.x1 + 1; ++x) {
        for (int z = area.z0; z <= area.z1 + 1; ++z) {
            const float h = GetTerrainAltitudeByVertex(x, z, includeWater);
            if (h < lo) lo = h;
            if (hi < h) hi = h;
        }
    }
    return hi - lo;
}

// "Slope" is the horizontal distance (in vertex units) between the lowest and the highest vertex found.
float cSC4LotDeveloper::GetAreaSlope(const LotCellRect& area, bool includeWater) const {
    float lo = FLT_MAX, hi = -FLT_MAX;
    float loPos[2] = {}, hiPos[2] = {};
    for (int x = area.x0; x <= area.x1 + 1; ++x) {
        for (int z = area.z0; z <= area.z1 + 1; ++z) {
            const float h = GetTerrainAltitudeByVertex(x, z, includeWater);
            if (h < lo) {
                loPos[0] = static_cast<float>(x);
                loPos[1] = static_cast<float>(z);
                lo = h;
            }
            if (hi < h) {
                hiPos[0] = static_cast<float>(x);
                hiPos[1] = static_cast<float>(z);
                hi = h;
            }
        }
    }
    const float dx = hiPos[0] - loPos[0], dz = hiPos[1] - loPos[1];
    return std::sqrt(dz * dz + dx * dx);
}

std::int64_t cSC4LotDeveloper::GetLotDevelopmentCost(const LotCellRect& area) const {
    const float slope = GetAreaSlope(area, true);
    const float perCell = developmentCostCurve_.GetOutputValue(slope * 0.0625f);
    return static_cast<std::int64_t>(static_cast<float>((area.z1 + 1) - area.z0) *
                                     static_cast<float>((area.x1 + 1) - area.x0) * perCell);
}

}  // namespace sc4
