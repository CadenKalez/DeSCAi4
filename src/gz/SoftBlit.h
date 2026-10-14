// nGZDrawSoftRender::cBlitter — software blitters of the GZ draw layer (888 functions: 49 destination /
// source pixel-format pairs x 12-13 operations x a colour-key flag), recovered as ONE template.
//
//   cBlitter<DstFormat, SrcFormat, bool KeyEnabled>::{Blt, BltModulate, BltConstantAlpha, BltModulateAlpha,
//     BltStretch*, BltTiled*}(spans, spanCount, dst, src, info)       spans = (start, end) pairs, start/end =
//     (y << 16) | x, the span covers end - start pixels
//   cBlitter<...>::BltRect(width, height, dst, src, info)             straight rectangle copy
//
// RGBFormat<A, R, G, B, bytesPerPixel, bits> is the pixel layout (channel masks). The 9 formats the
// game instantiates: solid (all masks 0), A8, xRGB8888/ARGB8888, xRGB1555/ARGB1555, RGB565 and
// (A)RGB4444. Source channels are widened by bit replication, destination channels narrowed by
// truncation; blends are exact integer lane arithmetic: (s*a + d*(2^k-a) + 2^(k-1)) >> k with k = 8 for
// 8-bit alpha and 5 for 565 destinations. Verified against the decompiled originals by
// scripts/tests/gz_softblit_oracle_test.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nGZDrawSoftRender {

template <std::uint32_t A, std::uint32_t R, std::uint32_t G, std::uint32_t B, int Bytes, int Bits>
struct RGBFormat {
    static constexpr std::uint32_t kAlphaMask = A, kRedMask = R, kGreenMask = G, kBlueMask = B;
    static constexpr int kBytes = Bytes;
    static constexpr int kBits = Bits;
    static constexpr bool kSolid = (A | R | G | B) == 0;
    static constexpr std::uint32_t kColourMask = R | G | B;
};

// One surface as the blitters receive it (by value, 0x20 bytes).
struct cGZBufferSurfaceDesc {
    std::uint8_t* pixels;
    std::int64_t pitch;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t reserved[4];
};

// Blit parameters (by value, 0x20 bytes on the stack at +0x48).
struct cBltInfo {
    std::int32_t startX;     // +0x48 source start (stretch: 16.16, tiled: pixels)
    std::int32_t startY;     // +0x4c
    std::int32_t stepX;      // +0x50 source step per destination pixel (16.16)
    std::int32_t stepY;      // +0x54
    std::uint32_t srcFormatId;   // +0x58 source format id (only the span renderers' dispatch reads it)
    std::uint32_t colour;        // +0x5c modulate colour / constant alpha (A in the top byte)
    std::uint32_t key;           // +0x60 colour key (or enable flag for solid / alpha-only sources)
    std::uint8_t keyed;          // +0x64 colour key enabled (selects the keyed blitter in the dispatch table)
};

namespace detail {

constexpr int Ctz(std::uint32_t v) { return v ? __builtin_ctz(v) : 0; }
constexpr int Bits(std::uint32_t mask) { return mask ? __builtin_popcount(mask) : 0; }

struct Argb { std::uint32_t a, r, g, b; };

// widen an n-bit channel to 8 bits by replication
constexpr std::uint32_t Widen(std::uint32_t v, int n) {
    switch (n) {
        case 1: return v ? 255u : 0u;
        case 4: return v * 17u;
        case 5: return (v << 3) | (v >> 2);
        case 6: return (v << 2) | (v >> 4);
        default: return v;
    }
}

template <class F>
inline std::uint32_t Load(const std::uint8_t* p) {
    if constexpr (F::kBytes == 4) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
    else if constexpr (F::kBytes == 2) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
    else return *p;
}
template <class F>
inline void Store(std::uint8_t* p, std::uint32_t v) {
    if constexpr (F::kBytes == 4) std::memcpy(p, &v, 4);
    else if constexpr (F::kBytes == 2) { std::uint16_t s = static_cast<std::uint16_t>(v); std::memcpy(p, &s, 2); }
    else *p = static_cast<std::uint8_t>(v);
}

template <class F>
inline Argb Unpack(std::uint32_t v) {
    Argb c;
    c.a = F::kAlphaMask ? Widen((v & F::kAlphaMask) >> Ctz(F::kAlphaMask), Bits(F::kAlphaMask)) : 255u;
    // alpha-only and solid sources are white
    constexpr std::uint32_t none = F::kColourMask == 0 ? 255u : 0u;
    c.r = F::kRedMask ? Widen((v & F::kRedMask) >> Ctz(F::kRedMask), Bits(F::kRedMask)) : none;
    c.g = F::kGreenMask ? Widen((v & F::kGreenMask) >> Ctz(F::kGreenMask), Bits(F::kGreenMask)) : none;
    c.b = F::kBlueMask ? Widen((v & F::kBlueMask) >> Ctz(F::kBlueMask), Bits(F::kBlueMask)) : none;
    return c;
}

template <class F>
inline std::uint32_t Pack(const Argb& c) {
    auto field = [](std::uint32_t v8, std::uint32_t mask) -> std::uint32_t {
        if (!mask) return 0;
        return (v8 >> (8 - Bits(mask))) << Ctz(mask);
    };
    std::uint32_t v = field(c.a, F::kAlphaMask) | field(c.r, F::kRedMask) | field(c.g, F::kGreenMask) | field(c.b, F::kBlueMask);
    if constexpr (F::kAlphaMask == 0 && !F::kSolid) {
        // formats without an alpha mask keep their spare high bits set ("x" bits are written as opaque)
        constexpr std::uint32_t width = F::kBytes == 4 ? 0xffffffffu : (F::kBytes == 2 ? 0xffffu : 0xffu);
        v |= width & ~F::kColourMask;
    }
    return v;
}

inline std::uint32_t Mul8(std::uint32_t c, std::uint32_t m) { return (c * m + 0x80u) >> 8; }

}  // namespace detail

template <class Dst, class Src, bool KeyEnabled>
struct cBlitter {
    // ---- per-pixel pipeline ---------------------------------------------------------------------
    enum Op { kPlain, kModulate, kConstantAlpha, kModulateAlpha };

    template <int op>
    static inline void Pixel(std::uint8_t* d, std::uint32_t raw, const cBltInfo& info) {
        using namespace detail;
        if constexpr (KeyEnabled) {
            if constexpr (Src::kColourMask == 0) {
                if (info.key == 0) return;             // solid / alpha-only source: the key field is an enable flag
            } else {
                if ((raw & Src::kColourMask) == info.key) return;
            }
        }
        Argb s = Unpack<Src>(raw);
        if constexpr (op == kModulate || op == kModulateAlpha) {
            s.a = Mul8(s.a, info.colour >> 24);
            s.r = Mul8(s.r, (info.colour >> 16) & 0xff);
            s.g = Mul8(s.g, (info.colour >> 8) & 0xff);
            s.b = Mul8(s.b, info.colour & 0xff);
        }
        if constexpr (op == kPlain || op == kModulate) {
            Store<Dst>(d, Pack<Dst>(s));
        } else {
            // blend at the destination's channel depth: alpha is reduced to the red channel's width k and
            // lane = (d * 2^k + (s - d) * a + 2^(k-1)) >> k; the destination ends up fully opaque
            constexpr int k = Dst::kRedMask ? Bits(Dst::kRedMask) : 8;
            const std::uint32_t a8 = op == kConstantAlpha ? (info.colour >> 24) : s.a;
            const std::int64_t a = a8 >> (8 - k);
            const std::uint32_t old = Load<Dst>(d);
            auto lane = [&](std::uint32_t mask, std::uint32_t s8) -> std::uint32_t {
                if (!mask) return 0;
                const int n = Bits(mask), sh = Ctz(mask);
                const std::int64_t dc = (old & mask) >> sh;
                const std::int64_t sc = s8 >> (8 - n);
                return std::uint32_t(((dc << k) + (sc - dc) * a + (std::int64_t(1) << (k - 1))) >> k) << sh;
            };
            std::uint32_t v = lane(Dst::kRedMask, s.r) | lane(Dst::kGreenMask, s.g) | lane(Dst::kBlueMask, s.b) | Dst::kAlphaMask;
            if constexpr (Dst::kAlphaMask == 0 && !Dst::kSolid) {
                constexpr std::uint32_t width = Dst::kBytes == 4 ? 0xffffffffu : (Dst::kBytes == 2 ? 0xffffu : 0xffu);
                v |= width & ~Dst::kColourMask;
            }
            Store<Dst>(d, v);
        }
    }

    // ---- samplers: spans over a source ---------------------------------------------------------------
    enum Sampler { kDirect, kStretch, kTiled };

    template <int op, int sampler>
    static void Spans(const std::uint32_t* spans, std::uint32_t count, const cGZBufferSurfaceDesc& dst,
                      const cGZBufferSurfaceDesc& src, const cBltInfo& info) {
        for (; count; --count, spans += 2) {
            const std::uint32_t start = spans[0], end = spans[1];
            if (start == end) continue;
            const std::uint32_t x0 = start & 0xffff, y = start >> 16;
            std::uint8_t* d = dst.pixels + std::uint64_t(y) * dst.pitch + std::uint64_t(x0) * Dst::kBytes;
            const std::uint32_t n = end - start;
            if constexpr (sampler == kDirect) {
                const std::uint8_t* s = src.pixels + std::uint64_t(y) * src.pitch + std::uint64_t(x0) * Src::kBytes;
                for (std::uint32_t i = 0; i < n; ++i, d += Dst::kBytes, s += Src::kBytes)
                    Pixel<op>(d, detail::Load<Src>(s), info);
            } else if constexpr (sampler == kStretch) {
                // 16.16 source coordinates: column advances by stepX per destination pixel, row fixed per span
                std::uint32_t xFixed = std::uint32_t(info.stepX) * x0 + std::uint32_t(info.startX);
                const std::uint32_t row = (std::uint32_t(info.stepY) * y + std::uint32_t(info.startY)) >> 16;
                const std::uint8_t* rowPtr = src.pixels + std::uint64_t(row) * src.pitch;
                for (std::uint32_t i = 0; i < n; ++i, d += Dst::kBytes, xFixed += std::uint32_t(info.stepX))
                    Pixel<op>(d, detail::Load<Src>(rowPtr + std::uint64_t(xFixed >> 16) * Src::kBytes), info);
            } else {
                // tiled: the source repeats every width x height pixels
                std::uint32_t col = std::uint32_t(std::uint64_t(std::uint32_t(info.startX) + x0) % src.width);
                const std::uint32_t row = std::uint32_t(std::uint64_t(std::uint32_t(info.startY) + y) % src.height);
                const std::uint8_t* rowPtr = src.pixels + std::uint64_t(row) * src.pitch;
                for (std::uint32_t i = 0; i < n; ++i, d += Dst::kBytes) {
                    Pixel<op>(d, detail::Load<Src>(rowPtr + std::uint64_t(col) * Src::kBytes), info);
                    if (++col >= src.width) col = 0;
                }
            }
        }
    }

    // ---- public interface ------------------------------------------------------------------------------
    static void BltRect(std::uint32_t width, std::uint32_t height, cGZBufferSurfaceDesc dst, cGZBufferSurfaceDesc src,
                        cBltInfo info) {
        std::uint8_t* d = dst.pixels;
        const std::uint8_t* s = src.pixels;
        for (std::uint32_t y = 0; y < height; ++y) {
            std::uint8_t* dp = d;
            const std::uint8_t* sp = s;
            for (std::uint32_t x = 0; x < width; ++x, dp += Dst::kBytes, sp += Src::kBytes)
                Pixel<kPlain>(dp, detail::Load<Src>(sp), info);
            d += dst.pitch;
            s += src.pitch;
        }
    }
#define SOFTBLIT_METHOD(Name, Op, Sampler) \
    static void Name(const std::uint32_t* spans, std::uint32_t count, cGZBufferSurfaceDesc dst, \
                     cGZBufferSurfaceDesc src, cBltInfo info) { Spans<Op, Sampler>(spans, count, dst, src, info); }
    SOFTBLIT_METHOD(Blt, kPlain, kDirect)
    SOFTBLIT_METHOD(BltModulate, kModulate, kDirect)
    SOFTBLIT_METHOD(BltConstantAlpha, kConstantAlpha, kDirect)
    SOFTBLIT_METHOD(BltModulateAlpha, kModulateAlpha, kDirect)
    SOFTBLIT_METHOD(BltStretch, kPlain, kStretch)
    SOFTBLIT_METHOD(BltStretchModulate, kModulate, kStretch)
    SOFTBLIT_METHOD(BltStretchConstantAlpha, kConstantAlpha, kStretch)
    SOFTBLIT_METHOD(BltStretchModulateAlpha, kModulateAlpha, kStretch)
    SOFTBLIT_METHOD(BltTiled, kPlain, kTiled)
    SOFTBLIT_METHOD(BltTiledModulate, kModulate, kTiled)
    SOFTBLIT_METHOD(BltTiledConstantAlpha, kConstantAlpha, kTiled)
    SOFTBLIT_METHOD(BltTiledModulateAlpha, kModulateAlpha, kTiled)
#undef SOFTBLIT_METHOD
};

}  // namespace nGZDrawSoftRender
