#include <cmath>
#include <cstdio>
#include "s3d/Camera.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 2e-4f) { return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b)); }
using namespace s3d;
int main() {
    cS3DCamera cam;
    // defaults: 100x100 viewport, ortho box [-1,1]x[1,-1] (y flipped), depth 1..2
    const float* m = cam.ProjectionMatrix();
    CHECK(m[0] == 1.0f && m[5] == 1.0f && m[10] == -2.0f && m[15] == 1.0f && m[3] == 0.0f && m[7] == 0.0f && m[11] == -3.0f);
    const float* inv = cam.InverseProjectionMatrix();
    CHECK(inv[0] == 1.0f && inv[5] == 1.0f && inv[10] == -0.5f && inv[11] == -1.5f && inv[15] == 1.0f);
    cS3DVector3 s;
    CHECK(cam.Project({0, 0, 0}, s));
    CHECK(Near(s.x, 50.0f) && Near(s.y, 50.0f));
    CHECK(cam.Project({1, 1, -1}, s) && Near(s.x, 100.0f) && Near(s.y, 0.0f));         // +y is up on screen
    cS3DVector3 w;
    CHECK(cam.UnProject({75.0f, 25.0f, 0.0f}, w));
    CHECK(Near(w.x, 0.5f) && Near(w.y, 0.5f));

    // position / look-at / view transform
    cam.SetPosition({10, 20, 30});
    CHECK(cam.LocalXform().flags == 1 && cam.LocalXform().changeCounter == 1);
    cam.LookAt({0, 0, 0}, {0, 1, 0});
    CHECK(cam.LocalXform().flags == 5 && cam.LocalXform().changeCounter == 3);
    cS3DVector3 la;
    cam.GetLookAt(la);
    CHECK(la.x == 0 && la.y == 0 && la.z == 0);
    cS3DVector3 pos;
    cam.GetPosition(pos);
    CHECK(pos.x == 10 && pos.y == 20 && pos.z == 30);
    // the camera's own position projects to the screen centre; the look-at point lies on the optical axis
    cam.SetOrtho(-50, 50, 50, -50, 0.1f, 1000.0f);
    CHECK(cam.GetProjectionType() == kProjectionOrtho);
    cS3DVector3 sc;
    CHECK(cam.Project({0, 0, 0}, sc) && Near(sc.x, 50.0f, 1e-3f) && Near(sc.y, 50.0f, 1e-3f));
    // round trip through project / unproject for arbitrary points
    for (int i = 0; i < 100; ++i) {
        cS3DVector3 p{float((i * 37) % 41 - 20), float((i * 53) % 29 - 14), float((i * 17) % 31 - 15)}, q, back;
        CHECK(cam.Project(p, q));
        CHECK(cam.UnProject(q, back));
        CHECK(Near(back.x, p.x, 1e-3f) && Near(back.y, p.y, 1e-3f) && Near(back.z, p.z, 1e-3f));
    }
    // view volume with a viewport offset
    cam.SetViewport(200.0f, 100.0f);
    cam.SetViewportOffset({10.0f, 5.0f});
    float l, r, t, b, n, f;
    cam.GetViewVolume(l, r, t, b, n, f);
    CHECK(Near(l, -50 + 100 * 10.0f / 200) && Near(r, 50 + 100 * 10.0f / 200) && Near(t, 50 - (50 - -50) * 5.0f / 100) && Near(b, -50 - 100 * 5.0f / 100));
    CHECK(n == 0.1f && f == 1000.0f);

    // sub-view maps the viewport rectangle: projecting the view centre lands in the middle of the sub-view
    cS3DCamera sub;
    sub.SetViewport(400.0f, 300.0f);
    sub.SetOrtho(-1, 1, 1, -1, 0, 10);
    sub.SetSubview(100.0f, 100.0f, 200.0f, 100.0f);      // vertically centred, so the view centre stays at the viewport centre
    cS3DVector3 sp;
    CHECK(sub.Project({0, 0, -5}, sp));
    CHECK(Near(sp.x, 200.0f) && Near(sp.y, 150.0f));
    sub.SetSubview(100.0f, 50.0f, 200.0f, 100.0f);
    CHECK(sub.Project({0, 0, -5}, sp) && Near(sp.y, 50.0f));      // a non-centred sub-view shifts y (as in the original)
    sub.ClearSubview();
    CHECK(sub.Project({0, 0, -5}, sp) && Near(sp.x, 200.0f) && Near(sp.y, 150.0f));
    const std::uint32_t stateBefore = sub.GetProjectionState();
    sub.SetDepthSize(1.0f);
    CHECK(sub.GetProjectionState() == stateBefore + 1);

    // perspective: a point on the optical axis projects to the centre, w from -z
    cS3DCamera persp;
    persp.SetViewport(640.0f, 480.0f);
    persp.SetOrtho(-0.5f, 0.5f, 0.375f, -0.375f, 1.0f, 100.0f);
    persp.SetProjectionType(kProjectionPerspective);
    const float* pm = persp.ProjectionMatrix();
    CHECK(Near(pm[0], 2.0f) && Near(pm[5], -2.0f / 0.75f));
    CHECK(pm[14] == -1.0f && pm[15] == 0.0f);
    CHECK(persp.InverseProjectionMatrix()[15] == 0.0f);       // the original's overwritten element
    cS3DVector3 ps;
    CHECK(persp.Project({0, 0, -10}, ps) && Near(ps.x, 320.0f) && Near(ps.y, 240.0f));
    CHECK(persp.Project({0.5f * 10, 0, -10}, ps) && Near(ps.x, 640.0f, 1e-3f));    // frustum edge at distance 10
    CHECK(!persp.Project({0, 0, 0}, ps));                                           // w == 0
    cS3DVector3 origin, dir;
    CHECK(persp.GetEyeRay({320.0f, 240.0f}, origin, dir));
    CHECK(Near(dir.x, 0.0f) && Near(dir.y, 0.0f) && Near(dir.z, 1.0f));      // eye-ray direction points from the scene back to the eye

    // MakeIdentity resets a placed camera and bumps counters
    cam.MakeIdentity();
    CHECK(cam.LocalXform().flags == 0 && cam.LocalXform().translation[0] == 0.0f);
    CHECK(cam.ViewXform().flags == 0);
    CHECK(cam.AddRef() == 1 && cam.AddRef() == 2 && cam.Release() == 1 && cam.Release() == 0);

    // eye ray through the screen centre points from the eye towards the look-at target (ortho)
    cS3DCamera eye;
    eye.SetViewport(100.0f, 100.0f);
    eye.SetOrtho(-10, 10, 10, -10, 0.1f, 500.0f);
    eye.SetPosition({0, 0, 100});
    eye.LookAt({0, 0, 0}, {0, 1, 0});
    CHECK(eye.GetEyeRay({50.0f, 50.0f}, origin, dir));
    CHECK(Near(dir.x, 0.0f, 1e-3f) && Near(dir.y, 0.0f, 1e-3f) && Near(dir.z, -500.0f, 1e-3f));
    if (failures) return 1;
    std::printf("s3d_camera: all passed\n");
}
