#include "sc4/LotRetainingWallModel.h"

#include <cmath>
#include <cstring>

#include "sc4/DrawContext.h"
#include "sc4/Lot.h"

namespace sc4 {

namespace {

ILotRetainingWallModelHost* g_host = nullptr;

// The float-to-int conversion of the original: add the 2^23 magic constant, take the bit pattern and subtract its
// integer value. That rounds to nearest (ties to even) and, unlike a C cast, works the same for negatives.
// 12582912.0f is 0x4b400000 (the "addl $0xb4c00000" after the add in SetupVerticesFromBBoxAndWallEdge).
std::int32_t RoundToInt(float value) {
    const float biased = value + 12582912.0f;
    std::int32_t bits = 0;
    std::memcpy(&bits, &biased, sizeof bits);
    return bits - 0x4b400000;
}

cSC4PackedRenderState g_packedRenderState;
bool g_staticInitialized = false;

}  // namespace

ILotRetainingWallModelHost* LotRetainingWallModelHost() { return g_host; }
void SetLotRetainingWallModelHost(ILotRetainingWallModelHost* host) { g_host = host; }

const cSC4PackedRenderState& LotRetainingWallPackedRenderState() { return g_packedRenderState; }
bool& LotRetainingWallStaticInitialized() { return g_staticInitialized; }

// -------------------------------------------------------------------------------------------- cSC4LotRetainingWallModel

cSC4LotRetainingWallModel::cSC4LotRetainingWallModel() {   // @ 0x1001671ac
    // the two boxes start as the engine's "empty" box (min {1,1,1} / max {0,0,0}), like cS3DModelInstance's bounds
    localMin_ = cS3DVector3{1.0f, 1.0f, 1.0f};
    localMax_ = cS3DVector3{0.0f, 0.0f, 0.0f};
    worldMin_ = cS3DVector3{1.0f, 1.0f, 1.0f};
    worldMax_ = cS3DVector3{0.0f, 0.0f, 0.0f};
}

cSC4LotRetainingWallModel::~cSC4LotRetainingWallModel() {   // @ 0x100167284
    if (binding_) {
        binding_->Release();   // cS3DTextureBinding::Release @ 0x1000d9bfa
        binding_ = nullptr;
    }
    // the vertex vector the original frees by hand at 0x1001672cf..0x1001672e2 is a plain member here
}

bool cSC4LotRetainingWallModel::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1001673d4
    if (iid == kGZIID_cISC4LotRetainingWallModel) {
        AddRef();
        *out = static_cast<cISC4LotRetainingWallModel*>(this);
        return true;
    }
    return cS3DModelInstanceBasic::QueryInterface(iid, out);   // the base subobject at +8
}

cISC4LotRetainingWallModel* cSC4LotRetainingWallModel::GetWallModel() {   // @ 0x100168850
    return this;
}

std::uint32_t cSC4LotRetainingWallModel::AddRef() {   // @ 0x100168834
    return cS3DModelInstanceBasic::AddRef();
}

std::uint32_t cSC4LotRetainingWallModel::Release() {   // @ 0x100168842
    return cS3DModelInstanceBasic::Release();
}

void cSC4LotRetainingWallModel::GetWorldBounds(cS3DBoundingBox& out) {   // @ 0x100167452
    out.min[0] = worldMin_.x;
    out.min[1] = worldMin_.y;
    out.min[2] = worldMin_.z;
    out.max[0] = worldMax_.x;
    out.max[1] = worldMax_.y;
    out.max[2] = worldMax_.z;
}

bool cSC4LotRetainingWallModel::Pick(cSC4PickContext* context) {   // @ 0x1001674ac
    (void)context;
    return false;
}

std::uint32_t cSC4LotRetainingWallModel::TextureId() const {
    if (!binding_) return 0;
    return binding_->TextureId();
}

void cSC4LotRetainingWallModel::GetSortValue(std::uint32_t& out) const {   // @ 0x1001674c4
    if (!binding_) {
        out = kRetainingWallSortValueNoTexture;
        return;
    }
    out = binding_->TextureId() | kRetainingWallSortValueMask;
}

// ----------------------------------------------------------------------------------------------------------- drawing

int cSC4LotRetainingWallModel::Draw(SC4DrawContext* context, bool highlight) {   // @ 0x100167510
    if (highlight) return 1;
    const int zoom = context->Zoom();
    if (static_cast<std::uint8_t>(zoom) != zoom_) DoZoomChange(zoom_, zoom);
    if (static_cast<std::uint8_t>(context->ViewRotation()) != rotation_) {
        rotation_ = static_cast<std::uint8_t>(context->ViewRotation());
        bool vertical = false;
        // The build path is not re-run: the original only recolours the existing vertices for a new view rotation
        // (SetupVertices(false, true, ...), 0x10016754e..0x10016756d).
        SetupVertices(false, true, vertical, nullptr, nullptr, nullptr);
    }
    if (segmentsBuilt_ == 0) return 1;
    // Only the two edges that face the camera are drawn.
    const int rotation = context->ViewRotation();
    const int edge = static_cast<signed char>(wallEdge_);
    if (edge != ((rotation + 3) % 4) && edge != ((rotation + 4) % 4)) return 1;
    if (!binding_) return 1;
    const std::uint32_t texture = binding_->TextureId();
    context->ResetModelViewTransform();
    context->SetRenderState(g_packedRenderState);
    context->SetTexture(static_cast<int>(texture), 0);
    context->DrawPrims(0, 0x0a, static_cast<int>(vertices_.size()), vertices_.data());
    return 1;
}

void cSC4LotRetainingWallModel::DoZoomChange(int oldZoom, int newZoom) {   // @ 0x10016762a
    zoom_ = static_cast<std::uint8_t>(newZoom);
    if (!g_staticInitialized) {
        g_staticInitialized = true;
        SetupPackedRenderState();
    }
    SetupTextures(oldZoom, newZoom);
}

void cSC4LotRetainingWallModel::SetupPackedRenderState() {   // @ 0x100167692
    // The original reads SL::spRenderer and calls vslot 0x50 (cSC43DRender::GetDrawContext) for no reason.
    if (g_host) g_host->TouchRendererDrawContext();
    g_packedRenderState.PackRenderState(true, true, true, false, false, 1, false, 3, -1, -1, -1, -1.0f, -1, -1);
}

void cSC4LotRetainingWallModel::SetupTextures(int oldZoom, int newZoom) {   // @ 0x1001676f8
    if (oldZoom == newZoom) return;
    ILotRetainingWallTextures* factory = g_host ? g_host->Textures() : nullptr;
    if (binding_) {
        binding_->Release();
        binding_ = nullptr;
    }
    if (!factory) return;   // the original dereferences the framework service without a null check
    const auto index = static_cast<std::size_t>(static_cast<std::int32_t>(newZoom));
    if (textureIds_[index] == 0) return;
    SC4TextureBindingKey key{kRetainingWallTextureType, kRetainingWallTextureGroup, textureIds_[index]};
    binding_ = factory->CreateTexture(key);
    if (!binding_) {
        // Quirk kept: a texture id that does not bind is bumped by a fixed step and retried once.
        textureIds_[index] += kRetainingWallTextureRetryStep;
        key.instance = textureIds_[index];
        binding_ = factory->CreateTexture(key);
    }
}

void cSC4LotRetainingWallModel::SetTextureData(const std::uint32_t* textureIds, float scale) {   // @ 0x100168808
    for (int i = 0; i < 5; ++i) textureIds_[i] = textureIds[i];
    textureScale_ = scale;
}

// ------------------------------------------------------------------------------------------------------------ geometry

void cSC4LotRetainingWallModel::SetupVerticesFromBBoxAndWallEdge(const cS3DBoundingBox& bounds, int wallEdge,
                                                                 SC4PointInt& start, SC4PointInt& end) {   // @ 0x10016872a
    // metres -> cells, rounded to nearest with the original's bias (0.1 for the minima, -0.9 for the maxima)
    const std::int32_t x0 = RoundToInt(bounds.min[0] * kRetainingWallUvScale + 0.1f);
    const std::int32_t z0 = RoundToInt(bounds.min[2] * kRetainingWallUvScale + 0.1f);
    const std::int32_t x1 = RoundToInt(bounds.max[0] * kRetainingWallUvScale - 0.9f);
    const std::int32_t z1 = RoundToInt(bounds.max[2] * kRetainingWallUvScale - 0.9f);
    switch (wallEdge) {
        case 1:
            start.x = x1 + 1;
            start.z = z0;
            end.x = x1 + 1;
            end.z = z1 + 1;
            break;
        case 2:
            start.x = x0;
            start.z = z1 + 1;
            end.x = x1 + 1;
            end.z = z1 + 1;
            break;
        case 3:
            start.x = x0;
            start.z = z0;
            end.x = x0;
            end.z = z1 + 1;
            break;
        default:   // edge 0 (and every other value, the original's switch falls through to it)
            start.x = x0;
            start.z = z0;
            end.x = x1 + 1;
            end.z = z0;
            break;
    }
}

bool cSC4LotRetainingWallModel::SegmentIsAdjacentToFlatLot(int x, int z, ILotRetainingWallLotManager* lotManager,
                                                           ILotRetainingWallTerrain* terrain) {   // @ 0x100168638
    if (!terrain) terrain = g_host ? g_host->Terrain() : nullptr;
    if (!lotManager) lotManager = g_host ? g_host->LotManager() : nullptr;
    if (!terrain || !lotManager) return false;
    const int countX = terrain->CellCountX();
    const int countZ = terrain->CellCountZ();
    const int edge = static_cast<signed char>(wallEdge_);
    if (edge == 3) {
        --x;
    } else if (edge == 0) {
        --z;
    }
    if (z >= countZ) return false;
    if (x >= countX) return false;
    if ((x | z) < 0) return false;
    cSC4Lot* lot = lotManager->LotAtCell(x, z);
    if (!lot) return false;
    return (lot->GetFlags() & 1u) != 0;
}

bool cSC4LotRetainingWallModel::Init(const cS3DBoundingBox& bounds, int wallEdge, bool& changed) {   // @ 0x1001686d4
    built_ = 1;
    wallEdge_ = static_cast<std::int8_t>(wallEdge);
    SetupVerticesFromBBoxAndWallEdge(bounds, wallEdge, wallStart_, wallEnd_);
    SetupVertices(true, true, changed, nullptr, nullptr, nullptr);
    return true;   // the original answers true whatever the geometry build found (0x10016871f)
}

bool cSC4LotRetainingWallModel::SetupVertices(bool rebuild, bool recolor, bool& changed,
                                              ILotRetainingWallLotManager* lotManager,
                                              ILotRetainingWallTerrain* terrain,
                                              ILotRetainingWallLighting* lighting) {   // @ 0x1001677ea
    auto finish = [&](bool result) {
        if (recolor) ApplyLighting(lighting);
        return result;
    };
    if (!rebuild) return finish(false);

    segmentsBuilt_ = 0;
    if (!lotManager) lotManager = g_host ? g_host->LotManager() : nullptr;
    if (!terrain) terrain = g_host ? g_host->Terrain() : nullptr;
    if (!lotManager || !terrain) return finish(false);

    const int edge = static_cast<signed char>(wallEdge_);
    const bool xAligned = (edge | 2) == 2;   // edges 0 / 2 run along x, edges 1 / 3 along z

    // The lot whose altitude the wall is built against: the cell just outside the wall on the far side.
    int lotCellX = wallStart_.x;
    int lotCellZ = wallStart_.z;
    if (edge == 2) {
        --lotCellZ;
    } else if (edge == 1) {
        --lotCellX;
    }
    cSC4Lot* lot = lotManager->LotAtCell(lotCellX, lotCellZ);
    if (!lot) return finish(false);

    const float lotAltitude = lot->GetAltitude();
    float nearHeight = terrain->AltitudeAtVertex(wallStart_.x, wallStart_.z);   // the near end of the first cell
    // The original keeps the two world coordinates in xmm11 (x0 << 4) and xmm7 (z0 << 4) and advances the one the
    // edge runs along: x for edges 0 / 2, z for edges 1 / 3 (0x1001681a0..0x1001681b8).
    float worldX = static_cast<float>(wallStart_.x << 4);
    float worldZ = static_cast<float>(wallStart_.z << 4);
    int varying = xAligned ? wallStart_.x : wallStart_.z;
    const int fixedCell = xAligned ? wallStart_.z : wallStart_.x;
    const int end = xAligned ? wallEnd_.x : wallEnd_.z;

    float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
    // The along axis starts as an empty +FLT_MAX / -FLT_MAX range (0x7f7fffff / 0xff7fffff at 0x100a161e0 /
    // 0x100a169a0); the fixed axis starts as the 0.001..0.002 sliver the wall is.
    if (xAligned) {
        minX = 3.40282347e+38f;
        maxX = -3.40282347e+38f;
        minZ = worldZ + 0.001f;
        maxZ = worldZ + 0.002f;
    } else {
        minX = worldX + 0.001f;
        maxX = worldX + 0.002f;
        minZ = 3.40282347e+38f;
        maxZ = -3.40282347e+38f;
    }
    float minY = lotAltitude;
    float maxY = lotAltitude;
    const float threshold = lotAltitude + 0.002f;

    auto addVertex = [&](std::size_t index, float x, float y, float z) {
        if (vertices_.size() <= index) vertices_.resize(index + 1);
        LotRetainingWallVertex& vertex = vertices_[index];
        vertex.x = x;
        vertex.y = y;
        vertex.z = z;
        vertex.u = (xAligned ? x : z) * kRetainingWallUvScale;
        vertex.v = y * kRetainingWallUvScale;
    };
    auto extendAlong = [&]() {
        const float along = xAligned ? worldX : worldZ;   // the coordinate the wall runs along
        if (xAligned) {
            if (along < minX) minX = along;
            if (along + 16.0f > maxX) maxX = along + 16.0f;
        } else {
            if (along < minZ) minZ = along;
            if (along + 16.0f > maxZ) maxZ = along + 16.0f;
        }
    };

    std::size_t count = 0;
    while (varying < end) {
        const int next = varying + 1;
        const float farHeight = xAligned ? terrain->AltitudeAtVertex(next, fixedCell)
                                         : terrain->AltitudeAtVertex(fixedCell, next);
        const float savedNear = nearHeight;
        if (!(savedNear > lotAltitude) && !(farHeight > lotAltitude)) {
            nearHeight = farHeight;
            varying = next;
            if (xAligned) worldX += 16.0f; else worldZ += 16.0f;
            continue;
        }
        const bool flatNeighbour =
            xAligned ? SegmentIsAdjacentToFlatLot(varying, fixedCell, lotManager, terrain)
                     : SegmentIsAdjacentToFlatLot(fixedCell, varying, lotManager, terrain);
        if (flatNeighbour) {
            nearHeight = farHeight;
            varying = next;
            if (xAligned) worldX += 16.0f; else worldZ += 16.0f;
            continue;
        }
        const float stepX = xAligned ? 16.0f : 0.0f;
        const float stepZ = xAligned ? 0.0f : 16.0f;
        if (savedNear <= threshold) {
            if (farHeight <= threshold) {
                nearHeight = farHeight;
                varying = next;
                if (xAligned) worldX += stepX; else worldZ += stepZ;
                continue;
            }
            // near at or below the lot, far above it: one triangle from the lot level down to the near ground
            ++segmentsBuilt_;
            addVertex(count + 0, worldX, savedNear - 0.2f, worldZ);
            addVertex(count + 1, worldX + stepX, threshold, worldZ + stepZ);
            addVertex(count + 2, worldX + stepX, farHeight + 0.2f, worldZ + stepZ);
            count += 3;
            if (savedNear < minY) minY = savedNear;
            if (farHeight > maxY) maxY = farHeight;
        } else if (farHeight <= threshold) {
            // near above the lot, far at or below it
            ++segmentsBuilt_;
            addVertex(count + 0, worldX, threshold, worldZ);
            addVertex(count + 1, worldX + stepX, farHeight - 0.2f, worldZ + stepZ);
            addVertex(count + 2, worldX, savedNear + 0.2f, worldZ);
            count += 3;
            if (savedNear > maxY) maxY = savedNear;
            if (farHeight < minY) minY = farHeight;
        } else {
            // both ends above the lot: a quad (two triangles: 0,1,2 / 2,1,5 in the original's order)
            ++segmentsBuilt_;
            addVertex(count + 0, worldX, threshold, worldZ);
            addVertex(count + 1, worldX + stepX, threshold, worldZ + stepZ);
            addVertex(count + 2, worldX, savedNear + 0.2f, worldZ);
            addVertex(count + 3, worldX, savedNear + 0.2f, worldZ);
            addVertex(count + 4, worldX + stepX, threshold, worldZ + stepZ);
            addVertex(count + 5, worldX + stepX, farHeight + 0.2f, worldZ + stepZ);
            count += 6;
            if (savedNear > maxY) maxY = savedNear;
            if (farHeight > maxY) maxY = farHeight;
        }
        extendAlong();
        nearHeight = farHeight;
        varying = next;
        if (xAligned) worldX += stepX; else worldZ += stepZ;
    }
    vertices_.resize(count);

    bool moved = false;
    if (segmentsBuilt_ == 0) {
        // no segment: an empty unit box at the wall's first vertex
        worldMin_.x = static_cast<float>(wallStart_.x << 4);
        worldMin_.y = terrain->AltitudeAtVertex(wallStart_.x, wallStart_.z);
        worldMin_.z = static_cast<float>(wallStart_.z << 4);
        worldMax_.x = worldMin_.x + 1.0f;
        worldMax_.y = worldMin_.y + 1.0f;
        worldMax_.z = worldMin_.z + 1.0f;
        localMin_ = worldMin_;
        localMax_ = worldMax_;
    } else {
        // the along-axis range is shrunk by 0.001 on both ends (the original's -0x48 / -0x3c adjustment)
        if (xAligned) {
            minX += 0.001f;
            maxX -= 0.001f;
        } else {
            minZ += 0.001f;
            maxZ -= 0.001f;
        }
        const float oldMinX = worldMin_.x, oldMinY = worldMin_.y, oldMinZ = worldMin_.z;
        const float oldMaxX = worldMax_.x, oldMaxY = worldMax_.y, oldMaxZ = worldMax_.z;
        worldMin_ = cS3DVector3{minX, minY, minZ};
        worldMax_ = cS3DVector3{maxX, maxY, maxZ};
        localMin_ = cS3DVector3{0.0f, 0.0f, 0.0f};
        localMax_ = cS3DVector3{maxX - minX, maxY - minY, maxZ - minZ};
        auto differs = [](float a, float b) { return std::fabs(a - b) >= 0.0001f; };
        moved = differs(oldMaxX, maxX) || differs(oldMaxY, maxY) || differs(oldMaxZ, maxZ) ||
                differs(oldMinX, minX) || differs(oldMinY, minY) || differs(oldMinZ, minZ);
    }
    changed = moved;
    return finish(moved);
}

void cSC4LotRetainingWallModel::ApplyLighting(ILotRetainingWallLighting* lighting) {
    if (vertices_.empty()) return;
    if (!lighting) lighting = g_host ? g_host->Lighting() : nullptr;
    if (!lighting || !g_host) return;
    cS3DVector3 normal{0.0f, 0.0f, 0.0f};
    g_host->GetVectorForEdge(&normal.x, static_cast<signed char>(wallEdge_));   // SC4ViewUtilities::GetVectorForEdge
    normal.x = -normal.x;   // the original negates all three components with an xorps of -0.0
    normal.y = -normal.y;
    normal.z = -normal.z;
    for (LotRetainingWallVertex& vertex : vertices_) {
        const cS3DVector3 position{vertex.x, vertex.y, vertex.z};
        std::uint8_t r = 0, g = 0, b = 0;
        lighting->GetColor(position, normal, r, g, b);   // cISC4LightingManager vslot 0xc0
        vertex.color[0] = r;
        vertex.color[1] = g;
        vertex.color[2] = b;
        vertex.color[3] = 0xff;
    }
}

}  // namespace sc4
