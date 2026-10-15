// Checks nPixelCopy against the decompiled multiply-shift expressions for every 16-bit input.
#include <cstdio>
#include <cstring>
#include <vector>
#include "render/PixelCopy.h"
using u32 = std::uint32_t; using u64 = std::uint64_t;
static u32 Mag(u32 x, u64 c) { return u32((u64(x) * 0xff * c) >> 0x21); }
static u32 D565(u32 u) { return ((u & 0x1f) * 0xff) / 0x1f | (Mag(u >> 11, 0x108421085) & 0xff0) << 0xc | (Mag((u >> 5) & 0x3f, 0x104104105) & 0x1fffe0) << 3 | 0xff000000u; }
static u32 D555(u32 u) { return ((u & 0x1f) * 0xff) / 0x1f | (Mag((u >> 5) & 0x1f, 0x108421085) & 0xffff0) << 4 | (Mag((u >> 10) & 0x1f, 0x108421085) & 0xff0) << 0xc | 0xff000000u; }
static u32 D1555(u32 u) { return ((u & 0x1f) * 0xff) / 0x1f | (u >> 0xf) * 0xff000000u | (Mag((u >> 10) & 0x1f, 0x108421085) & 0xfffffff0) << 0xc | (Mag((u >> 5) & 0x1f, 0x108421085) & 0xfffffff0) << 4; }
static u32 D5551(u32 u) { return (((u >> 1) & 0x1f) * 0xff) / 0x1f | (Mag(u >> 11, 0x108421085) & 0xfffffff0) << 0xc | (u & 1) * 0xff000000u | (Mag((u >> 6) & 0x1f, 0x108421085) & 0xfffffff0) << 4; }
int main() {
    std::vector<std::uint16_t> in(65536);
    for (u32 i = 0; i < 65536; ++i) in[i] = std::uint16_t(i);
    struct Case { const char* n; void (*fn)(const std::uint8_t*, std::uint8_t*, u32); u32 (*ref)(u32); } cases[] = {
        {"565", nPixelCopy::PixelCopy16_32_565_8888, D565}, {"555", nPixelCopy::PixelCopy16_32_555_8888, D555},
        {"1555", nPixelCopy::PixelCopy16_32_1555_8888, D1555}, {"5551", nPixelCopy::PixelCopy16_32_5551_8888, D5551}};
    int bad = 0;
    for (auto& c : cases) {
        std::vector<u32> out(65536);
        c.fn(reinterpret_cast<const std::uint8_t*>(in.data()), reinterpret_cast<std::uint8_t*>(out.data()), 65536);
        for (u32 i = 0; i < 65536; ++i) if (out[i] != c.ref(i)) { if (++bad < 10) std::printf("MISMATCH %s %04x\n", c.n, i); }
    }
    std::uint8_t one[4] = {0xaa, 0xaa, 0xaa, 0xaa};
    nPixelCopy::PixelCopy16_32_565_8888(reinterpret_cast<const std::uint8_t*>(in.data()), one, 0);   // zero count writes nothing
    if (one[0] != 0xaa) ++bad;
    if (bad) return 1;
    std::printf("render_pixelcopy: all passed\n");
}
