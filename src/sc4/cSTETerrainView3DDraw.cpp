// cSTETerrainView3D — the drawing half: terrain surface (textured and texture-less passes), city-edge cliffs, water box
// and surface, grid overlay, plumbing overlay, and the water colour pass. All device access goes through
// ITerrainDrawContext / ITerrainVertBufExtn. See cSTETerrainView3D.cpp for the data setup.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "sc4/GeometryUtilities.h"
#include "sc4/PackedRenderState.h"
#include "sc4/cSTETerrainView3D.h"

namespace sc4 {
namespace {
inline int FloorToInt(float v) { return static_cast<int>(std::floor(v)); }
inline int CeilToInt(float v) { return static_cast<int>(std::ceil(v)); }
constexpr int kStrataTextureResId[3] = {0x11, 0x10, 0x15};                     // DrawSideCliffs strataTextureResId @0x100a29e80
constexpr int kHighlightColorTable[4] = {3, 2, 0, 1};                          // DrawPlumbingInfo highlightColorTable @0x100a29ed0
constexpr std::uint32_t kPipeOccupantType = 0x278128a0;

int TextureId(const std::shared_ptr<TerrainTextureBinding>& b, std::size_t sub = 0) {
    return b && sub < b->ids.size() ? b->ids[sub] : 0;
}

// General 4x4 inverse (row-major); returns false when singular.
bool Invert4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return false;
    const float d = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * d;
    return true;
}

// Alpha a vertex contributes to texture `key`: the blend byte of the layer holding the key (layers end at the first 0).
std::uint8_t LayerAlpha(const cSTETerrain::Vertex& v, std::uint32_t key) {
    for (int layer = 0; layer < 3; ++layer) {
        std::uint16_t id;
        std::memcpy(&id, &v.surface[6 + layer * 4], 2);
        if (id == key) return v.surface[8 + layer * 4];
        if (id == 0) break;
    }
    return 0;
}

const cSC4PackedRenderState& GridRenderState() {
    static cSC4PackedRenderState state;
    static bool inited = false;
    if (!inited) {
        state.PackRenderState(true, false, true, false, true, 1, true, 3, 4, 5, -1, -1.0f, 1, -1);
        inited = true;
    }
    return state;
}

const cSC4PackedRenderState& TerrainRenderState() {
    static cSC4PackedRenderState state;
    static bool inited = false;
    if (!inited) {
        state.PackRenderState(true, true, true, false, false, 1, true, 3, 1, 1, 0, 0.0f, -1, -1);
        inited = true;
    }
    return state;
}
}  // namespace

// ===============================================================================================================
// Update and the two pass entry points
// ===============================================================================================================
bool cSTETerrainView3D::Update(ITerrainDrawContext& dc) {                                           // 0x10075014c
    if (!dc.DrawEnabled()) return true;
    if (terrain_) {
        heightHi_ = terrain_->MaxHeight();
        heightLo_ = terrain_->MinHeight();
    }
    ITerrainVertBufExtn* vb = dc.GetVertexBufferExtension();
    const int savedDepthOffset = dc.DepthOffset();
    dc.SetDepthOffset(-3);
    dc.SetRenderState(TerrainRenderState());
    dc.ResetModelViewTransform();
    if (dc.Pass() == 0) DrawTerrainLayers(dc, vb);
    else DrawWaterLayers(dc, vb);
    if (vb) dc.ReleaseVertexBufferExtension();
    dc.SetDepthOffset(savedDepthOffset);
    if (dc.Pass() == 0 && host_ && host_->GetBoolPlumbingDisplay()) DrawPlumbingInfo(dc);
    return true;
}

void cSTETerrainView3D::DrawTerrainLayers(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {      // 0x10075029a
    Shared& s = S();
    ComputeDrawFrustumOptimizationInfo(dc);
    ComputeViewFrustumOptimizationInfo(dc);
    const int type = terrain_->TerrainType();
    if (s.curTerrainType != type) ChangeTerrainType(type);
    if (s.curZoom != dc.Zoom()) ChangeZoomLevel(dc);
    if (s.curRotation != dc.Rotation()) ChangeRotation(dc);
    if (!lightingDone_) DoLighting();
    if (s.displayTerrainSurface) DrawTerrainSurface(dc, vb);
    DrawSideCliffs(dc);
    if (s.displayGrid) {
        DrawGrid(dc, vb);
        dc.SetRenderState(TerrainRenderState());
    }
}

void cSTETerrainView3D::DrawWaterLayers(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {        // 0x100750372
    if (host_ && host_->GetBoolPlumbingDisplay()) return;
    if (host_ && !host_->WaterSurfaceDisplayState()) return;
    if (!S().displayWaterSurface) return;
    if (!waterColorDone_) SetColorOfWaterSurface();
    DrawWaterBox(dc);
    DrawWaterSurface(dc, vb);
}

// ===============================================================================================================
// Terrain surface
// ===============================================================================================================
bool cSTETerrainView3D::DrawTerrainSurface(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {      // 0x10074aff6
    Shared& s = S();
    if (vb) {
        vb->SetSourceVertexData(s.points.data());
        vb->SetIndexBuffer(s.allTriangleIndices.data(), true);
    }
    const TerrainFrustumCull& c = s.draw;
    auto copyColour = [&](int surfaceOffset) {
        for (std::int32_t row = c.terrainRowBegin; row < c.terrainRowEnd; ++row) {
            if (c.colMin >= c.colMax) continue;
            for (std::int32_t col = c.colMin; col < c.colMax; ++col) {
                const std::int32_t idx = static_cast<std::int32_t>(s.vertexCountX) * row + col;
                const cSTETerrain::Vertex& v = TV(idx);
                s.points[static_cast<std::size_t>(idx)].diffuse[0] = v.surface[surfaceOffset];
                s.points[static_cast<std::size_t>(idx)].diffuse[1] = v.surface[surfaceOffset + 1];
                s.points[static_cast<std::size_t>(idx)].diffuse[2] = v.surface[surfaceOffset + 2];
            }
        }
    };
    copyColour(3);                                                       // colour A feeds the texture-less pass
    dc.EnableTextureStateFlag(false, 0);
    if (!vb) DrawTextureLessTerrainSurface(dc);
    else DrawTextureLessTerrainSurfaceWithVertBufExtension(vb);
    copyColour(0);                                                       // colour B feeds the textured pass
    dc.EnableBlendStateFlag(true);
    dc.SetBlendFunc(4, 1);
    dc.EnableTextureStateFlag(true, 0);
    dc.SetTexEnvMode(1, 0);
    dc.SetTexFiltering(1, 7, 0);
    dc.SetTexWrapModes(3, 3, 0);
    DrawTexturedTerrainSurface(dc, vb);
    dc.EnableBlendStateFlag(false);
    return true;
}

bool cSTETerrainView3D::DrawTextureLessTerrainSurfaceWithVertBufExtension(ITerrainVertBufExtn* vb) {   // 0x10074b218
    const Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    vb->CreateBuffer();
    for (std::int32_t row = c.terrainRowBegin; row < c.terrainRowLast; ++row) {
        if (!c.terrainVisible[static_cast<std::size_t>(row)] && !c.terrainVisible[static_cast<std::size_t>(row) + 1]) continue;
        const std::int32_t end = c.colEnd[static_cast<std::size_t>(row)] - 1, start = c.colStart[static_cast<std::size_t>(row)];
        if (start < end) vb->DrawPrimsIndexedUsingVertBuf(row, start, end);
    }
    vb->FlushBuffer();
    return true;
}

bool cSTETerrainView3D::DrawTextureLessTerrainSurface(ITerrainDrawContext& dc) {                    // 0x10074b2a2
    const Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    for (std::int32_t row = c.terrainRowBegin; row < c.terrainRowLast; ++row) {
        if (!c.terrainVisible[static_cast<std::size_t>(row)] && !c.terrainVisible[static_cast<std::size_t>(row) + 1]) continue;
        dc.SetVertexBuffer(0xb, static_cast<int>(s.vertexCountX) * 2, &s.points[static_cast<std::size_t>(s.vertexCountX) * static_cast<std::size_t>(row)]);
        dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), &s.allTriangleIndices[static_cast<std::size_t>(s.numTriangleIndicesPerRow) * static_cast<std::size_t>(row)]);
        const std::int32_t start = c.colStart[static_cast<std::size_t>(row)];
        dc.DrawPrimsIndexed(0, start * 6, (c.colEnd[static_cast<std::size_t>(row)] - start) * 6 - 6);
    }
    return true;
}

bool cSTETerrainView3D::DrawTexturedTerrainSurface(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {   // 0x10074b370
    Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    if (vb) vb->CreateBuffer();
    std::vector<int> rows;
    for (std::int32_t row = c.terrainRowBegin; row < c.terrainRowLast; ++row) {
        if (!c.terrainVisible[static_cast<std::size_t>(row)] && !c.terrainVisible[static_cast<std::size_t>(row) + 1]) continue;
        if (s.texSpanInfoDirty[static_cast<std::size_t>(row)]) ComputeTextureSpansForRow(row);
        rows.push_back(row);
    }
    if (!rows.empty()) {
        if (!vb) DrawTerrainSurfaceRows(dc, rows);
        else DrawTerrainSurfaceRowsWithVertBufExtension(dc, vb, rows);
    }
    if (vb) vb->FlushBuffer();
    return true;
}

void cSTETerrainView3D::DrawTerrainSurfaceRows(ITerrainDrawContext& dc, const std::vector<int>& rows) {   // 0x10074b4be
    Shared& s = S();
    std::set<std::uint32_t> drawn;
    int index = 0;
    std::uint32_t key = 0;
    std::vector<TerrainRenderTexInfo> out;
    while (true) {
        GetNextTextureToDraw(rows, index, drawn, key, out);
        if (out.empty()) return;
        drawn.insert(key);
        const auto& bindings = s.terrainTextureBinding[static_cast<std::size_t>(std::clamp(s.curZoom, 0, 4))];
        const std::uint32_t slot = key & 0xffff0fffu;
        dc.SetTexture(slot < bindings.size() ? TextureId(bindings[slot], (key >> 12) & 0xf) : 0, 0);
        for (const TerrainRenderTexInfo& info : out) {
            const std::int32_t rowBase = info.row * static_cast<std::int32_t>(s.vertexCountX);
            dc.SetVertexBuffer(0xb, static_cast<int>(s.vertexCountX) * 2, &s.points[static_cast<std::size_t>(rowBase)]);
            dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), &s.allTriangleIndices[static_cast<std::size_t>(info.row) * s.numTriangleIndicesPerRow]);
            for (const TerrainSpan& span : info.spans) {
                for (std::int32_t x = span.first + rowBase; x <= span.last + rowBase; ++x) {
                    s.points[static_cast<std::size_t>(x)].diffuse[3] = LayerAlpha(TV(x), key);
                    const std::size_t below = static_cast<std::size_t>(x) + s.vertexCountX;
                    s.points[below].diffuse[3] = LayerAlpha(TV(static_cast<int>(below)), key);
                }
                if (span.last > span.first) dc.DrawPrimsIndexed(0, span.first * 6, (span.last - span.first) * 6);
            }
        }
    }
}

void cSTETerrainView3D::DrawTerrainSurfaceRowsWithVertBufExtension(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb, const std::vector<int>& rows) {   // 0x10074b9ee
    Shared& s = S();
    std::set<std::uint32_t> drawn;
    int index = 0;
    std::uint32_t key = 0;
    std::vector<TerrainRenderTexInfo> out;
    while (true) {
        GetNextTextureToDraw(rows, index, drawn, key, out);
        if (out.empty()) return;
        vb->FlushBuffer();
        vb->CreateBuffer();
        drawn.insert(key);
        const auto& bindings = s.terrainTextureBinding[static_cast<std::size_t>(std::clamp(s.curZoom, 0, 4))];
        const std::uint32_t slot = key & 0xffff0fffu;
        dc.SetTexture(slot < bindings.size() ? TextureId(bindings[slot], (key >> 12) & 0xf) : 0, 0);
        for (const TerrainRenderTexInfo& info : out) {
            const std::int32_t rowBase = info.row * static_cast<std::int32_t>(s.vertexCountX);
            for (const TerrainSpan& span : info.spans) {
                for (std::int32_t x = span.first + rowBase; x <= span.last + rowBase; ++x) {
                    s.points[static_cast<std::size_t>(x)].diffuse[3] = LayerAlpha(TV(x), key);
                    const std::size_t below = static_cast<std::size_t>(x) + s.vertexCountX;
                    s.points[below].diffuse[3] = LayerAlpha(TV(static_cast<int>(below)), key);
                }
                if (span.first < span.last) vb->DrawPrimsIndexedUsingVertBuf(info.row, span.first, span.last);
            }
        }
    }
}

// ===============================================================================================================
// Mesh subsets (grid overlay, selection overlays)
// ===============================================================================================================
void cSTETerrainView3D::DrawSubsetCommon(const TerrainFrustumCull& c, const std::vector<std::int32_t>& cs, const std::vector<std::int32_t>& ce,
                                         const std::vector<std::uint8_t>& vis, TerrainRect r, ITerrainDrawContext& dc, bool rowsFromView) {
    Shared& s = S();
    const std::int32_t vx = static_cast<std::int32_t>(s.vertexCountX);
    std::int32_t rowStart = std::max(r.top, 0);
    std::int32_t rowEnd = std::min<std::int32_t>(r.bottom, static_cast<std::int32_t>(s.vertexCountZ));
    const std::int32_t right = std::min(r.right, vx), left = std::max(r.left, 0);
    // The view-frustum variant bounds the rows with the whole frustum rows (+0x110/+0x114), the draw variant with the terrain rows.
    rowStart = std::max(rowStart, rowsFromView ? c.rowBegin : c.terrainRowBegin);
    rowEnd = std::min(rowEnd, rowsFromView ? c.rowEnd : c.terrainRowEnd);
    for (std::int32_t row = rowStart; row < rowEnd - 1; ++row) {
        if (!vis[static_cast<std::size_t>(row)] && !vis[static_cast<std::size_t>(row) + 1]) continue;
        const std::int32_t start = std::max(cs[static_cast<std::size_t>(row)], left);
        const std::int32_t end = std::min(ce[static_cast<std::size_t>(row)], right);
        if (end - start == 0 || start > end) continue;
        dc.SetVertexBuffer(0xb, vx * 2, &s.points[static_cast<std::size_t>(vx) * static_cast<std::size_t>(row)]);
        dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), &s.allTriangleIndices[static_cast<std::size_t>(s.numTriangleIndicesPerRow) * static_cast<std::size_t>(row)]);
        const std::int32_t tris = (end - start) * 2 - 2;
        if (tris > 0) dc.DrawPrimsIndexed(0, start * 6, tris * 3);
        const std::size_t rowBase = static_cast<std::size_t>(vx) * static_cast<std::size_t>(row);
        const std::uint16_t lo = s.allLevelCellIndices[start + rowBase], hi = s.allLevelCellIndices[end - 1 + rowBase];
        if (lo < hi) dc.DrawPrims(6, 0xb, (hi - lo) * 4, &s.levelCellQuads[static_cast<std::size_t>(row)][lo]);
    }
}

void cSTETerrainView3D::DrawTerrainMeshSubsetInDrawFrustum(TerrainRect r, ITerrainDrawContext& dc) {   // 0x100749f40
    const Shared& s = S();
    DrawSubsetCommon(s.draw, s.draw.colStart, s.draw.colEnd, s.draw.terrainVisible, r, dc, false);
}

void cSTETerrainView3D::DrawTerrainMeshSubsetInViewFrustum(TerrainRect r, ITerrainDrawContext& dc) {   // 0x10074a56e
    const Shared& s = S();
    DrawSubsetCommon(s.view, s.view.colStart, s.view.colEnd, s.view.terrainVisible, r, dc, true);
}

void cSTETerrainView3D::DrawVertBufSubsetCommon(const TerrainFrustumCull& c, const std::vector<std::int32_t>& cs, const std::vector<std::int32_t>& ce,
                                                const std::vector<std::uint8_t>& vis, ITerrainDrawContext& dc, ITerrainVertBufExtn* vb, TerrainRect r) {
    Shared& s = S();
    if (!vb->CreateBuffer()) return;
    vb->SetSourceVertexData(s.points.data());
    vb->SetIndexBuffer(s.allTriangleIndices.data(), true);
    const std::int32_t left = std::max(r.left, 0), right = std::min<std::int32_t>(r.right, static_cast<std::int32_t>(s.vertexCountX));
    std::int32_t rowStart = std::max(r.top, 0);
    std::int32_t rowEnd = std::min<std::int32_t>(r.bottom, static_cast<std::int32_t>(s.vertexCountZ));
    rowStart = std::max(rowStart, c.terrainRowBegin);
    rowEnd = std::min(rowEnd, c.terrainRowEnd) - 1;
    for (std::int32_t row = rowStart; row < rowEnd; ++row) {
        if (!vis[static_cast<std::size_t>(row)] && !vis[static_cast<std::size_t>(row) + 1]) continue;
        const std::int32_t start = std::max(cs[static_cast<std::size_t>(row)], left);
        const std::int32_t end = std::min(ce[static_cast<std::size_t>(row)], right);
        if (start < end - 1) vb->DrawPrimsIndexedUsingVertBuf(row, start, end - 1);
    }
    vb->FlushBuffer();
    // The level-cell quads of the visible rows. The original batches them through a driver vertex buffer (copying into
    // 0x30-byte vertices on ATI cards); the geometry submitted is the same as a plain per-row DrawPrims.
    for (std::int32_t row = rowStart; row < rowEnd; ++row) {
        if (!vis[static_cast<std::size_t>(row)] && !vis[static_cast<std::size_t>(row) + 1]) continue;
        const std::int32_t start = std::max(cs[static_cast<std::size_t>(row)], left);
        const std::int32_t end = std::min(ce[static_cast<std::size_t>(row)], right);
        if (start >= end) continue;
        const std::int32_t rowIdx = row * static_cast<std::int32_t>(s.cellCountX + 1);
        const std::uint16_t lo = s.allLevelCellIndices[static_cast<std::size_t>(start + rowIdx)];
        const std::uint16_t hi = s.allLevelCellIndices[static_cast<std::size_t>(end - 1 + rowIdx)];
        if (lo < hi) dc.DrawPrims(6, 0xb, (hi - lo) * 4, &s.levelCellQuads[static_cast<std::size_t>(row)][lo]);
    }
}

void cSTETerrainView3D::DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb, TerrainRect r) {   // 0x100749ace
    const Shared& s = S();
    DrawVertBufSubsetCommon(s.draw, s.draw.colStart, s.draw.colEnd, s.draw.terrainVisible, dc, vb, r);
}

void cSTETerrainView3D::DrawTerrainMeshSubsetWithVertBufExtensionInViewFrustum(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb, TerrainRect r) {   // 0x10074a72c
    const Shared& s = S();
    DrawVertBufSubsetCommon(s.view, s.view.colStart, s.view.colEnd, s.view.terrainVisible, dc, vb, r);
}

void cSTETerrainView3D::DrawMixedCommon(const TerrainFrustumCull& c, const std::vector<std::int32_t>& cs, const std::vector<std::int32_t>& ce,
                                        const std::vector<std::uint8_t>& terrainVis, const std::vector<std::uint8_t>& waterVis,
                                        const TerrainRect& r, ITerrainDrawContext& dc) {
    Shared& s = S();
    const std::int32_t vx = static_cast<std::int32_t>(s.vertexCountX);
    const std::int32_t corner[4] = {0, 1, vx, vx + 1};
    TerrainSurfacePoint quad[4];
    dc.SetVertexBuffer(0xb, 4, quad);
    std::int32_t rowStart = std::max(0, r.top);
    std::int32_t rowEnd = std::min(r.bottom, static_cast<std::int32_t>(s.vertexCountZ));
    rowStart = std::max(rowStart, c.rowBegin);
    rowEnd = std::min(rowEnd, c.rowEnd);
    for (std::int32_t row = rowStart; row < rowEnd - 1; ++row) {
        if (!terrainVis[static_cast<std::size_t>(row)] && !terrainVis[static_cast<std::size_t>(row) + 1] &&
            !waterVis[static_cast<std::size_t>(row)] && !waterVis[static_cast<std::size_t>(row) + 1]) continue;
        const std::int32_t start = std::max(r.left, cs[static_cast<std::size_t>(row)]);
        const std::int32_t end = std::min(r.right, ce[static_cast<std::size_t>(row)]);
        for (std::int32_t col = start; col < end; ++col) {
            for (int k = 0; k < 4; ++k) {
                const std::size_t idx = static_cast<std::size_t>(corner[k] + vx * row + col);
                quad[k] = s.points[idx];
                const float water = s.waterPoints[idx].y;
                quad[k].y = water <= quad[k].y ? quad[k].y : water;           // the higher of terrain and water
            }
            const bool flipped = terrain_->IsTriangulationFlipped(static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row));
            dc.SetIndexBuffer(6, flipped ? s.flippedTriQuadIndices : s.triQuadIndices);
            dc.DrawPrimsIndexed(0, 0, 6);
        }
    }
}

void cSTETerrainView3D::DrawMixedMeshSubsetInDrawFrustum(const TerrainRect& r, ITerrainDrawContext& dc) {   // 0x10074ab9e
    const Shared& s = S();
    DrawMixedCommon(s.draw, s.draw.colStart, s.draw.colEnd, s.draw.terrainVisible, s.draw.waterVisible, r, dc);
}

void cSTETerrainView3D::DrawMixedMeshSubsetInViewFrustum(const TerrainRect& r, ITerrainDrawContext& dc) {   // 0x10074adca
    // Original quirk: the column table is the *draw* frustum's even though the visibility tables are the view's.
    const Shared& s = S();
    DrawMixedCommon(s.view, s.draw.colStart, s.draw.colEnd, s.view.terrainVisible, s.view.waterVisible, r, dc);
}

// ===============================================================================================================
// Grid
// ===============================================================================================================
bool cSTETerrainView3D::DrawGrid(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {                 // 0x100749234
    Shared& s = S();
    if (!gridTexture_ || dc.GridSuppressed()) return false;
    dc.SetRenderState(GridRenderState());
    dc.SetTexture(TextureId(gridTexture_), 0);
    dc.SetTexFiltering(1, 7, 0);
    dc.SetTexWrapModes(3, 3, 0);
    dc.ResetModelViewTransform();
    float mv[16], inv[16], tex[16] = {};
    dc.GetModelViewMatrix(mv);
    if (!Invert4(mv, inv)) for (int i = 0; i < 16; ++i) inv[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    // Eye-space -> world -> grid texture: one texture repeat per 16 m cell (u = x / 16, v = z / 16).
    float scale[16] = {};
    scale[0] = 0.0625f; scale[6] = 0.0625f; scale[15] = 1.0f;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += scale[r * 4 + k] * inv[k * 4 + c];
            tex[r * 4 + c] = sum;
        }
    dc.SetTexTransform4(tex, 0);
    const TerrainFrustumCull& c = s.draw;
    // Fade the grid out where the terrain dips below the water surface: alpha ramps over the first 2 m above it.
    for (std::int32_t row = c.terrainRowBegin; row < c.terrainRowEnd; ++row) {
        const std::int32_t start = c.colStart[static_cast<std::size_t>(row)], end = c.colEnd[static_cast<std::size_t>(row)];
        if (start >= end) continue;
        for (std::int32_t col = start; col < end; ++col) {
            const std::size_t idx = static_cast<std::size_t>(s.vertexCountX) * static_cast<std::size_t>(row) + static_cast<std::size_t>(col);
            const float d = (s.points[idx].y - s.waterPoints[idx].y) - 2.0f;
            std::uint8_t a = 0xff;
            if (d < 2.0f) {
                if (d >= 0.0f) {
                    const int i = static_cast<int>(std::nearbyint(d * 128.0f));        // (int)(d * 0.5 + 49152.0) bit trick, 1/256 steps
                    a = static_cast<std::uint8_t>(i - (i >> 8));
                } else {
                    a = 0;
                }
            }
            s.points[idx].diffuse[3] = a;
        }
    }
    const TerrainRect all{0, 0, static_cast<std::int32_t>(s.vertexCountX), static_cast<std::int32_t>(s.vertexCountZ)};
    dc.DriverSetState(0, 1);
    if (!vb) DrawTerrainMeshSubsetInDrawFrustum(all, dc);
    else DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum(dc, vb, all);
    dc.DriverSetState(1, 1);
    dc.ClearTexTransform(0);
    return true;
}

// ===============================================================================================================
// City-edge cliffs and the water box
// ===============================================================================================================
namespace {
// Cell range [first, last] (clamped to the edge) of one city-edge strip that the frustum can see.
void EdgeCellRange(int side, int cellCount, int vertexCount, const float edgeOrigin[2], const float edgeDir[2],
                   const TerrainFrustumPlanes& fp, int& first, int& last) {
    float hitA[2] = {0, 0}, hitB[2] = {0, 0};
    SC4GeometryUtilities::RaysIntersect2D(edgeOrigin, edgeDir, fp.leftOrigin, fp.leftDir, hitA);
    SC4GeometryUtilities::RaysIntersect2D(edgeOrigin, edgeDir, fp.rightOrigin, fp.rightDir, hitB);
    const int axis = side & 1;
    const float lo = std::min(hitA[axis], hitB[axis]) * 0.0625f, hi = std::max(hitA[axis], hitB[axis]) * 0.0625f;
    const int cell0 = FloorToInt(lo), cell1 = CeilToInt(hi);
    int a, b;
    if (side == 1 || side == 2) { a = cellCount - cell1; b = cellCount - cell0; }
    else { a = cell0; b = cell1; }
    first = a < 0 ? 0 : (a < vertexCount ? a : vertexCount - 1);
    last = b < 0 ? 0 : (b >= vertexCount ? vertexCount - 1 : b);
}

// Draws the runs of cells whose strip has height (top above bottom row). `topY(i)` / `bottomY(i)` give the two heights.
template <typename TopFn, typename BottomFn>
void DrawStripRuns(ITerrainDrawContext& dc, int first, int last, int vertexCount, TopFn topY, BottomFn bottomY) {
    int runStart = -1;
    int i = first;
    for (;; ++i) {
        if (i > last) break;
        if (topY(i) <= bottomY(i)) {
            if (runStart >= 0) {
                if (i - runStart > 0) dc.DrawPrimsIndexed(0, runStart * 6, (i - runStart) * 6);
                runStart = -1;
            }
        } else if (runStart < 0) {
            runStart = i - 1;
            if (i < 2) runStart = 0;
        }
    }
    if (runStart >= 0) {
        const int end = i < vertexCount ? i : vertexCount - 1;
        if (end - runStart != 0 && runStart <= end) dc.DrawPrimsIndexed(0, runStart * 6, (end - runStart) * 6);
    }
}
}  // namespace

bool cSTETerrainView3D::DrawSideCliffs(ITerrainDrawContext& dc) {                                   // 0x10074a0fa
    Shared& s = S();
    const TerrainFrustumPlanes& fp = dc.DrawFrustum();
    const int savedOffset = dc.DepthOffset();
    dc.SetDepthOffset(1);
    dc.SetTexEnvMode(1, 0);
    dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), s.triangleIndices.data());
    for (int side = 0; side < 4; ++side) {
        if (side != visibleEdge_[0] && side != visibleEdge_[1]) continue;
        const std::vector<TerrainBorderPoint>& p0 = s.borderPoints[side][0];
        const int cellCount = static_cast<int>((side & 1) ? s.cellCountZ : s.cellCountX);
        const int vertexCount = static_cast<int>((side & 1) ? s.vertexCountZ : s.vertexCountX);
        const float origin[2] = {p0[0].x, p0[0].z};
        const float dir[2] = {p0[static_cast<std::size_t>(cellCount)].x - p0[0].x, p0[static_cast<std::size_t>(cellCount)].z - p0[0].z};
        int first, last;
        EdgeCellRange(side, cellCount, vertexCount, origin, dir, fp, first, last);
        for (int layer = 0; layer < 3; ++layer) {
            const std::vector<TerrainBorderPoint>& pts = s.borderPoints[side][layer];
            dc.SetVertexBuffer(10, vertexCount * 2, pts.data());
            const auto& bindings = s.terrainTextureBinding[static_cast<std::size_t>(std::clamp(s.curZoom, 0, 4))];
            const std::size_t strata = static_cast<std::size_t>(kStrataTextureResId[layer]);
            dc.SetTexture(strata < bindings.size() ? TextureId(bindings[strata]) : 0, 0);
            if (first <= last)
                DrawStripRuns(dc, first, last, vertexCount, [&](int i) { return pts[static_cast<std::size_t>(i)].y; },
                              [&](int i) { return pts[static_cast<std::size_t>(i + vertexCount)].y; });
        }
    }
    dc.SetDepthOffset(savedOffset);
    return true;
}

void cSTETerrainView3D::DrawWaterBox(ITerrainDrawContext& dc) {                                      // 0x10074bd0e
    Shared& s = S();
    if (terrain_->MinHeight() > terrain_->GetSeaLevel()) return;
    const TerrainFrustumPlanes& fp = dc.DrawFrustum();
    dc.EnableCullFaceFlag(false);
    dc.EnableTextureStateFlag(false, 0);
    dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), s.triangleIndices.data());
    dc.SetDepthFunc(1);
    const int savedOffset = dc.DepthOffset();
    for (int side = 0; side < 4; ++side) {
        dc.SetDepthOffset((side == visibleEdge_[0] || side == visibleEdge_[1]) ? -1 : -5);
        const std::vector<TerrainWaterBoxPoint>& pts = s.waterBoxPoints[side];
        const int cellCount = static_cast<int>((side & 1) ? s.cellCountZ : s.cellCountX);
        const int vertexCount = static_cast<int>((side & 1) ? s.vertexCountZ : s.vertexCountX);
        // Original quirk: the ray origin's z is taken from the *second* point (pts[1].z) while the direction uses pts[0].
        const float origin[2] = {pts[0].x, pts[1].z};
        const float dir[2] = {pts[static_cast<std::size_t>(cellCount)].x - pts[0].x, pts[static_cast<std::size_t>(cellCount)].z - pts[0].z};
        int first, last;
        EdgeCellRange(side, cellCount, vertexCount, origin, dir, fp, first, last);
        dc.SetVertexBuffer(1, static_cast<int>(s.vertexCountX) * 2, pts.data());
        if (first <= last)
            DrawStripRuns(dc, first, last, vertexCount, [&](int i) { return pts[static_cast<std::size_t>(i)].y; },
                          [&](int i) { return pts[static_cast<std::size_t>(i + vertexCount)].y; });
    }
    dc.SetDepthOffset(savedOffset);
    dc.SetDepthFunc(3);
    dc.EnableTextureStateFlag(true, 0);
    dc.EnableCullFaceFlag(true);
}

// ===============================================================================================================
// Water surface
// ===============================================================================================================
bool cSTETerrainView3D::DrawWaterSurface(ITerrainDrawContext& dc, ITerrainVertBufExtn* vb) {         // 0x10074c110
    Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    dc.EnableBlendStateFlag(true);
    dc.SetBlendFunc(4, 5);
    dc.SetTexEnvMode(1, 0);
    const std::size_t zoom = static_cast<std::size_t>(std::clamp(s.curZoom, 0, 3));
    dc.SetTexture(TextureId(s.waterSurfaceTexture[zoom]), 0);
    dc.SetDepthFunc(1);
    if (!vb) {
        for (std::int32_t row = c.waterRowBegin; row < c.waterRowLast; ++row) {
            if (!c.waterVisible[static_cast<std::size_t>(row)] && !c.waterVisible[static_cast<std::size_t>(row) + 1]) continue;
            const std::int32_t tris = (c.colEnd[static_cast<std::size_t>(row)] - c.colStart[static_cast<std::size_t>(row)]) * 2 - 2;
            if (tris <= 0) continue;
            dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), &s.allTriangleIndices[static_cast<std::size_t>(s.numTriangleIndicesPerRow) * static_cast<std::size_t>(row)]);
            dc.SetVertexBuffer(0xb, static_cast<int>(s.vertexCountX), &s.waterPoints[static_cast<std::size_t>(s.vertexCountX) * static_cast<std::size_t>(row)]);
            dc.DrawPrimsIndexed(0, c.colStart[static_cast<std::size_t>(row)] * 6, tris * 3);
        }
    } else {
        vb->CreateBuffer();
        vb->SetSourceVertexData(s.waterPoints.data());
        vb->SetIndexBuffer(s.allTriangleIndices.data(), true);
        for (std::int32_t row = c.waterRowBegin; row < c.waterRowLast; ++row) {
            if (!c.waterVisible[static_cast<std::size_t>(row)] && !c.waterVisible[static_cast<std::size_t>(row) + 1]) continue;
            const std::int32_t end = c.colEnd[static_cast<std::size_t>(row)] - 1, start = c.colStart[static_cast<std::size_t>(row)];
            if (start < end) vb->DrawPrimsIndexedUsingVertBuf(row, start, end);
        }
        vb->FlushBuffer();
    }
    dc.SetDepthFunc(3);
    dc.EnableBlendStateFlag(false);
    return true;
}

bool cSTETerrainView3D::DrawWaterSubsetCommon(const TerrainFrustumCull& c, const std::vector<std::int32_t>& cs, const std::vector<std::int32_t>& ce,
                                              const std::vector<std::uint8_t>& vis, TerrainRect r, ITerrainDrawContext& dc) {
    Shared& s = S();
    std::int32_t rowStart = std::max(r.top, 0);
    std::int32_t rowEnd = std::min<std::int32_t>(r.bottom, static_cast<std::int32_t>(s.vertexCountZ));
    const std::int32_t right = std::min<std::int32_t>(r.right, static_cast<std::int32_t>(s.vertexCountX));
    rowStart = std::max(rowStart, c.waterRowBegin);
    rowEnd = std::min(rowEnd, c.waterRowEnd);
    if (rowStart < rowEnd) {
        dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), s.triangleIndices.data());
        const std::int32_t left = std::max(r.left, 0);
        for (std::int32_t row = rowStart; row < rowEnd - 1; ++row) {
            if (!vis[static_cast<std::size_t>(row)] && !vis[static_cast<std::size_t>(row) + 1]) continue;
            const std::int32_t start = std::max(cs[static_cast<std::size_t>(row)], left);
            const std::int32_t end = std::min(ce[static_cast<std::size_t>(row)], right);
            const std::int32_t tris = (end - start) * 2 - 2;
            if (tris <= 0) continue;
            dc.SetVertexBuffer(0xb, static_cast<int>(s.vertexCountX), &s.waterPoints[static_cast<std::size_t>(row) * s.vertexCountX]);
            dc.DrawPrimsIndexed(0, start * 6, tris * 3);
        }
    }
    return true;
}

bool cSTETerrainView3D::DrawWaterSurfaceSubsetInDrawFrustum(TerrainRect r, ITerrainDrawContext& dc) {   // 0x10074c300
    const Shared& s = S();
    return DrawWaterSubsetCommon(s.draw, s.draw.colStart, s.draw.colEnd, s.draw.waterVisible, r, dc);
}

bool cSTETerrainView3D::DrawWaterSurfaceSubsetInViewFrustum(TerrainRect r, ITerrainDrawContext& dc) {   // 0x10074c446
    const Shared& s = S();
    return DrawWaterSubsetCommon(s.view, s.view.colStart, s.view.colEnd, s.view.waterVisible, r, dc);
}

// ===============================================================================================================
// Single quads
// ===============================================================================================================
void cSTETerrainView3D::DrawTerrainQuad(ITerrainDrawContext& dc, std::uint32_t x, std::uint32_t z) {   // 0x10074e4be
    Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    if (static_cast<std::int32_t>(z) < c.terrainRowBegin || static_cast<std::int32_t>(z) >= c.terrainRowEnd) return;
    if (static_cast<std::int32_t>(x) < c.colStart[z] || static_cast<std::int32_t>(x) >= c.colEnd[z]) return;
    if (!c.terrainVisible[z] && !c.terrainVisible[z + 1]) return;
    const std::uint32_t base = z * s.vertexCountX;
    dc.SetVertexBuffer(0xb, static_cast<int>(s.vertexCountX), &s.points[base]);
    const bool flipped = (TV(static_cast<int>(base + x)).flags & 0x3000) != 0;
    dc.SetIndexBuffer(static_cast<int>(s.numTriangleIndicesPerRow), flipped ? s.flippedTriangleIndices.data() : s.triangleIndices.data());
    dc.DrawPrimsIndexed(0, static_cast<int>(x) * 6, 6);
}

void cSTETerrainView3D::DrawTexturedTerrainQuad(ITerrainDrawContext& dc, std::uint32_t x, std::uint32_t z, const void* quadVertices, bool force) {   // 0x10074e5a6
    Shared& s = S();
    const TerrainFrustumCull& c = s.draw;
    if (!force) {
        if (static_cast<std::int32_t>(z) < c.terrainRowBegin || static_cast<std::int32_t>(z) >= c.terrainRowEnd) return;
        if (static_cast<std::int32_t>(x) < c.colStart[z] || static_cast<std::int32_t>(x) >= c.colEnd[z]) return;
        if (!c.terrainVisible[z] && !c.terrainVisible[z + 1]) return;
    }
    const bool flipped = (TV(static_cast<int>(z * s.vertexCountX + x)).flags & 0x3000) != 0;
    dc.SetIndexBuffer(6, flipped ? s.flippedTriQuadIndices : s.triQuadIndices);
    dc.SetVertexBuffer(0xb, 4, quadVertices);
    dc.DrawPrimsIndexed(0, 0, 6);
}

// ===============================================================================================================
// Plumbing overlay
// ===============================================================================================================
void cSTETerrainView3D::DrawPlumbingInfo(ITerrainDrawContext& dc) {                                  // 0x10074c58c
    Shared& s = S();
    if (!host_) return;
    const std::int32_t vx = static_cast<std::int32_t>(s.vertexCountX);
    const std::int32_t cornerOffset[4] = {0, vx, vx + 1, 1};
    // Triangle tables as laid out on the original's stack: {0,1,2 | 0,2,3} for a normal cell, {0,1,3 | 1,2,3} when flipped;
    // the second half (offset 12 bytes) is the odd triangle.
    static const int tableNormal[6] = {0, 1, 2, 0, 2, 3}, tableFlipped[6] = {0, 1, 3, 1, 2, 3};
    dc.EnableBlendStateFlag(true);
    dc.SetBlendFunc(4, 5);
    dc.EnableDepthMaskFlag(false);
    dc.EnableTextureStateFlag(false, 0);
    const int savedOffset = dc.DepthOffset();
    dc.SetDepthOffset(-2);
    std::uint32_t colors[4] = {0x66002fb6, 0x66ffffa7, 0x66ff837d, 0x667477b6};
    bool showAll = true;
    ReadPlumbingTuningData(colors, showAll);
    const TerrainFrustumCull& c = s.draw;
    for (std::int32_t z = c.rowBegin; z < c.rowLast; ++z) {
        if (!c.terrainVisible[static_cast<std::size_t>(z)] && !c.terrainVisible[static_cast<std::size_t>(z) + 1]) continue;
        const std::int32_t xEnd = c.colEnd[static_cast<std::size_t>(z)] - 1;
        for (std::int32_t x = c.colStart[static_cast<std::size_t>(z)]; x < xEnd; ++x) {
            if (terrain_->IsWholeCellHidden(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z))) continue;
            bool flipped = terrain_->IsTriangulationFlipped(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            bool evenHidden = false, oddHidden = false, bVar17 = false;
            const std::uint8_t flags = host_->PlumbingCellFlags(x, z);
            int colorIndex = -1;
            bool drawOk = true;
            if (flags & 8) {
                bool found = host_->NetworkHiddenTriangles(x, z, flipped, evenHidden, oddHidden);
                bool local_f9 = true;
                if (found) {
                    local_f9 = !evenHidden || !oddHidden ? true : false;
                    local_f9 = (!evenHidden) || (!oddHidden);
                    bVar17 = oddHidden && evenHidden;
                }
                if (evenHidden != oddHidden) {
                    bool listOk = false;
                    const bool hasPipe = host_->CellHasOccupantType(x, z, kPipeOccupantType, listOk);
                    int tableRow;
                    int tableCol;
                    if (!listOk || !hasPipe) {
                        if (!host_->PlumbingQuery58(x, z)) {
                            local_f9 = showAll ? local_f9 : false;
                            tableRow = 0; tableCol = 0;
                        } else {
                            tableRow = 0; tableCol = 1;
                        }
                    } else {
                        tableRow = 1;
                        tableCol = host_->PlumbingQuery40(x, z) ? 1 : 0;
                    }
                    colorIndex = kHighlightColorTable[tableRow * 2 + tableCol];
                    bVar17 = true;
                }
                drawOk = local_f9;
            }
            if ((flags & 1) && !bVar17) {
                colorIndex = 2 - ((flags >> 1) & 1);
                bVar17 = true;
            }
            const bool notYet = !bVar17;
            const bool wire = (flags & 2) != 0;
            if (wire && notYet) bVar17 = true;
            if (!drawOk || !(bVar17 || showAll)) continue;
            const int* table = flipped ? tableFlipped : tableNormal;
            int pick = (wire && notYet) ? 0 : colorIndex;
            if (!bVar17 && showAll) pick = 3;
            pick = std::clamp(pick, 0, 3);
            const int tableOffset = evenHidden ? 3 : 0;
            const int count = (!oddHidden && !evenHidden) ? 6 : 3;
            struct PlumbVertex { float x, y, z; std::uint8_t color[4]; } verts[6];
            const std::size_t base = static_cast<std::size_t>(vx) * static_cast<std::size_t>(z) + static_cast<std::size_t>(x);
            for (int k = 0; k < count; ++k) {
                const TerrainSurfacePoint& p = s.points[base + static_cast<std::size_t>(cornerOffset[table[tableOffset + k]])];
                verts[k].x = p.x; verts[k].y = p.y + 0.2f; verts[k].z = p.z;
                std::memcpy(verts[k].color, &colors[pick], 4);
            }
            dc.DrawPrims(0, 1, count, verts);
        }
    }
    dc.SetDepthOffset(savedOffset);
    dc.EnableBlendStateFlag(false);
    dc.EnableDepthMaskFlag(true);
}

// ===============================================================================================================
// Water colour
// ===============================================================================================================
void cSTETerrainView3D::SetColorOfWaterSurface() {                                                   // 0x10074f9c6
    Shared& s = S();
    const bool flag738 = host_ ? host_->RenderFlag738() : false;
    const float night = nightFactor_;
    float adjust[3] = {1, 1, 1};
    if (night > 0.0f)
        for (int i = 0; i < 3; ++i) adjust[i] = 1.0f - (1.0f - s.waterNightColorAdjustment[i]) * night;
    if (!s.waterGlareMaskBuf) return;
    const float alphaScale = s.waterDepthForMaxAlpha <= 0.0f ? 1.0f
                                                             : static_cast<float>(255 - (static_cast<std::uint8_t>(s.waterMinAlpha))) / s.waterDepthForMaxAlpha;
    const std::uint32_t maskW = static_cast<std::uint32_t>(s.waterGlareMaskBuf->Width()), maskH = static_cast<std::uint32_t>(s.waterGlareMaskBuf->Height());
    const std::uint32_t maxX = maskW - 1, maxZ = maskH - 1;
    const std::vector<float>& depths = terrain_->WaterDepth();
    const std::uint8_t minAlpha8 = static_cast<std::uint8_t>(s.waterMinAlpha);
    for (std::int32_t z = waterRect_.top; z <= waterRect_.bottom; ++z) {
        for (std::int32_t x = waterRect_.left; x <= waterRect_.right; ++x) {
            const std::size_t idx = static_cast<std::size_t>(s.vertexCountX) * static_cast<std::size_t>(z) + static_cast<std::size_t>(x);
            const float depth = depths[idx];
            std::int32_t gi = static_cast<std::int32_t>(s.waterDepthToColorGradientFactor * depth);
            if (gi < 0) gi = 0;
            const std::uint32_t gradIndex = std::min<std::uint32_t>(static_cast<std::uint32_t>(gi), s.waterColorDepthGradientHt);
            const std::uint32_t col = s.waterColorDepthGradient ? s.waterColorDepthGradient->Pixel(0, static_cast<int>(gradIndex)) : 0;
            float r = static_cast<float>((col >> 16) & 0xff), g = static_cast<float>((col >> 8) & 0xff), b = static_cast<float>(col & 0xff);
            if (night <= 0.0f) {
                std::int32_t a, bb;
                switch (s.curRotation) {
                    case 0: a = z; bb = x; break;
                    case 1: a = static_cast<std::int32_t>(s.cellCountZ) - x; bb = z; break;
                    case 2: a = static_cast<std::int32_t>(s.cellCountZ) - z; bb = static_cast<std::int32_t>(s.cellCountX) - x; break;
                    case 3: a = x; bb = static_cast<std::int32_t>(s.cellCountX) - z; break;
                    default: a = 0; bb = 0; break;
                }
                const std::uint64_t sampleZ = static_cast<std::uint64_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint64_t>(maxZ)) / s.vertexCountZ;
                const std::uint64_t sampleX = static_cast<std::uint64_t>(static_cast<std::uint32_t>(bb) * static_cast<std::uint64_t>(maxX)) / s.vertexCountX;
                if (!flag738 && waterGlare_) {
                    const std::uint32_t px = static_cast<std::uint32_t>(std::min<std::uint64_t>(sampleX, maxX));
                    const std::uint32_t pz = static_cast<std::uint32_t>(std::min<std::uint64_t>(sampleZ, maxZ));
                    const float t = static_cast<float>(s.waterGlareMaskBuf->Pixel(static_cast<int>(px), static_cast<int>(pz)) & 0xff) / 255.0f;
                    const float keep = 1.0f - t, add = t * 255.0f;
                    r = r * keep + add;
                    g = g * keep + add;
                    b = keep * b + add;
                }
            } else {
                r *= adjust[0]; g *= adjust[1]; b *= adjust[2];
            }
            TerrainSurfacePoint& p = s.waterPoints[idx];
            p.diffuse[0] = static_cast<std::uint8_t>(static_cast<int>(r));
            p.diffuse[1] = static_cast<std::uint8_t>(static_cast<int>(g));
            p.diffuse[2] = static_cast<std::uint8_t>(static_cast<int>(b));
            std::uint8_t alpha = 0xff;
            if (depth < s.waterDepthForMaxAlpha) {
                alpha = minAlpha8;
                if (depth > 0.0f) alpha = static_cast<std::uint8_t>(static_cast<std::int32_t>(std::nearbyint(depth * alphaScale + s.waterMinAlpha)));
            }
            p.diffuse[3] = alpha;
        }
    }
    if (flag738)
        for (std::int32_t z = waterRect_.top; z <= waterRect_.bottom; ++z)
            for (std::int32_t x = waterRect_.left; x <= waterRect_.right; ++x)
                s.waterPoints[static_cast<std::size_t>(s.vertexCountX) * static_cast<std::size_t>(z) + static_cast<std::size_t>(x)].diffuse[3] = 0xff;
    waterColorDone_ = true;
}

}  // namespace sc4
