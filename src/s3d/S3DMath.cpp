#include "s3d/S3DMath.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace nS3D {

float Sqrt(float v) { return std::sqrt(v); }

int CeilPow2(int v) {
    int p = 1, next = 1;
    do {
        p = next;
        next = p * 2;
    } while (p < v);
    return p;
}

void MakeBoxCorners(const cS3DBoundingBox& b, cS3DVector3 out[8]) {
    const float x[2] = {b.min[0], b.max[0]}, y[2] = {b.min[1], b.max[1]}, z[2] = {b.min[2], b.max[2]};
    for (int i = 0; i < 8; ++i) out[i] = {x[i & 1], y[(i >> 1) & 1], z[i >> 2]};
}

void TriangleDoubleAreaNormal(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c, cS3DVector3& out) {
    const float x = (a.z + c.z) * (c.y - a.y) + (b.z + c.z) * (b.y - c.y) + (a.z + b.z) * (a.y - b.y);
    const float y = (a.x + c.x) * (c.z - a.z) + (b.x + c.x) * (b.z - c.z) + (a.x + b.x) * (a.z - b.z);
    const float z = (a.y + c.y) * (c.x - a.x) + (b.y + c.y) * (b.x - c.x) + (a.y + b.y) * (a.x - b.x);
    out = {x, y, z};
}

void AlignZToDirWithYUp(const cS3DVector3& d, Matrix3& o) {
    float* m = o.m;
    m[2] = d.x; m[5] = d.y; m[8] = d.z;
    float x = d.x;
    float z = d.z;
    const float len = std::sqrt(x * x + z * z + 0.0f);
    float f6, f2, f1;
    if (len <= -0.0f) {
        m[0] = 0; m[3] = 0; m[6] = 1.0f;
        x = 0.0f; f6 = 1.0f; f1 = 0.0f;
        m[1] = f6; m[4] = x; m[7] = f1;
        return;
    }
    const float k = -1.0f / len;
    const float a = -(z * k);
    const float b = k * 0.0f;
    x = x * k;
    m[0] = a; m[3] = b; m[6] = x;
    f6 = x * d.y - b * d.z;
    f2 = a * d.z - x * d.x;
    f1 = z * k * d.y + b * d.x;
    m[1] = f6; m[4] = f2; m[7] = f1;
}

void AlignXToDirWithYUp(const cS3DVector3& d, Matrix3& o) {
    float* m = o.m;
    m[0] = d.x; m[3] = d.y; m[6] = d.z;
    const float x = d.x, z = d.z;
    const float len = std::sqrt(x * x + z * z + 0.0f);
    if (len <= 0.0f) {
        m[1] = 0.0f; m[2] = 1.0f;       // the original writes 0x3f80000000000000 over (+4, +8)
        m[4] = 0.0f; m[5] = 0.0f;       // and zero over (+0x10, +0x14)
        m[7] = 1.0f;
        m[8] = 0.0f;
        return;
    }
    const float k = 1.0f / len;
    const float a = -(z * k);
    const float b = k * 0.0f;
    const float xn = x * k;
    m[2] = a; m[5] = b; m[8] = xn;
    const float f7 = a * d.y - b * x;
    m[1] = b * d.z - xn * d.y;
    m[4] = z * k * d.z + xn * x;
    m[7] = f7;
}

void AlignYToDir(const cS3DVector3& d, Matrix3& o) {
    float* m = o.m;
    m[1] = d.x; m[4] = d.y; m[7] = d.z;
    const float x = d.x, y = d.y, z = d.z;
    if (std::fabs(x) <= std::fabs(z)) {
        const float k = 1.0f / std::sqrt(y * y + z * z + 0.0f);
        const float f8 = k * -0.0f;
        const float f7 = -(z * k);
        const float f5 = k * y;
        m[0] = z * k * z + y * f5;
        m[3] = z * f8 - x * f5;
        m[6] = f7 * x - y * f8;
        m[2] = f8;
        m[5] = f7;
        m[8] = f5;
        return;
    }
    const float k = 1.0f / std::sqrt(x * x + y * y + 0.0f);
    const float f8 = y * k;
    const float f6 = -x * k;
    const float f5 = k * 0.0f;
    const float f7 = x * f5 - z * f8;
    m[2] = z * f6 - y * f5;
    m[8] = y * f8 - x * f6;
    m[0] = f8;
    m[3] = f6;
    m[5] = f7;
    m[6] = f5;
}

void AlignDirToZWithYUp(const cS3DVector3& d, Matrix3& o) {
    float* m = o.m;
    m[6] = d.x; m[7] = d.y; m[8] = d.z;
    const float x = d.x, z = d.z;
    const float len = std::sqrt(x * x + z * z + 0.0f);
    float f6, f2, f1;
    if (len <= -0.0f) {
        m[0] = 0; m[1] = 0; m[2] = 1.0f;
        f2 = 0.0f; f6 = 1.0f; f1 = 0.0f;
    } else {
        const float k = -1.0f / len;
        const float a = -(z * k);
        const float b = k * 0.0f;
        const float xn = x * k;
        m[0] = a; m[1] = b; m[2] = xn;
        f6 = xn * d.y - b * d.z;
        f2 = a * d.z - xn * d.x;
        f1 = z * k * d.y + b * d.x;
    }
    m[3] = f6; m[4] = f2; m[5] = f1;
}

void AlignDirToXWithYUp(const cS3DVector3& d, Matrix3& o) {
    float* m = o.m;
    m[0] = d.x; m[1] = d.y; m[2] = d.z;
    const float x = d.x, z = d.z;
    const float len = std::sqrt(x * x + z * z + 0.0f);
    if (len <= 0.0f) {
        m[3] = 0.0f; m[4] = 0.0f; m[5] = 1.0f;
        m[6] = 1.0f; m[7] = 0.0f; m[8] = 0.0f;
        return;
    }
    const float k = 1.0f / len;
    const float a = -(z * k);
    const float b = k * 0.0f;
    const float xn = x * k;
    m[6] = a; m[7] = b; m[8] = xn;
    m[3] = b * d.z - xn * d.y;
    m[4] = z * k * d.z + xn * d.x;
    m[5] = a * d.y - b * d.x;
}

cS3DVector2 RandomDirection2(cRZRandom& random) {
    float x, y, d;
    do {
        x = float(random.RandomDoubleRangeUniform(-1.0, 1.0));
        y = float(random.RandomDoubleRangeUniform(-1.0, 1.0));
        d = y * y + x * x;
        if (d <= 1.0f) break;
    } while (d < 0.0001f);
    const float inv = 1.0f / std::sqrt(d);
    return {x * inv, inv * y};
}

cS3DVector3 RandomDirection3(cRZRandom& random) {
    float x, y, z, d;
    do {
        do {
            x = float(random.RandomDoubleRangeUniform(-1.0, 1.0));
            y = float(random.RandomDoubleRangeUniform(-1.0, 1.0));
            z = float(random.RandomDoubleRangeUniform(-1.0, 1.0));
            d = z * z + y * y + x * x;
        } while (1.0f < d);
    } while (d < 0.0001f);
    const float inv = 1.0f / std::sqrt(d);
    return {x * inv, y * inv, inv * z};
}

float GetLength(const cS3DVector3& v) { return Sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

void Normalize(cS3DVector3& v) {
    const float k = 1.0f / GetLength(v);
    v.x = k * v.x;
    v.y = k * v.y;
    v.z = k * v.z;
}

void ScaleBy(cS3DVector3& v, float s) {
    v.x = s * v.x;
    v.y = s * v.y;
    v.z = s * v.z;
}

void TransformPoints(const cS3DTransform& t, const cS3DVector3* in, cS3DVector3* out, int count) {
    const float(*r)[3] = t.rotation;
    const float* tr = t.translation;
    const float s = t.scale;
    for (; count > 0; --count, ++in, ++out) {
        const float x = in->x, y = in->y, z = in->z;
        switch (t.flags) {
            case 0: *out = *in; break;
            case 1: *out = {x + tr[0], y + tr[1], z + tr[2]}; break;
            case 2: *out = {x * s, y * s, s * z}; break;
            case 3: *out = {x * s + tr[0], y * s + tr[1], s * z + tr[2]}; break;
            case 4:
                *out = {r[0][2] * z + r[0][1] * y + r[0][0] * x, r[1][2] * z + r[1][1] * y + r[1][0] * x,
                        z * r[2][2] + y * r[2][1] + x * r[2][0]};
                break;
            case 5:
                *out = {r[0][2] * z + r[0][1] * y + r[0][0] * x + tr[0], r[1][2] * z + r[1][1] * y + r[1][0] * x + tr[1],
                        z * r[2][2] + y * r[2][1] + x * r[2][0] + tr[2]};
                break;
            default:
                *out = {(r[0][2] * z + r[0][1] * y + r[0][0] * x) * s + tr[0], (r[1][2] * z + r[1][1] * y + r[1][0] * x) * s + tr[1],
                        (z * r[2][2] + y * r[2][1] + x * r[2][0]) * s + tr[2]};
                break;
        }
    }
}

void AssignTransform(cS3DTransform& dst, const cS3DTransform& src) {
    const std::uint8_t counter = std::uint8_t(dst.changeCounter + 1);
    dst = src;
    dst.changeCounter = counter;
}

void MultiplyTransform(cS3DTransform& self, const cS3DTransform& other) {
    if (other.flags == 0) return;
    if (self.flags == 0) {
        AssignTransform(self, other);
        return;
    }
    const float(*t)[3] = self.rotation;
    const float(*o)[3] = other.rotation;
    const float s = self.scale;
    float tx = self.translation[0], ty = self.translation[1], tz = self.translation[2];
    const float ox = other.translation[0], oy = other.translation[1], oz = other.translation[2];
    // (the terms are added right-nested, c + (b + a), as the machine code does: checked by scripts/machine_oracle/lightingmanager_oracle.cpp)
    tx = (t[0][2] * oz + (t[0][1] * oy + t[0][0] * ox)) * s + tx;
    ty = (t[1][2] * oz + (t[1][1] * oy + t[1][0] * ox)) * s + ty;
    tz = (t[2][2] * oz + (t[2][1] * oy + t[2][0] * ox)) * s + tz;
    float n[3][3];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) n[row][col] = o[2][col] * t[row][2] + (o[1][col] * t[row][1] + o[0][col] * t[row][0]);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) self.rotation[row][col] = n[row][col];
    self.translation[0] = tx; self.translation[1] = ty; self.translation[2] = tz;
    self.scale = s * other.scale;
    self.flags = std::uint8_t(self.flags | other.flags);
    self.changeCounter = std::uint8_t(self.changeCounter + 1);
}


void SetTranslation(cS3DTransform& t, const cS3DVector3& p) {
    t.translation[0] = p.x;
    t.translation[1] = p.y;
    t.translation[2] = p.z;
    t.flags = std::uint8_t(t.flags | kTransformTranslation);
    t.changeCounter = std::uint8_t(t.changeCounter + 1);
}

namespace {
inline bool IsSet(float v) { return v != kOrientationUnset || v != v; }

void ResetRotation(cS3DTransform& t) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) t.rotation[r][c] = r == c ? 1.0f : 0.0f;
}
}  // namespace

void Rotate(cS3DTransform& t, const cS3DVector3& axis, float radians) {
    const float half = radians * -0.5f;
    const float lengthSquared = axis.z * axis.z + (axis.y * axis.y + axis.x * axis.x);
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    const float ax = axis.x * inverse, ay = axis.y * inverse, az = axis.z * inverse;
    const float s = std::sin(half), w = std::cos(half);
    const float qx = ax * s, qy = ay * s, qz = az * s;
    const float xx2 = qx * qx + qx * qx, yy2 = qy * qy + qy * qy, zz2 = qz * qz + qz * qz;
    const float xz2 = qx * qz + qx * qz, xy2 = qx * qy + qx * qy, yz2 = qy * qz + qy * qz;
    const float wx2 = w * qx + w * qx, wy2 = w * qy + w * qy, wz2 = w * qz + w * qz;
    const float r0 = (1.0f - yy2) - zz2, r1 = xy2 + wz2, r2 = xz2 - wy2;
    const float r3 = xy2 - wz2, r4 = (1.0f - xx2) - zz2, r5 = yz2 + wx2;
    const float r6 = xz2 + wy2, r7 = yz2 - wx2, r8 = (1.0f - xx2) - yy2;
    const float m0 = t.rotation[0][0], m1 = t.rotation[0][1], m2 = t.rotation[0][2];
    const float m3 = t.rotation[1][0], m4 = t.rotation[1][1], m5 = t.rotation[1][2];
    const float m6 = t.rotation[2][0], m7 = t.rotation[2][1], m8 = t.rotation[2][2];
    t.rotation[0][0] = (m0 * r0 + m1 * r3) + m2 * r6;
    t.rotation[0][1] = (m0 * r1 + m1 * r4) + m2 * r7;
    t.rotation[0][2] = (m0 * r2 + m1 * r5) + m2 * r8;
    t.rotation[1][0] = (m3 * r0 + m4 * r3) + m5 * r6;
    t.rotation[1][1] = (m4 * r4 + m3 * r1) + m5 * r7;
    t.rotation[1][2] = (m4 * r5 + m3 * r2) + m5 * r8;
    t.rotation[2][0] = (m7 * r3 + m6 * r0) + m8 * r6;
    t.rotation[2][1] = (m7 * r4 + m6 * r1) + m8 * r7;
    t.rotation[2][2] = (m6 * r2 + m7 * r5) + m8 * r8;
    t.flags = std::uint8_t(t.flags | kTransformRotation);
    t.changeCounter = std::uint8_t(t.changeCounter + 1);
}

bool SetOrientationEuler(cS3DTransform& t, const cS3DVector3& a) {
    const bool hasX = IsSet(a.x), hasY = IsSet(a.y), hasZ = IsSet(a.z);
    if (!hasX && !hasY && !hasZ) return false;
    float (*m)[3] = t.rotation;
    if (hasX && hasY && hasZ) {
        const float sx = std::sin(a.x), cx = std::cos(a.x);
        const float sy = std::sin(a.y), cy = std::cos(a.y);
        const float sz = std::sin(a.z), cz = std::cos(a.z);
        m[0][0] = cy * cz;
        m[0][1] = -(cy * sz);
        m[0][2] = sy;
        m[1][0] = cx * sz + sy * sx * cz;
        m[1][1] = cx * cz - sy * sx * sz;
        m[1][2] = -(sx * cy);
        m[2][0] = sx * sz - cx * sy * cz;
        m[2][1] = sx * cz + cx * sy * sz;
        m[2][2] = cx * cy;
        t.flags = std::uint8_t(t.flags | kTransformRotation);
        t.changeCounter = std::uint8_t(t.changeCounter + 1);
        return true;
    }
    if (t.flags & kTransformRotation) {
        ResetRotation(t);
        t.flags = std::uint8_t(t.flags & ~kTransformRotation);
        t.changeCounter = std::uint8_t(t.changeCounter + 1);
    }
    if (hasX) {                           // columns 1 and 2 turn about the x axis
        const float s = std::sin(a.x), c = std::cos(a.x);
        for (int row = 0; row < 3; ++row) {
            const float c1 = m[row][1], c2 = m[row][2];
            m[row][1] = s * c2 + c * c1;
            m[row][2] = c2 * c - c1 * s;
        }
    }
    if (hasY) {                           // columns 0 and 2 about the y axis
        const float s = std::sin(a.y), c = std::cos(a.y);
        for (int row = 0; row < 3; ++row) {
            const float c0 = m[row][0], c2 = m[row][2];
            m[row][0] = c * c0 - s * c2;
            m[row][2] = c2 * c + c0 * s;
        }
    }
    if (hasZ) {                           // columns 0 and 1 about the z axis
        const float s = std::sin(a.z), c = std::cos(a.z);
        for (int row = 0; row < 3; ++row) {
            const float c0 = m[row][0], c1 = m[row][1];
            m[row][0] = s * c1 + c * c0;
            m[row][1] = c1 * c - c0 * s;
        }
    }
    t.flags = std::uint8_t(t.flags | kTransformRotation);
    t.changeCounter = std::uint8_t(t.changeCounter + 1);
    return true;
}

void SetOrientationForwardUp(cS3DTransform& t, const cS3DVector3& forward, const cS3DVector3& up) {
    float fx = forward.x, fy = forward.y, fzRaw = forward.z;
    float k = 1.0f / std::sqrt(fzRaw * fzRaw + fy * fy + fx * fx);
    fx = fx * k;
    fy = fy * k;
    const float fz = k * fzRaw;
    float sx = up.y * fz - up.z * fy;
    float sy = up.z * fx - fz * up.x;
    float szRaw = up.x * fy - up.y * fx;
    const float k2 = 1.0f / std::sqrt(szRaw * szRaw + sy * sy + sx * sx);
    sx = sx * k2;
    sy = sy * k2;
    const float sz = k2 * szRaw;
    float (*m)[3] = t.rotation;
    m[0][0] = sx; m[1][0] = sy; m[2][0] = sz;
    m[0][1] = fy * sz - fz * sy;
    m[1][1] = fz * sx - fx * sz;
    m[2][1] = fx * sy - fy * sx;
    m[0][2] = fx; m[1][2] = fy; m[2][2] = fz;
    t.flags = std::uint8_t(t.flags | kTransformRotation);
    t.changeCounter = std::uint8_t(t.changeCounter + 1);
}

void ModelVectorToWorldVector(const cS3DTransform& t, cS3DVector3& v) {
    const float(*m)[3] = t.rotation;
    const float x = v.x, y = v.y, z = v.z;
    v.x = m[0][2] * z + m[0][1] * y + m[0][0] * x;
    v.y = m[1][2] * z + m[1][1] * y + m[1][0] * x;
    v.z = z * m[2][2] + y * m[2][1] + x * m[2][0];
}

void ModelPointToWorldPoint(const cS3DTransform& t, cS3DVector3& p) {
    ModelVectorToWorldVector(t, p);
    p.x = p.x * t.scale + t.translation[0];
    p.y = p.y * t.scale + t.translation[1];
    p.z = p.z * t.scale + t.translation[2];
}

}  // namespace nS3D

// ---- free operators ----

cS3DVector3 operator*(const cS3DVector3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
cS3DVector3 operator-(const cS3DVector3& a, const cS3DVector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
cS3DVector3 operator-(const cS3DVector3& v) { return {-v.x, -v.y, -v.z}; }
cS3DVector3 operator+(const cS3DVector3& a, const cS3DVector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

cS3DVector3 cross(const cS3DVector3& a, const cS3DVector3& b) {
    cS3DVector3 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

float dot(const cS3DVector3& a, const cS3DVector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

cS3DMatrix4 operator*(const cS3DMatrix4& a, const cS3DMatrix4& b) {
    const auto& A = a.m;
    const auto& B = b.m;
    cS3DMatrix4 r;
    auto& o = r.m;
    for (int c = 0; c < 4; ++c) o[0][c] = A[0][0] * B[0][c] + A[0][1] * B[1][c] + A[0][2] * B[2][c] + A[0][3] * B[3][c];
    for (int row = 1; row < 4; ++row) {
        // Columns 0 and 2 are summed last term first, columns 1 and 3 first term first.
        o[row][0] = B[3][0] * A[row][3] + B[2][0] * A[row][2] + B[1][0] * A[row][1] + B[0][0] * A[row][0];
        o[row][1] = B[0][1] * A[row][0] + B[1][1] * A[row][1] + B[2][1] * A[row][2] + B[3][1] * A[row][3];
        o[row][2] = B[3][2] * A[row][3] + B[2][2] * A[row][2] + B[1][2] * A[row][1] + B[0][2] * A[row][0];
        o[row][3] = A[row][0] * B[0][3] + A[row][1] * B[1][3] + A[row][2] * B[2][3] + A[row][3] * B[3][3];
    }
    return r;
}

void Bezier3D4ControlsArray(const cS3DVector3& p0, const cS3DVector3& p1, const cS3DVector3& p2, const cS3DVector3& p3,
                            cS3DVector3* out, int count) {
    if (count <= 0) return;
    float t = 1.0f / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float t2 = t * t;
        const float t3 = t * t2;
        const float w0 = ((1.0f - t * 3.0f) + t2 * 3.0f) - t3;
        const float w1 = t2 * -6.0f + t * 3.0f + t3 * 3.0f;
        const float w2 = t2 * 3.0f - t3 * 3.0f;
        out[i].x = p3.x * t3 + p2.x * w2 + p1.x * w1 + p0.x * w0;
        out[i].y = p3.y * t3 + p2.y * w2 + p1.y * w1 + p0.y * w0;
        out[i].z = p3.z * t3 + p2.z * w2 + p1.z * w1 + p0.z * w0;
        t = t + 1.0f / static_cast<float>(count);
    }
}

vector4 operator*(const matrix44& m, const vector4& v) {
    vector4 r;
    for (int c = 0; c < 4; ++c)
        r.v[c] = v.v[3] * m.m[12 + c] + v.v[2] * m.m[8 + c] + v.v[1] * m.m[4 + c] + m.m[c] * v.v[0];
    return r;
}

matrix44 RotateRadMatrix44(char axis, float radians) {
    matrix44 r;
    for (float& e : r.m) e = 0.0f;
    const float s = std::sin(radians), c = std::cos(radians);
    switch (axis) {
        case 'X': case 'x':
            r.m[0] = 1.0f; r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; break;
        case 'Y': case 'y':
            r.m[0] = c; r.m[2] = -s; r.m[5] = 1.0f; r.m[8] = s; r.m[10] = c; break;
        case 'Z': case 'z':
            r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; r.m[10] = 1.0f; break;
        default: break;
    }
    r.m[15] = 1.0f;
    return r;
}
