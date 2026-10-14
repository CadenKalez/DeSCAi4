#include "sc4/SC4GeometryUtilities.h"

#include <cmath>

namespace SC4GeometryUtilities {

bool RayIntersectsPlane(const float origin[3], const float direction[3], const float plane[4], float out[3]) {
    const float distance = plane[2] * origin[2] + origin[1] * plane[1] + plane[0] * origin[0] + plane[3];
    const float denominator = direction[2] * plane[2] + plane[0] * direction[0] + direction[1] * plane[1];
    if (0.01f <= std::fabs(denominator)) {
        if (out) {
            const float t = -distance / denominator;
            out[0] = origin[0] + direction[0] * t;
            out[1] = direction[1] * t + origin[1];
            out[2] = t * direction[2] + origin[2];
        }
        return true;
    }
    if (0.01f <= std::fabs(distance)) {
        if (out) out[0] = out[1] = out[2] = 0.0f;
        return false;
    }
    if (out) {
        out[0] = origin[0];
        out[1] = origin[1];
        out[2] = origin[2];
    }
    return true;
}

float LengthOfLineSquared(const float a[3], const float b[3]) {
    const float dx = b[0] - a[0];
    const float dz = b[2] - a[2];
    return dz * dz + (b[1] - a[1]) * (b[1] - a[1]) + dx * dx;
}

}  // namespace SC4GeometryUtilities
