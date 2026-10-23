// SC4ViewUtilities: construction, camera/frustum state (UpdateCameraPosition), all the projection and unprojection
// paths, the view / draw frustum culling queries, the draw frustum build for a viewport sub-rectangle, the ground
// plane helpers and rays, the block helpers, the city corners (including the latent non-square-city bug) and the
// model/triangle picking. The expectations are derived from the disassembly, not from the port's own output.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "sc4/CameraControl.h"
#include "sc4/ViewUtilities.h"
#include "s3d/Camera.h"
#include "s3d/S3DMath.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 2e-4f) {
    return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b));
}

namespace {

struct TestViewHost final : IViewUtilitiesHost {
    s3d::cS3DCamera* camera = nullptr;
    s3d::cS3DCamera* GetCamera() override { return camera; }
};

struct PickModel final : s3d::cIS3DModelInstance {
    cS3DTransform transform;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZUnknown* GetOwner() override { return nullptr; }
    void SetOwner(cIGZUnknown*) override {}
    void* GetCustomData() override { return nullptr; }
    void SetCustomData(void*) override {}
    void GetBounds(cS3DBoundingBox&) override {}
    void GetWorldBounds(cS3DBoundingBox&) override {}
    void GetTransform(cS3DTransform& out) override { out = transform; }
    void SetTransform(const cS3DTransform& value) override { transform = value; }
    bool Pick(cSC4PickContext*) override { return false; }
    int Draw(SC4DrawContext*, bool) override { return 1; }
    int GetProperties() override { return 2; }
    void SetProperties(int) override {}
    s3d::SC4DrawAttributes GetDrawAttributes() const override { return {}; }
    void SetDrawAttributes(const s3d::SC4DrawAttributes&) override {}
    void GetSortValue(std::uint32_t& out) const override { out = 0; }
    s3d::cIS3DResModel* GetResModel() const override { return nullptr; }
};

// A quad made of two triangles in the y = height plane, fed through the real cS3DTriangleIterator.
struct QuadMesh final : IMeshPickHost {
    cS3DVector3 vertices[6];
    bool pickable = true;

    explicit QuadMesh(float height = 0.0f) { SetHeight(height); }
    void SetHeight(float height) {
        vertices[0] = {-10.0f, height, -10.0f};
        vertices[1] = {10.0f, height, -10.0f};
        vertices[2] = {10.0f, height, 10.0f};
        vertices[3] = {-10.0f, height, -10.0f};
        vertices[4] = {10.0f, height, 10.0f};
        vertices[5] = {-10.0f, height, 10.0f};
    }
    bool HasPickableVertices() const override { return pickable; }
    int PrimitiveGroupCount() const override { return 1; }
    void InitTriangleIterator(s3d::cS3DTriangleIterator& iterator, int group) const override {
        static const s3d::MeshPrimitive kPrimitive{0 /* triangle list */, 0, 6};   // 0x1001a9f28
        s3d::MeshView view;
        view.vertexFormat = 2;
        view.vertexStride = 12;
        view.vertexData = reinterpret_cast<const std::uint8_t*>(vertices);
        view.indexData = nullptr;
        view.primitives = &kPrimitive;
        iterator.Init(view, group);
    }
};

struct MeshList final : IMeshMaterialHost {
    const IMeshPickHost* meshes[2] = {nullptr, nullptr};
    int count = 0;
    int MeshCount() const override { return count; }
    const IMeshPickHost* Mesh(int index) const override { return meshes[index]; }
};

// A camera placed the way the city view places it: orthographic, looking down at the target from a fixed offset.
struct ViewFixture {
    s3d::cS3DCamera* camera = new s3d::cS3DCamera();   // owned by the control after Init (heap: Release deletes it)
    sc4::cSC4CameraControl control;
    TestViewHost host;
    SC4ViewUtilities view;

    explicit ViewFixture(float cityX = 4096.0f, float cityZ = 4096.0f, int zoom = 2, int rotation = 0) {
        const cS3DVector3 target{2048.0f, 0.0f, 2048.0f};
        control.Init(target, camera);
        control.SetViewportSize(800, 600);
        control.SetCitySize(cityX, cityZ);
        control.SetZoomAndRotation(zoom, rotation);
        host.camera = camera;
        view.SetHost(&host);
        view.SetCitySize(cityX, cityZ);
        view.UpdateCameraPosition(&control);
    }
    ~ViewFixture() { control.Shutdown(); }

    void Rerotate(int rotation) {
        control.SetRotation(rotation);
        view.UpdateCameraPosition(&control);
    }
};

float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

cS3DVector3 BasisRight(const float* eye, const float* lookAt, float* dirOut) {
    const float dir[3] = {lookAt[0] - eye[0], lookAt[1] - eye[1], lookAt[2] - eye[2]};
    const float length = std::sqrt(Dot3(dir, dir));
    float d[3] = {dir[0] / length, dir[1] / length, dir[2] / length};
    const float worldUp[3] = {0, 1, 0};
    float right[3] = {d[1] * worldUp[2] - d[2] * worldUp[1], d[2] * worldUp[0] - d[0] * worldUp[2],
                      d[0] * worldUp[1] - d[1] * worldUp[0]};
    const float rightLength = std::sqrt(Dot3(right, right));
    cS3DVector3 result{right[0] / rightLength, right[1] / rightLength, right[2] / rightLength};
    dirOut[0] = d[0];
    dirOut[1] = d[1];
    dirOut[2] = d[2];
    return result;
}

}  // namespace

int main() {
    // -----------------------------------------------------------------------------------------------------------
    // Construction and plain accessors
    // -----------------------------------------------------------------------------------------------------------
    {
        SC4ViewUtilities view;
        CHECK(sizeof(SC4ViewUtilities) == 0x478);
        CHECK(!view.IsReady());
        CHECK(view.Host() == nullptr);
        int zoom = -1, rotation = -1;
        view.GetZoomAndRotation(zoom, rotation);
        CHECK(zoom == 0 && rotation == 0);
        CHECK(view.ViewFrustumBounds().boundsMin[0] == 0.0f);
        CHECK(view.ViewPlanes().reserved[0] == 0.0f);
        CHECK(view.DrawFrustumBounds().subLeft == 0.0f);
        CHECK(!view.DrawFrustumReduced());
        CHECK(view.ViewportRect()[0] == 0);
        CHECK(view.CitySizeX() == 0.0f && view.CitySizeZ() == 0.0f);

        // ClipWorldPositionToCityPosition clamps x / z to [0, citySize - 1] and leaves y alone.
        view.SetCitySize(4096.0f, 2048.0f);
        float inside[3] = {100.0f, 7.0f, 300.0f};
        view.ClipWorldPositionToCityPosition(inside);
        CHECK(inside[0] == 100.0f && inside[1] == 7.0f && inside[2] == 300.0f);
        float outside[3] = {4096.0f, 1.0f, 5000.0f};
        view.ClipWorldPositionToCityPosition(outside);
        CHECK(outside[0] == 4095.0f && outside[2] == 2047.0f);
        float negative[3] = {-5.0f, 2.0f, -0.5f};
        view.ClipWorldPositionToCityPosition(negative);
        CHECK(negative[0] == 0.0f && negative[2] == 0.0f);
        float edge[3] = {4095.0f, 0.0f, 2047.0f};
        view.ClipWorldPositionToCityPosition(edge);
        CHECK(edge[0] == 4095.0f && edge[2] == 2047.0f);
    }

    // -----------------------------------------------------------------------------------------------------------
    // UpdateCameraPosition: camera state, basis, frustum corners / planes / edges and culling
    // -----------------------------------------------------------------------------------------------------------
    ViewFixture fixture;
    SC4ViewUtilities& vu = fixture.view;
    const FrustumBounds& frustum = vu.ViewFrustumBounds();
    {
        CHECK(vu.IsReady());
        CHECK(vu.Zoom() == 2);
        CHECK(vu.Rotation() == 0);

        cS3DVector3 eyeVector, lookVector;
        fixture.camera->GetPosition(eyeVector);
        fixture.camera->GetLookAt(lookVector);
        const float eye[3] = {eyeVector.x, eyeVector.y, eyeVector.z};
        const float lookAt[3] = {lookVector.x, lookVector.y, lookVector.z};

        float left = 0, right = 0, top = 0, bottom = 0, nearPlane = 0, farPlane = 0;
        fixture.camera->GetViewVolume(left, right, top, bottom, nearPlane, farPlane);
        CHECK(frustum.subLeft == left && frustum.subRight == right);
        CHECK(frustum.subLow == top && frustum.subHigh == bottom);
        CHECK(frustum.nearPlane == nearPlane && frustum.farPlane == farPlane);
        CHECK(vu.ViewportRect()[0] == 0 && vu.ViewportRect()[2] == 0);   // not built yet

        // world <-> viewport round trip; the z helper agrees with the full projection
        const float samples[3][3] = {{2048.0f, 0.0f, 2048.0f}, {1000.0f, 50.0f, 1500.0f}, {3000.0f, 120.0f, 700.0f}};
        for (const float* expected : samples) {
            float point[3] = {expected[0], expected[1], expected[2]};
            vu.ProjectPointToViewportCoordinates(point);
            CHECK(Near(point[2], vu.ProjectPointToViewportZ(expected)));
            vu.UnProjectPointFromViewportCoordinatesToWorldCoordinates(point);
            CHECK(Near(point[0], expected[0]) && Near(point[1], expected[1]) && Near(point[2], expected[2]));
        }

        // The 2D variant writes x / y only; z is left at the *view* z (it calls ProjectPointToViewCoordinates,
        // which rotates z but does not map it to the viewport range).
        float flat[3] = {1200.0f, 30.0f, 900.0f};
        float flatFull[3] = {flat[0], flat[1], flat[2]};
        vu.ProjectPointToViewportCoordinates2D(flat);
        vu.ProjectPointToViewportCoordinates(flatFull);
        CHECK(flat[0] == flatFull[0] && flat[1] == flatFull[1]);
        CHECK(flat[2] != flatFull[2]);

        // project / unproject view coordinates are inverses
        {
            float point[3] = {1234.0f, 56.0f, 789.0f};
            const float original[3] = {point[0], point[1], point[2]};
            vu.ProjectPointToViewCoordinates(point);
            vu.UnProjectPointFromViewCoordinatesToWorldCoordinates(point);
            CHECK(Near(point[0], original[0]) && Near(point[1], original[1]) && Near(point[2], original[2]));
        }

        // fixed-view projection: the yaw / pitch rotation without the translation
        {
            const float angle = kSC4YViewAngle[2];
            const float pitch = kSC4PitchViewAngle[2];
            const float sinNegYaw = std::sin(-angle);   // +0x450 stores sinf(-yaw)
            float point[3] = {100.0f, 20.0f, -30.0f};
            const float zView = point[2] * std::cos(angle) + point[0] * sinNegYaw;
            const float expected[3] = {
                std::cos(angle) * point[0] - sinNegYaw * point[2],
                std::cos(pitch) * point[1] - std::sin(pitch) * zView,
                zView * std::cos(pitch) + point[1] * std::sin(pitch),
            };
            vu.ProjectPointToFixedViewCoordinates(point);
            CHECK(Near(point[0], expected[0]) && Near(point[1], expected[1]) && Near(point[2], expected[2]));
        }
        // viewport -> fixed view is the unprojection followed by the fixed-view rotation
        {
            float a[3] = {123.0f, 456.0f, 0.0f};
            float b[3] = {a[0], a[1], a[2]};
            vu.ProjectPointFromViewportCoordinatesToFixedViewCoordinates(a);
            vu.UnProjectPointFromViewportCoordinatesToWorldCoordinates(b);
            vu.ProjectPointToFixedViewCoordinates(b);
            CHECK(Near(a[0], b[0]) && Near(a[1], b[1]) && Near(a[2], b[2]));
        }

        // the basis the frustum is built from
        float dir[3];
        const cS3DVector3 rightAxis = BasisRight(eye, lookAt, dir);
        float up[3] = {rightAxis.y * dir[2] - rightAxis.z * dir[1], rightAxis.z * dir[0] - rightAxis.x * dir[2],
                       rightAxis.x * dir[1] - rightAxis.y * dir[0]};
        {
            const float length = std::sqrt(Dot3(up, up));
            for (float& value : up) value /= length;
        }
        const ViewPlaneBlock& planes = vu.ViewPlanes();
        CHECK(Near(planes.right[0], rightAxis.x) && Near(planes.right[1], rightAxis.y) && Near(planes.right[2], rightAxis.z));
        CHECK(Near(planes.up[0], up[0]) && Near(planes.up[1], up[1]) && Near(planes.up[2], up[2]));
        CHECK(Near(planes.dir[0], dir[0]) && Near(planes.dir[1], dir[1]) && Near(planes.dir[2], dir[2]));
        CHECK(Near(planes.rightD, -Dot3(planes.right, frustum.corner[3])));
        CHECK(Near(planes.negRightD, Dot3(planes.right, frustum.corner[0])));
        CHECK(Near(planes.upD, -Dot3(planes.up, frustum.corner[2])));
        CHECK(Near(planes.negUpD, Dot3(planes.up, frustum.corner[3])));
        CHECK(Near(planes.dirD, -Dot3(planes.dir, frustum.corner[3])));
        CHECK(Near(planes.negDirFarD, -Dot3(planes.dir, frustum.corner[7])));
        // quirk: the +0x1d0 copy drops -right.y and stores -right.z twice
        CHECK(Near(planes.rightPlaneCopy[0], planes.rightD));
        CHECK(Near(planes.rightPlaneCopy[1], -planes.right[0]));
        CHECK(Near(planes.rightPlaneCopy[2], -planes.right[2]));
        CHECK(Near(planes.rightPlaneCopy[3], planes.negRightD));

        const ViewEdgeBlock& edges = vu.ViewEdges();
        CHECK(edges.leftOriginX == frustum.corner[3][0] && edges.leftOriginZ == frustum.corner[3][2]);
        CHECK(Near(edges.leftDirX, frustum.corner[7][0] - frustum.corner[3][0]));
        CHECK(Near(edges.leftDirZ, frustum.corner[7][2] - frustum.corner[3][2]));
        CHECK(edges.rightOriginX == frustum.corner[0][0] && edges.rightOriginZ == frustum.corner[0][2]);
        CHECK(Near(edges.rightDirX, frustum.corner[4][0] - frustum.corner[0][0]));
        CHECK(Near(edges.rightDirZ, frustum.corner[4][2] - frustum.corner[0][2]));

        // corners: near / far quads in the order +R+U, +R-U, -R-U, -R+U; the 4th is the negation of the 1st across
        // the right axis, the 2nd the negation of the 3rd.
        for (int i = 0; i < 4; ++i) {
            const float along = Dot3(frustum.corner[i], dir) - Dot3(eye, dir);
            CHECK(Near(along, nearPlane, 1e-3f));
            const float farAlong = Dot3(frustum.corner[i + 4], dir) - Dot3(eye, dir);
            CHECK(Near(farAlong, farPlane, 1e-3f));
        }
        {
            const float* c0 = frustum.corner[0];
            const float* c1 = frustum.corner[1];
            const float* c2 = frustum.corner[2];
            const float* c3 = frustum.corner[3];
            for (int k = 0; k < 3; ++k) {
                CHECK(Near(c0[k] - c1[k], c3[k] - c2[k]));     // +U side minus -U side is the same on both
                CHECK(Near(c0[k] - c3[k], c1[k] - c2[k]));     // +R side minus -R side
            }
        }
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 3; ++k) {
                CHECK(frustum.boundsMin[k] <= frustum.corner[i][k] && frustum.corner[i][k] <= frustum.boundsMax[k]);
            }
        }
        float boxMin[3], boxMax[3];
        vu.GetViewFrustumBoundingBox(boxMin, boxMax);
        CHECK(std::memcmp(boxMin, frustum.boundsMin, sizeof boxMin) == 0);
        CHECK(std::memcmp(boxMax, frustum.boundsMax, sizeof boxMax) == 0);

        // city-clipped bounds: y min 0, y max = near corner 3 height, x / z inside the city
        CHECK(frustum.clippedMin[1] == 0.0f);
        CHECK(Near(frustum.clippedMax[1], frustum.corner[3][1]));
        CHECK(0.0f <= frustum.clippedMin[0] && frustum.clippedMin[0] <= frustum.clippedMax[0] &&
              frustum.clippedMax[0] <= 4096.0f);
        CHECK(0.0f <= frustum.clippedMin[2] && frustum.clippedMin[2] <= frustum.clippedMax[2] &&
              frustum.clippedMax[2] <= 4096.0f);
        CHECK(Near(frustum.clippedMax[2], 4096.0f, 1e-4f) || frustum.clippedMax[2] < 4096.0f);   // the city clip bites

        // point culling
        const float target[3] = {2048.0f, 0.0f, 2048.0f};
        CHECK(vu.IsPointInViewFrustum(target));
        float nearCenter[3] = {0, 0, 0};
        float farCenter[3] = {0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 3; ++k) {
                nearCenter[k] += frustum.corner[i][k] * 0.25f;
                farCenter[k] += frustum.corner[i + 4][k] * 0.25f;
            }
        }
        // the depth test is strict at both ends: 10 units behind the near plane is rejected, 10 in front accepted
        float behindNear[3] = {nearCenter[0] - dir[0] * 10.0f, nearCenter[1] - dir[1] * 10.0f,
                               nearCenter[2] - dir[2] * 10.0f};
        CHECK(!vu.IsPointInViewFrustum(behindNear));
        float inFrontOfNear[3] = {nearCenter[0] + dir[0] * 10.0f, nearCenter[1] + dir[1] * 10.0f,
                                  nearCenter[2] + dir[2] * 10.0f};
        CHECK(vu.IsPointInViewFrustum(inFrontOfNear));
        float between[3];
        for (int k = 0; k < 3; ++k) between[k] = nearCenter[k] + (farCenter[k] - nearCenter[k]) * 0.5f;
        CHECK(vu.IsPointInViewFrustum(between));

        float smallMin[3] = {between[0] - 5.0f, between[1] - 5.0f, between[2] - 5.0f};
        float smallMax[3] = {between[0] + 5.0f, between[1] + 5.0f, between[2] + 5.0f};
        CHECK(vu.IsBoundingBoxInViewFrustum(smallMin, smallMax));

        float sidePoint[3] = {target[0] - 30000.0f, target[1], target[2]};
        float sideMin[3] = {sidePoint[0] - 10.0f, -10.0f, sidePoint[2] - 10.0f};
        float sideMax[3] = {sidePoint[0] + 10.0f, 10.0f, sidePoint[2] + 10.0f};
        CHECK(!vu.IsPointInViewFrustum(sidePoint));
        CHECK(!vu.IsBoundingBoxInViewFrustum(sideMin, sideMax));
        float abovePoint[3] = {target[0], 20000.0f, target[2]};
        CHECK(!vu.IsPointInViewFrustum(abovePoint));
        float aboveMin[3] = {abovePoint[0] - 5.0f, abovePoint[1] - 5.0f, abovePoint[2] - 5.0f};
        float aboveMax[3] = {abovePoint[0] + 5.0f, abovePoint[1] + 5.0f, abovePoint[2] + 5.0f};
        CHECK(!vu.IsBoundingBoxInViewFrustum(aboveMin, aboveMax));

        // viewport ground points, computed independently: unproject the near-plane pixel, extend the eye ray to y = 0
        float ground[4][2];
        vu.GetViewportLimitsGroundPlanePositions(ground[0], ground[1], ground[2], ground[3]);
        const int cornerX[4] = {0, 800, 0, 800};
        const int cornerY[4] = {0, 0, 600, 600};
        for (int i = 0; i < 4; ++i) {
            // the ground point projects back to the pixel it came from
            float point[3] = {ground[i][0], 0.0f, ground[i][1]};
            vu.ProjectPointToViewportCoordinates(point);
            CHECK(Near(point[0], float(cornerX[i]), 2e-3f));
            CHECK(Near(point[1], float(cornerY[i]), 2e-3f));
        }
        float centerHit[3];
        CHECK(vu.GetViewportPointIntersectionWithGroundPlane(400, 300, centerHit));
        CHECK(Near(centerHit[0], 2048.0f, 1e-2f) && Near(centerHit[1], 0.0f) && Near(centerHit[2], 2048.0f, 1e-2f));

        // rays
        float origin[3], direction[3];
        CHECK(vu.ViewportPositionToRay(400, 300, origin, direction));
        CHECK(Near(std::sqrt(Dot3(direction, direction)), 1.0f));
        CHECK(Near(origin[0], eye[0]) && Near(origin[1], eye[1]) && Near(origin[2], eye[2]));
        CHECK(Near(direction[0], dir[0]) && Near(direction[1], dir[1]) && Near(direction[2], dir[2]));

        // camera view angles
        float yaw = 0, pitch = 0, screenPitch = 0;
        vu.GetCameraViewAngles(yaw, pitch, screenPitch);
        CHECK(Near(yaw, kSC4YViewAngle[2]));
        CHECK(Near(pitch, kSC4PitchViewAngle[2]));
        CHECK(Near(screenPitch, std::atan(std::tan(kSC4PitchViewAngle[2]) / std::cos(kSC4YViewAngle[2]))));

        // viewport units
        CHECK(Near(vu.ViewportUnitsToWorldUnits(100.0f), 100.0f * fixture.control.WorldPerPixel()));
    }

    // -----------------------------------------------------------------------------------------------------------
    // Rotations: visible edges, the projected bounding box rectangle and the city corners
    // -----------------------------------------------------------------------------------------------------------
    {
        const int expectedEdges[4][2] = {{1, 2}, {2, 3}, {3, 0}, {0, 1}};
        const float cityX = 4096.0f, cityZ = 2048.0f;
        ViewFixture rotating(cityX, cityZ);
        rotating.view.SetCitySize(cityX, cityZ);
        for (int rotation = 0; rotation < 4; ++rotation) {
            rotating.Rerotate(rotation);
            CHECK(rotating.view.Rotation() == rotation);
            CHECK(rotating.view.IsReady());
            int edgeA = -1, edgeB = -1;
            rotating.view.GetVisibleEdges(edgeA, edgeB);
            CHECK(edgeA == expectedEdges[rotation][0] && edgeB == expectedEdges[rotation][1]);

            float yaw = 0, pitch = 0, screenPitch = 0;
            rotating.view.GetCameraViewAngles(yaw, pitch, screenPitch);
            CHECK(Near(yaw, kSC4YViewAngle[2] + float(rotation) * 1.5707964f));
            CHECK(Near(pitch, kSC4PitchViewAngle[2]));

            // a box around the camera target projects to a rectangle that contains the projected target
            const float target[3] = {2048.0f, 0.0f, 2048.0f};
            float boxLo[3] = {1800.0f, 0.0f, 1800.0f};
            float boxHi[3] = {2300.0f, 300.0f, 2300.0f};
            int rectMin[2] = {0, 0}, rectMax[2] = {0, 0};
            rotating.view.ProjectBoundingBoxToViewportRectangle(boxLo, boxHi, rectMin, rectMax);
            CHECK(rectMax[0] > rectMin[0] && rectMax[1] > rectMin[1]);
            float projected[3] = {target[0], target[1], target[2]};
            rotating.view.ProjectPointToViewportCoordinates(projected);
            CHECK(rectMin[0] <= projected[0] && projected[0] <= rectMax[0]);
            CHECK(rectMin[1] <= projected[1] && projected[1] <= rectMax[1]);

            // city corners for this rotation (rotation 1 / corner 2 hits the original's x-for-z slip)
            const float expected[4][4][3] = {
                {{0, 0, cityZ}, {0, 0, 0}, {cityX, 0, 0}, {cityX, 0, cityZ}},
                {{0, 0, 0}, {cityX, 0, 0}, {cityX, 0, cityX}, {0, 0, cityZ}},
                {{cityX, 0, 0}, {cityX, 0, cityZ}, {0, 0, cityZ}, {0, 0, 0}},
                {{cityX, 0, cityZ}, {0, 0, cityZ}, {0, 0, 0}, {cityX, 0, 0}},
            };
            for (int corner = 0; corner < 4; ++corner) {
                float out[3] = {9, 9, 9};
                rotating.view.GetCityCornerPosition(corner, out);
                CHECK(out[0] == expected[rotation][corner][0]);
                CHECK(out[1] == 0.0f);
                CHECK(out[2] == expected[rotation][corner][2]);
            }
        }
        // an out-of-range corner only clears the y component
        float out[3] = {7, 7, 7};
        rotating.view.GetCityCornerPosition(4, out);
        CHECK(out[0] == 7.0f && out[1] == 0.0f && out[2] == 7.0f);
        float negative[3] = {7, 7, 7};
        rotating.view.GetCityCornerPosition(-1, negative);
        CHECK(negative[0] == 7.0f && negative[2] == 7.0f);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Draw frustum: the full-view path copies the view block, the sub-rectangle path rebuilds it
    // -----------------------------------------------------------------------------------------------------------
    {
        ViewFixture drawing;
        SC4ViewUtilities& view = drawing.view;
        const FrustumBounds& viewBounds = view.ViewFrustumBounds();

        int fullMin[2] = {0, 0};
        int fullMax[2] = {800, 600};
        view.BuildDrawFrustumDataFromViewportRect(fullMin, fullMax);
        CHECK(!view.DrawFrustumReduced());
        CHECK(view.ViewportRect()[0] == 0 && view.ViewportRect()[1] == 0);
        CHECK(view.ViewportRect()[2] == 800 && view.ViewportRect()[3] == 600);
        const FrustumBounds& draw = view.DrawFrustumBounds();
        CHECK(std::memcmp(draw.boundsMin, viewBounds.boundsMin, sizeof draw.boundsMin) == 0);
        CHECK(std::memcmp(draw.boundsMax, viewBounds.boundsMax, sizeof draw.boundsMax) == 0);
        CHECK(std::memcmp(draw.clippedMin, viewBounds.clippedMin, sizeof draw.clippedMin) == 0);
        CHECK(std::memcmp(draw.corner, viewBounds.corner, sizeof draw.corner) == 0);
        CHECK(std::memcmp(draw.ground, viewBounds.ground, sizeof draw.ground) == 0);
        CHECK(std::memcmp(&view.DrawPlanes(), &view.ViewPlanes(), sizeof(ViewPlaneBlock)) == 0);
        CHECK(std::memcmp(&view.DrawEdges(), &view.ViewEdges(), sizeof(ViewEdgeBlock)) == 0);

        // sub-rectangle
        int rectMin[2] = {100, 100};
        int rectMax[2] = {700, 500};
        view.BuildDrawFrustumDataFromViewportRect(rectMin, rectMax);
        CHECK(view.DrawFrustumReduced());
        CHECK(view.ViewportRect()[0] == 100 && view.ViewportRect()[1] == 100);
        CHECK(view.ViewportRect()[2] == 700 && view.ViewportRect()[3] == 500);
        // sub-view extents: (rect - centre) * world units per viewport unit
        const float wpu = drawing.control.WorldPerPixel();
        CHECK(Near(draw.subLeft, (100.0f - 400.0f) * wpu));
        CHECK(Near(draw.subRight, (700.0f - 400.0f) * wpu));
        CHECK(Near(draw.subHigh, (300.0f - 100.0f) * wpu));
        CHECK(Near(draw.subLow, (300.0f - 500.0f) * wpu));
        CHECK(draw.nearPlane == viewBounds.nearPlane && draw.farPlane == viewBounds.farPlane);
        // the draw block sits inside the view block
        for (int k = 0; k < 3; ++k) {
            CHECK(viewBounds.boundsMin[k] <= draw.boundsMin[k] && draw.boundsMin[k] <= draw.boundsMax[k] &&
                  draw.boundsMax[k] <= viewBounds.boundsMax[k]);
        }
        // ground points of the sub-rectangle: the quirk passes rectMin's y to the second (top-right) query
        float expectedGround[4][2];
        expectedGround[0][0] = 0;   // filled below
        float hit[3];
        view.GetViewportPointIntersectionWithGroundPlane(rectMin[0], rectMin[1], hit);
        expectedGround[0][0] = hit[0];
        expectedGround[0][1] = hit[2];
        view.GetViewportPointIntersectionWithGroundPlane(rectMax[0], rectMin[1], hit);
        expectedGround[1][0] = hit[0];
        expectedGround[1][1] = hit[2];
        view.GetViewportPointIntersectionWithGroundPlane(rectMin[0], rectMax[1], hit);
        expectedGround[2][0] = hit[0];
        expectedGround[2][1] = hit[2];
        view.GetViewportPointIntersectionWithGroundPlane(rectMax[0], rectMax[1], hit);
        expectedGround[3][0] = hit[0];
        expectedGround[3][1] = hit[2];
        for (int i = 0; i < 4; ++i) {
            CHECK(Near(draw.ground[i][0], expectedGround[i][0]));
            CHECK(draw.ground[i][1] == 0.0f);
            CHECK(Near(draw.ground[i][2], expectedGround[i][1]));
        }
        // and the quirk is distinguishable from the "correct" top-right corner
        float correctTopRight[3];
        view.GetViewportPointIntersectionWithGroundPlane(rectMax[0], rectMax[1], correctTopRight);
        CHECK(!Near(expectedGround[1][0], correctTopRight[0]) || !Near(expectedGround[1][1], correctTopRight[2]));
        float drawGround[4][2];
        view.GetDrawFrustumGroundPlanePositions(drawGround[0], drawGround[1], drawGround[2], drawGround[3]);
        for (int i = 0; i < 4; ++i) {
            CHECK(drawGround[i][0] == draw.ground[i][0] && drawGround[i][1] == draw.ground[i][2]);
        }
        // the draw edges are rebuilt from the draw corners
        const ViewEdgeBlock& drawEdges = view.DrawEdges();
        CHECK(drawEdges.leftOriginX == draw.corner[3][0] && drawEdges.leftOriginZ == draw.corner[3][2]);
        CHECK(Near(drawEdges.leftDirX, draw.corner[7][0] - draw.corner[3][0]));
        CHECK(drawEdges.rightOriginX == draw.corner[0][0]);
        // the draw plane block is built from the draw corners but keeps the view direction group
        const ViewPlaneBlock& drawPlanes = view.DrawPlanes();
        CHECK(Near(drawPlanes.rightD, -Dot3(drawPlanes.right, draw.corner[3])));
        CHECK(Near(drawPlanes.upD, -Dot3(drawPlanes.up, draw.corner[2])));
        CHECK(Near(drawPlanes.negUpD, Dot3(drawPlanes.up, draw.corner[3])));
        CHECK(Near(drawPlanes.dirD, -Dot3(drawPlanes.dir, viewBounds.corner[3])));
        CHECK(Near(drawPlanes.negDirFarD, -Dot3(drawPlanes.dir, viewBounds.corner[7])));
        // culling inside the sub-rectangle
        float center[3] = {(draw.corner[0][0] + draw.corner[2][0]) * 0.5f,
                           (draw.corner[0][1] + draw.corner[2][1]) * 0.5f,
                           (draw.corner[0][2] + draw.corner[2][2]) * 0.5f};
        float smallMin[3] = {center[0] - 2.0f, center[1] - 2.0f, center[2] - 2.0f};
        float smallMax[3] = {center[0] + 2.0f, center[1] + 2.0f, center[2] + 2.0f};
        CHECK(view.IsBoundingBoxInDrawFrustum(smallMin, smallMax));

        // a sub-rectangle with a single axis reduced (only the x range differs)
        int stripMin[2] = {0, 100};
        int stripMax[2] = {800, 500};
        view.BuildDrawFrustumDataFromViewportRect(stripMin, stripMax);
        CHECK(Near(draw.subLeft, viewBounds.subLeft));
        CHECK(Near(draw.subRight, viewBounds.subRight));
        CHECK(Near(draw.subLow, (300.0f - 500.0f) * wpu));
    }

    // -----------------------------------------------------------------------------------------------------------
    // Ground-plane visible area rectangle and the block helpers
    // -----------------------------------------------------------------------------------------------------------
    {
        ViewFixture area;
        SC4ViewUtilities& view = area.view;
        float midMin[3], midMax[3], blockMin[3], blockMax[3];
        view.GetGroundPlaneVisibleAreaRectangle(midMin, midMax, blockMin, blockMax);
        // the mid rectangle is the AABB of the two horizontal edge midpoints
        float p00[3], p10[3], p01[3], p11[3];
        view.GetViewportPointIntersectionWithGroundPlane(0, 0, p00);
        view.GetViewportPointIntersectionWithGroundPlane(800, 0, p10);
        view.GetViewportPointIntersectionWithGroundPlane(0, 600, p01);
        view.GetViewportPointIntersectionWithGroundPlane(800, 600, p11);
        const float midTopX = (p00[0] + p10[0]) * 0.5f;
        const float midTopZ = (p00[2] + p10[2]) * 0.5f;
        const float midBottomX = (p01[0] + p11[0]) * 0.5f;
        const float midBottomZ = (p01[2] + p11[2]) * 0.5f;
        CHECK(Near(midMin[0], std::fmin(midTopX, midBottomX)));
        CHECK(Near(midMin[2], std::fmin(midTopZ, midBottomZ)));
        CHECK(Near(midMax[0], std::fmax(midTopX, midBottomX)));
        CHECK(Near(midMax[2], std::fmax(midTopZ, midBottomZ)));
        CHECK(midMin[1] == 0.0f && midMax[1] == 0.0f);
        // the block rectangle contains the corner extremes and is aligned to 64
        const float minX = std::fmin(std::fmin(p00[0], p10[0]), std::fmin(p01[0], p11[0]));
        const float maxX = std::fmax(std::fmax(p00[0], p10[0]), std::fmax(p01[0], p11[0]));
        const float minZ = std::fmin(std::fmin(p00[2], p10[2]), std::fmin(p01[2], p11[2]));
        const float maxZ = std::fmax(std::fmax(p00[2], p10[2]), std::fmax(p01[2], p11[2]));
        CHECK(Near(blockMin[0], SC4ViewUtilities::RoundDownToNearestBlockBoundary(minX, 64)));
        CHECK(Near(blockMin[2], SC4ViewUtilities::RoundDownToNearestBlockBoundary(minZ, 64)));
        CHECK(Near(blockMax[0], SC4ViewUtilities::RoundUpToNearestBlockBoundary(maxX, 64)));
        CHECK(Near(blockMax[2], SC4ViewUtilities::RoundUpToNearestBlockBoundary(maxZ, 64)));
        CHECK(blockMin[1] == 0.0f && blockMax[1] == 0.0f);
        CHECK(std::fmod(blockMin[0], 64.0f) == 0.0f && std::fmod(blockMax[2], 64.0f) == 0.0f);
        CHECK(blockMin[0] <= midMin[0] && midMax[0] <= blockMax[0]);
        CHECK(blockMin[2] <= midMin[2] && midMax[2] <= blockMax[2]);
        // the cached rectangle is returned again (and the flags were reset by the draw-frustum build)
        float againMin[3], againMax[3], againBlockMin[3], againBlockMax[3];
        view.GetGroundPlaneVisibleAreaRectangle(againMin, againMax, againBlockMin, againBlockMax);
        CHECK(Near(againMin[0], midMin[0]) && Near(againBlockMax[2], blockMax[2]));
        // nullptr outputs are allowed
        view.GetGroundPlaneVisibleAreaRectangle(nullptr, nullptr, nullptr, nullptr);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Perspective projection type: the far corners are derived from the values already stored (quirk kept)
    // -----------------------------------------------------------------------------------------------------------
    {
        ViewFixture perspective;
        SC4ViewUtilities& view = perspective.view;
        const FrustumBounds orthoFar = view.ViewFrustumBounds();   // snapshot of the orthographic corners
        float savedFar[4][3];
        std::memcpy(savedFar, orthoFar.corner[4], sizeof savedFar);
        cS3DVector3 eyeVector, lookVector;
        perspective.camera->GetPosition(eyeVector);
        perspective.camera->GetLookAt(lookVector);
        perspective.camera->SetProjectionType(s3d::kProjectionPerspective);
        view.UpdateCameraPosition(&perspective.control);
        CHECK(view.IsReady());
        const float ratio = orthoFar.farPlane / orthoFar.nearPlane;
        const FrustumBounds& frustum = view.ViewFrustumBounds();
        for (int i = 0; i < 4; ++i) {
            CHECK(Near(frustum.corner[i + 4][0], (savedFar[i][0] - eyeVector.x) * ratio + eyeVector.x));
            CHECK(Near(frustum.corner[i + 4][1], (savedFar[i][1] - eyeVector.y) * ratio + eyeVector.y));
            CHECK(Near(frustum.corner[i + 4][2], (savedFar[i][2] - eyeVector.z) * ratio + eyeVector.z));
        }
        // the near corners do not depend on the projection type
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 3; ++k) CHECK(frustum.corner[i][k] == orthoFar.corner[i][k]);
        }

        // draw frustum sub-rectangle with the perspective camera: the far corners are pulled toward the
        // sub-rectangle's near-plane centre from the values already in the draw block
        int fullMin[2] = {0, 0}, fullMax[2] = {800, 600};
        view.BuildDrawFrustumDataFromViewportRect(fullMin, fullMax);
        float savedDrawFar[4][3];
        std::memcpy(savedDrawFar, view.DrawFrustumBounds().corner[4], sizeof savedDrawFar);
        int rectMin[2] = {100, 100}, rectMax[2] = {700, 500};
        view.BuildDrawFrustumDataFromViewportRect(rectMin, rectMax);
        CHECK(view.DrawFrustumReduced());
        const FrustumBounds& draw = view.DrawFrustumBounds();
        const float baseRatio = draw.farPlane / draw.nearPlane;
        const float dirLength = std::sqrt((lookVector.x - eyeVector.x) * (lookVector.x - eyeVector.x) +
                                          (lookVector.y - eyeVector.y) * (lookVector.y - eyeVector.y) +
                                          (lookVector.z - eyeVector.z) * (lookVector.z - eyeVector.z));
        const float d[3] = {(lookVector.x - eyeVector.x) / dirLength, (lookVector.y - eyeVector.y) / dirLength,
                            (lookVector.z - eyeVector.z) / dirLength};
        float center[3] = {400.0f, 300.0f, 0.0f};
        view.UnProjectPointFromViewportCoordinatesToWorldCoordinates(center);   // the sub-rect centre, on the near plane
        const float base[3] = {center[0] - d[0] * draw.nearPlane, center[1] - d[1] * draw.nearPlane,
                               center[2] - d[2] * draw.nearPlane};
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 3; ++k) {
                CHECK(Near(draw.corner[i + 4][k], (savedDrawFar[i][k] - base[k]) * baseRatio + base[k]));
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // GetFrustumAABBCityClipped with no overlap between the frustum footprint and the city rectangle
    // -----------------------------------------------------------------------------------------------------------
    {
        ViewFixture empty;
        empty.view.SetCitySize(1.0f, 1.0f);   // the target (2048) is far outside a 1 x 1 city
        cS3DVector3 min, max;
        empty.view.GetFrustumAABBCityClipped(true, min, max);
        CHECK(min.x == 0.0f && min.y == 0.0f && min.z == 0.0f);
        CHECK(max.x == 0.0f && max.z == 0.0f);
        CHECK(Near(max.y, empty.view.ViewFrustumBounds().corner[3][1]));
        // a clipping rectangle that does overlap gives a non-empty result inside the city
        empty.view.SetCitySize(4096.0f, 4096.0f);
        empty.view.GetFrustumAABBCityClipped(true, min, max);
        CHECK(min.x <= max.x && min.z <= max.z);
        CHECK(0.0f <= min.x && max.x <= 4096.0f && 0.0f <= min.z && max.z <= 4096.0f);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Small helpers
    // -----------------------------------------------------------------------------------------------------------
    {
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(0.0f, 64) == 0.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(1.0f, 64) == 0.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(63.9f, 64) == 0.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(64.0f, 64) == 64.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(300.0f, 64) == 256.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(-1.0f, 64) == -64.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(-64.0f, 64) == -64.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(-300.0f, 64) == -320.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(0.0f, 64) == 0.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(1.0f, 64) == 64.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(64.0f, 64) == 64.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(300.0f, 64) == 320.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(-1.0f, 64) == 0.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(-64.0f, 64) == -64.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(-300.0f, 64) == -256.0f);
        CHECK(SC4ViewUtilities::RoundDownToNearestBlockBoundary(100.0f, 16) == 96.0f);
        CHECK(SC4ViewUtilities::RoundUpToNearestBlockBoundary(100.0f, 16) == 112.0f);

        float v[3] = {1.0f, 2.0f, 3.0f};
        SC4ViewUtilities::RotateVector(v, 1);
        CHECK(v[0] == -3.0f && v[1] == 2.0f && v[2] == 1.0f);
        v[0] = 1.0f; v[1] = 2.0f; v[2] = 3.0f;
        SC4ViewUtilities::RotateVector(v, 2);
        CHECK(v[0] == -1.0f && v[2] == -3.0f);
        v[0] = 1.0f; v[1] = 2.0f; v[2] = 3.0f;
        SC4ViewUtilities::RotateVector(v, 3);
        CHECK(v[0] == 3.0f && v[2] == -1.0f);
        v[0] = 1.0f; v[1] = 2.0f; v[2] = 3.0f;
        SC4ViewUtilities::RotateVector(v, 5);   // 5 mod 4 == 1
        CHECK(v[0] == -3.0f && v[2] == 1.0f);
        v[0] = 1.0f; v[1] = 2.0f; v[2] = 3.0f;
        SC4ViewUtilities::RotateVector(v, -1);   // quirk: the original's wrap leaves negative indices untouched
        CHECK(v[0] == 1.0f && v[2] == 3.0f);

        float edge[3] = {9, 9, 9};
        const float expectedX[4] = {0.0f, 1.0f, 0.0f, -1.0f};
        const float expectedZ[4] = {-1.0f, 0.0f, 1.0f, 0.0f};
        for (int e = 0; e < 4; ++e) {
            edge[0] = edge[1] = edge[2] = 9.0f;
            SC4ViewUtilities::GetVectorForEdge(edge, e);
            CHECK(edge[0] == expectedX[e] && edge[1] == 0.0f && edge[2] == expectedZ[e]);
        }
        edge[0] = 5.0f; edge[1] = 5.0f; edge[2] = 5.0f;
        SC4ViewUtilities::GetVectorForEdge(edge, -1);
        CHECK(edge[0] == 5.0f && edge[1] == 0.0f && edge[2] == 5.0f);
        edge[0] = 5.0f; edge[1] = 5.0f; edge[2] = 5.0f;
        SC4ViewUtilities::GetVectorForEdge(edge, 4);
        CHECK(edge[0] == 0.0f && edge[1] == 0.0f && edge[2] == -1.0f);

        CHECK(Near(SC4ViewUtilities::GetObjectYRotationAngle(1), -1.5707964f));
        CHECK(Near(SC4ViewUtilities::GetObjectYRotationAngle(2), -3.1415927f));
        CHECK(Near(SC4ViewUtilities::GetYawAngle2D(0.0f, 1.0f), std::atan2(-0.0f, -1.0f), 1e-5f));
        CHECK(Near(SC4ViewUtilities::GetYawAngle2D(1.0f, 0.0f), std::atan2(-1.0f, -0.0f), 1e-5f));
        const float s = std::sin(kSC4YViewAngle[2]), c = std::cos(kSC4YViewAngle[2]);
        CHECK(Near(SC4ViewUtilities::GetCityWidthFromOrthoView(100.0f, 2), c * 100.0f - s * 100.0f));
        CHECK(Near(SC4ViewUtilities::GetCityWidthFromOrthoView(100.0f, 4), c * 100.0f - s * 100.0f));

        // GetCameraViewAnglesForRotation: yaw = rotation * pi/2 + kSC4YViewAngle, screen pitch from tan / cos
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (int zoom = 0; zoom < 5; ++zoom) {
                float yaw = 0, pitch = 0, screenPitch = 0;
                SC4ViewUtilities::GetCameraViewAnglesForRotation(rotation, zoom, yaw, pitch, screenPitch);
                CHECK(Near(yaw, float(rotation) * 1.5707964f + kSC4YViewAngle[zoom]));
                CHECK(Near(pitch, kSC4PitchViewAngle[zoom]));
                CHECK(Near(screenPitch,
                           std::atan(std::tan(kSC4PitchViewAngle[zoom]) / std::cos(kSC4YViewAngle[zoom]))));
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // TransformBoundingBox and picking
    // -----------------------------------------------------------------------------------------------------------
    {
        cS3DTransform transform;
        transform.translation[0] = 100.0f;
        transform.translation[1] = 20.0f;
        transform.translation[2] = -5.0f;
        transform.rotation[0][0] = 0.0f;
        transform.rotation[0][1] = -1.0f;
        transform.rotation[1][0] = 1.0f;
        transform.rotation[1][1] = 0.0f;
        transform.scale = 2.0f;
        transform.flags = 7;
        cS3DBoundingBox box;
        box.min[0] = -1.0f; box.min[1] = -2.0f; box.min[2] = -3.0f;
        box.max[0] = 1.0f; box.max[1] = 2.0f; box.max[2] = 3.0f;
        SC4ViewUtilities::TransformBoundingBox(transform, box);
        CHECK(Near(box.min[0], 96.0f) && Near(box.max[0], 104.0f));
        CHECK(Near(box.min[1], 18.0f) && Near(box.max[1], 22.0f));
        CHECK(Near(box.min[2], -11.0f) && Near(box.max[2], 1.0f));
        // every transformed corner stays inside the result
        for (int corner = 0; corner < 8; ++corner) {
            cS3DVector3 source;
            source.x = (corner & 1) ? 1.0f : -1.0f;
            source.y = (corner & 2) ? 2.0f : -2.0f;
            source.z = (corner & 4) ? 3.0f : -3.0f;
            cS3DVector3 world = source;
            nS3D::TransformPoints(transform, &source, &world, 1);
            CHECK(box.min[0] - 1e-3f <= world.x && world.x <= box.max[0] + 1e-3f);
            CHECK(box.min[1] - 1e-3f <= world.y && world.y <= box.max[1] + 1e-3f);
            CHECK(box.min[2] - 1e-3f <= world.z && world.z <= box.max[2] + 1e-3f);
        }

        // TrianglePick. The recovered triangle test has no t >= 0 check (RayIntersectsTriangle is a line test), so a
        // triangle behind the ray origin still "hits"; the caller's distance comparison is what orders the hits.
        cSC4PickContext context;
        context.origin = {0.0f, 10.0f, 0.0f};
        context.direction = {0.0f, -1.0f, 0.0f};
        context.maxDistance = 3.4028235e38f;
        QuadMesh quad(0.0f);
        CHECK(SC4ViewUtilities::TrianglePick(&context, &quad.vertices[0].x, &quad.vertices[1].x, &quad.vertices[2].x));
        CHECK(Near(context.maxDistance, 10.0f));
        // a closer triangle replaces the distance ...
        QuadMesh high(5.0f);
        CHECK(SC4ViewUtilities::TrianglePick(&context, &high.vertices[0].x, &high.vertices[1].x, &high.vertices[2].x));
        CHECK(Near(context.maxDistance, 5.0f));
        // ... and the farther one no longer passes
        CHECK(!SC4ViewUtilities::TrianglePick(&context, &quad.vertices[0].x, &quad.vertices[1].x, &quad.vertices[2].x));
        CHECK(Near(context.maxDistance, 5.0f));
        // a triangle above the origin is still intersected (line test quirk), 90 units away
        context.maxDistance = 3.4028235e38f;
        QuadMesh above(100.0f);
        CHECK(SC4ViewUtilities::TrianglePick(&context, &above.vertices[0].x, &above.vertices[1].x, &above.vertices[2].x));
        CHECK(Near(context.maxDistance, 90.0f));
        // a sideways miss leaves the distance untouched
        context.origin = {100.0f, 10.0f, 100.0f};
        context.maxDistance = 42.0f;
        CHECK(!SC4ViewUtilities::TrianglePick(&context, &quad.vertices[0].x, &quad.vertices[1].x, &quad.vertices[2].x));
        CHECK(context.maxDistance == 42.0f);

        // MeshPick walks the triangles of the mesh through the transform
        cS3DTransform identity;
        context.origin = {0.0f, 10.0f, 0.0f};
        context.direction = {0.0f, -1.0f, 0.0f};
        context.maxDistance = 3.4028235e38f;
        CHECK(SC4ViewUtilities::MeshPick(&context, quad, identity));
        CHECK(Near(context.maxDistance, 10.0f));
        // the transform is applied to the vertices
        cS3DTransform lifted;
        lifted.translation[1] = 100.0f;
        lifted.flags = 1;
        context.maxDistance = 3.4028235e38f;
        CHECK(SC4ViewUtilities::MeshPick(&context, quad, lifted));
        CHECK(Near(context.maxDistance, 90.0f));
        cS3DTransform sideways;
        sideways.translation[0] = 100.0f;
        sideways.flags = 1;
        context.maxDistance = 42.0f;
        CHECK(!SC4ViewUtilities::MeshPick(&context, quad, sideways));
        CHECK(context.maxDistance == 42.0f);
        // a mesh with no vertices is rejected before the iteration
        QuadMesh empty;
        empty.pickable = false;
        context.maxDistance = 42.0f;
        CHECK(!SC4ViewUtilities::MeshPick(&context, empty, identity));
        CHECK(context.maxDistance == 42.0f);

        // ModelPick ORs every mesh and keeps the closest distance
        PickModel model;
        MeshList list;
        list.count = 2;
        list.meshes[0] = &quad;
        list.meshes[1] = &high;
        context.origin = {0.0f, 10.0f, 0.0f};
        context.maxDistance = 3.4028235e38f;
        CHECK(SC4ViewUtilities::ModelPick(&context, &model, list));
        CHECK(Near(context.maxDistance, 5.0f));   // both were tested: the second mesh is the closer one
        list.count = 0;
        context.maxDistance = 3.4028235e38f;
        CHECK(!SC4ViewUtilities::ModelPick(&context, &model, list));
        list.count = 1;
        list.meshes[0] = &high;
        context.maxDistance = 3.4028235e38f;
        CHECK(SC4ViewUtilities::ModelPick(&context, &model, list));
        CHECK(Near(context.maxDistance, 5.0f));
    }

    // -----------------------------------------------------------------------------------------------------------
    // Quirks: a failed ray still reports success (NaN ground point) and the pick context is only updated on a hit
    // -----------------------------------------------------------------------------------------------------------
    {
        TestViewHost emptyHost;
        SC4ViewUtilities blind;
        blind.SetHost(&emptyHost);
        blind.SetCitySize(1024.0f, 1024.0f);
        float origin[3] = {1, 2, 3};
        float direction[3] = {4, 5, 6};
        CHECK(!blind.ViewportPositionToRay(0, 0, origin, direction));
        CHECK(origin[0] == 1.0f && direction[2] == 6.0f);   // untouched
        float hit[3] = {7, 8, 9};
        CHECK(blind.GetViewportPointIntersectionWithGroundPlane(0, 0, hit));   // the original always returns true
        CHECK(std::isnan(hit[0]) && std::isnan(hit[1]) && std::isnan(hit[2]));
        // and the same with no host at all
        SC4ViewUtilities bare;
        CHECK(!bare.ViewportPositionToRay(10, 10, origin, direction));
        CHECK(bare.GetViewportPointIntersectionWithGroundPlane(10, 10, hit));
        CHECK(std::isnan(hit[0]));
        // GetRayIntersectionWithGroundPlane: a simple ray
        const float rayOrigin[3] = {10.0f, 5.0f, -2.0f};
        const float rayDirection[3] = {0.0f, -2.0f, 4.0f};
        float rayHit[3];
        SC4ViewUtilities::GetRayIntersectionWithGroundPlane(rayOrigin, rayDirection, rayHit);
        CHECK(Near(rayHit[0], 10.0f) && Near(rayHit[1], 0.0f) && Near(rayHit[2], 8.0f));
        // RayIntersectsGroundPlane ignores the result but still writes the intersection
        SC4ViewUtilities::RayIntersectsGroundPlane(rayOrigin, rayDirection, rayHit);
        CHECK(Near(rayHit[0], 10.0f) && Near(rayHit[1], 0.0f) && Near(rayHit[2], 8.0f));

        // a fresh object without a camera is not ready and ignores UpdateCameraPosition
        SC4ViewUtilities noCamera;
        noCamera.UpdateCameraPosition(nullptr);
        CHECK(!noCamera.IsReady());
    }

    if (failures == 0) std::printf("sc4_viewutilities: all checks passed\n");
    else std::printf("sc4_viewutilities: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
