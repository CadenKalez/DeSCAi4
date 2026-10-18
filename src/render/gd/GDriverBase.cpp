#include "render/gd/GDriverBase.h"

#include <bit>

namespace gd {

#pragma STDC FP_CONTRACT OFF

// The shipped driver converts a unit float to a byte as: v = bits(f + 49152.0f) - 0x47400000 (round-half-even, f*256 in the
// low mantissa bits); byte = (v & 0xff) - (v >> 8), so 1.0 -> 255 and 0.5 -> 128.
std::uint32_t QuantizeUnitFloat(float f) {
    const std::int32_t v = static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(f + 49152.0f)) - 0x47400000;
    return static_cast<std::uint32_t>(v - (static_cast<std::uint32_t>(v) >> 8)) & 0xff;
}

std::uint32_t PackClearColor(float r, float g, float b, float a) {
    return QuantizeUnitFloat(b) | QuantizeUnitFloat(g) << 8 | QuantizeUnitFloat(r) << 16 | QuantizeUnitFloat(a) << 24;
}

std::uint32_t AlphaRefByte(float ref) { return QuantizeUnitFloat(ref); }

}  // namespace gd
