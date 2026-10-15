// cS3DCamera — the S3D engine camera (0x100163afe..0x100164b60): a local transform (position + orientation),
// the derived view transform, and a projection (orthographic by default; a perspective frustum when the
// projection type is 2) with an optional sub-view inside the viewport. Project / UnProject / GetEyeRay convert
// between world space and screen pixels.
//
// Original layout: +8 refcount, +0xc/+0x10 viewport size, +0x14 depth size, +0x18 sub-view flag,
// +0x1c..+0x28 sub-view rect, +0x2c local cS3DTransform, +0x64 view cS3DTransform (cached from the local one),
// +0x9c view stamp, +0xa0 projection matrix (4x4, row-major), +0x120 its inverse, +0x160 projection stamp,
// +0x164 look-at target, +0x170 look-at stamp, +0x174 up vector, +0x180 projection state counter,
// +0x184 projection type, +0x188/+0x18c near/far, +0x190..+0x19c left/right/bottom/top,
// +0x1a0 viewport offset, +0x1a8 depth offset.
// Quirk kept: for the perspective type the inverse matrix element [15] ends up 0 (the original overwrites it),
// so UnProject is only meaningful for the orthographic types.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "s3d/S3DMath.h"

namespace s3d {

enum ProjectionType : int { kProjectionDefault = 0, kProjectionOrtho = 1, kProjectionPerspective = 2 };

class cS3DCamera {
public:
    cS3DCamera();

    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();            // returns the remaining count; 0 means the caller may delete
    bool Init() { return true; }
    bool Shutdown() { return true; }

    void MakeIdentity();

    // viewport / sub-view
    void SetViewport(float width, float height);
    void ClearSubview();
    void SetDepthSize(float depth);
    void SetSubview(float x, float y, float width, float height);
    void SetViewportOffset(const cS3DVector2& offset);
    void SetDepthOffset(float offset);

    // placement
    void GetPosition(cS3DVector3& out) const;
    void SetPosition(const cS3DVector3& position);
    void LookAt(const cS3DVector3& target, const cS3DVector3& up);
    void GetLookAt(cS3DVector3& out);
    cS3DTransform& LocalXform() { return local_; }
    cS3DTransform& ViewXform();

    // projection
    bool SetOrtho(float left, float right, float top, float bottom, float nearPlane, float farPlane);
    int GetProjectionType() const { return projectionType_; }
    void SetProjectionType(int type) { projectionType_ = type; ++projectionState_; }
    void GetViewVolume(float& left, float& right, float& top, float& bottom, float& nearPlane, float& farPlane) const;
    const float* ProjectionMatrix();        // 16 floats, row-major
    const float* InverseProjectionMatrix();
    std::uint32_t GetProjectionState() const { return projectionState_; }
    std::uint8_t GetViewState() const { return local_.changeCounter; }

    // screen <-> world
    bool GetEyeRay(const cS3DVector2& screen, cS3DVector3& origin, cS3DVector3& direction);
    bool UnProject(const cS3DVector3& screen, cS3DVector3& world);
    bool Project(const cS3DVector3& world, cS3DVector3& screen);

    // exposed state
    float ViewportWidth() const { return viewportW_; }
    float ViewportHeight() const { return viewportH_; }

private:
    void UpdateXform();
    void UpdateProjection();

    std::uint32_t refCount_ = 0;
    float viewportW_ = 100.0f, viewportH_ = 100.0f, depthSize_ = 65536.0f;
    bool subviewEnabled_ = false;
    float subX_ = 0, subY_ = 0, subW_ = 100.0f, subH_ = 100.0f;
    cS3DTransform local_, view_;
    std::uint32_t viewStamp_ = 0;
    float proj_[16] = {}, inverse_[16] = {};
    std::uint32_t projStamp_ = 0;
    float lookAtTarget_[3] = {0, 0, 0};
    std::uint32_t lookAtStamp_ = 0;
    float up_[3] = {0, 0, 0};
    std::uint32_t projectionState_ = 1;
    int projectionType_ = kProjectionDefault;
    float near_ = 1.0f, far_ = 2.0f, left_ = -1.0f, right_ = 1.0f, bottom_ = 1.0f, top_ = -1.0f;
    float viewportOffsetX_ = 0, viewportOffsetY_ = 0, depthOffset_ = 0;
};

}  // namespace s3d
