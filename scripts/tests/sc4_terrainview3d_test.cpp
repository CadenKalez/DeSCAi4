// cSTETerrainView3D: data setup, texture spans, culling, level cells, picking, messages and the draw passes against
// a recording fake draw context.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "sc4/cSTETerrain.h"
#include "sc4/cSTETerrainView3D.h"

using namespace sc4;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

struct FakeBuffer : IPixelBuffer {
    int w, h; std::uint32_t colour;
    FakeBuffer(int w_, int h_, std::uint32_t c) : w(w_), h(h_), colour(c) {}
    int Width() const override { return w; }
    int Height() const override { return h; }
    std::uint32_t Pixel(int x, int y) const override { return colour + static_cast<std::uint32_t>(x + y); }
};

struct FakeHost : ITerrainViewHost {
    std::vector<std::uint32_t> textures = {1, 2, 3};
    int textureLoads = 0, lightCalls = 0, redisplay = 0, registered = 0;
    bool plumbingDisplay = false;
    const std::vector<std::uint32_t>& TexturesUsedInTerrainType(int) const override { return textures; }
    std::shared_ptr<TerrainTextureBinding> LoadTerrainTexture(std::uint32_t instance) override {
        ++textureLoads;
        auto b = std::make_shared<TerrainTextureBinding>();
        b->ids = {static_cast<int>(100 + instance)};
        return b;
    }
    std::shared_ptr<TerrainTextureBinding> LoadWaterTexture(int zoom, bool) override {
        auto b = std::make_shared<TerrainTextureBinding>(); b->ids = {500 + zoom}; return b;
    }
    std::shared_ptr<TerrainTextureBinding> LoadGridTexture() override {
        auto b = std::make_shared<TerrainTextureBinding>(); b->ids = {900}; return b;
    }
    std::shared_ptr<IPixelBuffer> GetBuffer(std::uint32_t instance, int) override {
        if (instance == 0xc9efb727) return std::make_shared<FakeBuffer>(1, 16, 0x102030);
        if (instance == 0x69efb6f8) return std::make_shared<FakeBuffer>(1, 16, 0x305070);
        if (instance == 0x69efb7da) return std::make_shared<FakeBuffer>(8, 8, 0);
        return std::make_shared<FakeBuffer>(0x101, 1, 127);
    }
    bool HasTuningExemplar() override { return true; }
    bool ReadTuningFloat(std::uint32_t id, float& v) override {
        if (id == 0x89a312c0) { v = 100.0f; return true; }
        if (id == 0x29ab15c0) { v = 10.0f; return true; }
        if (id == 0x29ab15c1) { v = 50.0f; return true; }
        return false;
    }
    bool ReadTuningFloatVector(std::uint32_t id, std::vector<float>& v) override {
        if (id == 0x29ab15c6) { v = {4.0f, 8.0f}; return true; }
        if (id == 0x29ab15c5) { v = {0.5f, 0.5f, 0.5f}; return true; }
        return false;
    }
    bool HasLighting() const override { return true; }
    void GetPointLight(const float*, const float*, std::uint8_t out[3]) const override { out[0] = 10; out[1] = 20; out[2] = 30; }
    void ApplyTerrainLighting(const TerrainRect&) override { ++lightCalls; }
    void RegisterMessage(std::uint32_t) override { ++registered; }
    void RedisplayTerrain(const TerrainRect&) override { ++redisplay; }
    bool GetBoolPlumbingDisplay() const override { return plumbingDisplay; }
};

struct FakeDC : ITerrainDrawContext {
    TerrainFrustumPlanes draw, view;
    int zoom = 5, rotation = 0, pass = 0;
    int depthOffset = 0;
    std::vector<std::string> log;
    int primsIndexed = 0, prims = 0, textures = 0;
    std::vector<int> textureIds;
    FakeDC() {
        for (auto* f : {&draw, &view}) {
            f->minZ = -50; f->maxZ = 1000;
            f->leftOrigin[0] = -10; f->leftOrigin[1] = 0; f->leftDir[0] = 0; f->leftDir[1] = 1;
            f->rightOrigin[0] = 1000; f->rightOrigin[1] = 0; f->rightDir[0] = 0; f->rightDir[1] = 1;
            f->planeA = 0; f->planeB = 1; f->planeC = 0; f->planeD = 100000;     // everything is above the threshold
        }
    }
    int Zoom() const override { return zoom; }
    int Rotation() const override { return rotation; }
    int DepthOffset() const override { return depthOffset; }
    int Pass() const override { return pass; }
    const TerrainFrustumPlanes& DrawFrustum() const override { return draw; }
    const TerrainFrustumPlanes& ViewFrustum() const override { return view; }
    void GetVisibleEdges(int& a, int& b) const override { a = 5; b = 4; }       // (6 - a) % 4 = 1, (6 - b) % 4 = 2
    void SetDepthOffset(int o) override { depthOffset = o; }
    void SetTexture(int id, int) override { ++textures; textureIds.push_back(id); }
    void DrawPrimsIndexed(int, int, int) override { ++primsIndexed; }
    void DrawPrims(int, int, int, const void*) override { ++prims; }
};

static void MakeTerrain(cSTETerrain& t, ITerrainViewHost*) {
    TerrainTuning tuning;
    tuning.seaLevel = 15.0f;
    t.Init(8, 8, tuning);
    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 9; ++x) t.Vertices()[t.GetVertIndex(x, z)].altitude = 10.0f + x * 2.0f + z;
    t.RecomputeWaterLevel({0, 0, 8, 8});
}

int main() {
    FakeHost host;
    cSTETerrainView3D::StaticShutdown();
    CHECK(cSTETerrainView3D::StaticInit(&host));
    auto& S = cSTETerrainView3D::S();
    CHECK(Near(S.terrainTexTilingFactor, 1.0f));                       // 100 / 100
    CHECK(S.cityEdgeTexTilingFactors[0] == 0.25f && S.cityEdgeTexTilingFactors[1] == 0.125f);
    CHECK(Near(S.cityEdgeLayerHeights[0][0], (127 - 127.0f) * 0.5f + 4.0f));
    CHECK(S.waterBoxGradientHt == 15);
    CHECK(S.waterColorDepthGradientHt == 0);                            // original bug kept: never initialised

    cSTETerrain terrain;
    MakeTerrain(terrain, &host);
    cSTETerrainView3D view(&host);
    CHECK(view.Init(&terrain));
    CHECK(!view.Init(&terrain));
    CHECK(S.cellCountX == 8 && S.vertexCountX == 9 && S.numVertices == 81 && S.citySizeX == 128.0f);
    CHECK(S.points.size() == 81 && S.points[10].x == 16.0f && S.points[10].z == 16.0f);
    CHECK(Near(S.points[10].y, terrain.Vertices()[10].altitude));
    CHECK(S.numTriangleIndicesPerRow == 48);
    CHECK(S.triangleIndices[0] == 0 && S.triangleIndices[1] == 9 && S.triangleIndices[2] == 10 && S.triangleIndices[5] == 1);
    CHECK(S.flippedTriangleIndices[3] == 1 && S.flippedTriangleIndices[4] == 9);
    CHECK(S.levelCellQuadTriIndices[6] == 4 && S.levelCellQuadTriIndices[8] == 6 && S.levelCellQuadTriIndices[11] == 4);
    CHECK(S.allTriangleIndices[6] == 1 && S.allTriangleIndices[7] == 10);          // cell (1, 0)
    CHECK(S.borderPoints[0][0].size() == 18 && S.waterBoxPoints[1].size() == 18);
    CHECK(S.borderPoints[0][0][0].z == 128.0f && S.borderPoints[2][0][3].x == 128.0f - 3 * 16.0f);
    CHECK(S.waterPoints[10].y == 15.0f || S.waterPoints[10].y > terrain.Vertices()[10].altitude);
    // border strata heights follow the terrain edge
    CHECK(Near(S.borderPoints[3][0][2].y, terrain.Vertices()[terrain.GetVertIndex(0, 2)].altitude));
    CHECK(Near(S.waterBoxPoints[3][9 + 2].y, std::min(terrain.Vertices()[terrain.GetVertIndex(0, 2)].altitude, 15.0f)));

    // ---- texture spans -------------------------------------------------------------------------------------
    auto setKey = [&](int x, int z, int layer, std::uint16_t key, std::uint8_t alpha) {
        auto& v = terrain.Vertices()[terrain.GetVertIndex(x, z)];
        std::memcpy(&v.surface[6 + layer * 4], &key, 2);
        v.surface[8 + layer * 4] = alpha;
    };
    for (int x = 2; x <= 5; ++x) { setKey(x, 3, 0, 0x1001, 200); setKey(x, 4, 0, 0x1001, 200); }
    setKey(7, 3, 0, 0x1002, 90); setKey(7, 4, 0, 0x1002, 90);
    S.texSpanInfoDirty[3] = false;
    cSTETerrainView3D::MarkTextureSpanInfoDirty(3, 3);
    CHECK(S.texSpanInfoDirty[3]);
    view.ComputeTextureSpansForRow(3);
    CHECK(!S.texSpanInfoDirty[3]);
    CHECK(S.textureSpansForRows[3].size() == 2);
    CHECK(S.textureSpansForRows[3].front().textureKey == 0x1001);
    CHECK(S.textureSpansForRows[3].front().spans.size() == 1 && S.textureSpansForRows[3].front().spans.front().first == 1 &&
          S.textureSpansForRows[3].front().spans.front().last == 6);
    // merging overlapping spans
    std::list<TerrainTextureSpanInfo> row;
    cSTETerrainView3D::AddTextureSpan(row, 7, 1, 2);        // [0, 3]
    cSTETerrainView3D::AddTextureSpan(row, 7, 5, 6);        // [4, 7]
    CHECK(row.size() == 1 && row.front().spans.size() == 2);
    cSTETerrainView3D::AddTextureSpan(row, 7, 3, 4);        // [2, 5] overlaps both
    CHECK(row.size() == 1 && row.front().spans.size() == 1 && row.front().spans.front().first == 0 && row.front().spans.front().last == 7);
    cSTETerrainView3D::AddTextureSpan(row, 3, 0, 0);
    CHECK(row.size() == 2 && row.front().textureKey == 3);

    // ---- culling ---------------------------------------------------------------------------------------------
    FakeDC dc;
    view.ComputeDrawFrustumOptimizationInfo(dc);
    const TerrainFrustumCull& c = view.DrawCull();
    CHECK(c.rowBegin == 0 && c.rowEnd == 9 && c.rowLast == 8);
    CHECK(c.colStart[0] == 0 && c.colEnd[0] == 9 && c.colMin == 0 && c.colMax == 9);
    CHECK(c.terrainVisible[0] && c.terrainVisible[8]);
    CHECK(c.terrainRowBegin == 0 && c.terrainRowEnd == 9 && c.terrainRowLast == 8);
    CHECK(view.VisibleEdgeA() == 1 && view.VisibleEdgeB() == 2);
    int rb = 0, re = 0; const std::int32_t* cs = nullptr; const std::int32_t* ce = nullptr;
    view.GetCullingInfo(&rb, &re, &cs, &ce);
    CHECK(rb == 0 && re == 9 && cs[4] == 0 && ce[4] == 9);
    // a frustum that only sees z in [30, 70] and x in [20, 60]
    FakeDC narrow;
    narrow.draw.minZ = 30; narrow.draw.maxZ = 70;
    narrow.draw.leftOrigin[0] = 20; narrow.draw.rightOrigin[0] = 60;
    view.ComputeDrawRectsForDrawFrustum(narrow);
    CHECK(c.rowBegin == 1 && c.rowEnd == 6);                             // floor(30/16)=1, ceil(70/16)+1=6
    CHECK(c.colStart[3] == 1 && c.colEnd[3] == 5);
    CHECK(c.colMin == 1 && c.colMax == 5);
    // height culling: a plane that requires altitudes >= 1e9 hides everything
    FakeDC high; high.draw.minZ = -50; high.draw.planeD = -1e9f; high.draw.planeB = 1.0f;
    view.ComputeDrawRectsForDrawFrustum(high);
    view.ComputeHeightBasedCullingInfoForDrawFrustum(high);
    CHECK(!c.terrainVisible[3] && c.terrainRowEnd >= c.terrainRowBegin);

    // ---- level cells and triangles -------------------------------------------------------------------------------
    view.ComputeDrawFrustumOptimizationInfo(dc);
    S.texSpanInfoDirty.assign(S.texSpanInfoDirty.size(), true);
    view.LevelCell(3, 2, 99.0f);
    const std::size_t rowBase = 9 * 2;
    CHECK(S.allLevelCellIndices[rowBase + 3] == 0 && S.allLevelCellIndices[rowBase + 4] == 1 && S.allLevelCellIndices[rowBase + 8] == 1);
    CHECK(S.levelCellQuads[2].size() == 1 && S.levelCellInfos[2][0].column == 3 && S.levelCellInfos[2][0].height == 99.0f);
    CHECK(S.levelCellQuads[2][0].corner[0].y == 99.0f && S.levelCellQuads[2][0].corner[2].x == 64.0f);
    CHECK(S.levelCellQuads[2][0].corner[0].diffuse[0] == 10 && S.levelCellQuads[2][0].corner[0].diffuse[3] == 0xff);
    CHECK((terrain.Vertices()[terrain.GetVertIndex(3, 2)].flags & 0x60) == 0x60);
    // the hidden cell now has degenerate triangles
    const std::size_t cellIdx = 2 * 48 + 3 * 6;
    CHECK(S.allTriangleIndices[cellIdx] == 3 && S.allTriangleIndices[cellIdx + 1] == 3 && S.allTriangleIndices[cellIdx + 5] == 3);
    view.LevelCell(5, 2, 50.0f);
    view.LevelCell(1, 2, 40.0f);
    CHECK(S.levelCellInfos[2].size() == 3 && S.levelCellInfos[2][0].column == 1 && S.levelCellInfos[2][2].column == 5);
    CHECK(S.allLevelCellIndices[rowBase + 2] == 1 && S.allLevelCellIndices[rowBase + 4] == 2 && S.allLevelCellIndices[rowBase + 8] == 3);
    view.LevelCell(3, 2, 77.0f);                                              // update in place
    CHECK(S.levelCellInfos[2].size() == 3 && S.levelCellInfos[2][1].height == 77.0f);
    view.RemoveCellLevelling(3, 2);
    CHECK(S.levelCellInfos[2].size() == 2 && S.allLevelCellIndices[rowBase + 4] == 1 && S.allLevelCellIndices[rowBase + 8] == 2);
    CHECK((terrain.Vertices()[terrain.GetVertIndex(3, 2)].flags & 0x60) == 0);
    CHECK(S.allTriangleIndices[cellIdx] == 3 && S.allTriangleIndices[cellIdx + 1] == 12 && S.allTriangleIndices[cellIdx + 2] == 13);
    view.RemoveCellLevelling(3, 2);                                           // nothing there: no-op
    CHECK(S.levelCellInfos[2].size() == 2);
    // flipped + alternate triangulation
    terrain.Vertices()[terrain.GetVertIndex(0, 0)].flags = 0x1000;
    view.SetTriangleIndicesForCell(0, 0);
    CHECK(S.allTriangleIndices[0] == 0 && S.allTriangleIndices[1] == 9 && S.allTriangleIndices[2] == 1 &&
          S.allTriangleIndices[3] == 1 && S.allTriangleIndices[4] == 9 && S.allTriangleIndices[5] == 10);
    terrain.Vertices()[terrain.GetVertIndex(0, 0)].flags = 0x0200;
    view.SetTriangleIndicesForCell(0, 0);
    CHECK(S.allTriangleIndices[0] == 10 && S.allTriangleIndices[1] == 9 && S.allTriangleIndices[2] == 0 &&
          S.allTriangleIndices[3] == 1 && S.allTriangleIndices[4] == 10 && S.allTriangleIndices[5] == 0);
    terrain.Vertices()[terrain.GetVertIndex(0, 0)].flags = 0;
    view.SetTriangleIndicesForCell(0, 0);

    // ---- picking ---------------------------------------------------------------------------------------------
    const float origin[3] = {40.0f, 500.0f, 40.0f}, down[3] = {0, -1, 0};
    float hit[3] = {0, 0, 0};
    CHECK(view.Pick(origin, down, hit));
    CHECK(Near(hit[0], 40.0f, 0.5f) && Near(hit[2], 40.0f, 0.5f) && Near(hit[1], terrain.GetAltitude(40.0f, 40.0f), 0.5f));
    const float away[3] = {-10000.0f, 500.0f, -10000.0f};
    CHECK(!view.Pick(away, down, hit));

    // ---- messages / persistence / cheats --------------------------------------------------------------------------
    CHECK(!view.GetDisplayGridFlag());
    TerrainViewMessage toggle; toggle.type = 0x277a9364;
    CHECK(view.DoMessage(toggle) && view.GetDisplayGridFlag() && host.redisplay == 1);
    auto bytes = view.Write();
    CHECK(bytes.size() == 3 && bytes[0] == 1 && bytes[2] == 1);
    view.SetDisplayGridFlag(false);
    CHECK(view.Read(bytes) && view.GetDisplayGridFlag());
    CHECK(!view.Read({2, 0, 1}));
    float night = 0.75f;
    TerrainViewMessage nightMsg; nightMsg.type = 0xc9da96ea; std::memcpy(&nightMsg.data2, &night, 4);
    CHECK(view.DoMessage(nightMsg) && view.NightFactor() == 0.75f && !view.LightingDone());
    TerrainViewMessage heightMsg; heightMsg.type = 0x278e6003; heightMsg.data1 = (1u << 16) | 3u; heightMsg.data2 = (1u << 16) | 3u;
    terrain.Vertices()[terrain.GetVertIndex(2, 2)].altitude = 123.0f;
    CHECK(view.DoMessage(heightMsg));
    view.ObtainHeightFieldInfo({2, 2, 2, 2});
    CHECK(S.points[2 * 9 + 2].y == 123.0f);
    CHECK(view.HandleCheatCommand({"level", "6", "6", "42"}) && S.levelCellInfos[6].size() == 1);
    CHECK(view.HandleCheatCommand({"unlevel", "6", "6"}) && S.levelCellInfos[6].empty());
    CHECK(!view.HandleCheatCommand({"level", "1"}) && !view.HandleCheatCommand({}));
    CHECK(view.QueryInterface(0xc9b84e10, nullptr) == 1 && view.QueryInterface(0x1234, nullptr) == 0);

    // cIGZSerializable identity: the city-load serializer reaches Read/Write/GetGZCLSID through the
    // 0xe4fda3d4 view. (A full SC4ReadGZSerializable round trip needs sc4/DBSegment.cpp, which is not in
    // this test's link set; the stream Write/Read above covers the payload.)
    {
        void* sz = nullptr;
        CHECK(view.QueryInterface(0xe4fda3d4, &sz) && sz != nullptr);
        auto* serializable = static_cast<cIGZSerializable*>(sz);
        CHECK(serializable->GetGZCLSID() == cSTETerrainView3D::kGZCLSID);
        serializable->Release();   // undo the QueryInterface reference
    }

    // ---- lighting / rects ------------------------------------------------------------------------------------------
    view.SetLightingUpdateRect(nullptr);
    view.SetLightingUpdateRect(&(const TerrainRect&)TerrainRect{1, 1, 2, 2});
    CHECK(view.LightRect().left == 0 && view.LightRect().right == 8);
    view.DoLighting();
    CHECK(view.LightingDone() && host.lightCalls == 1);
    view.DoLighting();
    CHECK(host.lightCalls == 1);
    view.SetWaterGlareStatus(false);
    CHECK(!view.IsWaterGlareEnabled() && !view.WaterColorDone());

    // ---- zoom / rotation ------------------------------------------------------------------------------------------
    S.curZoom = 0;
    dc.zoom = 0; dc.rotation = 1;
    S.textureZoom = 5; S.textureRotation = 4;
    view.AssignTerrainAndWaterTextureCoordinates(dc);
    CHECK(S.textureZoom == 0 && S.textureRotation == 1);
    const TerrainSurfacePoint& p = S.points[1 * 9 + 2];                               // x = 32, z = 16, scale 1.0 * 1.0
    CHECK(Near(p.u, p.z) && Near(p.v, 128.0f - p.x));
    dc.rotation = 2;
    view.AssignTerrainAndWaterTextureCoordinates(dc);
    CHECK(Near(p.u, 128.0f - p.x) && Near(p.v, 128.0f - p.z));
    view.AssignTerrainAndWaterTextureCoordinates(dc);                                 // unchanged: no-op
    S.curZoom = 5;

    // ---- draw passes ------------------------------------------------------------------------------------------------
    FakeDC draw;
    draw.zoom = 0;
    S.curZoom = 0;
    S.curRotation = 0;
    S.displayGrid = true;
    S.displayTerrainSurface = true;
    host.textureLoads = 0;
    cSTETerrainView3D::ChangeTerrainType(7);                                           // reload textures for the type
    CHECK(host.textureLoads >= 3 && S.terrainTexturesLoaded[0]);
    CHECK(cSTETerrainView3D::S().terrainTextureBinding[0].size() >= 4 && cSTETerrainView3D::S().terrainTextureBinding[0][2]->ids[0] == 102);
    cSTETerrainView3D::SetUpTexturesForCurrentTerrainTypeAndZoom(0);                 // already loaded: no-op
    CHECK(view.Update(draw));
    CHECK(draw.primsIndexed > 0);
    CHECK(draw.depthOffset == 0);                                                     // restored
    // textured pass picked the texture bindings of keys 0x1001/0x1002 (ids 100 + base key & 0xffff0fff)
    S.curZoom = 0;
    int terrainDraws = draw.primsIndexed;
    draw.pass = 1;
    CHECK(view.Update(draw));
    CHECK(draw.primsIndexed > terrainDraws);                                          // water surface and water box
    CHECK(view.WaterColorDone());
    // water colour: depth 0 vertices get the clamped gradient colour (pixel 0) and the minimum alpha
    view.SetWaterColorUpdateRect(nullptr);
    view.SetColorOfWaterSurface();
    CHECK(view.WaterColorDone());
    // the draw context disabled: nothing happens
    struct Off : FakeDC { bool DrawEnabled() const override { return false; } } off;
    CHECK(view.Update(off) && off.primsIndexed == 0);

    // ---- shutdown --------------------------------------------------------------------------------------------------------
    view.Shutdown();
    CHECK(!view.IsInitialized() && view.Selection() == nullptr);
    CHECK(cSTETerrainView3D::StaticShutdown());
    CHECK(!cSTETerrainView3D::S().inited && cSTETerrainView3D::S().points.empty());

    std::printf(failures ? "sc4_terrainview3d: %d FAILED\n" : "sc4_terrainview3d: all ok\n", failures);
    return failures ? 1 : 0;
}
