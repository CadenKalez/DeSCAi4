// Observable behaviour of the driver's texture table: handle allocation / reuse, creation limits, parameters, alpha queries and
// the pixel conversions of LoadTextureLevel.
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "render/gd/TextureTable.h"

using namespace gd;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Recorder final : ITextureBackend {
    struct Surf { std::uint32_t format, w, h, mips; std::map<std::uint32_t, std::vector<std::uint8_t>> levels; };
    std::map<std::uint32_t, Surf> surfaces;
    int created = 0, destroyed = 0, uploads = 0;
    bool CreateSurface(std::uint32_t id, std::uint32_t f, std::uint32_t w, std::uint32_t h, std::uint32_t m) override { surfaces[id] = {f, w, h, m, {}}; ++created; return true; }
    void DestroySurface(std::uint32_t id) override { surfaces.erase(id); ++destroyed; }
    bool UploadLevel(std::uint32_t id, std::uint32_t level, std::uint32_t, std::uint32_t, std::uint32_t rw, std::uint32_t rh, const std::uint8_t* d, bool compressed) override {
        ++uploads;
        auto& s = surfaces[id];
        const std::uint32_t w = rw, h = rh;
        const std::size_t n = compressed ? ((w + 3) / 4) * ((h + 3) / 4) * (s.format == kFmtDXT1 ? 8 : 16) : std::size_t(w) * h * 4;
        s.levels[level].assign(d, d + n);
        return true;
    }
};
}  // namespace

int main() {
    Recorder rec;
    cTextureTable t(&rec);
    t.Init();
    CHECK(t.IsValid(0) && t.HasTexture(0) && !t.IsValid(1));
    CHECK(rec.surfaces[0].levels[0].size() == 4 && rec.surfaces[0].levels[0][0] == 255 && rec.surfaces[0].levels[0][3] == 255);   // opaque white
    // handles: sequential, LIFO reuse, no surface until created
    CHECK(t.NewTexture() == 1 && t.NewTexture() == 2 && t.NewTexture() == 3);
    CHECK(t.IsValid(2) && !t.HasTexture(2));
    t.FreeTexture(1);
    t.FreeTexture(3);
    CHECK(!t.IsValid(1) && !t.IsValid(3));
    CHECK(t.NewTexture() == 3 && t.NewTexture() == 1 && t.NewTexture() == 4);   // most recently freed first
    t.FreeTexture(99);                                                          // out of range: ignored
    CHECK(t.IsValid(4));
    // creation limits
    CHECK(!t.CreateTexture(50, 0, kFmtARGB8888, 8, 8, 1));                      // never allocated
    CHECK(!t.CreateTexture(1, 0, kFmtARGB8888, 4097, 8, 1) && !t.HasTexture(1));
    CHECK(!t.CreateTexture(1, 0, kFmtARGB8888, 0, 8, 1));
    CHECK(t.CreateTexture(1, 0, kFmtARGB8888, 4096, 1, 1) && t.HasTexture(1));
    CHECK(t.CreateTexture(2, 0, kFmtARGB4444, 4, 4, 3));
    // alpha bits: > 1 only
    CHECK(t.HasAlpha(1) && t.HasAlpha(2));
    CHECK(t.CreateTexture(4, 0, kFmtARGB1555, 4, 4, 1) && !t.HasAlpha(4));
    t.NewTexture();
    CHECK(t.CreateTexture(5, 0, kFmtRGB565, 4, 4, 1) && !t.HasAlpha(5));
    CHECK(!t.HasAlpha(77));
    // parameters
    TextureParams p;
    CHECK(t.GetTextureParams(2, p) && p.magFilter == 0 && p.minFilter == 0 && p.wrapS == 3 && p.wrapT == 3);
    CHECK(t.SetTextureParam(2, 1, 7) && t.SetTextureParam(2, 3, 2) && !t.SetTextureParam(2, 4, 1) && !t.SetTextureParam(99, 0, 1));
    t.GetTextureParams(2, p);
    CHECK(p.minFilter == 7 && p.wrapT == 2 && p.magFilter == 0);
    CHECK(!t.GetTextureParams(99, p));

    // ---- LoadTextureLevel ----
    // 8888 (BGRA bytes) into ARGB8888: unchanged
    CHECK(t.CreateTexture(1, 0, kFmtARGB8888, 2, 2, 1));
    const std::uint8_t px[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160};
    CHECK(t.LoadTextureLevel(1, 0, 0, 0, 2, 2, 3, 1, 0, px));
    CHECK(std::memcmp(rec.surfaces[1].levels[0].data(), px, 16) == 0);
    // same source into a 4444 texture: each channel truncated to 4 bits and re-expanded
    CHECK(t.LoadTextureLevel(2, 0, 0, 0, 2, 2, 3, 1, 0, px));
    // texture 2 is 4x4 with 3 mips; only the first uploaded 2x2 region matters for the check of precision
    const auto& l = rec.surfaces[2].levels[0];
    CHECK(l[0] == 0x00 && l[1] == 0x11 && l[2] == 0x11 && l[3] == 0x22);   // B,G,R,A = 10,20,30,40 -> nibbles 0,1,1,2 expanded
    // levels beyond the mip count fail, level 0 never does
    CHECK(!t.LoadTextureLevel(2, 3, 0, 0, 1, 1, 3, 1, 0, px));
    CHECK(t.LoadTextureLevel(2, 2, 0, 0, 1, 1, 3, 1, 0, px));
    CHECK(!t.LoadTextureLevel(2, 1, 0, 0, 2, 2, 5, 1, 0, px));              // unsupported (format, type)
    CHECK(!t.LoadTextureLevel(3, 0, 0, 0, 2, 2, 3, 1, 0, px));              // valid handle without a surface
    CHECK(!t.LoadTextureLevel(88, 0, 0, 0, 2, 2, 3, 1, 0, px));
    // 565 source (type 14) into 8888 expands with bit replication
    CHECK(t.CreateTexture(5, 0, kFmtARGB8888, 1, 1, 1));
    const std::uint16_t red565 = 0xf800;   // RGB565 "REV RGB": R in the top bits as the converter reads it
    CHECK(t.LoadTextureLevel(5, 0, 0, 0, 1, 1, 2, 14, 0, &red565));
    CHECK(rec.surfaces[5].levels[0][2] == 255 && rec.surfaces[5].levels[0][1] == 0 && rec.surfaces[5].levels[0][0] == 0);
    // row length (in source pixels) skips the padding between rows
    CHECK(t.CreateTexture(5, 0, kFmtARGB8888, 2, 2, 1));
    const std::uint32_t wide[8] = {1, 2, 99, 99, 3, 4, 99, 99};   // 2 pixels per row used, row pitch 4
    CHECK(t.LoadTextureLevel(5, 0, 0, 0, 2, 2, 3, 1, 4, wide));
    std::uint32_t got[4];
    std::memcpy(got, rec.surfaces[5].levels[0].data(), 16);
    CHECK(got[0] == 1 && got[1] == 2 && got[2] == 3 && got[3] == 4);
    // compressed: only matching block format, whole levels only
    CHECK(t.CreateTexture(5, 0, kFmtDXT1, 8, 8, 2));
    std::uint8_t blocks[32];
    for (int i = 0; i < 32; ++i) blocks[i] = static_cast<std::uint8_t>(i);
    CHECK(t.LoadTextureLevel(5, 0, 0, 0, 8, 8, 7, 1, 0, blocks));          // DXT1 source (format 7, type 1)
    CHECK(rec.surfaces[5].levels[0].size() == 32 && rec.surfaces[5].levels[0][31] == 31);
    const int before = rec.uploads;
    CHECK(t.LoadTextureLevel(5, 0, 4, 0, 4, 4, 7, 1, 0, blocks) && rec.uploads == before);   // partial write: succeeds, copies nothing
    CHECK(!t.LoadTextureLevel(5, 0, 0, 0, 8, 8, 9, 1, 0, blocks));          // DXT3 source into a DXT1 texture
    CHECK(!t.LoadTextureLevel(5, 0, 0, 0, 8, 8, 3, 1, 0, px));              // uncompressed source into a DXT texture
    // freeing destroys the surface and recycles the handle
    const int d = rec.destroyed;
    t.FreeTexture(5);
    CHECK(rec.destroyed == d + 1 && !t.IsValid(5));
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
