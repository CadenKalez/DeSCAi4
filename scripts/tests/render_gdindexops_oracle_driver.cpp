// Compares src/render/GDIndexOps.cpp with the Ghidra decompilation (work/gdindex/oracle.c), bit for bit.
#include "render/GDIndexOps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
void oracle_10060c286(int, uint16_t*);
void oracle_10060c2ca(int, uint16_t*);
void oracle_10060c30d(int, uint16_t*);
void oracle_10060c35e(int, uint16_t*);
void oracle_10060c38c(int, uint32_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c49c(int, uint16_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c5a2(int, uint8_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c6ca(int, uint32_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c7d8(int, uint16_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c8eb(int, uint8_t*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060c9fc(int, const void*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060ca58(int, const void*, uint16_t*, uint32_t*, uint32_t*);
void oracle_10060cb9c(int, const void*, uint16_t*, uint32_t*, uint32_t*);
uint32_t oracle_10060cbf9(int);
uint32_t oracle_10060cc18(uint32_t);
uint32_t oracle_10060fbc5(uint32_t);
uint64_t oracle_10060fbe1(int, uint32_t, uint32_t*);
void oracle_1007b5356(uint8_t*, const uint8_t*, long, int);
void oracle_1007b538a(uint8_t*, const uint8_t*, long, int);
void oracle_1007b53bd(uint8_t*, const uint8_t*, long, int);
void oracle_1007b53fd(uint8_t*, const uint8_t*, long, int);
void oracle_1007b5449(uint8_t*, const uint8_t*, long, int);
}
static int bad = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++bad; } } while (0)

template <class F, class G>
static void GenCase(const char* name, F mine, G ref) {
    for (int n = 0; n <= 70; ++n) {
        std::vector<uint16_t> a(6 * 70 + 64, 0xdead), b(a.size(), 0xdead);
        mine(n, a.data());
        ref(n, b.data());
        CHECK(a == b, "%s n=%d", name, n);
    }
    // large counts exercise the 16-bit position counters (the originals never terminate above ~65532 vertices)
    for (int n : {16384, 21845, 21846, 32768, 60001, 65000}) {
        std::vector<uint16_t> a(1 << 18, 0xdead), b(a.size(), 0xdead);
        mine(n, a.data());
        ref(n, b.data());
        CHECK(a == b, "%s n=%d", name, n);
    }
}

template <class T, class M, class R>
static void IdxCase(const char* name, M mine, R ref, int minN, bool allowOdd) {
    for (int n = minN; n <= 130; ++n) {
        if (!allowOdd && (n % 2)) continue;
        for (int iter = 0; iter < 6; ++iter) {
            std::vector<T> in(n + 8 + 64);
            const uint64_t mask = iter < 3 ? 0xff : (sizeof(T) == 1 ? 0xff : (iter == 3 ? 0xffff : 0xffffffffull));
            for (auto& v : in) v = T(uint64_t(rand()) * 7919u & mask);
            std::vector<uint16_t> a(6 * n + 64, 0xdead), b(a.size(), 0xdead);
            uint32_t amin = 1, amax = 2, bmin = 1, bmax = 2;
            mine(n, in.data(), a.data(), &amin, &amax);
            ref(n, in.data(), b.data(), &bmin, &bmax);
            CHECK(a == b && amin == bmin && amax == bmax, "%s n=%d iter=%d (%u..%u vs %u..%u)", name, n, iter, amin, amax, bmin, bmax);
        }
    }
}

int main() {
    srand(99);
    GenCase("Quads", nSGLDX7::GenerateIndexedTriListForQuads, oracle_10060c286);
    GenCase("QuadStrip", nSGLDX7::GenerateIndexedTriListForQuadStrip, oracle_10060c2ca);
    GenCase("TriStrip", nSGLDX7::GenerateIndexedTriListForTriangleStrip, oracle_10060c30d);
    GenCase("TriFan", nSGLDX7::GenerateIndexedTriListForTriangleFan, oracle_10060c35e);

    IdxCase<uint32_t>("IQuads32", nSGLDX7::GenerateIndexedTriListForIndexedQuads32,
                      [](int n, uint32_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c38c(n, i, o, a, b); }, 4, false);
    IdxCase<uint16_t>("IQuads16", nSGLDX7::GenerateIndexedTriListForIndexedQuads16,
                      [](int n, uint16_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c49c(n, i, o, a, b); }, 4, false);
    IdxCase<uint8_t>("IQuads8", nSGLDX7::GenerateIndexedTriListForIndexedQuads8,
                     [](int n, uint8_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c5a2(n, i, o, a, b); }, 4, false);
    IdxCase<uint32_t>("IQuadStrip32", nSGLDX7::GenerateIndexedTriListForIndexedQuadStrip32,
                      [](int n, uint32_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c6ca(n, i, o, a, b); }, 4, false);
    IdxCase<uint16_t>("IQuadStrip16", nSGLDX7::GenerateIndexedTriListForIndexedQuadStrip16,
                      [](int n, uint16_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c7d8(n, i, o, a, b); }, 4, false);
    IdxCase<uint8_t>("IQuadStrip8", nSGLDX7::GenerateIndexedTriListForIndexedQuadStrip8,
                     [](int n, uint8_t* i, uint16_t* o, uint32_t* a, uint32_t* b) { oracle_10060c8eb(n, i, o, a, b); }, 4, false);

    auto conv = [](const char* name, auto mine, auto ref, auto tag) {
        using T = decltype(tag);
        for (int n = 1; n <= 40; ++n) {
            std::vector<T> in(n + 8);
            for (auto& v : in) v = T(uint64_t(rand()) * 2654435761u);
            std::vector<uint16_t> a(n + 8, 0xdead), b(a.size(), 0xdead);
            uint32_t amin, amax, bmin, bmax;
            mine(n, in.data(), a.data(), &amin, &amax);
            ref(n, in.data(), b.data(), &bmin, &bmax);
            CHECK(a == b && amin == bmin && amax == bmax, "%s n=%d", name, n);
        }
    };
    conv("U32", nSGLDX7::ConvertUint32IndicesToUint16, oracle_10060c9fc, uint32_t{});
    conv("U16", nSGLDX7::ConvertUint16IndicesToUint16, oracle_10060ca58, uint16_t{});
    conv("U8", nSGLDX7::ConvertUint8IndicesToUint16, oracle_10060cb9c, uint8_t{});

    for (int i = -3; i < 12; ++i) CHECK(nSGLDX7::GetFormatInfoNumAlphaBits(i) == oracle_10060cbf9(i), "alphabits %d", i);
    for (uint32_t i = 0; i < 12; ++i) CHECK(nSGLDX7::GetFormatInfoNumColorBits(i) == oracle_10060cc18(i), "colorbits %u", i);
    for (uint32_t i = 0; i < 20; ++i) CHECK(nSGLDX7::GetSourceFormatNumBits(i) == oracle_10060fbc5(i), "srcbits %u", i);
    for (int f = -1; f < 14; ++f)
        for (uint32_t t = 0; t < 20; ++t) {
            uint32_t a = 0x77, b = 0x77;
            bool ra = nSGLDX7::TranslateGDFormatToSourceFormat(f, t, &a);
            bool rb = oracle_10060fbe1(f, t, &b) != 0;
            CHECK(ra == rb && a == b, "translate fmt=%d type=%u", f, t);
        }

    using VFn = void (*)(uint8_t*, const uint8_t*, long, int);
    struct V { const char* n; VFn mine; VFn ref; int srcBytes; };
    const V vs[] = {
        {"V3F", nSGLDX7::ConvertVerticesFromV3F, oracle_1007b5356, 12},
        {"V3F_C4UB", nSGLDX7::ConvertVerticesFromV3F_C4UB, oracle_1007b538a, 16},
        {"V3F_T2F", nSGLDX7::ConvertVerticesFromV3F_T2F, oracle_1007b53bd, 20},
        {"V3F_2T2F", nSGLDX7::ConvertVerticesFromV3F_2T2F, oracle_1007b53fd, 28},
        {"V3F_C4UB_T2F", nSGLDX7::ConvertVerticesFromV3F_C4UB_T2F, oracle_1007b5449, 24},
    };
    for (const V& v : vs)
        for (long stride : {long(v.srcBytes / 4), long(v.srcBytes / 4 + 3)}) {
            const int n = 9;
            std::vector<uint8_t> src(size_t(stride) * 4 * n + 64), a(32 * n, 0x5a), b(32 * n, 0x5a);
            for (auto& x : src) x = uint8_t(rand());
            v.mine(a.data(), src.data(), stride, n);
            v.ref(b.data(), src.data(), stride, n);
            CHECK(a == b, "vertices %s stride=%ld", v.n, stride);
        }
    if (bad) return 1;
    std::printf("gdindexops oracle ok\n");
}
