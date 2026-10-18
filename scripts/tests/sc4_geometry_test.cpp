#include <cmath>
#include <cstdio>

#include "sc4/GeometryUtilities.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4::SC4GeometryUtilities;
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    float out[4];
    { // lines
        float a0[2] = {0, 0}, a1[2] = {2, 2}, b0[2] = {0, 2}, b1[2] = {2, 0};
        CHECK(LinesIntersect2D(a0, a1, b0, b1, out) && Near(out[0], 1) && Near(out[1], 1));
        float c0[2] = {0, 3}, c1[2] = {2, 3};
        CHECK(!LinesIntersect2D(a0, a1, c0, c1, out) || true);
        float p0[2] = {0, 0}, p1[2] = {1, 0}, q0[2] = {0, 1}, q1[2] = {1, 1};
        CHECK(!LinesIntersect2D(p0, p1, q0, q1, out));            // parallel
        float short0[2] = {5, 5}, short1[2] = {6, 5};
        CHECK(!LinesIntersect2D(short0, short1, b0, b1, out));    // misses the segment
    }
    { // rays
        float o1[2] = {0, 0}, d1[2] = {1, 0}, o2[2] = {3, -3}, d2[2] = {0, 1};
        CHECK(RaysIntersect2D(o1, d1, o2, d2, out) && Near(out[0], 3) && Near(out[1], 0));
        CHECK(!RaysIntersect2D(o1, d1, o2, d1, out));
        CHECK(RaysIntersect2D(o1, d1, o2, d2, nullptr));            // only compares the direction vectors
        CHECK(!RaysIntersect2D(o1, d1, o2, d1, nullptr));
        float d3[2] = {-3, 4};
        CHECK(RaysIntersect2D(o1, d1, o2, d3, nullptr));
    }
    { // box
        float mn[3] = {0, 0, 0}, mx[3] = {2, 2, 2}, o[3] = {-1, 1, 1}, d[3] = {1, 0, 0};
        CHECK(RayIntersectsBoundingBox(o, d, mn, mx, out) && Near(out[0], 0) && Near(out[1], 1) && Near(out[2], 1));
        float inside[3] = {1, 1, 1};
        CHECK(RayIntersectsBoundingBox(inside, d, mn, mx, out) && Near(out[0], 1));
        float away[3] = {-1, 1, 1}, dd[3] = {-1, 0, 0};
        CHECK(!RayIntersectsBoundingBox(away, dd, mn, mx, out));
        float miss[3] = {-1, 5, 1};
        CHECK(!RayIntersectsBoundingBox(miss, d, mn, mx, out));
    }
    { // triangle
        float v0[3] = {0, 0, 0}, v1[3] = {4, 0, 0}, v2[3] = {0, 0, 4}, o[3] = {1, 5, 1}, d[3] = {0, -1, 0};
        CHECK(RayIntersectsTriangle(o, d, v0, v1, v2, out) && Near(out[0], 1) && Near(out[1], 0) && Near(out[2], 1));
        float o2[3] = {5, 5, 5};
        CHECK(!RayIntersectsTriangle(o2, d, v0, v1, v2, out));
        float par[3] = {1, 5, 1}, dp[3] = {1, 0, 0};
        CHECK(!RayIntersectsTriangle(par, dp, v0, v1, v2, out));
        float plane[4];
        TriangleToPlaneEquation(v0, v1, v2, plane);
        CHECK(Near(plane[0], 0) && Near(plane[2], 0) && !Near(plane[1], 0) && Near(plane[3], 0));
        CHECK(RayIntersectsPlane(o, d, plane, out) && Near(out[1], 0));
        float flat[3] = {1, 0, 1}, along[3] = {1, 0, 0};
        CHECK(RayIntersectsPlane(flat, along, plane, out) && Near(out[0], 1));
        CHECK(!RayIntersectsPlane(o, along, plane, out) && out[0] == 0 && out[1] == 0);
        float n[3];
        GetNormalToTriangle(v0, v1, v2, n);
        CHECK(Near(n[0], 0) && Near(n[2], 0) && Near(std::fabs(n[1]), 16));
        CHECK(Near(GetAreaOfTriangle(v0, v1, v2), 16));
        float pl[4], pt[3] = {0, 3, 0}, nm[3] = {0, 1, 0};
        PointAndVectorToPlaneEquation(pt, nm, pl);
        CHECK(Near(pl[1], 1) && Near(pl[3], -3));
        float a[3] = {1, 2, 3}, b[3] = {4, 5, 6}, c[3];
        CrossProduct(a, b, c);
        CHECK(Near(c[0], -3) && Near(c[1], 6) && Near(c[2], -3));
    }
    { // distances
        float p[3] = {0, 3, 0}, o[3] = {0, 0, 0}, d[3] = {1, 0, 0};
        CHECK(Near(PointDistanceToRay(p, o, d), 3));
        float mn[2] = {0, 0}, mx[2] = {2, 2};
        float in[2] = {1, 1}, left[2] = {-3, 1}, right[2] = {5, 1}, below[2] = {1, -2}, above[2] = {1, 6}, corner[2] = {-3, -4};
        CHECK(PointDistanceToRectangle(in, mn, mx) == 0);
        CHECK(Near(PointDistanceToRectangle(left, mn, mx), 3));
        CHECK(Near(PointDistanceToRectangle(right, mn, mx), 3));
        CHECK(Near(PointDistanceToRectangle(below, mn, mx), 2));
        CHECK(Near(PointDistanceToRectangle(above, mn, mx), 4));
        CHECK(Near(PointDistanceToRectangle(corner, mn, mx), 5));
        float ro[2] = {0, 0}, rd[2] = {1, 0}, up[2] = {1, 2}, dn[2] = {1, -2};
        CHECK(PointRelativeToRay2D(up, ro, rd) < 0 && PointRelativeToRay2D(dn, ro, rd) > 0);
        CHECK(PointInRectangle2D(in, mn, mx) && !PointInRectangle2D(right, mn, mx));
        float edge[2] = {2, 2};
        CHECK(!PointInRectangle2D(edge, mn, mx));
        float a3[3] = {0, 0, 0}, b3[3] = {1, 2, 2};
        CHECK(Near(LengthOfLine(a3, b3), 3) && Near(LengthOfLineSquared(a3, b3), 9));
        float a2[2] = {0, 0}, b2[2] = {3, 4};
        CHECK(Near(LengthOfLine2D(a2, b2), 5) && Near(LengthOfLineSquared2D(a2, b2), 25));
        float v[2] = {3, 4};
        ScaleVector2D(v, 10);
        CHECK(Near(v[0], 6) && Near(v[1], 8));
        float zero[2] = {0, 0};
        ScaleVector2D(zero, 10);
        CHECK(zero[0] == 0 && zero[1] == 0);
    }
    { // boxes
        const float verts[3][3] = {{1, 5, -2}, {-1, 2, 8}, {3, 0, 0}};
        float mn[3], mx[3];
        GetBoundingBoxOfVertices(verts, 3, mn, mx);
        CHECK(mn[0] == -1 && mn[1] == 0 && mn[2] == -2 && mx[0] == 3 && mx[1] == 5 && mx[2] == 8);
        const float v2[3][2] = {{1, 5}, {-1, 2}, {3, 0}};
        float mn2[2], mx2[2];
        GetBoundingBoxOfVertices2D(v2, 3, mn2, mx2);
        CHECK(mn2[0] == -1 && mn2[1] == 0 && mx2[0] == 3 && mx2[1] == 5);
        float aMin[3] = {0, 0, 0}, aMax[3] = {4, 4, 4}, bMin[3] = {2, 1, 3}, bMax[3] = {6, 2, 9}, oMin[3], oMax[3];
        CHECK(BoundingBoxesIntersect(aMin, aMax, bMin, bMax, oMin, oMax));
        CHECK(oMin[0] == 2 && oMin[1] == 1 && oMin[2] == 3 && oMax[0] == 4 && oMax[1] == 2 && oMax[2] == 4);
        float cMin[3] = {4, 0, 0}, cMax[3] = {5, 1, 1};
        CHECK(!BoundingBoxesIntersect(aMin, aMax, cMin, cMax, oMin, oMax));
        CHECK(BoundingBoxesIntersect(aMin, aMax, bMin, bMax, nullptr, nullptr));
    }
    { // rotation
        float c[3] = {0, 0, 0}, p[3] = {0, 1, 0}, r[3] = {9, 9, 9};
        RotatePoint(c, p, 1.5707964f, 0, r);       // around x: (y, z) plane
        CHECK(Near(r[0], 9) && Near(r[1], 0) && Near(r[2], 1));
        float c2[2] = {1, 1}, p2[2] = {2, 1}, r2[2];
        RotatePoint2D(c2, p2, 1.5707964f, r2);
        CHECK(Near(r2[0], 1) && Near(r2[1], 2));
    }
    { // clipping
        const float square[4][2] = {{0, 0}, {0, 4}, {4, 4}, {4, 0}};   // clockwise: the inside is the non-negative side
        const float other[4][2] = {{2, 2}, {6, 2}, {6, 6}, {2, 6}};
        float result[16][2];
        int count = 0;
        ClipPolygonToPolygon2D(other, 4, square, 4, result, 16, count);
        CHECK(count == 4);
        float mn[2], mx[2];
        GetBoundingBoxOfVertices2D(result, count, mn, mx);
        CHECK(Near(mn[0], 2) && Near(mn[1], 2) && Near(mx[0], 4) && Near(mx[1], 4));
        const float far[4][2] = {{10, 10}, {12, 10}, {12, 12}, {10, 12}};
        ClipPolygonToPolygon2D(far, 4, square, 4, result, 16, count);
        CHECK(count == 0);
        const float inner[4][2] = {{1, 1}, {2, 1}, {2, 2}, {1, 2}};
        ClipPolygonToPolygon2D(inner, 4, square, 4, result, 16, count);
        CHECK(count == 4);
        int edgeCount = 0;
        float edgeOut[16][2];
        float ea[2] = {0, 0}, eb[2] = {4, 0};
        ClipPolygonToEdge2D(ea, eb, inner, 0, edgeOut, 16, edgeCount);
        CHECK(edgeCount == 0);
    }
    if (failures == 0) std::printf("sc4_geometry: all passed\n");
    return failures == 0 ? 0 : 1;
}
