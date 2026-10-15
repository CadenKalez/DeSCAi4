#include "sc4/RegionalTerrainMap.h"

#include <cmath>
#include <cstring>

namespace sc4 {

bool cSC4RegionalTerrainMap::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cISC4TerrainMap) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

void cSC4RegionalTerrainMap::GetVertex(std::int32_t index, std::int32_t& x, std::int32_t& z) const {
    x = index % vertsPerRow_;
    z = index / vertsPerRow_;
}

bool cSC4RegionalTerrainMap::LocationIsInBounds(float x, float z) const {
    if (!(0.0f <= x) || !(0.0f <= z)) return false;
    if (static_cast<float>(cellsX_) <= x / cellWidth_) return false;
    return z / cellWidth_ < static_cast<float>(cellsZ_);
}

namespace {
template <class Fill>
void FillRect(const TerrainRect& rect, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t columnStride, Fill value) {
    // rows [top, bottom), columns [left, right] — see the header comment
    for (std::int32_t row = rect.top; row < rect.bottom; ++row, out += rowStride) {
        if (rect.left > rect.right) continue;
        float* p = out;
        for (std::int32_t column = rect.left - 1; column < rect.right; ++column, p += columnStride) *p = value();
    }
}
}  // namespace

void cSC4RegionalTerrainMap::GetAltitudes(const TerrainRect& rect, float* out, std::ptrdiff_t rowStride,
                                          std::ptrdiff_t columnStride) const {
    FillRect(rect, out, rowStride, columnStride, [this] { return altitude_; });
}

void cSC4RegionalTerrainMap::GetWaterDepths(const TerrainRect& rect, float* out, std::ptrdiff_t rowStride,
                                            std::ptrdiff_t columnStride) const {
    FillRect(rect, out, rowStride, columnStride, [] { return 0.0f; });
}

void cSC4RegionalTerrainMap::GetCellCornerAltitudes(std::uint32_t, std::uint32_t, float* corners) const {
    corners[0] = corners[1] = corners[2] = corners[3] = altitude_;
}

// Rounds (v / cellWidth) down through the 1.5 * 2^23 trick: adding the constant leaves the integer in the low
// mantissa bits, so the float's bit pattern minus 0x4b400000 is the integer. When v / cellWidth is exactly
// the next integer up (+0x4b3fffff) that one is returned.
std::int32_t cSC4RegionalTerrainMap::GetTerrainGridIndex(float x, float z) const {
    auto index = [this](float v) -> std::int32_t {
        const float t = v / cellWidth_;
        float f = t + -0.5f;
        f = f + 12582912.0f;
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        const std::int32_t floorIndex = static_cast<std::int32_t>(bits - 0x4b400000u);
        const std::int32_t upIndex = static_cast<std::int32_t>(bits - 0x4b3fffffu);
        const float upAsFloat = static_cast<float>(upIndex);
        std::int32_t result = upIndex;
        if (t != upAsFloat) result = floorIndex;
        if (std::isnan(t) || std::isnan(upAsFloat)) result = floorIndex;
        return result;
    };
    const std::int32_t zi = index(z);
    const std::int32_t xi = index(x);
    return xi + zi * vertsPerRow_;
}

}  // namespace sc4
