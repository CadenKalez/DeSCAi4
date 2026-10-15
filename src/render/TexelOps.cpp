#include "render/TexelOps.h"

#include <cstddef>
#include <cstring>

namespace nsTexelOp {
namespace {

struct Px { uint32_t a, r, g, b; };  // 8-bit channels

inline uint32_t Up4(uint32_t v) { return (v << 4) | v; }
inline uint32_t Up5(uint32_t v) { return (v << 3) | (v >> 2); }
inline uint32_t Up6(uint32_t v) { return (v << 2) | (v >> 4); }

struct ARGB8 { static constexpr int kBytes = 4;
    static Px Load(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return {v >> 24, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff}; }
    static void Store(uint8_t* p, Px c) { uint32_t v = c.a << 24 | c.r << 16 | c.g << 8 | c.b; std::memcpy(p, &v, 4); } };
struct ABGR8 { static constexpr int kBytes = 4;
    static Px Load(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return {v >> 24, v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff}; }
    static void Store(uint8_t* p, Px c) { uint32_t v = c.a << 24 | c.b << 16 | c.g << 8 | c.r; std::memcpy(p, &v, 4); } };
struct RGB8 { static constexpr int kBytes = 3;
    static Px Load(const uint8_t* p) { return {0xff, p[2], p[1], p[0]}; }
    static void Store(uint8_t* p, Px c) { p[0] = uint8_t(c.b); p[1] = uint8_t(c.g); p[2] = uint8_t(c.r); } };
struct BGR8 { static constexpr int kBytes = 3;
    static Px Load(const uint8_t* p) { return {0xff, p[0], p[1], p[2]}; }
    static void Store(uint8_t* p, Px c) { p[0] = uint8_t(c.r); p[1] = uint8_t(c.g); p[2] = uint8_t(c.b); } };
struct ARGB4 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {Up4(v >> 12), Up4((v >> 8) & 15), Up4((v >> 4) & 15), Up4(v & 15)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.a >> 4) << 12 | (c.r >> 4) << 8 | (c.g >> 4) << 4 | (c.b >> 4)); std::memcpy(p, &v, 2); } };
struct ABGR4 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {Up4(v >> 12), Up4(v & 15), Up4((v >> 4) & 15), Up4((v >> 8) & 15)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.a >> 4) << 12 | (c.b >> 4) << 8 | (c.g >> 4) << 4 | (c.r >> 4)); std::memcpy(p, &v, 2); } };
struct A1RGB5 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {(v >> 15) ? 0xffu : 0u, Up5((v >> 10) & 31), Up5((v >> 5) & 31), Up5(v & 31)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.a >> 7) << 15 | (c.r >> 3) << 10 | (c.g >> 3) << 5 | (c.b >> 3)); std::memcpy(p, &v, 2); } };
struct A1BGR5 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {(v >> 15) ? 0xffu : 0u, Up5(v & 31), Up5((v >> 5) & 31), Up5((v >> 10) & 31)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.a >> 7) << 15 | (c.b >> 3) << 10 | (c.g >> 3) << 5 | (c.r >> 3)); std::memcpy(p, &v, 2); } };
struct RGB565 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {0xff, Up5(v >> 11), Up6((v >> 5) & 63), Up5(v & 31)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.r >> 3) << 11 | (c.g >> 2) << 5 | (c.b >> 3)); std::memcpy(p, &v, 2); } };
struct BGR565 { static constexpr int kBytes = 2;
    static Px Load(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return {0xff, Up5(v & 31), Up6((v >> 5) & 63), Up5(v >> 11)}; }
    static void Store(uint8_t* p, Px c) { uint16_t v = uint16_t((c.b >> 3) << 11 | (c.g >> 2) << 5 | (c.r >> 3)); std::memcpy(p, &v, 2); } };

template <class S, class D>
void Run(void* dst, const void* src, int dstStride, int srcStride, int width, int height) {
    for (int y = 0; y < height; ++y) {
        uint8_t* d = static_cast<uint8_t*>(dst) + ptrdiff_t(y) * dstStride;
        const uint8_t* s = static_cast<const uint8_t*>(src) + ptrdiff_t(y) * srcStride;
        for (int x = 0; x < width; ++x, d += D::kBytes, s += S::kBytes) D::Store(d, S::Load(s));
    }
}

}  // namespace

void ConvertARGB8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB8, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB8, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB8, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
// The original maps this pair straight onto the 4-byte row copy (destination stays 32-bit).
void ConvertARGB8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Copy4UB(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Copy4UB(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR8, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR8, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR8, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR8, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR8, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB8, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB8, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB8, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB8, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB8, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR8, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR8, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR8, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR8, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR8, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB4ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB4, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB4ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB4, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB4ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB4, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertARGB4ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ARGB4, RGB8>(dst, src, dstStride, srcStride, width, height); }
// The original packs this pair as bytes A,R,G,B in memory order (B in the top byte), unlike the other *ToARGB8 converters.
void ConvertARGB4ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) {
    for (int y = 0; y < height; ++y) {
        auto* d = static_cast<uint8_t*>(dst) + ptrdiff_t(y) * dstStride;
        const auto* s = static_cast<const uint8_t*>(src) + ptrdiff_t(y) * srcStride;
        for (int x = 0; x < width; ++x, s += 2) {
            uint16_t v;
            std::memcpy(&v, s, 2);
            uint32_t n = (v & 0xfu) << 24 | (v & 0xf0u) << 12 | (v & 0xf00u) | (v >> 12);
            n = n << 4 | n;
            std::memcpy(d + size_t(x) * 4, &n, 4);
        }
    }
}
void ConvertABGR4ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR4, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR4ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR4, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR4ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR4, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR4ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR4, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertABGR4ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<ABGR4, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1RGB5ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1RGB5, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1RGB5ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1RGB5, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1RGB5ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1RGB5, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1RGB5ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1RGB5, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1RGB5ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1RGB5, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1BGR5ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1BGR5, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1BGR5ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1BGR5, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1BGR5ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1BGR5, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1BGR5ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1BGR5, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertA1BGR5ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<A1BGR5, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB565ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB565, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB565ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB565, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB565ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB565, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB565ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB565, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertRGB565ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<RGB565, ARGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR565ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR565, ARGB4>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR565ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR565, RGB565>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR565ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR565, A1RGB5>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR565ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR565, RGB8>(dst, src, dstStride, srcStride, width, height); }
void ConvertBGR565ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height) { Run<BGR565, ARGB8>(dst, src, dstStride, srcStride, width, height); }

void Copy1UB(void* dst, const void* src, int dstStride, int srcStride, int width, int height) {
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    for (; height != 0; --height, s += srcStride, d += dstStride) {
        if (width < 0x20) std::memcpy(d, s, size_t(width)); else std::memmove(d, s, size_t(width));
    }
}

void Copy4UB(void* dst, const void* src, int dstStride, int srcStride, int width, int height) {
    const int rowBytes = width * 4;
    if (srcStride == dstStride && rowBytes == dstStride) {
        std::memcpy(dst, src, size_t(srcStride) * size_t(height));
        return;
    }
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    for (; height != 0; --height, s += srcStride, d += dstStride) {
        if (width < 8) std::memcpy(d, s, size_t(rowBytes)); else std::memmove(d, s, size_t(rowBytes));
    }
}

}  // namespace nsTexelOp
