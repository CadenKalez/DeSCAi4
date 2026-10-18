// Triangle-list generation for indexed strips and fans (0x1006048c4..0x100604c33; six instantiations over the input index
// type: unsigned short, unsigned int, unsigned char; the nSGLDX7 quad variants live in render/GDIndexOps). `indexCount` is the number of indices in the strip / fan (2 or
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

