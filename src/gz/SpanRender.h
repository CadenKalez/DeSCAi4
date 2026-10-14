// nGZDrawSoftRender::cSpanRender{8,565,1555,4444,8888} — per-pixel-format span renderers of the GZ
// software draw layer (0x100659ffc..0x10066a...). A span list is (start, end) pairs, start/end =
// (y << 16) | x; a span covers end - start pixels of row y.
//
//   RenderSolid(spans, n, dst, colour)           fill with a native pixel value
//   RenderGradient(spans, n, dst, gradient)      16.16 fixed point B/G/R(/A) ramps, no blending
//   RenderAlpha(spans, n, dst, colour, alpha)    blend a native colour; alpha = top byte of the argument
//   RenderAlphaGradient(spans, n, dst, gradient) blend a gradient using its alpha ramp
//   RenderBlt(spans, n, dst, src, info, method)  dispatches into cBlitterSet<Format> by source format id
//                                                (info.srcFormatId) and the colour-key flag
// The channel arithmetic reproduces the original's packed integer expressions exactly.
#pragma once
#include <cstdint>
#include <cstring>

#include "gz/SoftBlit.h"

namespace nGZDrawSoftRender {

// Gradient set-up: base values and per-pixel (dx) / per-row (dy) steps, 16.16 fixed point, in B, G, R, A order.
struct cGradientInfo {
    std::int32_t base[4];     // +0x00
    std::int32_t dx[4];       // +0x10
    std::int32_t dy[4];       // +0x20
};

// Source format ids used by RenderBlt (info.srcFormatId).
enum SrcFormatId : std::uint32_t { kSrcARGB4444, kSrcARGB1555, kSrcXRGB1555, kSrcRGB565, kSrcARGB8888, kSrcXRGB8888, kSrcA8 };

using BlitFn = void (*)(const std::uint32_t*, std::uint32_t, cGZBufferSurfaceDesc, cGZBufferSurfaceDesc, cBltInfo);
constexpr int kBlitMethodCount = 12;     // Blt, ConstantAlpha, Modulate, ModulateAlpha, then the Stretch and Tiled groups

// cBlitterSet<Dst>::sRenderVtbl: [srcFormatId * 2 + keyed][method]
template <class Dst>
struct cBlitterSet {
    template <class Src, bool Keyed>
    static constexpr void FillRow(BlitFn (&row)[kBlitMethodCount]) {
        using B = cBlitter<Dst, Src, Keyed>;
        row[0] = &B::Blt;
        row[1] = &B::BltConstantAlpha;
        row[2] = &B::BltModulate;
        row[3] = &B::BltModulateAlpha;
        row[4] = &B::BltStretch;
        row[5] = &B::BltStretchConstantAlpha;
        row[6] = &B::BltStretchModulate;
        row[7] = &B::BltStretchModulateAlpha;
        row[8] = &B::BltTiled;
        row[9] = &B::BltTiledConstantAlpha;
        row[10] = &B::BltTiledModulate;
        row[11] = &B::BltTiledModulateAlpha;
    }
    using ARGB4444 = RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>;
    using ARGB1555 = RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>;
    using XRGB1555 = RGBFormat<0u, 31744u, 992u, 31u, 2, 5>;
    using RGB565 = RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>;
    using ARGB8888 = RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>;
    using XRGB8888 = RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>;
    using A8 = RGBFormat<255u, 0u, 0u, 0u, 1, 0>;
    using Solid = RGBFormat<0u, 0u, 0u, 0u, 1, 0>;

    static const BlitFn (&Table())[14][kBlitMethodCount] {
        static BlitFn rows[14][kBlitMethodCount];
        static bool built = false;
        if (!built) {
            FillRow<ARGB4444, false>(rows[0]);  FillRow<ARGB4444, true>(rows[1]);
            FillRow<ARGB1555, false>(rows[2]);  FillRow<ARGB1555, true>(rows[3]);
            FillRow<XRGB1555, false>(rows[4]);  FillRow<XRGB1555, true>(rows[5]);
            FillRow<RGB565, false>(rows[6]);    FillRow<RGB565, true>(rows[7]);
            FillRow<ARGB8888, false>(rows[8]);  FillRow<ARGB8888, true>(rows[9]);
            FillRow<XRGB8888, false>(rows[10]); FillRow<XRGB8888, true>(rows[11]);
            FillRow<A8, false>(rows[12]);       FillRow<A8, true>(rows[13]);
            built = true;
        }
        return rows;
    }
};

// A cBlitter pair does not exist for every (dst, src) the table can name; the original reuses the code of
// an equivalent pair (identical code folding). The template covers every pair, so the table is complete.

class cSpanRender {
public:
    virtual ~cSpanRender() = default;
    virtual void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) = 0;
    virtual void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) = 0;
    virtual void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t alpha) = 0;
    virtual void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) = 0;
    virtual void RenderBlt(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, cGZBufferSurfaceDesc& src,
                           const cBltInfo& info, std::uint32_t method) = 0;
};

namespace detail {
template <class Fn>
inline void ForEachSpan(const std::uint32_t* spans, std::uint32_t count, Fn&& fn) {
    for (; count; --count, spans += 2) fn(spans[0], spans[1]);
}
inline std::uint16_t* Row16(const cGZBufferSurfaceDesc& d, std::uint32_t start) {
    return reinterpret_cast<std::uint16_t*>(d.pixels + std::uint64_t(start >> 16) * d.pitch + std::uint64_t(start & 0xffff) * 2);
}
inline std::uint32_t* Row32(const cGZBufferSurfaceDesc& d, std::uint32_t start) {
    return reinterpret_cast<std::uint32_t*>(d.pixels + std::uint64_t(start >> 16) * d.pitch + std::uint64_t(start & 0xffff) * 4);
}
}  // namespace detail

template <class Dst>
class cSpanRenderBase : public cSpanRender {
public:
    void RenderBlt(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, cGZBufferSurfaceDesc& src,
                   const cBltInfo& info, std::uint32_t method) override {
        const BlitFn fn = cBlitterSet<Dst>::Table()[info.srcFormatId * 2 + (info.keyed ? 1 : 0)][method];
        fn(spans, count, dst, src, info);
    }
};

// ---- 8-bit (alpha / index) surfaces ---------------------------------------------------------------
class cSpanRender8 final : public cSpanRenderBase<RGBFormat<255u, 0u, 0u, 0u, 1, 0>> {
public:
    void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            std::memset(dst.pixels + std::uint64_t(s & 0xffff) + std::uint64_t(s >> 16) * dst.pitch, int(colour & 0xff), e - s);
        });
    }
    void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::int32_t a = std::int32_t(std::uint32_t(g.dy[3]) * y + std::uint32_t(g.dx[3]) * x + std::uint32_t(g.base[3]));
            std::uint8_t* p = dst.pixels + std::uint64_t(x) + std::uint64_t(y) * dst.pitch;
            for (std::uint32_t n = e - s; n; --n, ++p, a += g.dx[3]) *p = std::uint8_t(std::uint32_t(a) >> 16);
        });
    }
    // 8-bit surfaces have no colour to blend: the alpha entry points forward to the opaque ones
    void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t) override {
        RenderSolid(spans, count, dst, colour);
    }
    void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        RenderGradient(spans, count, dst, g);
    }
};

// ---- 16-bit surfaces ----------------------------------------------------------------------------------
class cSpanRender565 final : public cSpanRenderBase<RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>> {
public:
    void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) *p++ = std::uint16_t(colour);
        });
    }
    void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t r = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) * 0x100;
            std::uint32_t gr = (std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1])) * 8;
            std::uint32_t b = std::uint32_t(std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0])) >> 3);
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) {
                *p++ = std::uint16_t(((b >> 16) & 0x1f) | ((gr >> 16) & 0x7e0) | ((r >> 16) & 0xf800));
                b += std::uint32_t(g.dx[0] >> 3);
                gr += std::uint32_t(g.dx[1]) * 8;
                r += std::uint32_t(g.dx[2]) * 0x100;
            }
        });
    }
    void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t alpha) override {
        const std::uint32_t a = ((alpha >> 24) * 0x818180u) >> 0x1a;
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (s == e) return;
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xf81f, d2 = *p & 0x7e0;
                *p = std::uint16_t((((d2 * 0x20 + 0x200 + ((colour & 0x7e0) - d2) * a) & 0xfc00) |
                                    ((d1 * 0x20 + 0x8010 + ((colour & 0xf81f) - d1) * a) & 0x1f03e0)) >> 5);
            }
        });
    }
    void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t av = (std::uint32_t(g.dy[3]) * y + std::uint32_t(g.dx[3]) * x + std::uint32_t(g.base[3])) * 0x84;
            std::uint32_t rv = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) * 0x100;
            std::uint32_t gv = (std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1])) * 8;
            std::uint32_t bv = std::uint32_t(std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0])) >> 3);
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xf81f, d2 = *p & 0x7e0;
                *p = std::uint16_t((((d2 * 0x20 + 0x200 + ((gv >> 0x10 & 0x7e0) - d2) * (av >> 0x1a)) | 0x1f03e0) &
                                    ((d1 * 0x20 + 0x8010 + ((((bv & 0x1f0000) | (rv & 0xf8000000)) >> 0x10) - d1) * (av >> 0x1a)) | 0xfc00)) >> 5);
                bv += std::uint32_t(g.dx[0] >> 3);
                gv += std::uint32_t(g.dx[1]) * 8;
                rv += std::uint32_t(g.dx[2]) * 0x100;
                av += std::uint32_t(g.dx[3]) * 0x84;
            }
        });
    }
};

class cSpanRender1555 final : public cSpanRenderBase<RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>> {
public:
    void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) *p++ = std::uint16_t(colour);
        });
    }
    void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::int32_t r = std::int32_t(std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2]));
            std::int32_t gr = std::int32_t(std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1]));
            std::int32_t b = std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0]));
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) {
                // (sic) blue lands in bits 0-4, green in 5-9 and red in 10-14 of the 16.16 values pre-scaled by 1, 0x20, 0x400
                *p++ = std::uint16_t(((std::uint32_t(b) >> 16) & 0x1f) | ((std::uint32_t(gr) >> 16) & 0x3e0) | ((std::uint32_t(r) >> 16) & 0x7c00));
                b += g.dx[0];
                gr += g.dx[1] * 0x20;
                r += g.dx[2] * 0x400;
            }
        });
    }
    void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t alpha) override {
        const std::uint32_t a = ((alpha >> 24) * 0x818180u) >> 0x1a;
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (s == e) return;
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0x7c1f, d2 = *p & 0x3e0;
                *p = std::uint16_t((((d2 * 0x20 + 0x200 + ((colour & 0x3e0) - d2) * a) | 0x1f83e0) &
                                    ((d1 * 0x20 + 0x4010 + ((colour & 0x7c1f) - d1) * a) | 0x107c00)) >> 5);
            }
        });
    }
    void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t av = (std::uint32_t(g.dy[3]) * y + std::uint32_t(g.dx[3]) * x + std::uint32_t(g.base[3])) * 0x84;
            std::uint32_t rv = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) * 0x80;
            std::uint32_t gv = (std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1])) * 4;
            std::uint32_t bv = std::uint32_t(std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0])) >> 3);
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0x7c1f, d2 = *p & 0x3e0;
                *p = std::uint16_t((((d2 * 0x20 + 0x200 + ((gv >> 0x10 & 0x3e0) - d2) * (av >> 0x1a)) | 0x1f83e0) &
                                    ((d1 * 0x20 + 0x4010 + ((((bv & 0x1f0000) | (rv & 0x7c000000)) >> 0x10) - d1) * (av >> 0x1a)) | 0x107c00)) >> 5);
                bv += std::uint32_t(g.dx[0] >> 3);
                gv += std::uint32_t(g.dx[1]) * 4;
                rv += std::uint32_t(g.dx[2]) * 0x80;
                av += std::uint32_t(g.dx[3]) * 0x84;
            }
        });
    }
};

class cSpanRender4444 final : public cSpanRenderBase<RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>> {
public:
    void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) *p++ = std::uint16_t(colour);
        });
    }
    void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t r = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) * 0x10 | 0xf0000000u;
            std::int32_t gr = std::int32_t(std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1]));
            std::uint32_t b = std::uint32_t(std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0])) >> 4);
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n) {
                *p++ = std::uint16_t(((b >> 16) & 0xf) | ((std::uint32_t(gr) >> 16) & 0xf0) | ((r >> 16) & 0xff00));
                b += std::uint32_t(g.dx[0] >> 4);
                gr += g.dx[1];
                r += std::uint32_t(g.dx[2]) * 0x10;
            }
        });
    }
    void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t alpha) override {
        const std::uint32_t a = ((alpha >> 0x1b) + 1) >> 1;
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (s == e) return;
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xf0f, d2 = *p & 0xf0;
                *p = std::uint16_t((((d2 * 0x10 + 0x80 + ((colour & 0xf0) - d2) * a) | 0xff0f0) &
                                    ((d1 * 0x10 + 0x808 + ((colour & 0xf0f) - d1) * a) | 0xf0f00)) >> 4);
            }
        });
    }
    void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t av = (std::uint32_t(g.dy[3]) * y + std::uint32_t(g.dx[3]) * x + std::uint32_t(g.base[3])) * 0x88;
            std::uint32_t rv = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) * 0x10;
            std::uint32_t gv = std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1]);
            std::uint32_t bv = std::uint32_t(std::int32_t(std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0])) >> 4);
            std::uint16_t* p = detail::Row16(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xf0f, d2 = *p & 0xf0;
                *p = std::uint16_t((((d2 * 0x10 + 0x80 + ((gv >> 0x10 & 0xf0) - d2) * (av >> 0x1b)) | 0xff0f0) &
                                    ((d1 * 0x10 + 0x808 + ((((bv & 0xf0000) | (rv & 0xf000000)) >> 0x10) - d1) * (av >> 0x1b)) | 0xf0f00)) >> 4);
                bv += std::uint32_t(g.dx[0] >> 4);
                gv += std::uint32_t(g.dx[1]);
                rv += std::uint32_t(g.dx[2]) * 0x10;
                av += std::uint32_t(g.dx[3]) * 0x88;
            }
        });
    }
};

// ---- 32-bit surface ---------------------------------------------------------------------------------------
class cSpanRender8888 final : public cSpanRenderBase<RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>> {
public:
    void RenderSolid(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            std::uint32_t* p = detail::Row32(dst, s);
            for (std::uint32_t n = e - s; n; --n) *p++ = colour;
        });
    }
    void RenderGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t r = (std::uint32_t(g.dy[2]) * y + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2])) | 0xff000000u;
            std::uint32_t gr = std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1]);
            std::uint32_t b = std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0]);
            std::uint32_t* p = detail::Row32(dst, s);
            for (std::uint32_t n = e - s; n; --n) {
                *p++ = (b >> 16 & 0xff) | (gr >> 8 & 0xff00) | (r & 0xffff0000u);
                b += std::uint32_t(g.dx[0]);
                gr += std::uint32_t(g.dx[1]);
                r += std::uint32_t(g.dx[2]);
            }
        });
    }
    void RenderAlpha(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, std::uint32_t colour, std::uint32_t alpha) override {
        const std::int32_t a = std::int32_t((alpha >> 24) - std::uint32_t(std::int32_t(alpha) >> 31));
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (s == e) return;
            std::uint32_t* p = detail::Row32(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xff00ff, d2 = *p & 0xff00;
                *p = ((((colour & 0xff00) - d2) >> 8) * std::uint32_t(a) + 0x80 + d2 | 0xffff00ffu) &
                     ((((((colour & 0xff00ff) - d1) * std::uint32_t(a) + 0x800080) >> 8) + d1) | 0xff00ff00u);
            }
        });
    }
    void RenderAlphaGradient(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc& dst, const cGradientInfo& g) override {
        detail::ForEachSpan(spans, count, [&](std::uint32_t s, std::uint32_t e) {
            if (e == s) return;
            const std::uint32_t x = s & 0xffff, y = s >> 16;
            std::uint32_t av = std::uint32_t(g.dy[3]) * y + std::uint32_t(g.dx[3]) * x + std::uint32_t(g.base[3]);
            std::uint32_t rv = std::uint32_t(g.dy[2]) * y + 0xff000000u + std::uint32_t(g.dx[2]) * x + std::uint32_t(g.base[2]);
            std::uint32_t gv = std::uint32_t(g.dy[1]) * y + std::uint32_t(g.dx[1]) * x + std::uint32_t(g.base[1]);
            std::uint32_t bv = std::uint32_t(g.dy[0]) * y + std::uint32_t(g.dx[0]) * x + std::uint32_t(g.base[0]);
            std::uint32_t* p = detail::Row32(dst, s);
            for (std::uint32_t n = e - s; n; --n, ++p) {
                const std::uint32_t d1 = *p & 0xff00ff, d2 = *p & 0xff00;
                *p = (((((gv >> 8) & 0xff00) - d2) >> 8) * (av >> 0x10) + 0x80 + d2 | 0xffff00ffu) &
                     (((((((bv >> 0x10) & 0xff) | (rv & 0xff0000)) - d1) * (av >> 0x10) + 0x800080) >> 8) + d1 | 0xff00ff00u);
                bv += std::uint32_t(g.dx[0]);
                gv += std::uint32_t(g.dx[1]);
                rv += std::uint32_t(g.dx[2]);
                av += std::uint32_t(g.dx[3]);
            }
        });
    }
};

}  // namespace nGZDrawSoftRender
