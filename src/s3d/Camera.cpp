#include "s3d/Camera.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace s3d {

namespace {
inline void ResetTransform(cS3DTransform& t) {
    const float identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) t.rotation[r][c] = identity[r][c];
    t.translation[0] = t.translation[1] = t.translation[2] = 0.0f;
    t.scale = 1.0f;
}
}  // namespace

cS3DCamera::cS3DCamera() {
    // matrices start as identity (as written by the constructor); MakeIdentity then finds nothing to reset
    for (int i = 0; i < 4; ++i) proj_[i * 5] = inverse_[i * 5] = 1.0f;
    MakeIdentity();
}

std::uint32_t cS3DCamera::Release() {
    if (1 < refCount_) return --refCount_;
    return 0;
}

void cS3DCamera::MakeIdentity() {
    if (local_.flags != 0) {
        ResetTransform(local_);
        local_.flags = 0;
        local_.changeCounter = std::uint8_t(local_.changeCounter + 1);
    }
    if (view_.flags != 0) {
        ResetTransform(view_);
        view_.flags = 0;
        view_.changeCounter = std::uint8_t(view_.changeCounter + 1);
    }
    viewStamp_ = local_.changeCounter;
}

void cS3DCamera::SetViewport(float width, float height) {
    viewportW_ = width;
    viewportH_ = height;
    subX_ = subY_ = 0.0f;
    subW_ = width;
    subH_ = height;
    subviewEnabled_ = false;
    projectionState_ += 2;
}

void cS3DCamera::ClearSubview() {
    subX_ = subY_ = 0.0f;
    subW_ = viewportW_;
    subH_ = viewportH_;
    subviewEnabled_ = false;
    ++projectionState_;
}

void cS3DCamera::SetDepthSize(float depth) {
    ++projectionState_;
    depthSize_ = depth;
}

void cS3DCamera::SetSubview(float x, float y, float width, float height) {
    subX_ = x;
    subY_ = y;
    subW_ = width;
    subH_ = height;
    subviewEnabled_ = true;
    ++projectionState_;
}

void cS3DCamera::SetViewportOffset(const cS3DVector2& offset) {
    ++projectionState_;
    viewportOffsetX_ = offset.x;
    viewportOffsetY_ = offset.y;
}

void cS3DCamera::SetDepthOffset(float offset) {
    ++projectionState_;
    depthOffset_ = offset;
}

void cS3DCamera::GetPosition(cS3DVector3& out) const {
    out = {local_.translation[0], local_.translation[1], local_.translation[2]};
}

void cS3DCamera::SetPosition(const cS3DVector3& p) {
    local_.translation[0] = p.x;
    local_.translation[1] = p.y;
    local_.translation[2] = p.z;
    local_.flags = std::uint8_t(local_.flags | nS3D::kTransformTranslation);
    local_.changeCounter = std::uint8_t(local_.changeCounter + 1);
}

void cS3DCamera::LookAt(const cS3DVector3& target, const cS3DVector3& up) {
    // "back" vector: from the target to the eye
    float bx = local_.translation[0] - target.x;
    float by = local_.translation[1] - target.y;
    float bzRaw = local_.translation[2] - target.z;
    float k = 1.0f / std::sqrt(bzRaw * bzRaw + by * by + bx * bx);
    bx = bx * k;
    by = by * k;
    const float bz = k * bzRaw;
    // side = up x back
    float sx = up.y * bz - up.z * by;
    float sy = up.z * bx - bz * up.x;
    float szRaw = up.x * by - up.y * bx;
    const float k2 = 1.0f / std::sqrt(szRaw * szRaw + sy * sy + sx * sx);
    sx = sx * k2;
    sy = sy * k2;
    const float sz = k2 * szRaw;
    float (*r)[3] = local_.rotation;
    r[0][0] = sx; r[1][0] = sy; r[2][0] = sz;
    r[0][1] = by * sz - bz * sy;
    r[1][1] = bz * sx - bx * sz;
    r[2][1] = bx * sy - by * sx;
    r[0][2] = bx; r[1][2] = by; r[2][2] = bz;
    const std::uint8_t counter = local_.changeCounter;
    local_.flags = std::uint8_t(local_.flags | nS3D::kTransformRotation);
    local_.changeCounter = std::uint8_t(counter + 2u);
    lookAtTarget_[0] = target.x; lookAtTarget_[1] = target.y; lookAtTarget_[2] = target.z;
    up_[0] = up.x; up_[1] = up.y; up_[2] = up.z;
    lookAtStamp_ = std::uint8_t(counter + 2u);
}

void cS3DCamera::GetLookAt(cS3DVector3& out) {
    if (lookAtStamp_ != local_.changeCounter) lookAtStamp_ = local_.changeCounter;
    out = {lookAtTarget_[0], lookAtTarget_[1], lookAtTarget_[2]};
}

cS3DTransform& cS3DCamera::ViewXform() {
    if (viewStamp_ != local_.changeCounter) UpdateXform();
    return view_;
}

void cS3DCamera::UpdateXform() {
    const std::uint8_t counter = view_.changeCounter;
    view_.changeCounter = std::uint8_t(counter + 1);
    const float(*l)[3] = local_.rotation;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) view_.rotation[r][c] = l[r][c];
    view_.scale = local_.scale;
    view_.flags = local_.flags;
    float tx = -local_.translation[0], ty = -local_.translation[1], tz = -local_.translation[2];
    view_.translation[0] = tx; view_.translation[1] = ty; view_.translation[2] = tz;
    if (local_.flags & nS3D::kTransformRotation) {
        // inverse rotation: transpose; translation becomes -(R^T * position)
        float(*v)[3] = view_.rotation;
        v[0][1] = l[1][0]; v[1][0] = l[0][1];
        v[0][2] = l[2][0]; v[2][0] = l[0][2];
        v[1][2] = l[2][1]; v[2][1] = l[1][2];
        const float nx = l[2][0] * tz + l[1][0] * ty + l[0][0] * tx;
        const float ny = l[2][1] * tz + l[1][1] * ty + l[0][1] * tx;
        const float nz = l[2][2] * tz + l[1][2] * ty + l[0][2] * tx;
        view_.translation[0] = nx; view_.translation[1] = ny; view_.translation[2] = nz;
        tx = nx; ty = ny; tz = nz;
    }
    if (local_.flags & nS3D::kTransformScale) {
        const float inv = 1.0f / local_.scale;
        view_.scale = inv;
        view_.translation[0] = tx * inv;
        view_.translation[1] = ty * inv;
        view_.translation[2] = tz * inv;
    }
    view_.changeCounter = std::uint8_t(counter + 2);
    viewStamp_ = local_.changeCounter;
}

bool cS3DCamera::SetOrtho(float left, float right, float top, float bottom, float nearPlane, float farPlane) {
    projectionType_ = kProjectionOrtho;
    left_ = left;
    right_ = right;
    top_ = top;
    bottom_ = bottom;
    near_ = nearPlane;
    far_ = farPlane;
    ++projectionState_;
    return true;
}

void cS3DCamera::GetViewVolume(float& left, float& right, float& top, float& bottom, float& nearPlane, float& farPlane) const {
    const float dx = ((right_ - left_) * viewportOffsetX_) / viewportW_;
    const float dy = ((top_ - bottom_) * viewportOffsetY_) / viewportH_;
    left = left_ + dx;
    right = dx + right_;
    top = top_ - dy;
    bottom = bottom_ - dy;
    nearPlane = near_;
    farPlane = far_;
}

const float* cS3DCamera::ProjectionMatrix() {
    if (projStamp_ != projectionState_) UpdateProjection();
    return proj_;
}

const float* cS3DCamera::InverseProjectionMatrix() {
    if (projStamp_ != projectionState_) UpdateProjection();
    return inverse_;
}

void cS3DCamera::UpdateProjection() {
    for (int i = 0; i < 16; ++i) proj_[i] = inverse_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    const float l = left_, r = right_, b = bottom_, t = top_, n = near_, f = far_;
    const float h = subH_, w = subW_;
    // lanes of the original SIMD setup: A0 = -2*offY/h, A1 = -2*offX/w, A2 = 1/(b-t), A3 = 1/(r-l)
    float a0 = (viewportOffsetY_ * -2.0f) / h;
    float a1 = (viewportOffsetX_ * -2.0f) / w;
    float a2 = 1.0f / (b - t);
    float a3 = 1.0f / (r - l);
    const float depthTerm = (-depthOffset_) / depthSize_;      // -offset / depth size
    if (subviewEnabled_) {
        const float vh = viewportH_ / h, vw = viewportW_ / w;
        const float c2 = a2 * vh;
        const float c3 = a3 * vw;
        const float e0 = ((vh + -1.0f) - (subY_ + subY_) / h) + a0;
        const float sx = (subX_ * -2.0f) / w;
        const float e1 = a1 + (sx + (vw + -1.0f));
        a0 = e0; a1 = e1; a2 = c2; a3 = c3;
    }
    const float invDepth = 1.0f / (f - n);
    if (projectionType_ == kProjectionPerspective) {
        const float n2 = n + n;
        proj_[0] = n2 * a3;
        proj_[5] = n2 * a2;
        proj_[11] = -((f * n2) * invDepth);
        proj_[2] = (r + l) * a3 - a1;
        proj_[6] = (b + t) * a2 - a0;
        proj_[10] = -((f + n) * invDepth) - depthTerm;
        proj_[14] = -1.0f;
        proj_[15] = 0.0f;
        const float i0 = 1.0f / proj_[0];
        const float i5 = 1.0f / proj_[5];
        const float i14 = 1.0f / proj_[11];
        inverse_[0] = i0;
        inverse_[5] = i5;
        inverse_[14] = i14;
        inverse_[3] = i0 * proj_[2];
        inverse_[11] = -1.0f;
        inverse_[7] = i5 * proj_[6];
        inverse_[15] = 0.0f;       // written as i14 * proj_[10] first, then overwritten with zero
    } else {
        proj_[0] = a3 + a3;
        proj_[5] = 2.0f * a2;
        proj_[10] = -2.0f * invDepth;
        proj_[3] = a1 - (r + l) * a3;
        proj_[7] = a0 - (b + t) * a2;
        proj_[11] = depthTerm - (n + f) * invDepth;
        const float i0 = 1.0f / proj_[0];
        const float i5 = 1.0f / proj_[5];
        const float i10 = 1.0f / proj_[10];
        inverse_[0] = i0;
        inverse_[5] = i5;
        inverse_[10] = i10;
        inverse_[3] = -(i0 * proj_[3]);
        inverse_[7] = -(i5 * proj_[7]);
        inverse_[11] = -(i10 * proj_[11]);
    }
    projStamp_ = projectionState_;
}

bool cS3DCamera::Project(const cS3DVector3& world, cS3DVector3& screen) {
    const float x = world.x, y = world.y, z = world.z;
    if (viewStamp_ != local_.changeCounter) UpdateXform();
    const float(*v)[3] = view_.rotation;
    const float s = view_.scale;
    const float ex = view_.translation[0] + s * (v[0][2] * z + v[0][1] * y + v[0][0] * x);
    const float ey = (v[1][2] * z + v[1][1] * y + v[1][0] * x) * s + view_.translation[1];
    const float ez = view_.translation[2] + s * (v[2][2] * z + v[2][1] * y + v[2][0] * x);
    const float* m = ProjectionMatrix();
    float w = m[14] * ez + m[13] * ey + m[12] * ex + m[15];
    if (!(0.0001f <= std::fabs(w))) return false;
    const float invW = 1.0f / w;
    screen.x = ((ez * m[2] + ey * m[1] + m[0] * ex + m[3]) * invW + 1.0f) * subW_ * 0.5f + subX_;
    screen.y = (1.0f - (m[6] * ez + m[5] * ey + m[4] * ex + m[7]) * invW) * subH_ * 0.5f + subY_;
    screen.z = invW * (m[10] * ez + m[9] * ey + m[8] * ex + m[11]);
    return true;
}

bool cS3DCamera::UnProject(const cS3DVector3& screen, cS3DVector3& world) {
    const float z = screen.z;
    float xn = (screen.x - subX_) / subW_;
    xn = xn + xn + -1.0f;
    float yn = ((subY_ + subH_) - screen.y) / subH_;
    yn = yn + yn + -1.0f;
    const float* m = InverseProjectionMatrix();
    float w = z * m[14] + m[12] * xn + yn * m[13] + m[15];
    if (!(0.0001f <= std::fabs(w))) return false;
    const float invW = 1.0f / w;
    float ex = (z * m[2] + yn * m[1] + xn * m[0] + m[3]) * invW;
    float ey = (m[6] * z + m[5] * yn + m[4] * xn + m[7]) * invW;
    float ez = invW * (m[10] * z + m[9] * yn + m[8] * xn + m[11]);
    if (viewStamp_ != local_.changeCounter) UpdateXform();
    const float(*v)[3] = view_.rotation;
    const float k = 1.0f / view_.scale;
    const float px = (ex - view_.translation[0]) * k;
    const float py = (ey - view_.translation[1]) * k;
    const float pz = k * (ez - view_.translation[2]);
    world.x = v[2][0] * pz + v[1][0] * py + v[0][0] * px;
    world.y = v[2][1] * pz + v[1][1] * py + v[0][1] * px;
    world.z = pz * v[2][2] + py * v[1][2] + px * v[0][2];
    return true;
}

bool cS3DCamera::GetEyeRay(const cS3DVector2& screen, cS3DVector3& origin, cS3DVector3& direction) {
    const float(*r)[3] = local_.rotation;
    if (projectionType_ == kProjectionPerspective) {
        const float x = screen.x / viewportW_ + screen.x / viewportW_ + -1.0f;
        const float fy = (viewportH_ - screen.y) / viewportH_;
        const float y = fy + fy + -1.0f;
        const float n = near_;
        origin = {local_.translation[0], local_.translation[1], local_.translation[2]};
        direction.x = -((r[0][1] * y + r[0][0] * x) - r[0][2] * n);
        direction.y = -((r[1][1] * y + r[1][0] * x) - r[1][2] * n);
        direction.z = -((y * r[2][1] + x * r[2][0]) - n * r[2][2]);
        return true;
    }
    float unprojected[3] = {0, 0, 0};
    origin = {local_.translation[0], local_.translation[1], local_.translation[2]};
    if (lookAtStamp_ != local_.changeCounter) lookAtStamp_ = local_.changeCounter;
    direction.x = lookAtTarget_[0] - origin.x;
    direction.y = lookAtTarget_[1] - origin.y;
    direction.z = lookAtTarget_[2] - origin.z;
    cS3DVector3 screenPoint{screen.x, screen.y, 1.0f}, world;
    UnProject(screenPoint, world);
    unprojected[0] = world.x; unprojected[1] = world.y; unprojected[2] = world.z;
    const float len2 = direction.z * direction.z + direction.x * direction.x + direction.y * direction.y;
    if (0.0f < len2) {
        const float k = far_ / std::sqrt(len2);
        direction.x = direction.x * k;
        direction.y = direction.y * k;
        direction.z = direction.z * k;
    }
    origin.x = (unprojected[0] - (direction.x + origin.x)) + origin.x;
    origin.y = (unprojected[1] - (direction.y + origin.y)) + origin.y;
    origin.z = (unprojected[2] - (direction.z + origin.z)) + origin.z;
    return true;
}

}  // namespace s3d
