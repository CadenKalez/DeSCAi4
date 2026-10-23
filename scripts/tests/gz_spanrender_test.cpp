// Tests the span renderers' dispatch and the 8-bit alpha entry points: RenderBlt must pick the cBlitter for the
// source format / colour-key flag / method (cBlitterSet table) and match that blitter called directly (the blitters
// themselves are checked against the original by scripts/run_blitter_oracle.sh); cSpanRender8's alpha entry points
// equal the opaque ones; every renderer is destroyed through the base pointer.
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "gz/SpanRender.h"

using namespace nGZDrawSoftRender;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

namespace {
struct Surface {
    std::vector<std::uint8_t> bytes;
    cGZBufferSurfaceDesc desc{};
    Surface(std::uint32_t w, std::uint32_t h, int bpp, std::uint8_t fill) : bytes(std::size_t(w) * h * bpp, fill) {
        desc.pixels = bytes.data(); desc.pitch = std::int64_t(w) * bpp; desc.width = w; desc.height = h;
    }
};

template <class Renderer, class Dst>
bool DispatchMatchesDirectBlit(int bpp) {
    constexpr std::uint32_t W = 8, H = 4;
    Surface src(W, H, 4, 0);
    for (std::size_t i = 0; i < src.bytes.size(); ++i) src.bytes[i] = std::uint8_t(i * 37 + 11);
    Surface viaRenderer(W, H, bpp, 0x5a), viaBlitter(W, H, bpp, 0x5a);
    const std::uint32_t spans[] = {(1u << 16) | 1, (1u << 16) | 7, (3u << 16) | 0, (3u << 16) | 8};
    cBltInfo info{};
    info.srcFormatId = kSrcARGB8888;
    info.colour = 0x80ffffff;
    bool ok = true;
    std::unique_ptr<cSpanRender> r(new Renderer());   // destroyed through the base class
    for (std::uint32_t method : {0u, 1u, 2u}) {
        r->RenderBlt(spans, 2, viaRenderer.desc, src.desc, info, method);
        const BlitFn direct = method == 0 ? &cBlitter<Dst, typename cBlitterSet<Dst>::ARGB8888, false>::Blt
                            : method == 1 ? &cBlitter<Dst, typename cBlitterSet<Dst>::ARGB8888, false>::BltConstantAlpha
                                          : &cBlitter<Dst, typename cBlitterSet<Dst>::ARGB8888, false>::BltModulate;
        direct(spans, 2, viaBlitter.desc, src.desc, info);
        ok = ok && viaRenderer.bytes == viaBlitter.bytes;
    }
    return ok;
}
}  // namespace

int main() {
    CHECK((DispatchMatchesDirectBlit<cSpanRender8, RGBFormat<255u, 0u, 0u, 0u, 1, 0>>(1)));
    CHECK((DispatchMatchesDirectBlit<cSpanRender565, RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>>(2)));
    CHECK((DispatchMatchesDirectBlit<cSpanRender1555, RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>>(2)));
    CHECK((DispatchMatchesDirectBlit<cSpanRender4444, RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>>(2)));
    CHECK((DispatchMatchesDirectBlit<cSpanRender8888, RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>>(4)));

    // 8-bit surfaces: the alpha entry points forward to the opaque ones
    cSpanRender8 r8;
    const std::uint32_t spans[] = {(0u << 16) | 2, (0u << 16) | 6, (2u << 16) | 0, (2u << 16) | 3};
    Surface a(8, 3, 1, 0), b(8, 3, 1, 0);
    r8.RenderAlpha(spans, 2, a.desc, 0x1234abu, 0x40000000u);
    r8.RenderSolid(spans, 2, b.desc, 0x1234abu);
    CHECK(a.bytes == b.bytes && a.bytes[2] == 0xab && a.bytes[16] == 0xab && a.bytes[1] == 0);
    cGradientInfo g{};
    g.base[3] = 10 << 16; g.dx[3] = 3 << 16; g.dy[3] = 1 << 16;
    Surface c(8, 3, 1, 0), d(8, 3, 1, 0);
    r8.RenderAlphaGradient(spans, 2, c.desc, g);
    r8.RenderGradient(spans, 2, d.desc, g);
    CHECK(c.bytes == d.bytes && c.bytes[2] == 16 && c.bytes[16] == 12);
    std::printf(g_fail ? "FAILED %d\n" : "gz_spanrender OK\n", g_fail);
    return g_fail ? 1 : 0;
}
