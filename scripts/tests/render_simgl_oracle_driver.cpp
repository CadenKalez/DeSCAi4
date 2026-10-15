// Compares src/render/SimGL.cpp with the Ghidra decompilation of nRZSimGL (work/simgl/oracle.c).
#include "render/SimGL.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
int oracle_1001e3694(int);
uint32_t oracle_1001e3770(uint32_t, const uint32_t*);
int oracle_1001e37ee(unsigned long);
int oracle_1001e3918(uint32_t, uint32_t, int);
uint32_t oracle_1001e3972(uint32_t, uint32_t);
void oracle_1001e39a8(int, const void*, void*, uint32_t, uint32_t, uint32_t);
uint64_t oracle_10032b348(uint32_t, uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
void oracle_10035feb2(const void*, uint32_t, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*BltFn)(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_10036003c(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_100360067(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_100360092(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_100360101(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_1003601f1(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_10036028f(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_100360390(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_10036048b(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
void oracle_100360636(void*, long, const void*, long, uint32_t, uint32_t, uint32_t);
}
static int bad = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++bad; } } while (0)

static uint32_t R32() { return uint32_t(rand()) * 2654435761u ^ uint32_t(rand()); }

int main() {
    srand(7);
    // ---- vertex formats
    const int formats[] = {0, 1, 2, 3, 4, 10, 11, 12, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 100};
    for (int f : formats) CHECK(nRZSimGL::PackStandardVertexFormat(f) == oracle_1001e3694(f), "pack %d", f);
    for (int i = 0; i < 4000; ++i) {
        const int f = int(R32());
        CHECK(nRZSimGL::PackStandardVertexFormat(f) == oracle_1001e3694(f), "pack rnd");
        CHECK(nRZSimGL::StandardVFStride(uint32_t(f)) == oracle_1001e37ee(uint64_t(int64_t(f))), "stride rnd %d", f);
    }
    for (int f : formats) {
        CHECK(nRZSimGL::StandardVFStride(uint32_t(f)) == oracle_1001e37ee(uint64_t(int64_t(f))), "stride %d", f);
        for (uint32_t t = 0; t < 10; ++t) {
            CHECK(nRZSimGL::StandardVFNumElements(uint32_t(f), t) == oracle_1001e3972(uint32_t(f), t), "numel %d %u", f, t);
            for (int idx = 0; idx < 3; ++idx)
                CHECK(nRZSimGL::StandardVFElementOffset(uint32_t(f), t, idx) == oracle_1001e3918(uint32_t(f), t, idx), "eltoff %d %u %d", f, t, idx);
        }
    }
    for (int i = 0; i < 3000; ++i) {
        const uint32_t f = R32() | ((i & 1) ? 0x80000000u : 0);
        const uint32_t t = R32() % 10;
        CHECK(nRZSimGL::StandardVFNumElements(f, t) == oracle_1001e3972(f, t), "numel rnd");
        const int idx = int(R32() % 5);
        CHECK(nRZSimGL::StandardVFElementOffset(f, t, idx) == oracle_1001e3918(f, t, idx), "eltoff rnd");
    }
    for (int i = 0; i < 200; ++i) {
        uint32_t types[12];
        const uint32_t n = R32() % 12;
        for (uint32_t k = 0; k < n; ++k) types[k] = R32() % 10;
        CHECK(nRZSimGL::MakeStandardVertexFormat(n, types) == oracle_1001e3770(n, types), "make");
    }
    for (int f : {1, 2, 3, 10, 11, 0x20, 0x21, 0x23, 0x26})
        for (uint32_t t = 0; t < 10; ++t) {
            if (nRZSimGL::StandardVFNumElements(uint32_t(f), t) == 0) continue;
            std::vector<uint8_t> verts(64 * 8), a(64 * 16, 0x11), b(a.size(), 0x11);
            for (auto& x : verts) x = uint8_t(R32());
            nRZSimGL::ExtractElements(5, verts.data(), a.data(), uint32_t(f), t, 0);
            oracle_1001e39a8(5, verts.data(), b.data(), uint32_t(f), t, 0);
            CHECK(a == b, "extract %d %u", f, t);
        }
    for (uint32_t f = 0; f < 14; ++f)
        for (uint32_t t = 0; t < 20; ++t) {
            uint32_t a[4] = {99, 99, 99, 99}, b[4] = {99, 99, 99, 99};
            const bool ra = nRZSimGL::FindBitCounts(f, t, a[0], a[1], a[2], a[3]);
            const bool rb = oracle_10032b348(f, t, &b[0], &b[1], &b[2], &b[3]) != 0;
            CHECK(ra == rb && std::memcmp(a, b, sizeof a) == 0, "bitcounts %u %u", f, t);
        }

    // ---- pixel helpers
    auto cmpRows = [&](const char* name, auto mine, auto ref, int bpp, uint32_t mask, bool dxt) {
        for (uint32_t w : {4u, 8u, 12u, 16u, 5u, 7u, 3u, 2u, 1u}) {
            if (dxt && (w & 3)) continue;
            for (uint32_t h : {4u, 8u, 3u, 1u}) {
                if (dxt && (h & 3)) continue;
                const long stride = long(w) * bpp + 8;
                std::vector<uint8_t> src(size_t(stride) * (h + 2) + 64), a(src.size() + 4096, 0x33), b(a.size(), 0x33);
                for (int iter = 0; iter < 12; ++iter) {
                    for (auto& x : src) x = uint8_t(R32());
                    const uint32_t tint = R32() & (mask ? mask : 0xffffffffu);
                    std::fill(a.begin(), a.end(), 0x33); std::fill(b.begin(), b.end(), 0x33);
                    mine(a.data() + 16, stride, src.data(), stride, w, h, tint);
                    ref(b.data() + 16, stride, src.data(), stride, w, h, tint);
                    CHECK(a == b, "%s w=%u h=%u iter=%d", name, w, h, iter);
                }
            }
        }
    };
    cmpRows("tint8888", nRZSimGL::TintTexture_ARGB8888, oracle_100360101, 4, 0, false);
    cmpRows("tint888", nRZSimGL::TintTexture_RGB888, oracle_1003601f1, 3, 0, false);
    cmpRows("tint4444", nRZSimGL::TintTexture_ARGB4444, oracle_10036028f, 2, 0, false);
    cmpRows("tint1555", nRZSimGL::TintTexture_ARGB1555, oracle_100360390, 2, 0, false);
    cmpRows("dxt1", nRZSimGL::TintTexture_DXT1, oracle_10036048b, 2, 0, true);
    cmpRows("dxt2345", nRZSimGL::TintTexture_DXT2345, oracle_100360636, 4, 0, true);

    // colour key: bias keys toward pixels that exist in the data
    auto cmpKey = [&](const char* name, auto mine, auto ref, int bpp) {
        for (uint32_t w : {1u, 2u, 3u, 4u, 5u, 8u, 9u, 16u})
            for (uint32_t h : {1u, 3u, 4u}) {
                const uint32_t stride = w * bpp + 4;
                std::vector<uint8_t> src(size_t(stride) * h + 16), a(src.size() + 64, 0x44), b(a.size(), 0x44);
                for (int iter = 0; iter < 20; ++iter) {
                    for (auto& x : src) x = uint8_t(R32() % 3 == 0 ? R32() : 0x21);
                    uint32_t key = 0;
                    std::memcpy(&key, src.data() + (R32() % w) * bpp, bpp);
                    std::fill(a.begin(), a.end(), 0x44); std::fill(b.begin(), b.end(), 0x44);
                    mine(a.data(), stride, src.data(), stride, w, h, key);
                    ref(b.data(), stride, src.data(), stride, w, h, key);
                    CHECK(a == b, "%s w=%u h=%u", name, w, h);
                }
            }
    };
    cmpKey("key1555", nRZSimGL::BltColorkeyToAlpha_ARGB1555, oracle_10036003c, 2);
    cmpKey("key4444", nRZSimGL::BltColorkeyToAlpha_ARGB4444, oracle_100360067, 2);
    cmpKey("key8888", nRZSimGL::BltColorkeyToAlpha_ARGB8888, oracle_100360092, 4);

    if (bad) return 1;
    std::printf("simgl oracle ok\n");
}
