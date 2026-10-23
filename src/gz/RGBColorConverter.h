// cRZRGBColorConverter — converts rows of pixels between two RGB(A) formats through four 256-entry lookup tables, one per
// source byte (4 functions: Format ctor 0x1002b4784 / 0x1002b4820, SetFormats 0x1002b4838, ctor 0x1002b4b20, Convert
// 0x1002b4b2a). A destination pixel is the sum of table[k][source byte k]; each channel is rescaled by multiplying its
// value (shifted down to bit 0) by 0xffffffff / maxValue and shifting the product down into the destination field, so
// narrowing truncates and widening does not replicate bits. A destination channel that the source lacks is filled to its
// maximum (an opaque alpha, for instance).
//
// Object layout of the original (0x1008 bytes): +0 tables[4][256], +0x1000 source bytes per pixel, +0x1004 destination bytes.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"

class cRZRGBColorConverter {
public:
    // A pixel format: bits per pixel and the four channel masks.
    struct Format {
        std::uint32_t bitsPerPixel = 0;
        std::uint32_t alphaMask = 0, redMask = 0, greenMask = 0, blueMask = 0;
        Format() = default;
        explicit Format(const cGZBufferColorType& colorType);   // 0x1002b4784: colour types 4..9; anything else is all zero
        Format(std::uint32_t bits, std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b)
            : bitsPerPixel(bits), alphaMask(a), redMask(r), greenMask(g), blueMask(b) {}
    };

    cRZRGBColorConverter() = default;
    cRZRGBColorConverter(const Format& destination, const Format& source) { SetFormats(destination, source); }   // 0x1002b4b20
    void SetFormats(const Format& destination, const Format& source);                                             // 0x1002b4838
    // Converts `width` x `height` pixels; the pitches are in bytes and may be negative. Only 2, 3 and 4 byte formats are
    // handled (anything else leaves the destination untouched).
    void Convert(void* destination, int destinationPitch, const void* source, int sourcePitch, std::uint32_t width,
                 std::uint32_t height) const;                                                                     // 0x1002b4b2a

private:
    std::uint32_t tables_[4][256] = {};
    std::uint32_t sourceBytes_ = 0;        // +0x1000
    std::uint32_t destinationBytes_ = 0;   // +0x1004
};
