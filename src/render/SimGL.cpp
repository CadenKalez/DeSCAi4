#include "render/SimGL.h"

#include <cstddef>
#include <cstring>

namespace nRZSimGL {
namespace {

// Vertex-format tables from the binary (kVFOffset, kVFMask, kVFEltSize, kVFOffsetMask).
constexpr uint32_t kVFOffset[10] = {0, 2, 5, 6, 7, 8, 0xa, 0xe, 0x12, 0x16};
constexpr uint32_t kVFMask[10] = {3, 7, 1, 1, 1, 3, 0xf, 0xf, 0xf, 0xf};
constexpr uint32_t kVFEltSize[10] = {0xc, 4, 4, 0xc, 4, 4, 4, 8, 0xc, 0x10};
constexpr uint32_t kVFOffsetMask[9] = {0, 3, 0x1f, 0x3f, 0x7f, 0xff, 0x3ff, 0x3fff, 0x3ffff};

inline uint16_t Load16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint32_t Load32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline void Store16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
inline void Store32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// 5:6:5 -> tinted 5:6:5 endpoint (tint components are 5-bit scale factors), shared by both DXT tinters.
inline uint32_t TintEndpoint565(uint32_t c, uint32_t r, uint32_t g, uint32_t b) {
    return ((((c & 0x7e0) * g + 0x400) & 0xf800) |
            ((((c & 0xf800) * r) | ((c & 0x1f) * b)) + 0x10010 & 0x3e03c0)) >> 6;
}

}  // namespace

int PackStandardVertexFormat(int format) {
    if (format < 0) return format;
    switch (format) {
        case 0x20: return -0x7fffffff;
        case 0x21: return -0x7fffffbf;
        case 0x22: return -0x7ffffebf;
        case 0x23: return -0x7fffbfbf;
        case 0x24: return -0x7fff7fbf;
        case 0x25: return -0x7fffbebf;
        case 0x26: return -0x7fff7ebf;
        case 1: return -0x7ffffeff;
        case 2: return -0x7fffbfff;
        case 3: return -0x7fff7fff;
        case 10: return -0x7fffbeff;
        case 0xb: return -0x7fff7eff;
        default: return 0;
    }
}

uint32_t MakeStandardVertexFormat(uint32_t elementCount, const uint32_t* elementTypes) {
    uint32_t counts[10] = {};
    for (uint32_t i = 0; i < elementCount; ++i) ++counts[elementTypes[i]];
    uint32_t packed = 0;
    for (int i = 0; i < 10; ++i) packed |= counts[i] << (kVFOffset[i] & 0x1f);
    return packed | 0x80000000u;
}

int StandardVFStride(uint32_t format) {
    switch (int(format)) {
        case 0x20: return 0xc;
        case 0x21: return 0x18;
        case 0x22: return 0x1c;
        case 0x23: return 0x20;
        case 0x24: return 0x28;
        case 0x25: return 0x24;
        case 0x26: return 0x2c;
        case 1: return 0x10;
        case 2: return 0x14;
        case 3: return 0x1c;
        case 10: return 0x18;
        case 0xb: return 0x20;
        default: break;
    }
    uint32_t packed = format;
    if (int(format) >= 0) packed = uint32_t(PackStandardVertexFormat(int(format)));
    const uint32_t shifted6 = packed >> 6;
    int stride = int(((packed >> 0xb) & 0x78) + ((packed >> 8) & 0x3c) + (shifted6 & 0xc) + (packed & 3) * 0xc);
    if ((packed & 0xf) != 0) {
        stride += int((packed >> 0x12 & 0xf0) + ((packed >> 5) & 4) + ((packed >> 3) & 4) + (packed & 0x1c) +
                      (-(shifted6 & 1) & 0xc) + (packed >> 0x12 & 0xf) * 0xc);
    }
    return stride;
}

int StandardVFElementOffset(uint32_t format, uint32_t elementType, int index) {
    if (int(format) >= 0) format = uint32_t(PackStandardVertexFormat(int(format)));
    int offset = index * int(kVFEltSize[elementType]);
    if (int(elementType) > 0) {
        offset += StandardVFStride((kVFOffsetMask[elementType - 1] | 0x80000000u) & format);
    }
    return offset;
}

uint32_t StandardVFNumElements(uint32_t format, uint32_t elementType) {
    if (int(format) >= 0) format = uint32_t(PackStandardVertexFormat(int(format)));
    return format >> (kVFOffset[elementType] & 0x1f) & kVFMask[elementType];
}

void ExtractElements(uint32_t count, const void* vertices, void* out, uint32_t format, uint32_t elementType, uint32_t index) {
    const uint32_t stride = uint32_t(StandardVFStride(format));
    const uint32_t offset = uint32_t(StandardVFElementOffset(format, elementType, int(index)));
    const uint32_t size = kVFEltSize[elementType];
    const auto* src = static_cast<const uint8_t*>(vertices) + offset;
    auto* dst = static_cast<uint8_t*>(out);
    for (; count != 0; --count, src += stride, dst += size) std::memcpy(dst, src, size);
}

bool FindBitCounts(uint32_t fmt, uint32_t type, uint32_t& b0, uint32_t& b1, uint32_t& b2, uint32_t& b3) {
    const auto set = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) { b0 = a; b1 = b; b2 = c; b3 = d; return true; };
    if (int(type) < 0xd) {
        if (type < 2) {
            switch (fmt) {
                case 0: case 2: return set(8, 8, 8, 0);
                case 1: case 3: return set(8, 8, 8, 8);
                case 7: return set(5, 5, 6, 0);
                case 8: return set(5, 5, 5, 1);
                case 9: case 10: return set(5, 5, 6, 4);
                default: return false;
            }
        }
        if (type != 10 || (fmt | 2) != 3) return false;
        return set(5, 5, 5, 1);
    }
    if (type == 0xd) {
        if ((fmt | 2) != 3) return false;
        return set(4, 4, 4, 4);
    }
    if (type != 0xe || (fmt | 2) != 2) return false;
    return set(5, 5, 6, 0);
}

void ConvertColorKeyToAlphaStridedTwin16(const void* src, uint32_t srcStride, void* dst, uint32_t dstStride,
                                         uint32_t width, uint32_t height, uint32_t key, uint32_t mask) {
    const uint32_t keyLow = key & mask & 0xffff;
    for (; height != 0; --height) {
        const auto* s = static_cast<const uint8_t*>(src) + uint32_t((height - 1) * srcStride);
        auto* d = static_cast<uint8_t*>(dst) + uint32_t((height - 1) * dstStride);
        uint32_t x = 0;
        for (; x + 1 < width; x += 2, s += 4, d += 4) {
            const uint32_t w = Load32(s);
            const uint32_t keepHigh = ((mask & 0xffff0000u & w) == ((key << 16) & mask)) ? (mask | 0xffff) : 0xffffffffu;
            const uint32_t keepLow = ((w & mask & 0xffff) == keyLow) ? (mask | 0xffff0000u) : 0xffffffffu;
            Store32(d, keepHigh & w & keepLow);
        }
        if (x < width) {
            const uint16_t p = Load16(s);
            const uint32_t masked = p & mask & 0xffff;
            Store16(d, masked == keyLow ? uint16_t(masked) : p);
        }
    }
}

void BltColorkeyToAlpha_ARGB1555(void* dst, long dstStride, const void* src, long srcStride, uint32_t w, uint32_t h, uint32_t key) {
    ConvertColorKeyToAlphaStridedTwin16(src, uint32_t(srcStride), dst, uint32_t(dstStride), w, h, key, 0x7fff7fff);
}
void BltColorkeyToAlpha_ARGB4444(void* dst, long dstStride, const void* src, long srcStride, uint32_t w, uint32_t h, uint32_t key) {
    ConvertColorKeyToAlphaStridedTwin16(src, uint32_t(srcStride), dst, uint32_t(dstStride), w, h, key, 0x0fff0fff);
}
void BltColorkeyToAlpha_ARGB8888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t key) {
    for (; height != 0; --height) {
        const uint32_t row = height - 1;
        const auto* s = static_cast<const uint8_t*>(src) + uint32_t(row * uint32_t(srcStride));
        auto* d = static_cast<uint8_t*>(dst) + uint32_t(row * uint32_t(dstStride));
        for (uint32_t x = 0; x < width; ++x, s += 4, d += 4) {
            const uint32_t p = Load32(s);
            const uint32_t rgb = p & 0xffffff;
            Store32(d, rgb == (key & 0xffffff) ? rgb : p);
        }
    }
}

void TintTexture_ARGB8888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    if (width == 0 || height == 0) return;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (; height != 0; --height, d += dstStride, s += srcStride) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t p = Load32(s + size_t(x) * 4);
            const uint32_t hi = ((p >> 8 & 0xff) * (t >> 8 & 0xff) + 0x800080 + (p >> 8 & 0xff0000) * (t >> 0x18)) & 0xff00ff00;
            const uint32_t lo = ((p & 0xff) * (t & 0xff) + 0x800080 + (p & 0xff0000) * (t >> 0x10 & 0xff)) >> 8 & 0xff00ff;
            Store32(d + size_t(x) * 4, hi | lo);
        }
    }
}

void TintTexture_RGB888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    if (width == 0 || height == 0) return;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (; height != 0; --height, d += dstStride, s += srcStride) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = s + size_t(x) * 3;
            uint8_t* q = d + size_t(x) * 3;
            q[0] = uint8_t((p[0] * (t & 0xff) + 0x80) >> 8);
            q[1] = uint8_t((p[1] * (t >> 8 & 0xff) + 0x80) >> 8);
            q[2] = uint8_t((p[2] * (t >> 0x10 & 0xff) + 0x80) >> 8);
        }
    }
}

void TintTexture_ARGB4444(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    if (width == 0 || height == 0) return;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    // Rows advance by stride - 4*width + 2*width: the original mixes up the pixel size here.
    const long dstStep = dstStride - long(width * 4u) + long(width) * 2;
    const long srcStep = srcStride - long(width * 4u) + long(width) * 2;
    for (; height != 0; --height, d += dstStep, s += srcStep) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t p = Load16(s + size_t(x) * 2);
            const uint32_t hi = (((p & 0xf000) * (t >> 0x1c)) | ((p & 0xf0) * (t >> 0xc & 0xf))) + 0x808 & 0xf0f00;
            const uint32_t lo = (((p & 0xf00) * (t >> 0x14 & 0xf)) | ((p & 0xf) * (t >> 4 & 0xf))) + 0x808 & 0xf0f0;
            Store16(d + size_t(x) * 2, uint16_t((hi | lo) >> 4));
        }
    }
}

void TintTexture_ARGB1555(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    if (width == 0 || height == 0) return;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    const long dstStep = dstStride - long(width * 4u) + long(width) * 2;
    const long srcStep = srcStride - long(width * 4u) + long(width) * 2;
    for (; height != 0; --height, d += dstStep, s += srcStep) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t p = Load16(s + size_t(x) * 2);
            const uint32_t green = ((p & 0x3e0u) * (t >> 0xb & 0x1f) + 0x200) & 0x7c00;
            const uint32_t redBlue = ((((p & 0x7c00u) * (t >> 0x13 & 0x1f)) | ((p & 0x1fu) * (t >> 3 & 0x1f))) + 0x4010) & 0xf83e0;
            const uint16_t alpha = uint16_t(uint16_t(t >> 0x10) & 0x8000 & p);
            Store16(d + size_t(x) * 2, uint16_t(uint16_t((green | redBlue) >> 5) | alpha));
        }
    }
}

void TintTexture_DXT1(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    const uint32_t blocksX = width >> 2, blocksY = height >> 2;
    if (blocksX == 0 || blocksY == 0) return;
    const uint32_t r = t >> 0x13 & 0x1f, g = t >> 0xb & 0x1f, b = t >> 3 & 0x1f;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint32_t by = blocksY; by != 0; --by, d += dstStride, s += srcStride) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const uint8_t* in = s + size_t(bx) * 8;
            uint8_t* out = d + size_t(bx) * 8;
            const uint16_t c0 = Load16(in), c1 = Load16(in + 2);
            const uint32_t t0 = TintEndpoint565(c0, r, g, b);
            const uint32_t t1 = TintEndpoint565(c1, r, g, b);
            uint32_t idx = Load32(in + 4);
            if (c1 < c0) {
                if (t0 < t1) {
                    Store16(out, uint16_t(t1)); Store16(out + 2, uint16_t(t0));
                    idx = ~idx;
                } else {
                    Store16(out, uint16_t(t0)); Store16(out + 2, uint16_t(t1));
                    if (t0 == t1) idx = 0;
                }
            } else if (t1 < t0) {
                Store16(out, uint16_t(t1)); Store16(out + 2, uint16_t(t0));
                idx = (idx >> 1 & 0x55555555u) ^ idx ^ 0x55555555u;
            } else {
                Store16(out, uint16_t(t0)); Store16(out + 2, uint16_t(t1));
            }
            Store32(out + 4, idx);
        }
    }
}

void TintTexture_DXT2345(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t t) {
    const uint32_t blocksX = width >> 2, blocksY = height >> 2;
    if (blocksX == 0 || blocksY == 0) return;
    const uint32_t r = t >> 0x13 & 0x1f, g = t >> 0xb & 0x1f, b = t >> 3 & 0x1f;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint32_t by = blocksY; by != 0; --by, d += dstStride, s += srcStride) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const uint8_t* in = s + size_t(bx) * 16;
            uint8_t* out = d + size_t(bx) * 16;
            const uint16_t c0 = Load16(in + 8), c1 = Load16(in + 10);
            const uint32_t t0 = TintEndpoint565(c0, r, g, b);
            const uint32_t t1 = TintEndpoint565(c1, r, g, b);
            // The original reads the "indices" from offset 4 (the second half of the alpha block) instead of 12,
            // so the colour index word of a tinted DXT2/3/4/5 block is derived from alpha data.
            uint32_t idx = Load32(in + 4);
            std::memcpy(out, in, 8);  // alpha block
            if (c1 < c0) {
                if (t0 < t1) idx = ~idx;
                else if (t0 == t1) idx = 0;
            } else if (t1 < t0) {
                idx = (idx >> 1 & 0x55555555u) ^ idx ^ 0x55555555u;
            }
            // The original finishes every path by storing the unswapped endpoints (even after rewriting
            // the indices for a swap), so the swap never takes effect here.
            Store16(out + 8, uint16_t(t0));
            Store16(out + 10, uint16_t(t1));
            Store32(out + 12, idx);
        }
    }
}

}  // namespace nRZSimGL
