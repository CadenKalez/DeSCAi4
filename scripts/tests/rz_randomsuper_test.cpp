#include <cstdio>
#include <vector>

#include "rz/cRZRandomSuper.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    cRZRandomSuper a(1234), b(1234), c(99);
    bool same = true, differs = false;
    for (int i = 0; i < 2000; ++i) {                       // crosses several reloads
        const std::uint32_t x = a.RandomUint32Uniform(), y = b.RandomUint32Uniform(), z = c.RandomUint32Uniform();
        same = same && x == y;
        differs = differs || x != z;
    }
    CHECK(same && differs);

    // the seeding recurrence is reproducible by hand
    cRZRandomSuper s(0x10001);
    std::uint32_t x = 0x10001u | 1u;
    // after construction Reload() already twisted the words, so check the pre-twist law through a second seed call
    std::vector<std::uint32_t> expected(624);
    for (int i = 0; i < 624; ++i) {
        expected[i] = ((x * 0x10dcdu) >> 16) | (x & 0xffff0000u);
        x = x * 0x1c587629u + 0x10dceu;
    }
    // re-derive the twist independently (textbook order) and compare to the generator's state
    auto twist = [](std::uint32_t m, std::uint32_t u, std::uint32_t v) {
        const std::uint32_t y = (u & 0x80000000u) | (v & 0x7fffffffu);
        return m ^ (y >> 1) ^ ((v & 1u) ? 0x9908b0dfu : 0u);
    };
    std::vector<std::uint32_t> t = expected;
    for (int i = 0; i < 227; ++i) t[i] = twist(t[i + 397], t[i], t[i + 1]);
    for (int i = 227; i < 623; ++i) t[i] = twist(t[i - 227], t[i], t[i + 1]);
    t[623] = twist(t[396], t[623], t[0]);
    bool matches = true;
    for (int i = 0; i < 624; ++i) matches = matches && s.State()[i] == t[i];
    CHECK(matches);

    // ranges
    cRZRandomSuper r(5);
    bool inRange = true;
    double minD = 1, maxD = 0;
    for (int i = 0; i < 5000; ++i) {
        inRange = inRange && r.RandomUint32Uniform(10) < 10;
        const std::int32_t v = r.RandomSint32RangeUniform(-5, 5);
        inRange = inRange && v >= -5 && v < 5;
        const double d = r.RandomDoubleUniform();
        minD = d < minD ? d : minD;
        maxD = d > maxD ? d : maxD;
        const double e = r.RandomDoubleRangeUniform(2.0, 4.0);
        inRange = inRange && e >= 2.0 && e <= 4.0;
    }
    CHECK(inRange && minD >= 0.0 && maxD < 1.0 && minD < 0.05 && maxD > 0.95);

    // weighted choice
    const double weights[4] = {0.0, 3.0, -1.0, 1.0};
    int hits[4] = {0, 0, 0, 0};
    for (int i = 0; i < 8000; ++i) ++hits[r.RandomUint32WeightedChoice(4, weights)];
    CHECK(hits[0] == 0 && hits[2] == 0 && hits[1] > 5000 && hits[3] > 1500);
    const double zero[3] = {0, -2, 0};
    int zeroHits[3] = {0, 0, 0};
    for (int i = 0; i < 3000; ++i) ++zeroHits[r.RandomUint32WeightedChoice(3, zero)];
    CHECK(zeroHits[0] > 500 && zeroHits[1] > 500 && zeroHits[2] > 500);   // no positive weight: uniform
    CHECK(r.RandomUint32WeightedChoice(1, weights) == 0 && r.RandomUint32WeightedChoice(0, weights) == 0);

    // clock seeding mixes in a counter, so back-to-back calls differ
    CHECK(cRZRandomSuper::Hash(1, 2) != cRZRandomSuper::Hash(1, 2));
    cRZRandomSuper clock(0xffffffffu);
    CHECK(clock.RandomUint32Uniform() != clock.RandomUint32Uniform());
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
