#include "gz/RGBColorConverter.h"

namespace {

// Number of low zero bits (0 for an empty mask).
inline unsigned LowShift(std::uint32_t mask) {
    unsigned n = 0;
    if (mask != 0) {
        while ((mask & 1) == 0) { mask >>= 1; ++n; }
    }
    return n;
}

// How many doublings bring the mask's top bit to bit 31 (0 for an empty mask or one already there).
inline unsigned HighShift(std::uint32_t mask) {
    unsigned n = 0;
    if (static_cast<std::int32_t>(mask) > 0) {
        std::int32_t m = static_cast<std::int32_t>(mask);
        do {
            m = static_cast<std::int32_t>(static_cast<std::uint32_t>(m) * 2);
            ++n;
        } while (m >= 0);
    }
    return n;
}

struct Channel {
    std::uint32_t sourceMask, destinationMask;
    unsigned sourceShift, destinationShift;
    std::uint32_t scale;
    std::uint32_t Convert(std::uint32_t value) const {
        return (((sourceMask & value) >> sourceShift) * scale >> destinationShift) & destinationMask;
    }
};

Channel MakeChannel(std::uint32_t sourceMask, std::uint32_t destinationMask) {
    Channel c;
    c.sourceMask = sourceMask;
    c.destinationMask = destinationMask;
    c.sourceShift = LowShift(sourceMask);
    c.destinationShift = HighShift(destinationMask);
    c.scale = sourceMask != 0 ? static_cast<std::uint32_t>(0xffffffffull / (sourceMask >> c.sourceShift)) : 0;
    return c;
}

}  // namespace

// 0x1002b4784
cRZRGBColorConverter::Format::Format(const cGZBufferColorType& colorType) {
    switch (colorType.type) {
        case kBufferColorXRGB1555: *this = Format(16, 0, 0x7c00, 0x3e0, 0x1f); break;
        case kBufferColorRGB565: *this = Format(16, 0, 0xf800, 0x7e0, 0x1f); break;
        case kBufferColorARGB1555: *this = Format(16, 0x8000, 0x7c00, 0x3e0, 0x1f); break;
        case kBufferColorARGB4444: *this = Format(16, 0xf000, 0xf00, 0xf0, 0xf); break;
        case kBufferColorXRGB8888: *this = Format(32, 0, 0xff0000, 0xff00, 0xff); break;
        case kBufferColorARGB8888: *this = Format(32, 0xff000000, 0xff0000, 0xff00, 0xff); break;
        default: *this = Format(); break;
    }
}

// 0x1002b4838
void cRZRGBColorConverter::SetFormats(const Format& destination, const Format& source) {
    const Channel alpha = MakeChannel(source.alphaMask, destination.alphaMask);
    const Channel red = MakeChannel(source.redMask, destination.redMask);
    const Channel green = MakeChannel(source.greenMask, destination.greenMask);
    const Channel blue = MakeChannel(source.blueMask, destination.blueMask);
    // A channel the source does not have is set to its maximum in the destination.
    std::uint32_t fill = 0;
    if (source.alphaMask == 0) fill += destination.alphaMask;
    if (source.redMask == 0) fill += destination.redMask;
    if (source.greenMask == 0) fill += destination.greenMask;
    if (source.blueMask == 0) fill += destination.blueMask;
    for (unsigned k = 0; k < 4; ++k) {
        for (std::uint32_t b = 0; b < 256; ++b) {
            const std::uint32_t value = b << (8 * k);
            tables_[k][b] = blue.Convert(value) + green.Convert(value) + red.Convert(value) + alpha.Convert(value) + (k == 0 ? fill : 0);
        }
    }
    sourceBytes_ = source.bitsPerPixel >> 3;
    destinationBytes_ = destination.bitsPerPixel >> 3;
}

// 0x1002b4b2a
void cRZRGBColorConverter::Convert(void* destination, int destinationPitch, const void* source, int sourcePitch,
                                   std::uint32_t width, std::uint32_t height) const {
    if (destinationBytes_ < 2 || destinationBytes_ > 4 || sourceBytes_ < 2 || sourceBytes_ > 4) return;
    auto* dstRow = static_cast<std::uint8_t*>(destination);
    auto* srcRow = static_cast<const std::uint8_t*>(source);
    for (std::uint32_t y = 0; y < height; ++y, dstRow += destinationPitch, srcRow += sourcePitch) {
        const std::uint8_t* s = srcRow;
        std::uint8_t* d = dstRow;
        for (std::uint32_t x = 0; x < width; ++x, s += sourceBytes_, d += destinationBytes_) {
            std::uint32_t pixel = 0;
            for (unsigned k = 0; k < sourceBytes_; ++k) pixel += tables_[k][s[k]];
            for (unsigned k = 0; k < destinationBytes_; ++k) d[k] = static_cast<std::uint8_t>(pixel >> (8 * k));
        }
    }
}
