// JobSystem + SimdKernels: parallel result must equal the scalar reference bit-for-bit.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include "ecs/JobSystem.h"
#include "ecs/SimdKernels.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> d(-100.f, 400.f);
    for (std::size_t n : {0u, 1u, 3u, 4u, 5u, 1023u, 65536u + 3u}) {
        std::vector<float> a(n), b;
        for (auto& v : a) v = d(rng);
        b = a;
        ecs::simd::ScaleAddClampScalar(a.data(), n, 0.97f, -1.5f, 0.f, 255.f);
        ecs::simd::ScaleAddClamp(b.data(), n, 0.97f, -1.5f, 0.f, 255.f);
        CHECK(n == 0 || std::memcmp(a.data(), b.data(), n * sizeof(float)) == 0);
    }
    ecs::JobSystem jobs(4);
    const std::size_t n = 1 << 20;
    std::vector<float> ref(n), par(n);
    for (auto& v : ref) v = d(rng);
    par = ref;
    ecs::simd::ScaleAddClampScalar(ref.data(), n, 0.9f, 2.f, 0.f, 255.f);
    for (int rep = 0; rep < 20; ++rep) {  // repeat to shake out races / lost wakeups
        auto tmp = par;
        jobs.ParallelFor(n, 4096, [&](std::size_t b, std::size_t e) {
            ecs::simd::ScaleAddClamp(tmp.data() + b, e - b, 0.9f, 2.f, 0.f, 255.f);
        });
        CHECK(std::memcmp(ref.data(), tmp.data(), n * sizeof(float)) == 0);
    }
    std::vector<int> hits(1000, 0);
    jobs.ParallelFor(hits.size(), 1, [&](std::size_t b, std::size_t e) { for (auto i = b; i < e; ++i) ++hits[i]; });
    for (int h : hits) CHECK(h == 1);
    std::vector<float> x(37, 1.f), y(37, 2.f);
    ecs::simd::Accumulate(x.data(), y.data(), x.size());
    for (float v : x) CHECK(v == 3.f);
    std::printf(failures ? "FAILED\n" : "ok\n");
    return failures != 0;
}
