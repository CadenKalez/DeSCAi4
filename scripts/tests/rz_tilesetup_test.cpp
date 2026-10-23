// cRZTileSetup: invariants of the tiling over many image sizes plus a few hand-worked layouts.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "rz/TileSetup.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s (w=%d h=%d)\n", __LINE__, #c, w, h); ++failures; return false; } } while (0)

static bool pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

static bool Verify(int w, int h, unsigned minT = 16, unsigned maxT = 256) {
    cRZTileSetup t;
    t.Init({w, h}, minT, maxT, 1);
    std::vector<int> covered(static_cast<std::size_t>(w) * h, 0);
    unsigned maxFragments = 0;
    for (int i = 0; i < t.GetTileCount(); ++i) {
        const cRZTile& tile = t.GetTile(i);
        CHECK(pow2(tile.size.width) && pow2(tile.size.height));
        CHECK(tile.size.width >= 0 && static_cast<unsigned>(tile.size.width) <= maxT && static_cast<unsigned>(tile.size.height) <= maxT);
        CHECK(!tile.subTiles.empty());
        maxFragments = std::max<unsigned>(maxFragments, tile.subTiles.size());
        std::vector<int> used(static_cast<std::size_t>(tile.size.width) * tile.size.height, 0);
        for (const cRZSubTile& s : tile.subTiles) {
            CHECK(s.width > 0 && s.height > 0);
            CHECK(s.tileX >= 0 && s.tileY >= 0 && s.tileX + s.width <= tile.size.width && s.tileY + s.height <= tile.size.height);
            CHECK(s.x >= 0 && s.y >= 0 && s.x + s.width <= w && s.y + s.height <= h);
            for (int y = 0; y < s.height; ++y)
                for (int x = 0; x < s.width; ++x) {
                    CHECK(used[(s.tileY + y) * tile.size.width + s.tileX + x]++ == 0);
                    CHECK(covered[(s.y + y) * w + s.x + x]++ == 0);
                }
        }
    }
    for (int v : covered) CHECK(v == 1);
    CHECK(t.GetMaxFragmentsPerTile() == maxFragments || t.GetMaxFragmentsPerTile() >= maxFragments);
    return true;
}

int main() {
    // hand-worked
    {
        cRZTileSetup t;
        t.Init({256, 256}, 16, 256, 1);
        if (t.GetTileCount() != 1 || t.GetTile(0).size.width != 256 || t.GetMaxFragmentsPerTile() != 1) { std::printf("FAIL 256x256\n"); ++failures; }
        cRZTileSetup u;
        u.Init({100, 30}, 16, 256, 1);   // 3000 missing pixels: 64x64 (16 -> 32x16 .. 64x64); 64x30 + 36x30 share the tile
        if (u.GetTileCount() != 1 || u.GetTile(0).size.width != 64 || u.GetTile(0).size.height != 64 || u.GetTile(0).subTiles.size() != 2 ||
            u.GetMaxFragmentsPerTile() != 2 || u.GetTile(0).subTiles[1].x != 64 || u.GetTile(0).subTiles[1].tileY != 30) {
            std::printf("FAIL 100x30\n"); ++failures;
        }
        cRZTileSetup v;
        v.Init({512, 256}, 16, 256, 1);
        if (v.GetTileCount() != 2 || v.GetTile(1).subTiles[0].x != 256) { std::printf("FAIL 512x256\n"); ++failures; }
        cRZTileSetup z;
        z.Init({1, 1}, 16, 256, 1);
        if (z.GetTileCount() != 1 || z.GetTile(0).size.width != 32 && z.GetTile(0).size.width != 16) { std::printf("FAIL 1x1\n"); ++failures; }
    }
    for (int w : {1, 2, 3, 15, 16, 17, 31, 33, 64, 100, 127, 128, 129, 255, 256, 257, 300, 511, 512, 513, 640, 800, 1000})
        for (int h : {1, 5, 16, 17, 31, 64, 100, 128, 255, 256, 257, 300, 480, 513, 768})
            if (!Verify(w, h)) return 1;
    for (int w = 1; w < 140; w += 7)
        for (int h = 1; h < 140; h += 11)
            if (!Verify(w, h, 8, 64)) return 1;
    if (failures == 0) std::printf("rz_tilesetup OK\n");
    return failures == 0 ? 0 : 1;
}
