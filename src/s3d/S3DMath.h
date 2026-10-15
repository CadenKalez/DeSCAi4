// Small 3D helpers of the S3D engine: nS3D vector/matrix utilities and cS3DTransform arithmetic, recovered
// from 0x1001c60b8, 0x1002e944b..0x1002e9b45, 0x10032b089, 0x1003f7680..0x1003f8150, 0x10017670c, 0x1001a717e
// and 0x1005f0c20. Compile without FMA contraction: results follow the original single-precision rounding.
// Matrices are row-major float[9] ("cS3DMatrix3"): row r, column c at m[3*r + c].
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "rz/cRZRandom.h"

namespace nS3D {

struct Matrix3 { float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; };

float Sqrt(float v);
int CeilPow2(int v);                       // smallest power of two >= v (1 for v <= 1)
void MakeBoxCorners(const cS3DBoundingBox& box, cS3DVector3 out[8]);   // x fastest, then y, then z
void TriangleDoubleAreaNormal(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c, cS3DVector3& out);

// Orthonormal bases with "Y up". AlignXToDir... store `dir` in the X column (or row for AlignDirToX...),
// Z axes analogously; AlignYToDir picks the least-aligned axis to build the frame. A (near) vertical dir
// falls back to the identity-like frame of the original.
void AlignZToDirWithYUp(const cS3DVector3& dir, Matrix3& out);
void AlignXToDirWithYUp(const cS3DVector3& dir, Matrix3& out);
void AlignYToDir(const cS3DVector3& dir, Matrix3& out);
void AlignDirToZWithYUp(const cS3DVector3& dir, Matrix3& out);
void AlignDirToXWithYUp(const cS3DVector3& dir, Matrix3& out);

// Uniformly distributed unit vectors by rejection sampling; the generator is advanced 2 / 3 draws per try.
cS3DVector2 RandomDirection2(cRZRandom& random);
cS3DVector3 RandomDirection3(cRZRandom& random);

// cS3DVector3
float GetLength(const cS3DVector3& v);
void Normalize(cS3DVector3& v);
void ScaleBy(cS3DVector3& v, float s);   // operator*=

}  // namespace nS3D

namespace nS3D {
// cS3DTransform::flags
constexpr std::uint8_t kTransformTranslation = 1, kTransformScale = 2, kTransformRotation = 4;

void TransformPoints(const cS3DTransform& t, const cS3DVector3* in, cS3DVector3* out, int count);
void AssignTransform(cS3DTransform& dst, const cS3DTransform& src);   // bumps dst's change counter
// this = this * other (other is applied first): translation, rotation and scale compose; a transform with no
// flags simply takes `other`.
void MultiplyTransform(cS3DTransform& self, const cS3DTransform& other);
}  // namespace nS3D
