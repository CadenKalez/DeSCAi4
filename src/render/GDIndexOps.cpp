#include "render/GDIndexOps.h"

#include <cstring>

namespace nSGLDX7 {
namespace {

// Running min/max of the indices seen so far.
struct Range {
    uint32_t lo, hi;
    void Add(uint32_t v) { if (v > hi) hi = v; if (v < lo) lo = v; }
};

// The 16-bit quad/strip generators do their bookkeeping in narrow counters that wrap; keep that.
template <class In, bool kMaskInput, bool kMinOnlyFirst>
void IndexedQuads(int n, const In* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex) {
    Range r{in[0], in[0]};
    if (uint32_t(n) + 1u > 2) {
        const uint32_t limit = uint32_t((n / 2) * 3);
        uint32_t src = 0, dst = 0;
        for (;;) {
            const uint32_t s = kMaskInput ? (src & 0xffff) : src;
            const In a = in[s], b = in[s + 1], c = in[s + 2], d = in[s + 3];
            out[dst + 0] = uint16_t(a); out[dst + 1] = uint16_t(b); out[dst + 2] = uint16_t(c);
            out[dst + 3] = uint16_t(a); out[dst + 4] = uint16_t(c); out[dst + 5] = uint16_t(d);
            r.Add(a);
            if (kMinOnlyFirst) {
                if (b > r.hi) r.hi = b;
                if (c > r.hi) r.hi = c;
                if (d > r.hi) r.hi = d;
            } else {
                r.Add(b); r.Add(c); r.Add(d);
            }
            dst += 6;
            if (limit <= dst) break;
            src += 4;
        }
    }
    *minIndex = r.lo;
    *maxIndex = r.hi;
}

template <class In>
void IndexedQuadStrip(int n, const In* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex) {
    Range r{in[0], in[0]};
    if (n * 3 != 6) {
        const uint32_t limit = uint32_t(n * 3 - 6);
        uint32_t src = 0, dst = 0;
        for (;;) {
            const In a = in[src], b = in[src + 1], c = in[src + 2], d = in[src + 3];
            out[dst + 0] = uint16_t(a); out[dst + 1] = uint16_t(b); out[dst + 2] = uint16_t(c);
            out[dst + 3] = uint16_t(b); out[dst + 4] = uint16_t(d); out[dst + 5] = uint16_t(c);
            r.Add(a); r.Add(b); r.Add(c); r.Add(d);
            dst += 6;
            if (limit <= dst) break;
            src += 2;
        }
    }
    *minIndex = r.lo;
    *maxIndex = r.hi;
}

template <class In>
void NarrowIndices(int count, const In* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex) {
    Range r{in[0], in[0]};
    for (int i = 0; i < count; ++i) {
        out[i] = uint16_t(in[i]);
        r.Add(in[i]);
    }
    *minIndex = r.lo;
    *maxIndex = r.hi;
}

constexpr uint32_t kAlphaBits[6] = {4, 1, 8, 1, 8, 8};
constexpr uint32_t kColorBits[8] = {16, 24, 16, 16, 32, 16, 16, 16};
constexpr uint32_t kSourceBits[15] = {32, 32, 24, 24, 16, 16, 16, 16, 16, 16, 4, 0, 8, 0, 8};

}  // namespace

void GenerateIndexedTriListForQuads(int vertexCount, uint16_t* out) {
    if (vertexCount <= 0) return;
    uint16_t v = 0;
    uint64_t pos = 0;
    do {
        pos &= 0xffff;
        out[pos] = v; out[pos + 1] = uint16_t(v + 1); out[pos + 2] = uint16_t(v + 2);
        out[pos + 3] = v; out[pos + 4] = uint16_t(v + 2); out[pos + 5] = uint16_t(v + 3);
        pos = uint32_t(pos) + 6;
        v = uint16_t(v + 4);
    } while (int(v) < vertexCount);
}

void GenerateIndexedTriListForQuadStrip(int vertexCount, uint16_t* out) {
    if (vertexCount <= 2) return;
    uint16_t* p = out + 5;
    int pair = 0;
    uint16_t odd = 1;
    do {
        p[-5] = uint16_t(odd - 1);
        p[-4] = odd;
        const uint16_t d = uint16_t(pair + 3);
        p[-3] = d;
        p[-2] = uint16_t(pair);
        p[-1] = d;
        pair += 2;
        p[0] = uint16_t(pair);
        p += 6;
        odd = uint16_t(odd + 2);
    } while (pair < vertexCount - 2);
}

void GenerateIndexedTriListForTriangleStrip(int vertexCount, uint16_t* out) {
    if (vertexCount <= 0) return;
    uint64_t pos = 0;
    uint32_t next = 0;
    uint16_t v = 0;
    do {
        uint16_t second;
        if ((next & 1) == 0) { out[pos] = v; second = uint16_t(v + 1); }
        else { out[pos] = uint16_t(v + 1); second = v; }
        out[pos + 1] = second;
        out[pos + 2] = uint16_t(v + 2);
        next = uint32_t(pos) + 3;
        pos = uint16_t(next);
        v = uint16_t(v + 1);
    } while (int(next & 0xffff) < vertexCount);
}

void GenerateIndexedTriListForTriangleFan(int vertexCount, uint16_t* out) {
    if (vertexCount <= 0) return;
    uint32_t pos = 0;
    uint16_t v = 1;
    do {
        out[pos] = 0;
        out[uint64_t(pos) + 1] = v;
        v = uint16_t(v + 1);
        out[uint64_t(pos) + 2] = v;
        pos = (pos + 3) & 0xffff;
    } while (int(pos) < vertexCount);
}

void GenerateIndexedTriListForIndexedQuads32(int n, const uint32_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuads<uint32_t, true, false>(n, in, out, lo, hi); }
void GenerateIndexedTriListForIndexedQuads16(int n, const uint16_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuads<uint16_t, true, true>(n, in, out, lo, hi); }
void GenerateIndexedTriListForIndexedQuads8(int n, const uint8_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuads<uint8_t, false, false>(n, in, out, lo, hi); }
void GenerateIndexedTriListForIndexedQuadStrip32(int n, const uint32_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuadStrip(n, in, out, lo, hi); }
void GenerateIndexedTriListForIndexedQuadStrip16(int n, const uint16_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuadStrip(n, in, out, lo, hi); }
void GenerateIndexedTriListForIndexedQuadStrip8(int n, const uint8_t* in, uint16_t* out, uint32_t* lo, uint32_t* hi) { IndexedQuadStrip(n, in, out, lo, hi); }

void ConvertUint32IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* lo, uint32_t* hi) {
    NarrowIndices(count, static_cast<const uint32_t*>(in), out, lo, hi);
}
void ConvertUint16IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* lo, uint32_t* hi) {
    NarrowIndices(count, static_cast<const uint16_t*>(in), out, lo, hi);
}
void ConvertUint8IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* lo, uint32_t* hi) {
    NarrowIndices(count, static_cast<const uint8_t*>(in), out, lo, hi);
}

namespace {
constexpr uint32_t kWhite = 0xffffffffu;
inline void Copy(uint8_t* dst, size_t dstOff, const uint8_t* src, size_t srcOff, size_t n) { std::memcpy(dst + dstOff, src + srcOff, n); }
inline void PutWhite(uint8_t* dst) { std::memcpy(dst + 12, &kWhite, 4); }
}  // namespace

void ConvertVerticesFromV3F(uint8_t* dst, const uint8_t* src, long stride, int count) {
    for (; count > 0; --count, src += stride * 4, dst += 32) { Copy(dst, 0, src, 0, 12); PutWhite(dst); }
}
void ConvertVerticesFromV3F_C4UB(uint8_t* dst, const uint8_t* src, long stride, int count) {
    for (; count > 0; --count, src += stride * 4, dst += 32) Copy(dst, 0, src, 0, 16);
}
void ConvertVerticesFromV3F_T2F(uint8_t* dst, const uint8_t* src, long stride, int count) {
    for (; count > 0; --count, src += stride * 4, dst += 32) { Copy(dst, 0, src, 0, 12); PutWhite(dst); Copy(dst, 16, src, 12, 8); }
}
void ConvertVerticesFromV3F_2T2F(uint8_t* dst, const uint8_t* src, long stride, int count) {
    for (; count > 0; --count, src += stride * 4, dst += 32) { Copy(dst, 0, src, 0, 12); PutWhite(dst); Copy(dst, 16, src, 12, 16); }
}
void ConvertVerticesFromV3F_C4UB_T2F(uint8_t* dst, const uint8_t* src, long stride, int count) {
    for (; count > 0; --count, src += stride * 4, dst += 32) Copy(dst, 0, src, 0, 24);
}

uint32_t GetFormatInfoNumAlphaBits(int fmt) { return uint32_t(fmt - 2) < 6 ? kAlphaBits[fmt - 2] : 0; }
uint32_t GetFormatInfoNumColorBits(uint32_t fmt) { return fmt < 8 ? kColorBits[fmt] : 0; }
uint32_t GetSourceFormatNumBits(uint32_t fmt) { return fmt < 15 ? kSourceBits[fmt] : 0; }

bool TranslateGDFormatToSourceFormat(int fmt, uint32_t type, uint32_t* out) {
    if (type < 2) {
        switch (fmt) {
            case 0: *out = kSrcBGR888; break;
            case 1: *out = kSrcABGR8888; break;
            case 2: *out = kSrcRGB888; break;
            case 3: *out = kSrcARGB8888; break;
            case 7: case 8: *out = kSrcDXT1; break;
            case 9: *out = kSrcDXT3; break;
            case 10: *out = kSrcDXT5; break;
            default: return false;
        }
    } else if (type == 10) {
        if (fmt == 3) *out = kSrcARGB1555Rev;
        else if (fmt == 1) *out = kSrcABGR1555Rev;
        else return false;
    } else if (type == 0xd) {
        if (fmt == 3) *out = kSrcARGB4444Rev;
        else if (fmt == 1) *out = kSrcABGR4444Rev;
        else return false;
    } else if (type == 0xe) {
        if (fmt == 2) *out = kSrcRGB565Rev;
        else if (fmt == 0) *out = kSrcBGR565Rev;
        else return false;
    } else {
        return false;
    }
    return true;
}

}  // namespace nSGLDX7
