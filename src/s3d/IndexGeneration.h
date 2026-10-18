// Triangle-list generation for indexed strips and fans (0x1006048c4..0x100604c33; six instantiations over the input index
// type: unsigned short, unsigned int, unsigned char). `indexCount` is the number of indices in the strip / fan (2 or
// fewer writes nothing). The output is 16-bit triangle indices; `minIndex` / `maxIndex` get the range of indices seen.
// Strips alternate the winding: triangles are (a,b,c), (c,b,d), (c,d,e), ... exactly as the original produced them.
#pragma once
#include <cstdint>

template <class T>
void GenerateIndexedTriListForIndexedTriangleStrip(int indexCount, const T* in, std::uint16_t* out, std::uint32_t* minIndex,
                                                   std::uint32_t* maxIndex) {
    std::uint32_t a = in[0], b = in[1];
    std::uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    if (indexCount * 3 != 6) {
        const T* next = in + 2;
        std::uint32_t at = 1;
        std::uint32_t end = static_cast<std::uint32_t>(indexCount * 3 - 6);
        for (;;) {
            const std::uint32_t c = *next++;
            out[at - 1] = static_cast<std::uint16_t>(a);
            out[at] = static_cast<std::uint16_t>(b);
            out[at + 1] = static_cast<std::uint16_t>(c);
            if (c < lo) lo = c;
            if (hi < c) hi = c;
            std::uint32_t nextA;
            if (((at - 1) & 1) != 0) {
                b = c;
                nextA = a;
            } else {
                nextA = c;
            }
            a = nextA;
            const std::uint32_t test = at + 2;
            at += 3;
            if (!(test < end)) break;
        }
    }
    *minIndex = lo;
    *maxIndex = hi;
}

template <class T>
void GenerateIndexedTriListForIndexedTriangleFan(int indexCount, const T* in, std::uint16_t* out, std::uint32_t* minIndex,
                                                 std::uint32_t* maxIndex) {
    const std::uint32_t hub = in[0];
    std::uint32_t previous = in[1];
    std::uint32_t lo = hub < previous ? hub : previous, hi = hub < previous ? previous : hub;
    if (indexCount * 3 != 6) {
        const T* next = in + 2;
        std::uint32_t at = 1;
        std::uint32_t end = static_cast<std::uint32_t>(indexCount * 3 - 6);
        for (;;) {
            const std::uint32_t c = *next++;
            out[at - 1] = static_cast<std::uint16_t>(hub);
            out[at] = static_cast<std::uint16_t>(previous);
            out[at + 1] = static_cast<std::uint16_t>(c);
            if (c < lo) lo = c;
            if (hi < c) hi = c;
            previous = c;
            const std::uint32_t test = at + 2;
            at += 3;
            if (!(test < end)) break;
        }
    }
    *minIndex = lo;
    *maxIndex = hi;
}

// ---- nSGLDX7 (0x10060c286..0x10060c8eb): the same for quads, and the plain (non indexed) versions. `count` is the number of
// vertices (or indices); the plain ones fill `out` with the triangle indices 0.. in strip / fan / quad order. -----------
namespace nSGLDX7 {

// Quads (v, v+1, v+2, v+3) become (v, v+1, v+2) and (v, v+2, v+3). 16-bit arithmetic, as in the original.
inline void GenerateIndexedTriListForQuads(int count, std::uint16_t* out) {
    if (0 < count) {
        std::uint16_t base = 0;
        std::uint32_t at = 0;
        do {
            at &= 0xffff;
            out[at] = base;
            out[at + 1] = static_cast<std::uint16_t>(base + 1);
            out[at + 2] = static_cast<std::uint16_t>(base + 2);
            out[at + 3] = base;
            out[at + 4] = static_cast<std::uint16_t>(base + 2);
            out[at + 5] = static_cast<std::uint16_t>(base + 3);
            at += 6;
            base = static_cast<std::uint16_t>(base + 4);
        } while (static_cast<int>(base) < count);
    }
}

// Quad strips: (k, k+1, k+3) and (k, k+3, k+2) for k = 0, 2, 4, ...
inline void GenerateIndexedTriListForQuadStrip(int count, std::uint16_t* out) {
    if (2 < count) {
        int k = 0;
        std::uint16_t odd = 1;
        std::uint16_t* p = out;
        do {
            p[0] = static_cast<std::uint16_t>(odd - 1);
            p[1] = odd;
            const std::uint16_t far = static_cast<std::uint16_t>(k + 3);
            p[2] = far;
            p[3] = static_cast<std::uint16_t>(k);
            p[4] = far;
            k += 2;
            p[5] = static_cast<std::uint16_t>(k);
            p += 6;
            odd = static_cast<std::uint16_t>(odd + 2);
        } while (k < count - 2);
    }
}

// Triangle strips: (i, i+1, i+2) with every other triangle's first two swapped.
inline void GenerateIndexedTriListForTriangleStrip(int count, std::uint16_t* out) {
    if (0 < count) {
        std::uint32_t at = 0, step = 0;
        std::uint16_t base = 0;
        do {
            std::uint16_t second;
            if ((step & 1) == 0) {
                out[at] = base;
                second = static_cast<std::uint16_t>(base + 1);
            } else {
                out[at] = static_cast<std::uint16_t>(base + 1);
                second = base;
            }
            out[at + 1] = second;
            out[at + 2] = static_cast<std::uint16_t>(base + 2);
            step = at + 3;
            at = static_cast<std::uint16_t>(step);
            base = static_cast<std::uint16_t>(base + 1);
        } while (static_cast<int>(step & 0xffff) < count);
    }
}

// Triangle fans: (0, i, i+1).
inline void GenerateIndexedTriListForTriangleFan(int count, std::uint16_t* out) {
    if (0 < count) {
        std::uint32_t at = 0;
        std::uint16_t next = 1;
        do {
            out[at] = 0;
            out[at + 1] = next;
            next = static_cast<std::uint16_t>(next + 1);
            out[at + 2] = next;
            at = (at + 3) & 0xffff;
        } while (static_cast<int>(at) < count);
    }
}

// Indexed quad list: each four indices (a,b,c,d) -> (a,b,c) and (a,c,d).
template <class T>
void GenerateIndexedTriListForIndexedQuads(int count, const T* in, std::uint16_t* out, std::uint32_t* minIndex,
                                           std::uint32_t* maxIndex) {
    std::uint32_t lo = in[0], hi = in[0];
    auto see = [&](std::uint32_t v) {
        if (v < lo) lo = v;
        else if (hi < v) hi = v;
    };
    if (2 < static_cast<std::uint32_t>(count) + 1u) {
        std::uint32_t k = 0, at = 2;
        const std::uint32_t limit = static_cast<std::uint32_t>((count / 2) * 3);
        for (;;) {
            k &= 0xffff;
            const std::uint32_t a = in[k], b = in[k + 1], c = in[k + 2], d = in[k + 3];
            out[at - 2] = static_cast<std::uint16_t>(a);
            out[at - 1] = static_cast<std::uint16_t>(b);
            out[at] = static_cast<std::uint16_t>(c);
            out[at + 1] = static_cast<std::uint16_t>(a);
            out[at + 2] = static_cast<std::uint16_t>(c);
            out[at + 3] = static_cast<std::uint16_t>(d);
            see(a); see(b); see(c); see(d);
            k += 4;
            const std::uint32_t test = at + 4;
            at += 6;
            if (!(test < limit)) break;
        }
    }
    *minIndex = lo;
    *maxIndex = hi;
}

// Indexed quad strip: indices (a,b,c,d,e,f,...) -> (a,b,c), (b,d,c), (c,d,e), (d,f,e), ...
template <class T>
void GenerateIndexedTriListForIndexedQuadStrip(int count, const T* in, std::uint16_t* out, std::uint32_t* minIndex,
                                               std::uint32_t* maxIndex) {
    std::uint32_t lo = in[0], hi = in[0];
    auto see = [&](std::uint32_t v) {
        if (v < lo) lo = v;
        else if (hi < v) hi = v;
    };
    if (count * 3 != 6) {
        std::uint32_t k = 0, at = 3;
        const std::uint32_t end = static_cast<std::uint32_t>(count * 3 - 6);
        for (;;) {
            const std::uint32_t a = in[k], b = in[k + 1], c = in[k + 2], d = in[k + 3];
            out[at - 3] = static_cast<std::uint16_t>(a);
            out[at - 2] = static_cast<std::uint16_t>(b);
            out[at - 1] = static_cast<std::uint16_t>(c);
            out[at] = static_cast<std::uint16_t>(b);
            out[at + 1] = static_cast<std::uint16_t>(d);
            out[at + 2] = static_cast<std::uint16_t>(c);
            see(a); see(b); see(c); see(d);
            if (end <= at + 3) break;
            k += 2;
            at += 6;
        }
    }
    *minIndex = lo;
    *maxIndex = hi;
}

}  // namespace nSGLDX7
