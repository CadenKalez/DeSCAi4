#include <cstdio>

#include "sc4/EffectMaps.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Terrain final : ITerrainMapSource {
    float water = 100.0f;
    float WaterLevel() override { return water; }
    int CellsX() override { return 8; }
    int CellsZ() override { return 6; }
    float CellSize() override { return 16.0f; }
    float VertexHeight(int x, int z) override { return 150.0f + x * 2.0f + z; }
};
}  // namespace

int main() {
    Terrain terrain;
    SetTerrainMapSource(&terrain);
    auto* maps = new cSC4EffectMaps();
    maps->AddRef();
    CHECK(maps->Init() && maps->MapCount() == 2 && maps->MapVertsX() == 9 && maps->MapVertsZ() == 7);
    CHECK(maps->MapVertexHeight(1, 3, 2) == 150.0f + 6.0f + 2.0f);

    maps->AddRectToMap(1, {2, 2, 3, 3}, 6.0f, 1);            // 3 per cell in the rectangle, 3 in the ring around it
    CHECK(maps->Dirty() && maps->MapValue(1, 2, 2) == 6.0f);   // ring 0 and ring 1 both cover it
    CHECK(maps->MapValue(1, 1, 1) == 3.0f && maps->MapValue(1, 4, 4) == 3.0f && maps->MapValue(1, 0, 0) == 0.0f);
    CHECK(maps->MapVertexHeight(1, 2, 2) == 150.0f + 4.0f + 2.0f + 6.0f);
    maps->ClearMaps();
    CHECK(maps->MapValue(1, 2, 2) == 0.0f);

    maps->SetValueAboveTerrain(1, {4, 3, 4, 3}, 8.0f, 2);       // 8, then 8*(2/3), then 8*(4/9) in the rings
    CHECK(maps->MapValue(1, 4, 3) == 8.0f);
    CHECK(maps->MapValue(1, 3, 3) > 5.3f && maps->MapValue(1, 3, 3) < 5.4f);
    CHECK(maps->MapValue(1, 2, 3) > 3.5f && maps->MapValue(1, 2, 3) < 3.6f);
    maps->SetValueAboveTerrain(1, {4, 3, 4, 3}, 1.0f, 0);       // never lowers a cell
    CHECK(maps->MapValue(1, 4, 3) == 8.0f);
    maps->ResetValueToTerrain(1, {0, 0, 8, 6});
    CHECK(maps->MapValue(1, 4, 3) == 0.0f);

    // below the water surface the offset only lifts the vertex towards (water + offset - 50)
    terrain.water = 200.0f;
    maps->AddRectToMap(1, {0, 0, 0, 0}, 5.0f, 0);
    CHECK(maps->MapVertexHeight(1, 0, 0) == 155.0f);            // 150 <= 200 + 5 - 50 -> 155
    CHECK(maps->MapVertexHeight(1, 8, 6) == 150.0f + 16.0f + 6.0f);   // untouched vertex: offset -50+200=150 < 172

    // out-of-range map index is ignored
    maps->AddRectToMap(3, {0, 0, 1, 1}, 1.0f, 0);
    CHECK(maps->MapValue(1, 1, 1) == 0.0f);
    float n[3];
    maps->MapLerpedNormal(1, 20.0f, 20.0f, n);
    CHECK(n[1] > 0.9f && n[1] <= 1.0f);
    CHECK(maps->MapLerpedHeight(1, 16.0f, 16.0f) == maps->MapVertexHeight(1, 1, 1));
    maps->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
