#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <random>
#include "gz/Bit1.h"
extern "C" {
unsigned char oracle_Bit1_PixelTest_dummy;
void oracle_Bit1_PixelSet(uint32_t*, int, int, int);
unsigned oracle_Bit1_PixelTest(uint32_t*, int, int, int);
void oracle_Bit1_Clear(uint32_t*, int);
void oracle_Bit1_Xor(uint32_t*, uint32_t*, int);
int oracle_Bit1_SelectionGrow(uint32_t*, uint32_t*, int, int, int*, int*);
unsigned long oracle_Bit1_SelectionGrowHoriz(uint32_t*, uint32_t*, int, int*, int*);
unsigned long oracle_Bit1_SelectionGrowVert(uint32_t*, uint32_t*, int, int*, int*);
void oracle_Bit1_RectSet(uint32_t*, int, unsigned char*);
void oracle_Bit1_RectClear(uint32_t*, int, unsigned char*);
unsigned long oracle_Bit1_RectTestAny(uint32_t*, int, unsigned char*);
void oracle_Bit1_RectOr(uint32_t*, uint32_t*, int, unsigned char*);
}
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main() {
    std::mt19937 rng(12345);
    for (int iter = 0; iter < 400; ++iter) {
        const int size = 32 * (1 + rng() % 4);
        const int words = size * size / 32;
        std::vector<uint32_t> a(words + 64), m(words + 64);
        for (auto& v : a) v = (rng() % 5 == 0) ? rng() : 0;
        for (auto& v : m) v = (rng() % 4 != 0) ? rng() | rng() : 0xffffffffu;
        std::vector<uint32_t> a2 = a, m2 = m;
        // pixel set / test
        int y = rng() % size, x = rng() % size;
        oracle_Bit1_PixelSet(a2.data(), y, x, size);
        Bit1_PixelSet(a.data(), y, x, size);
        CHECK(a == a2);
        CHECK((oracle_Bit1_PixelTest(a2.data(), y, x, size) & 1) == Bit1_PixelTest(a.data(), y, x, size));
        // xor
        std::vector<uint32_t> b = a, b2 = a2;
        oracle_Bit1_Xor(b2.data(), m2.data(), size);
        Bit1_Xor(b.data(), m.data(), size);
        CHECK(b == b2);
        // rect ops
        cRZRect r;
        r.left = rng() % size;
        r.right = r.left + rng() % (size - r.left);
        r.top = rng() % size;
        r.bottom = r.top + rng() % (size - r.top);
        {
            auto s1 = a, s2 = a2;
            Bit1_RectSet(s1.data(), size, r);
            oracle_Bit1_RectSet(s2.data(), size, reinterpret_cast<unsigned char*>(&r));
            CHECK(s1 == s2);
            s1 = a; s2 = a2;
            Bit1_RectClear(s1.data(), size, r);
            oracle_Bit1_RectClear(s2.data(), size, reinterpret_cast<unsigned char*>(&r));
            CHECK(s1 == s2);
            CHECK(Bit1_RectTestAny(a.data(), size, r) == (oracle_Bit1_RectTestAny(a2.data(), size, reinterpret_cast<unsigned char*>(&r)) & 1));
            s1 = a; s2 = a2;
            Bit1_RectOr(s1.data(), m.data(), size, r);
            oracle_Bit1_RectOr(s2.data(), m2.data(), size, reinterpret_cast<unsigned char*>(&r));
            CHECK(s1 == s2);
        }
        // selection growth: horizontal, vertical, full
        {
            auto s1 = a, s2 = a2;
            int lo1 = rng() % size, hi1 = lo1 + 1 + rng() % (size - lo1);
            if (hi1 > size) hi1 = size;
            int lo2 = lo1, hi2 = hi1;
            const bool h1 = Bit1_SelectionGrowHoriz(s1.data(), m.data(), size, &lo1, &hi1);
            const bool h2 = (oracle_Bit1_SelectionGrowHoriz(s2.data(), m2.data(), size, &lo2, &hi2) & 1) != 0;
            CHECK(s1 == s2 && h1 == h2 && lo1 == lo2 && hi1 == hi2);
            s1 = a; s2 = a2;
            lo1 = rng() % size; hi1 = lo1 + 1 + rng() % (size - lo1);
            if (hi1 > size) hi1 = size;
            lo2 = lo1; hi2 = hi1;
            const bool v1 = Bit1_SelectionGrowVert(s1.data(), m.data(), size, &lo1, &hi1);
            const bool v2 = (oracle_Bit1_SelectionGrowVert(s2.data(), m2.data(), size, &lo2, &hi2) & 1) != 0;
            CHECK(s1 == s2 && v1 == v2 && lo1 == lo2 && hi1 == hi2);
            s1 = a; s2 = a2;
            lo1 = rng() % size; hi1 = lo1 + 1 + rng() % (size - lo1);
            if (hi1 > size) hi1 = size;
            lo2 = lo1; hi2 = hi1;
            const int it = 1 + rng() % 6;
            const int g1 = Bit1_SelectionGrow(s1.data(), m.data(), size, it, &lo1, &hi1);
            const int g2 = oracle_Bit1_SelectionGrow(s2.data(), m2.data(), size, it, &lo2, &hi2);
            CHECK(s1 == s2 && g1 == g2 && lo1 == lo2 && hi1 == hi2);
        }
        auto c1 = a, c2 = a2;
        Bit1_Clear(c1.data(), size);
        oracle_Bit1_Clear(c2.data(), size);
        CHECK(c1 == c2);
        if (failures > 5) break;
    }
    std::puts(failures ? "bit1 oracle: FAILED" : "bit1 oracle ok");
    return failures != 0;
}
