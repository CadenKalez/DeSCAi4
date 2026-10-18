// SC4GeometryUtilities — the 2D/3D intersection and polygon helpers the views, the path finder and the automata use
// (24 functions, 0x1000d5e7e..0x1000d6f82). Everything works on raw float arrays (vec3 = x, y, z; vec2 = x, z-or-y;
// plane = a, b, c, d for a*x + b*y + c*z + d = 0). Quirks kept: GetAreaOfTriangle returns the length of the cross
// product (twice the area); RaysIntersect2D without an output only compares the two direction vectors; the 0.01
// epsilon is used wherever the original parallel-tests.
#pragma once

namespace sc4::SC4GeometryUtilities {

bool RaysIntersect2D(const float* origin1, const float* dir1, const float* origin2, const float* dir2, float* out);   // 0x1000d5e7e
bool RayIntersectsBoundingBox(const float* origin, const float* dir, const float* boxMin, const float* boxMax, float* out);   // 0x1000d5f46
bool LinesIntersect2D(const float* a0, const float* a1, const float* b0, const float* b1, float* out);   // 0x1000d60d0
bool RayIntersectsTriangle(const float* origin, const float* dir, const float* v0, const float* v1, const float* v2, float* out);   // 0x1000d61a2
void TriangleToPlaneEquation(const float* p0, const float* p1, const float* p2, float* plane);   // 0x1000d63ae
float PointDistanceToRay(const float* point, const float* origin, const float* dir);   // 0x1000d64c4
float PointDistanceToRectangle(const float* point, const float* rectMin, const float* rectMax);   // 0x1000d65a8
bool RayIntersectsPlane(const float* origin, const float* dir, const float* plane, float* out);   // 0x1000d6644
float PointRelativeToRay2D(const float* point, const float* origin, const float* dir);   // 0x1000d672e
bool PointInRectangle2D(const float* point, const float* rectMin, const float* rectMax);   // 0x1000d6756
void GetBoundingBoxOfVertices(const float (*vertices)[3], int count, float* boxMin, float* boxMax);   // 0x1000d6788
void GetBoundingBoxOfVertices2D(const float (*vertices)[2], int count, float* boxMin, float* boxMax);   // 0x1000d682c
bool BoundingBoxesIntersect(const float* min1, const float* max1, const float* min2, const float* max2, float* outMin, float* outMax);   // 0x1000d68a0
void GetNormalToTriangle(const float* p1, const float* p2, const float* p3, float* normal);   // 0x1000d6982
void CrossProduct(const float* a, const float* b, float* out);   // 0x1000d6a00
float GetAreaOfTriangle(const float* p1, const float* p2, const float* p3);   // 0x1000d6a58
void PointAndVectorToPlaneEquation(const float* point, const float* normal, float* plane);   // 0x1000d6ad6
float LengthOfLine(const float* a, const float* b);   // 0x1000d6b1c
float LengthOfLineSquared(const float* a, const float* b);   // 0x1000d6b5c
float LengthOfLine2D(const float* a, const float* b);   // 0x1000d6b98
float LengthOfLineSquared2D(const float* a, const float* b);   // 0x1000d6bbc
void ScaleVector2D(float* v, float length);   // 0x1000d6bd8
void RotatePoint(const float* center, const float* point, float angle, int axis, float* out);   // 0x1000d6c1a
void RotatePoint2D(const float* center, const float* point, float angle, float* out);   // 0x1000d6ccc
void ClipPolygonToEdge2D(const float* edgeA, const float* edgeB, const float (*polygon)[2], int count, float (*out)[2], int maxOut, int& outCount);   // 0x1000d6d52
void ClipPolygonToPolygon2D(const float (*subject)[2], int subjectCount, const float (*clip)[2], int clipCount, float (*out)[2], int maxOut, int& outCount);   // 0x1000d6f82

}  // namespace sc4::SC4GeometryUtilities
