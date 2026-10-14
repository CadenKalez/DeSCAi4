// SC4GeometryUtilities — the few geometry helpers the audio layer uses (plane = a, b, c, d with
// a*x + b*y + c*z + d = 0).
#pragma once

namespace SC4GeometryUtilities {

// @ 0x1000d6644. Intersects the ray origin + t * direction with the plane. A ray parallel to the
// plane (|n . dir| < 0.01) intersects only when the origin lies on it (|distance| < 0.01, the
// result is the origin); otherwise the result is (0, 0, 0) and false is returned. `out` may be null.
bool RayIntersectsPlane(const float origin[3], const float direction[3], const float plane[4], float out[3]);

// @ 0x1000d6b5c: squared distance between two points.
float LengthOfLineSquared(const float a[3], const float b[3]);

}  // namespace SC4GeometryUtilities
