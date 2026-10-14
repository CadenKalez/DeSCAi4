#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/cRZString.h"
#include "sc4/cSTETerrain.h"

using namespace sc4;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

static cGZDBSegmentPackedFile* Open(const std::string& path, bool write) {
    auto* seg = new cGZDBSegmentPackedFile();
    seg->AddRef();
    seg->Init();
    cRZString p(path.c_str());
    seg->SetPath(p);
    CHECK(seg->Open(true, write));
    return seg;
}

int main(int argc, char** argv) {
    // 4x4-cell terrain, altitude = 10 * x + 2 * z (a plane) except one raised vertex
    cSTETerrain t;
    TerrainTuning tuning;
    tuning.seaLevel = 20.0f;
    t.Init(4, 4, tuning);
    CHECK(t.VertexCountX() == 5 && t.VertexCount() == 25 && t.CellCountX() == 4);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) t.Vertices()[t.GetVertIndex(x, z)].altitude = 10.0f * x + 2.0f * z;
    t.RecomputeWaterLevel({0, 0, 4, 4});
    CHECK(t.GetAltitudeAtVertex(3, 2) == 34.0f && t.GetAltitudeAtVertex(5, 0) == 0.0f);
    CHECK(Near(t.GetAltitude(24.0f, 40.0f), 10.0f * 1.5f + 2.0f * 2.5f));     // plane: exact anywhere
    CHECK(Near(t.GetAltitude(64.0f, 64.0f), 10.0f * 4 + 2.0f * 4));          // the far edge uses the last cell
    CHECK(t.GetAltitudeAtNearestGrid(25.0f, 40.0f) == t.GetAltitudeAtVertex(2, 2));   // 2.5 rounds to even
    float corners[4];
    t.GetCellCornerAltitudes(1, 2, corners);
    CHECK(corners[0] == 10 + 4 && corners[1] == 10 + 6 && corners[2] == 20 + 6 && corners[3] == 20 + 4);

    // diagonal triangulation: a spike at vertex (1,1) shows up differently on either side
    t.Vertices()[t.GetVertIndex(1, 1)].altitude = 100.0f;
    float a = t.GetAltitude(24.0f, 20.0f);      // cell (1,1), lower-right triangle (fx > fz)
    float b = t.GetAltitude(20.0f, 24.0f);      // upper-left triangle
    CHECK(a > 0 && b > 0 && !Near(a, b));
    TerrainVector3 n{};
    t.GetFaceNormal(24.0f, 20.0f, n);
    CHECK(Near(n.x * n.x + n.y * n.y + n.z * n.z, 1.0f, 1e-3f) && n.y > 0);
    t.GetFaceNormal(-1.0f, 3.0f, n);            // out of bounds: untouched
    CHECK(Near(n.x * n.x + n.y * n.y + n.z * n.z, 1.0f, 1e-3f));
    t.Vertices()[t.GetVertIndex(1, 1)].altitude = 10 + 2;   // restore the plane

    // water: vertices below 20 m are flooded to the sea level
    CHECK(t.IsSeaVertex(0) && t.IsSeaVertex(1u, 0u) && !t.IsSeaVertex(3u, 3u));
    CHECK(t.GetWaterDepthAtVertex(0) == 20.0f && t.IsWaterVertex(0) && t.IsLandVertex(t.GetVertIndex(4, 4)));
    CHECK(t.GetWaterLevel(0) == 20.0f && Near(t.GetLerpedWaterLevel(8.0f, 8.0f), 20.0f));
    CHECK(t.CellHasWater(0, 0) && t.CellHasLand(3, 3) && !t.CellHasLand(0, 0));
    CHECK(Near(t.GetMaxSurfaceLevel(0, 0), 20.0f));      // flooded corner surfaces at 20 m
    CHECK(!t.IsShoreVertex(t.GetVertIndex(3, 0)));      // sea level is exactly 20 m: (2,0) is not below it
    t.SetSeaLevel(25.0f);
    CHECK(t.IsShoreVertex(t.GetVertIndex(3, 0)));       // 30 m land next to the 20 m vertex
    t.SetSeaLevel(20.0f);
    // changing the sea level re-floods; range is checked
    CHECK(!t.SetSeaLevel(500.0f) && t.GetSeaLevel() == 20.0f);
    CHECK(t.SetSeaLevel(30.0f) && t.GetWaterDepthAtVertex(2, 0) == 10.0f);
    CHECK(t.SetSeaLevel(20.0f));

    // SetAltitude: locked vertices are immune; a wet vertex keeps its surface
    t.LockAltitude({0, 0, 0, 0}, true);
    t.SetAltitude(0, 99.0f);
    CHECK(t.GetAltitude(0) == 0.0f);
    t.SetAltitude(t.GetVertIndex(1, 0), 15.0f);          // wet (depth 10 at altitude 10): surface 20 stays
    CHECK(t.GetWaterLevel(t.GetVertIndex(1, 0)) == 20.0f && t.GetWaterDepthAtVertex(t.GetVertIndex(1, 0)) == 5.0f);

    // cell flags and triangulation, with change notifications
    int notified = 0;
    t.SetCellChangedCallback([&](std::uint32_t, std::uint32_t) { ++notified; });
    t.HideEvenTriangle(1, 1, true);
    CHECK(t.IsEvenTriangleHidden(1, 1) && !t.IsOddTriangleHidden(1, 1) && !t.IsWholeCellHidden(1, 1));
    t.HideOddTriangle(1, 1, true);
    CHECK(t.IsWholeCellHidden(1, 1) && t.GetTerrainCellFlags(1, 1) == 3);
    CHECK(t.IsPointHidden(20.0f, 20.0f));
    t.HideTerrainCell(1, 1, false);
    CHECK(!t.IsWholeCellHidden(1, 1) && !t.IsPointHidden(20.0f, 20.0f));
    t.FlipInternalTriangulation(2, 2, true);
    CHECK(t.IsTriangulationFlipped(2, 2));
    t.FlipInternalTriangulation(2, 2, true);              // unchanged: no notification
    CHECK(t.SetTerrainCellFlags(0, 3, 0xf));
    CHECK(t.GetTerrainCellFlags(0, 3) == 0xf);
    CHECK(notified == 6);
    CHECK(!t.SetTerrainCellFlags(9, 9, 1));
    CHECK(t.GetNeighbors(0, 0) == 0x70 && t.GetNeighbors(2, 2) == 0xff);

    // min/max
    t.FindMinAndMaxHeights();
    CHECK(t.MinHeight() == 0.0f && t.MaxHeight() == 48.0f);
    float lo, hi;
    t.FindMinAndMaxHeights({1, 1, 2, 2}, lo, hi);
    CHECK(lo == 12.0f && hi == 24.0f);

    // save and load through a real package
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "sc4_terrain_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "city.dat").string();
    {
        auto* seg = Open(path, true);
        CHECK(t.Save(seg));
        seg->Close(); seg->Shutdown(); seg->Release();
        seg = Open(path, false);
        cSTETerrain loaded;
        CHECK(loaded.Load(seg) && loaded.LastLoadSucceeded());
        CHECK(loaded.VertexCountX() == 5 && loaded.VertexCountZ() == 5);
        loaded.Init(4, 4, tuning);   // lay out derived data over the loaded grid
        CHECK(loaded.Load(seg));
        CHECK(loaded.GetAltitudeAtVertex(3, 2) == t.GetAltitudeAtVertex(3, 2));
        CHECK(loaded.GetTerrainCellFlags(0, 3) == 0xf && loaded.IsTriangulationFlipped(2, 2));
        CHECK(loaded.MaxHeight() == 48.0f);
        seg->Close(); seg->Shutdown(); seg->Release();
    }
    std::filesystem::remove_all(dir);

    // a real game city
    if (argc > 1) {
        auto* seg = Open(argv[1], false);
        cSTETerrain city;
        CHECK(city.Load(seg) && city.LastLoadSucceeded());
        CHECK(city.VertexCountX() == 129 && city.VertexCountZ() == 129);
        city.Init(128, 128, tuning);
        CHECK(city.Load(seg));
        CHECK(city.MaxHeight() > city.MinHeight());
        city.FindMinAndMaxHeights();
        CHECK(city.MinHeight() >= 0.0f && city.MaxHeight() < 2000.0f);
        std::printf("real city terrain: %ux%u verts, altitude %.1f..%.1f m\n", city.VertexCountX(),
                    city.VertexCountZ(), city.MinHeight(), city.MaxHeight());
        seg->Close(); seg->Shutdown(); seg->Release();
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("sc4_terrain ok");
    return 0;
}
