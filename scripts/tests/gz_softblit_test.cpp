#include <cstdio>
#include <cstring>

#include "gz/SoftBlit.h"
#include "gz/SpanRender.h"

using namespace nGZDrawSoftRender;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using ARGB4444 = RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>;
using RGB565 = RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>;
using ARGB8888 = RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>;
using XRGB8888 = RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>;
using A8 = RGBFormat<255u, 0u, 0u, 0u, 1, 0>;

int main() {
    // 4444 -> 565 rectangle: channels widen by bit replication
    {
        std::uint16_t src[2] = {0xF123, 0xFFFF}, dst[2] = {0, 0};
        cGZBufferSurfaceDesc s{reinterpret_cast<std::uint8_t*>(src), 4, 2, 1, {}}, d{reinterpret_cast<std::uint8_t*>(dst), 4, 2, 1, {}};
        cBlitter<RGB565, ARGB4444, false>::BltRect(2, 1, d, s, {});
        CHECK(dst[0] == 0x1106 && dst[1] == 0xffff);
    }
    // 4444 -> xRGB8888 keeps the spare byte opaque; colour key skips matching pixels
    {
        std::uint16_t src[2] = {0x0123, 0x0456};
        std::uint32_t dst[2] = {0x11111111, 0x22222222};
        cGZBufferSurfaceDesc s{reinterpret_cast<std::uint8_t*>(src), 4, 2, 1, {}}, d{reinterpret_cast<std::uint8_t*>(dst), 8, 2, 1, {}};
        cBltInfo info{};
        info.key = 0x123;
        const std::uint32_t span[2] = {0, 2};
        cBlitter<XRGB8888, ARGB4444, true>::Blt(span, 1, d, s, info);
        CHECK(dst[0] == 0x11111111);
        CHECK(dst[1] == 0xff445566);
    }
    // constant alpha over ARGB8888: (255*128 + 0*128 + 128) >> 8 = 128, result opaque
    {
        std::uint32_t src[1] = {0x00ffffff}, dst[1] = {0x00000000};
        cGZBufferSurfaceDesc s{reinterpret_cast<std::uint8_t*>(src), 4, 1, 1, {}}, d{reinterpret_cast<std::uint8_t*>(dst), 4, 1, 1, {}};
        cBltInfo info{};
        info.colour = 0x80000000;
        const std::uint32_t span[2] = {0, 1};
        cBlitter<ARGB8888, ARGB8888, false>::BltConstantAlpha(span, 1, d, s, info);
        CHECK(dst[0] == 0xff808080);
    }
    // alpha-only source into ARGB8888: white with the alpha byte; modulate scales by the colour
    {
        std::uint8_t src[1] = {0x80};
        std::uint32_t dst[1] = {0};
        cGZBufferSurfaceDesc s{src, 1, 1, 1, {}}, d{reinterpret_cast<std::uint8_t*>(dst), 4, 1, 1, {}};
        cBltInfo info{};
        info.colour = 0xff102030;
        const std::uint32_t span[2] = {0, 1};
        cBlitter<ARGB8888, A8, false>::Blt(span, 1, d, s, info);
        CHECK(dst[0] == 0x80ffffff);
        cBlitter<ARGB8888, A8, false>::BltModulate(span, 1, d, s, info);
        CHECK(dst[0] == 0x80102030);
    }
    // tiling wraps the source
    {
        std::uint16_t src[2] = {0x1000 | 0x0111, 0x2000 | 0x0222}, dst[5] = {};
        cGZBufferSurfaceDesc s{reinterpret_cast<std::uint8_t*>(src), 4, 2, 1, {}}, d{reinterpret_cast<std::uint8_t*>(dst), 10, 5, 1, {}};
        cBltInfo info{};
        const std::uint32_t span[2] = {0, 5};
        cBlitter<RGB565, ARGB4444, false>::BltTiled(span, 1, d, s, info);
        CHECK(dst[0] == dst[2] && dst[2] == dst[4] && dst[1] == dst[3] && dst[0] != dst[1]);
    }
    // span renderers: solid fill and the format-id dispatch of RenderBlt
    {
        std::uint16_t px[8] = {};
        cGZBufferSurfaceDesc d{reinterpret_cast<std::uint8_t*>(px), 16, 8, 1, {}};
        const std::uint32_t span[2] = {2, 5};
        cSpanRender565 r;
        r.RenderSolid(span, 1, d, 0xabcd);
        CHECK(px[1] == 0 && px[2] == 0xabcd && px[4] == 0xabcd && px[5] == 0);
        std::uint16_t src4444[8] = {0xF123, 0xFFFF};
        cGZBufferSurfaceDesc s{reinterpret_cast<std::uint8_t*>(src4444), 16, 8, 1, {}};
        cBltInfo info{};
        info.srcFormatId = kSrcARGB4444;
        const std::uint32_t one[2] = {0, 2};
        r.RenderBlt(one, 1, d, s, info, 0);          // method 0 = Blt
        CHECK(px[0] == 0x1106 && px[1] == 0xffff);
    }
    std::puts(failures ? "gz_softblit: FAILED" : "gz_softblit ok");
    return failures != 0;
}
