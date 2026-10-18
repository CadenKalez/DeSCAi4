#include "sc4/GeometryUtilities.h"

#include <cmath>

namespace sc4::SC4GeometryUtilities {

namespace {
constexpr float kEpsilon = 0.01f;
}

bool RaysIntersect2D(const float* o1, const float* d1, const float* o2, const float* d2, float* out) {
    if (!out) return kEpsilon < std::fabs(d2[0] - d1[0]) && kEpsilon < std::fabs(d2[1] - d1[1]);
    const float det = d2[0] * d1[1] + -d2[1] * d1[0];
    if (kEpsilon > std::fabs(det)) return false;
    const float t = (d2[0] * (o2[1] - o1[1]) + (o2[0] - o1[0]) * -d2[1]) / det;
    out[0] = d1[0] * t + o1[0];
    out[1] = t * d1[1] + o1[1];
    return true;
}

bool RayIntersectsBoundingBox(const float* origin, const float* dir, const float* boxMin, const float* boxMax, float* out) {
    float candidate[3];
    int side[3];   // 0 beyond max, 1 below min, 2 inside the slab
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        if (boxMin[i] <= origin[i]) {
            if (boxMax[i] < origin[i]) {
                side[i] = 0;
                candidate[i] = boxMax[i];
                inside = false;
            } else {
                side[i] = 2;
            }
        } else {
            side[i] = 1;
            candidate[i] = boxMin[i];
            inside = false;
        }
    }
    if (inside) {
        if (out) {
            out[0] = origin[0];
            out[1] = origin[1];
            out[2] = origin[2];
        }
        return true;
    }
    float t[3];
    for (int i = 0; i < 3; ++i) {
        float value = -1.0f;
        if (side[i] != 2 && dir[i] != 0.0f) value = (candidate[i] - origin[i]) / dir[i];
        t[i] = value;
    }
    int best = 0;
    float bestT = t[0];
    for (int i = 1; i < 3; ++i) {
        if (t[i] > bestT) best = i;
        bestT = t[best];
    }
    if (bestT < 0.0f) return false;
    float hit[3];
    for (int i = 0; i < 3; ++i) {
        if (i == best) {
            hit[i] = candidate[i];
        } else {
            hit[i] = dir[i] * bestT + origin[i];
            if (hit[i] < boxMin[i] || boxMax[i] < hit[i]) return false;
        }
    }
    if (out) {
        out[0] = hit[0];
        out[1] = hit[1];
        out[2] = hit[2];
    }
    return true;
}

bool LinesIntersect2D(const float* a0, const float* a1, const float* b0, const float* b1, float* out) {
    const float bx = b0[0] - b1[0], by = b0[1] - b1[1];
    const float ax = a1[0] - a0[0], ay = a1[1] - a0[1];
    float det = by * ax - ay * bx;
    if (!(kEpsilon <= std::fabs(det))) return false;
    const float cx = b0[0] - a0[0], cy = b0[1] - a0[1];
    det = 1.0f / det;
    const float s = (ax * cy - ay * cx) * det;
    if (!(s <= 1.0f && 0.0f <= s)) return false;
    const float t = (by * cx - bx * cy) * det;
    if (!(0.0f <= t && t <= 1.0f)) return false;
    if (out) {
        out[0] = ax * t + a0[0];
        out[1] = (a1[1] - a0[1]) * t + a0[1];
    }
    return true;
}

bool RayIntersectsTriangle(const float* o, const float* d, const float* v0, const float* v1, const float* v2, float* out) {
    const float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
    const float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
    // p = d x e2
    const float px = e2z * d[1] - d[2] * e2y;
    const float py = e2x * d[2] - e2z * d[0];
    const float pz = e2y * d[0] - d[1] * e2x;
    float det = e1z * pz + e1y * py + e1x * px;
    if (!(kEpsilon <= std::fabs(det))) return false;
    det = 1.0f / det;
    const float tx = o[0] - v0[0], ty = o[1] - v0[1], tz = o[2] - v0[2];
    const float u = (px * tx + py * ty + pz * tz) * det;
    if (!(0.0f <= u && u <= 1.0f)) return false;
    // q = t x e1
    const float qy = e1x * tz - e1z * tx;
    const float qz = tx * e1y - ty * e1x;
    const float qx = ty * e1z - tz * e1y;
    const float v = (d[0] * qx + d[1] * qy + d[2] * qz) * det;
    if (!(0.0f <= v && u + v <= 1.0f)) return false;
    if (out) {
        const float t = det * (e2z * qz + e2y * qy + e2x * qx);
        out[0] = o[0] + d[0] * t;
        out[1] = d[1] * t + o[1];
        out[2] = t * d[2] + o[2];
    }
    return true;
}

void TriangleToPlaneEquation(const float* p0, const float* p1, const float* p2, float* plane) {
    plane[0] = (p0[2] - p1[2]) * p2[1] + (p2[2] - p0[2]) * p1[1] + (p1[2] - p2[2]) * p0[1];
    plane[1] = (p0[0] - p1[0]) * p2[2] + (p2[0] - p0[0]) * p1[2] + (p1[0] - p2[0]) * p0[2];
    plane[2] = (p0[1] - p1[1]) * p2[0] + (p2[1] - p0[1]) * p1[0] + (p1[1] - p2[1]) * p0[0];
    plane[3] = (-((p1[1] * p2[2] - p2[1] * p1[2]) * p0[0]) - (p2[1] * p0[2] - p2[2] * p0[1]) * p1[0]) -
               (p1[2] * p0[1] - p1[1] * p0[2]) * p2[0];
}

float PointDistanceToRay(const float* p, const float* o, const float* d) {
    const float t = ((d[2] * p[2] + d[1] * p[1] + d[0] * p[0]) - (d[2] * o[2] + d[1] * o[1] + d[0] * o[0])) /
                    (d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
    const float y = p[1] - (d[1] * t + o[1]);
    const float x = p[0] - (o[0] + t * d[0]);
    const float z = p[2] - (o[2] + t * d[2]);
    return std::sqrt(z * z + y * y + x * x);
}

float PointDistanceToRectangle(const float* p, const float* rectMin, const float* rectMax) {
    float refX = rectMin[0], refY;
    if (rectMin[0] <= p[0]) {
        refX = rectMax[0];
        refY = rectMin[1];
        if (p[0] <= refX) {
            if (p[1] < refY) return refY - p[1];
            if (p[1] <= rectMax[1]) return 0.0f;
            return p[1] - rectMax[1];
        }
        if (refY <= p[1]) {
            refY = rectMax[1];
            if (p[1] <= refY) return p[0] - refX;
        }
    } else {
        refY = rectMin[1];
        if (refY <= p[1]) {
            refY = rectMax[1];
            if (p[1] <= refY) return refX - p[0];
        }
    }
    return std::sqrt((p[1] - refY) * (p[1] - refY) + (p[0] - refX) * (p[0] - refX));
}

bool RayIntersectsPlane(const float* o, const float* d, const float* plane, float* out) {
    const float distance = plane[2] * o[2] + o[1] * plane[1] + plane[0] * o[0] + plane[3];
    const float denom = d[2] * plane[2] + plane[0] * d[0] + d[1] * plane[1];
    if (kEpsilon <= std::fabs(denom)) {
        if (out) {
            const float t = -distance / denom;
            out[0] = o[0] + d[0] * t;
            out[1] = d[1] * t + o[1];
            out[2] = t * d[2] + o[2];
        }
        return true;
    }
    if (kEpsilon <= std::fabs(distance)) {
        if (out) out[0] = out[1] = out[2] = 0.0f;
        return false;
    }
    if (out) {
        out[0] = o[0];
        out[1] = o[1];
        out[2] = o[2];
    }
    return true;
}

float PointRelativeToRay2D(const float* p, const float* o, const float* d) {
    const float a = d[1] * (p[0] - o[0]);
    const float b = d[0] * (p[1] - o[1]);
    return a - b;
}

bool PointInRectangle2D(const float* p, const float* rectMin, const float* rectMax) {
    if (p[0] < rectMin[0] || rectMax[0] <= p[0] || p[1] < rectMin[1]) return false;
    return p[1] < rectMax[1];
}

void GetBoundingBoxOfVertices(const float (*v)[3], int count, float* boxMin, float* boxMax) {
    for (int i = 0; i < 3; ++i) boxMin[i] = boxMax[i] = v[0][i];
    for (int n = 1; n < count; ++n)
        for (int i = 0; i < 3; ++i) {
            if (v[n][i] < boxMin[i]) boxMin[i] = v[n][i];
            if (boxMax[i] < v[n][i]) boxMax[i] = v[n][i];
        }
}

void GetBoundingBoxOfVertices2D(const float (*v)[2], int count, float* boxMin, float* boxMax) {
    for (int i = 0; i < 2; ++i) boxMin[i] = boxMax[i] = v[0][i];
    for (int n = 1; n < count; ++n)
        for (int i = 0; i < 2; ++i) {
            if (v[n][i] < boxMin[i]) boxMin[i] = v[n][i];
            if (boxMax[i] < v[n][i]) boxMax[i] = v[n][i];
        }
}

bool BoundingBoxesIntersect(const float* min1, const float* max1, const float* min2, const float* max2, float* outMin, float* outMax) {
    for (int i = 0; i < 3; ++i)
        if (max2[i] <= min1[i] || max1[i] <= min2[i]) return false;
    if (outMin) {
        for (int i = 0; i < 3; ++i) {
            outMin[i] = (min2[i] < min1[i]) ? min1[i] : min2[i];
            outMax[i] = (max1[i] < max2[i]) ? max1[i] : max2[i];
        }
    }
    return true;
}

void GetNormalToTriangle(const float* p1, const float* p2, const float* p3, float* normal) {
    const float ax = p3[0] - p2[0], ay = p3[1] - p2[1], az = p3[2] - p2[2];
    const float bx = p1[0] - p2[0], by = p1[1] - p2[1], bz = p1[2] - p2[2];
    normal[0] = ay * bz - az * by;
    normal[1] = az * bx - bz * ax;
    normal[2] = by * ax - bx * ay;
}

void CrossProduct(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float GetAreaOfTriangle(const float* p1, const float* p2, const float* p3) {
    const float ax = p1[0] - p2[0], ay = p1[1] - p2[1], az = p1[2] - p2[2];
    const float bx = p3[0] - p2[0], by = p3[1] - p2[1], bz = p3[2] - p2[2];
    const float cy = az * bx - bz * ax;
    const float cz = ax * by - bx * ay;
    const float cx = ay * bz - by * az;
    return std::sqrt(cx * cx + cy * cy + cz * cz);
}

void PointAndVectorToPlaneEquation(const float* point, const float* normal, float* plane) {
    plane[0] = normal[0];
    plane[1] = normal[1];
    plane[2] = normal[2];
    plane[3] = -(normal[2] * point[2] + normal[1] * point[1] + normal[0] * point[0]);
}

float LengthOfLine(const float* a, const float* b) {
    const float x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
    return std::sqrt(z * z + y * y + x * x);
}

float LengthOfLineSquared(const float* a, const float* b) {
    const float x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
    return z * z + y * y + x * x;
}

float LengthOfLine2D(const float* a, const float* b) {
    const float x = b[0] - a[0], y = b[1] - a[1];
    return std::sqrt(y * y + x * x);
}

float LengthOfLineSquared2D(const float* a, const float* b) {
    const float x = b[0] - a[0], y = b[1] - a[1];
    return y * y + x * x;
}

void ScaleVector2D(float* v, float length) {
    const float current = std::sqrt(v[1] * v[1] + v[0] * v[0]);
    if (kEpsilon < current) {
        const float scale = length / current;
        v[0] *= scale;
        v[1] *= scale;
    }
}

void RotatePoint(const float* center, const float* point, float angle, int axis, float* out) {
    int a, b;
    if (axis == 0) { a = 1; b = 2; }
    else if (axis == 1) { a = 0; b = 2; }
    else { a = 0; b = 1; }
    const float ca = center[a], cb = center[b];
    const float dx = point[a] - ca, dy = point[b] - cb;
    const float s = std::sin(angle), c = std::cos(angle);
    out[a] = (dx * c - dy * s) + ca;
    out[b] = dy * c + dx * s + cb;
}

void RotatePoint2D(const float* center, const float* point, float angle, float* out) {
    const float cx = center[0], cy = center[1];
    const float dx = point[0] - cx, dy = point[1] - cy;
    const float s = std::sin(angle), c = std::cos(angle);
    out[0] = (dx * c - dy * s) + cx;
    out[1] = dy * c + dx * s + cy;
}

// Sutherland-Hodgman against one edge; the "inside" side is the one where the cross product is not negative.
void ClipPolygonToEdge2D(const float* edgeA, const float* edgeB, const float (*polygon)[2], int count, float (*out)[2], int maxOut, int& outCount) {
    outCount = 0;
    if (count < 1) return;
    float edge[2] = {edgeB[0] - edgeA[0], edgeB[1] - edgeA[1]};
    int prev = count - 1;
    bool prevInside = 0.0f <= (polygon[prev][0] - edgeA[0]) * edge[1] - (polygon[prev][1] - edgeA[1]) * edge[0];
    for (int i = 0; i < count; ++i) {
        const float cx = polygon[i][0], cy = polygon[i][1];
        const bool outside = (cx - edgeA[0]) * (edgeB[1] - edgeA[1]) - (cy - edgeA[1]) * (edgeB[0] - edgeA[0]) < 0.0f;
        auto emitIntersection = [&] {
            float dir[2] = {cx - polygon[prev][0], cy - polygon[prev][1]};
            float hit[2];
            RaysIntersect2D(polygon[prev], dir, edgeA, edge, hit);
            if (outCount < maxOut) {
                out[outCount][0] = hit[0];
                out[outCount][1] = hit[1];
                ++outCount;
            }
        };
        auto emitCurrent = [&] {
            if (outCount < maxOut) {
                out[outCount][0] = polygon[i][0];
                out[outCount][1] = polygon[i][1];
                ++outCount;
            }
        };
        if (outside) {
            if (prevInside) emitIntersection();
        } else if (prevInside) {
            emitCurrent();
        } else {
            emitIntersection();
            emitCurrent();
        }
        prev = i;
        prevInside = !outside;
    }
}

void ClipPolygonToPolygon2D(const float (*subject)[2], int subjectCount, const float (*clip)[2], int clipCount, float (*out)[2], int maxOut, int& outCount) {
    outCount = subjectCount;
    for (int i = 0; i < subjectCount; ++i) {
        out[i][0] = subject[i][0];
        out[i][1] = subject[i][1];
    }
    int prev = clipCount - 1;
    for (int i = 0; i < clipCount; ++i) {
        float scratch[40][2];
        int produced = 0;
        ClipPolygonToEdge2D(clip[prev], clip[i], out, outCount, scratch, 40, produced);
        outCount = produced < maxOut ? produced : maxOut;
        for (int n = 0; n < outCount; ++n) {
            out[n][0] = scratch[n][0];
            out[n][1] = scratch[n][1];
        }
        prev = i;
    }
}

}  // namespace sc4::SC4GeometryUtilities
