#include <cstdio>
#include <cstring>
#include <vector>

#include "render/PrimitivePool.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Target final : IPrimitiveDrawTarget {
    std::vector<std::vector<int>> calls;
    std::uint32_t format = 0, vertexCount = 0;
    void SetVertexBuffer(std::uint32_t f, std::uint32_t n, const void*) override { format = f; vertexCount = n; }
    void DrawPrims(std::uint32_t t, int first, int count) override { calls.push_back({0, int(t), first, count}); }
    void DrawPrimsIndexed(std::uint32_t t, int count, const std::uint16_t* idx) override { calls.push_back({1, int(t), count, idx ? int(idx[0]) : -1}); }
};
float Float(const std::uint8_t* p) { float f; std::memcpy(&f, p, 4); return f; }
}  // namespace

int main() {
    cSC4PrimitivePool pool;
    pool.SetFormat(4, 4, 6, 10, &cSC4PrimitivePool::QuadVertexInit_V3F_C4UB_T2F, &cSC4PrimitivePool::QuadTriListIndexInit);
    auto* v = static_cast<std::uint8_t*>(pool.GetPrimVertices(2));
    CHECK(pool.VerticesUsed() == 8);
    // corner counter 1..4: u = 0,0,1,1 and v = 0,1,1,0 on every quad; the second quad restarts the pattern at its first
    // vertex only because the whole new range is initialised in one call
    const float u[8] = {0, 0, 1, 1, 0, 0, 1, 1}, vv[8] = {0, 1, 1, 0, 0, 1, 1, 0};
    for (int i = 0; i < 8; ++i) {
        std::uint32_t colour; std::memcpy(&colour, v + i * 24 + 12, 4);
        CHECK(colour == 0xffffffffu && Float(v + i * 24 + 16) == u[i] && Float(v + i * 24 + 20) == vv[i]);
    }
    std::uint16_t* idx = pool.GetPrimIndices(2);
    const std::uint16_t want[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    for (int i = 0; i < 12; ++i) CHECK(idx[i] == want[i]);
    CHECK(pool.IndicesUsed() == 12 && pool.Indices() == idx);
    Target t;
    pool.Draw(t);
    CHECK(t.format == 10 && t.vertexCount == 8 && t.calls.size() == 1 && t.calls[0][0] == 1 && t.calls[0][2] == 12 && t.calls[0][3] == 0);

    // flips
    std::uint8_t buf[4 * 24] = {};
    cSC4PrimitivePool::VFlip_QuadVertexInit_V3F_C4UB_T2F(buf, 0, 4);
    const float vu[4] = {0, 0, 1, 1}, vv2[4] = {1, 0, 0, 1};
    for (int i = 0; i < 4; ++i) CHECK(Float(buf + i * 24 + 16) == vu[i] && Float(buf + i * 24 + 20) == vv2[i]);
    cSC4PrimitivePool::HFlip_QuadVertexInit_V3F_C4UB_T2F(buf, 0, 4);
    const float hu[4] = {1, 1, 0, 0}, hv[4] = {0, 1, 1, 0};
    for (int i = 0; i < 4; ++i) CHECK(Float(buf + i * 24 + 16) == hu[i] && Float(buf + i * 24 + 20) == hv[i]);
    std::uint16_t half[12] = {};
    cSC4PrimitivePool::QuadTriListIndexInit(half, 6, 12);   // the second quad starts at vertex 4
    CHECK(half[6] == 4 && half[11] == 7 && half[0] == 0);

    // restart and chunked drawing of plain vertices
    pool.SetFormat(7, 1, 0, 10, nullptr, nullptr);
    pool.GetPrimVertices(0x700 + 0x700);
    Target t2;
    pool.Draw(t2);
    // 0xe00 vertices: 0x600 from offset 0x800, 0x600 from offset 0x200, then 0x200 from the start
    CHECK(t2.calls.size() == 3 && t2.calls[0][2] == 0x800 && t2.calls[0][3] == 0x600 && t2.calls[1][2] == 0x200 && t2.calls[2][2] == 0 && t2.calls[2][3] == 0x200);
    pool.RestartArrays();
    CHECK(pool.VerticesUsed() == 0 && pool.IndicesUsed() == 0);
    cSC4PrimitivePool copy(pool);
    CHECK(copy.VerticesUsed() == 0);
    std::printf(failures ? "FAILED\n" : "render_primitivepool ok\n");
    return failures != 0;
}
