#include "sc4/PowerLineGeometry.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace sc4 {

void PowerLineTunables::Read(const cISCPropertyHolder* e) {
    *this = PowerLineTunables();
    if (!e) return;
    using H = cSCPropertyHelp;
    H::GetPropertyValue(e, 0xe9a18d3f, maxHorizontalControlPointDistance);
    H::GetPropertyValue(e, 0x49a1e8c4, horizontalControlPointPercent);
    H::GetPropertyValue(e, 0x89a18fc4, maxVerticalControlPointDistance);
    H::GetPropertyValue(e, 0xe9a1e8d4, verticalControlPointPercent);
    H::GetPropertyValue(e, 0x49a195c0, curveDistanceBetweenPoints);
    H::GetPropertyValue(e, 0x89a1e8fb, minBezierPoints);
}

namespace {
// minss / maxss: the second operand wins when the comparison is false (including NaN)
inline float MinSS(float dest, float src) { return dest < src ? dest : src; }
inline float MaxSS(float dest, float src) { return dest > src ? dest : src; }
}  // namespace

void GetPowerLineControlPoints(const PowerLineTunables& t, const cS3DVector3& a, const cS3DVector3& b,
                               cS3DVector3& c0, cS3DVector3& c1, int& segments) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float len = std::sqrt((dx * dx + dy * dy) + dz * dz);
    const float inv = 1.0f / len;
    const float ux = dx * inv, uy = dy * inv, uz = dz * inv;
    const float horizontal = t.horizontalControlPointPercent / 100.0f * len;
    const float maxH = MinSS(t.maxHorizontalControlPointDistance, horizontal);
    const float vertical = t.verticalControlPointPercent / 100.0f * (len * len);
    const float drop = MinSS(t.maxVerticalControlPointDistance, vertical);
    const float half = 0.5f * len;
    float f = half - maxH;
    c0.x = ux * f + a.x;
    c0.z = f * uz + a.z;
    c0.y = (uy * f + a.y) - drop;
    f = half + maxH;
    c1.x = ux * f + a.x;
    c1.z = f * uz + a.z;
    c1.y = (uy * f + a.y) - drop;
    segments = int(MaxSS(float(t.minBezierPoints), len / t.curveDistanceBetweenPoints));
}

void GetPowerWireCells(float x, float z, std::uint32_t targetX, std::uint32_t targetZ, std::vector<WireCell>& out) {
    out.clear();
    const float dx = float(targetX) - x;
    const float dz = float(targetZ) - z;
    const float ax = std::fabs(dx), az = std::fabs(dz);
    const float longer = az > ax ? az : ax;
    long steps = static_cast<long>(static_cast<unsigned long>(longer));
    float stepX, stepZ;
    if (ax <= az) {
        stepX = dz == 0.0f ? 0.0f : ax / az;
        stepZ = 1.0f;
    } else {
        stepX = 1.0f;
        stepZ = az / ax;
    }
    if (int(steps) == -1) return;
    const bool xNegative = dx < 0.0f, zNegative = dz < 0.0f;
    for (;;) {
        out.push_back({int(x), int(z)});
        if (int(steps) == 0) break;
        x = x + (xNegative ? -stepX : stepX);
        z = z + (zNegative ? -stepZ : stepZ);
        --steps;
    }
}

}  // namespace sc4
