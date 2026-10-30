#include "sc4/LotDeveloper.h"

#include <cfloat>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>

#include "gz/StreamOps.h"
#include "sc4/DBSegment.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/Lot.h"
#include "sc4/LotBaseTextureOccupant.h"
#include "sc4/OccupantManager.h"
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

LotCellRect cSC4LotDeveloper::RectOfLot(const cSC4Lot& lot) {
    return {lot.RectByte(0), lot.RectByte(1), lot.RectByte(2), lot.RectByte(3)};
}
bool cSC4LotDeveloper::IsLotDeveloped(const cSC4Lot& lot) const { return IsAreaDeveloped(RectOfLot(lot)); }
bool cSC4LotDeveloper::IsLotFlat(const cSC4Lot& lot, bool includeWater) const { return IsAreaFlat(RectOfLot(lot), includeWater); }
float cSC4LotDeveloper::GetLotAltitudeRange(const cSC4Lot& lot, bool includeWater) const {
    return GetAreaAltitudeRange(RectOfLot(lot), includeWater);
}
float cSC4LotDeveloper::GetLotSlope(const cSC4Lot& lot, bool includeWater) const { return GetAreaSlope(RectOfLot(lot), includeWater); }
float cSC4LotDeveloper::GetOccupantSlope(void* occupant, bool includeWater) const {
    LotCellRect area;
    GetOccupantCellCoverage(occupant, area);
    return GetAreaSlope(area, includeWater);
}

std::uint32_t cSC4LotDeveloper::TestCondition(cSC4Lot& lot) const {
    static const std::int32_t kStepDown[4] = {1, 1, 2, 3};                   // 0x100a18ec0
    const int state = lot.GetState();
    std::uint32_t condition = lot.GetCondition();
    std::int32_t grade;
    if (state == 4) {
        if (condition == 0) condition = 1;
        if (static_cast<std::uint32_t>(lot.GetDaysSinceLastStateChange()) <= tunables_.conditionThreshold) return condition;
        if (3 < condition) return condition;
        grade = kStepDown[condition];
    } else {
        if (state != 3) return condition;
        const BuildingProfile* profile = gates_ ? gates_->ProfileOfBuilding(lot.GetBuildingType(false)) : nullptr;
        std::int32_t gap = static_cast<std::int32_t>(profile ? profile->wealth : 0) - static_cast<std::int32_t>(lot.GetOccupantWealth());
        const bool behind = gap < static_cast<std::int32_t>(condition - 1);
        if (behind || tunables_.conditionThreshold < static_cast<std::uint32_t>(lot.GetDaysSinceLastStateChange())) {
            const float occupancy = lot.GetOccupancy();
            if (occupancy < tunables_.ratioB || (behind && occupancy < tunables_.ratioC)) gap = gap + 1;
        }
        grade = 2;
        if (gap < 3) grade = gap;
        if (grade == 0) {
            std::int32_t counter = 0;
            if (!lot.Properties().HasProperty(kLotPropFailureCounter)) return 0;
            cSCPropertyHelp::GetPropertyValue(&lot.Properties(), kLotPropFailureCounter, counter);
            return counter != 0 ? 1u : 0u;
        }
        if (grade < 0) return condition;
    }
    return static_cast<std::uint32_t>(grade + 1);
}

}  // namespace sc4


// ==================================================================================================== lot base textures

namespace sc4 {
namespace {
ILotBaseTextureServices* g_textureServices = nullptr;
constexpr std::uint32_t kLotBaseTextureOccupantType = 0x497f6d9d;
constexpr std::uint32_t kOccupantInsertedFlag = 1;                 // cISC4Occupant vslot 0x100 IsFlagSet(1)
constexpr std::uint32_t kMsgLotBaseTextureChanged = 0x99ef1145;    // occupant vslot 0x78 after a move
}  // namespace

void cSC4LotDeveloper::SetBaseTextureServices(ILotBaseTextureServices* services) { g_textureServices = services; }

// @ 0x100145e4c. The lot's texture occupant at its anchor cell (occupant manager vslot 0x100 by type 0x497f6d9d); with
// `create` a new one comes from GZCOM (or new), is initialised and set to usage 2. The caller gets one reference.
bool cSC4LotDeveloper::GetLotBaseTextureOccupant(cSC4Lot* lot, cISC4LotBaseTextureOccupant*& out, bool create) {
    out = nullptr;
    ILotBaseTextureServices* services = g_textureServices;
    cISC4OccupantManager* occupants = services ? services->OccupantManager() : nullptr;
    if (!lot || !occupants) return false;
    int x = 0, z = 0;
    lot->GetLocation(x, z);                                                   // lot vslot 0xa8
    cISC4Occupant* found = nullptr;
    if (occupants->GetFirstOccupantByStandardCityCell(found, x, z, kLotBaseTextureOccupantType) && found) {
        void* raw = nullptr;
        if (found->QueryInterface(kGZIID_cISC4LotBaseTextureOccupant, &raw) && raw) {
            out = static_cast<cISC4LotBaseTextureOccupant*>(raw);
            out->SetUsage(2);                                                 // vslot 0x28
        }
        found->Release();
    } else if (create) {
        out = services->CreateBaseTextureOccupant();
        if (out) {
            out->AsOccupant()->Init();                                        // vslot 0x18 -> occupant vslot 0x18
            out->SetUsage(2);
        }
    }
    return out != nullptr;
}

// @ 0x100146066. An empty zoned lot's base texture: one specification per cell, the zone's drag colour (RGB) with the zone
// display alpha and combine mode 3, and the edge texture the boundary rules pick from which sides of the cell face the lot's
// own cells (0), its front (2, the side it faces) or anything else (1). Zone types 0, 11, 12 and 15 carry no texture.
// Landfill / airport / seaport zones (7..9) whose lot reports the (1, 1) pair (lot vslot 0xb0) take plain cells.
void cSC4LotDeveloper::SetEmptyLotBaseTexture(cSC4Lot* lot) {
    ILotBaseTextureServices* services = g_textureServices;
    if (!lot || !services) return;
    const std::uint32_t zoneType = lot->GetZoneType();                       // lot vslot 0xf8
    if (zoneType < 0x10 && ((0x9801u >> zoneType) & 1u) != 0) {
        RemoveLotBaseTexture(lot);
        return;
    }
    const std::uint32_t color = services->ZoneDragColor(static_cast<int>(zoneType));
    const std::uint8_t red = static_cast<std::uint8_t>(color >> 24), green = static_cast<std::uint8_t>(color >> 16);
    const std::uint8_t blue = static_cast<std::uint8_t>(color >> 8), alpha = services->ZoneDisplayAlpha();
    const int cellsX = services->CityCellsX(), cellsZ = services->CityCellsZ();
    std::int32_t rect[4] = {};
    lot->GetBoundingRect(rect);                                               // lot vslot 0x70: x0, z0, x1, z1
    // PARTIAL: lot vslot 0xb0 (the landfill / port pair) is not modelled by the port's cSC4Lot, so zones 7..9 take edges too.
    const bool edges = true;
    const std::uint8_t facing = lot->GetFacing();                             // lot vslot 0xb8
    std::vector<LotBaseTextureSpecification> specs;
    for (int x = rect[0]; x <= rect[2]; ++x) {
        for (int z = rect[1]; z <= rect[3]; ++z) {
            if (x >= cellsX || z >= cellsZ || x < 0 || z < 0) continue;      // (unsigned) +0x88 / +0x84
            int sides[4] = {1, 1, 1, 1};
            if (!edges) {
                sides[0] = sides[1] = sides[2] = sides[3] = 0;
            } else {
                auto neighbour = [&](int nx, int nz) { return services->LotAtCell(nx, nz); };
                cSC4Lot* north = (z >= 1 && z <= cellsZ) ? neighbour(x, z - 1) : nullptr;
                if (north == lot) sides[0] = 0;
                else if (facing == 0) sides[0] = 2;
                cSC4Lot* south = (z >= -1 && z < cellsZ - 1) ? neighbour(x, z + 1) : nullptr;
                if (south == lot) sides[2] = 0;
                else if (facing == 2) sides[2] = 2;
                cSC4Lot* west = (x <= cellsX && x >= 1) ? neighbour(x - 1, z) : nullptr;
                if (west == lot) sides[3] = 0;
                else if (facing == 3) sides[3] = 2;
                cSC4Lot* east = (x < cellsX - 1 && x >= -1) ? neighbour(x + 1, z) : nullptr;
                if (east == lot) sides[1] = 0;
                else if (facing == 1) sides[1] = 2;
            }
            LotBaseTextureSpecification spec;
            spec.cellX = static_cast<std::uint8_t>(x);
            spec.cellZ = static_cast<std::uint8_t>(z);
            bool flip = false;
            services->BoundaryLotTexture(sides, spec.textureId, spec.rotation, flip);   // this vslot 0x150
            spec.flip = flip ? 1 : 0;
            spec.red = red;
            spec.green = green;
            spec.blue = blue;
            spec.alpha = alpha;
            spec.combineMode = 3;
            spec.frame = 0xff;
            specs.push_back(spec);
        }
    }
    cISC4LotBaseTextureOccupant* texture = nullptr;
    if (GetLotBaseTextureOccupant(lot, texture, true)) {                      // this vslot 0x148
        texture->SetTextureSpecification(specs.data(), static_cast<std::uint32_t>(specs.size()), nullptr);   // vslot 0x40
        cISC4Occupant* occupant = texture->AsOccupant();
        cISC4OccupantManager* occupants = services->OccupantManager();
        if (occupants) {
            if (!occupant->IsFlagSet(kOccupantInsertedFlag)) {
                occupants->InsertOccupant(occupant, 0);                       // vslot 0xa0
            } else {
                occupants->MoveOccupant(occupant, false);                     // vslot 0xc0
                occupant->PostOccupantMessage(kMsgLotBaseTextureChanged, 0);
            }
        }
        texture->Release();
    }
}

// @ 0x10014652e (with GetLotBaseTextures @ 0x100146b10 and RemoveAndReleaseOccupants @ 0x100146b72): every texture
// occupant over the lot's cells leaves the occupant manager, is shut down and released.
void cSC4LotDeveloper::RemoveLotBaseTexture(cSC4Lot* lot) {
    ILotBaseTextureServices* services = g_textureServices;
    cISC4OccupantManager* occupants = services ? services->OccupantManager() : nullptr;
    if (!lot || !occupants) return;
    std::int32_t rect[4] = {};
    lot->GetBoundingRect(rect);
    OccupantList list;
    if (!occupants->GetOccupantsByStandardCityCells(list, rect, rect + 2, kLotBaseTextureOccupantType, 0xffffffffu)) return;
    for (cISC4Occupant* occupant : list) {
        if (occupant->IsFlagSet(kOccupantInsertedFlag)) occupants->RemoveOccupant(occupant, false, 0);   // vslot 0xa8
        occupant->Shutdown();                                                 // vslot 0x20
        occupant->Release();
    }
}

}  // namespace sc4
