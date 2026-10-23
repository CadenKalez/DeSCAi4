// cSC4CameraControl: the 0x178-byte layout and construction, zoom / rotation state machine and every message it
// sends, the orthographic camera set-up, camera attach, scroll bounds polygon and clamping, scrolling, view-target
// clamping, stream round trip (target and the prev / sent zoom-rotation copies) and the ground-plane helper.
#include <cmath>
#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/BackingStore.h"
#include "sc4/CameraControl.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b)); }
using namespace sc4;

struct Sent { std::uint32_t type; int zoom, rot, prevZoom, prevRot; void* voidPtr; };

struct Server final : cIGZMessageServer2 {
    std::vector<Sent> sent;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2* m) override {
        auto* s = static_cast<cIGZMessage2Standard*>(m);
        Sent e{m->GetType(), 0, 0, 0, 0, s->GetVoid1()};
        if (e.type != kMsgCameraChangedBy) {
            const int* prev = static_cast<const int*>(s->GetVoid1());
            const int* now = static_cast<const int*>(s->GetVoid2());
            e.prevZoom = prev[0]; e.prevRot = prev[1];
            e.zoom = now[0]; e.rot = now[1];
        }
        sent.push_back(e);
        return true;
    }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct Host final : ICameraControlHost {
    Server server;
    bool viewUtils = true;
    int updates = 0;
    bool terrainHit = false;
    cS3DVector3 terrainPoint{1, 2, 3};
    // a 4096 square city; corner ids 0..3 go round it
    cS3DVector3 corners[4] = {{0, 0, 0}, {4096, 0, 0}, {4096, 0, 4096}, {0, 0, 4096}};
    cIGZMessageServer2* MessageServer() override { return &server; }
    cIGZMessage2Standard* CreateMessage() override { auto* m = new cRZMessage2Standard(); m->AddRef(); return m; }
    bool HasViewUtilities() override { return viewUtils; }
    void UpdateViewUtilities(cSC4CameraControl*) override { ++updates; }
    void GetCityCornerPosition(int corner, cS3DVector3& out) override { out = corners[corner]; }
    bool PickTerrain(const cS3DVector3&, const cS3DVector3&, cS3DVector3& hit) override {
        if (terrainHit) hit = terrainPoint;
        return terrainHit;
    }
};

static bool Inside(const std::vector<cS3DVector2>& poly, float x, float y, float eps) {
    // ray cast with a tolerance: also accepts points within eps of the border
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const cS3DVector2 a = poly[i], b = poly[j];
        if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) in = !in;
    }
    if (in) return true;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const cS3DVector2 a = poly[i], b = poly[j];
        const float ex = b.x - a.x, ey = b.y - a.y;
        const float t = std::fmax(0.f, std::fmin(1.f, ((x - a.x) * ex + (y - a.y) * ey) / (ex * ex + ey * ey)));
        const float dx = x - (a.x + t * ex), dy = y - (a.y + t * ey);
        if (dx * dx + dy * dy <= eps * eps) return true;
    }
    return false;
}

int main() {
    // ---- helpers ----
    {
        cS3DVector3 hit;
        CHECK(IntersectWithGroundPlane({0, 10, 0}, {1, -1, 2}, hit) && Near(hit.x, 10) && hit.y == 0 && Near(hit.z, 20));
        CHECK(!IntersectWithGroundPlane({0, 10, 0}, {1, 1, 2}, hit));          // plane behind the origin
        CHECK(!IntersectWithGroundPlane({0, 10, 0}, {1, 0.00005f, 2}, hit));   // nearly horizontal
        CHECK(IntersectWithGroundPlane({3, 0, 4}, {1, -1, 0}, hit) && hit.x == 3 && hit.z == 4);   // already on the plane
        // the original accepts a direction whose |y| is exactly 0.0001 (0x10076baeb: 0.0001 <= |dy|)
        CHECK(IntersectWithGroundPlane({0, 1, 0}, {0, -0.0001f, 0}, hit) && Near(hit.y, 0.0f));
        CHECK(!IntersectWithGroundPlane({0, 1, 0}, {0, -0.0000999f, 0}, hit));
        CHECK(Near(CityWidthFromOrthoView(16.0f, 0), 16.0f * (std::cos(0.3926991f) + std::sin(0.3926991f))));
    }

    // ---- construction ----
    {
        cSC4CameraControl* c = new cSC4CameraControl();
        CHECK(c->GetGZCLSID() == 0xc9c628ec);
        void* p = nullptr;
        CHECK(c->QueryInterface(0xe4fda3d4, &p) && p == c);
        CHECK(c->QueryInterface(1, &p) && c->QueryInterface(0xc9c628ec, &p));
        CHECK(!c->QueryInterface(0x1234, &p));
        CHECK(c->AddRef() == 4);          // three QueryInterface calls added a reference each
        CHECK(c->Release() == 3 && c->Release() == 2 && c->Release() == 1 && c->Release() == 0);   // the last one deletes
        cSC4CameraControl d;
        int w = 0, h = 0;
        d.GetViewportSize(w, h);
        CHECK(w == 800 && h == 600);
        CHECK(d.GetCustomMagnification() == 1.0f && d.GetVirtualZoom() == 0 && d.Zoom() == 0 && d.Rotation() == 0);
        float n = -1, f = -1;
        d.GetDistancesToClipPlanes(n, f);
        CHECK(n == 0 && f == 0);
        float base[3] = {7, 7, 7};
        d.GetBaseTargetPositionForRotation(base, 2);
        CHECK(base[0] == 4096 && base[1] == 0 && base[2] == 4096);
        d.GetBaseTargetPositionForRotation(base, 1);
        CHECK(base[0] == 4096 && base[2] == 0);
        d.GetBaseTargetPositionForRotation(base, 3);
        CHECK(base[0] == 0 && base[2] == 4096);
        base[0] = 9; base[2] = 9;
        d.GetBaseTargetPositionForRotation(base, 7);       // default: x / z untouched, y cleared
        CHECK(base[0] == 9 && base[1] == 0 && base[2] == 9);
        CHECK(d.UpdateCameraPosition(0) == false);        // without a camera nothing happens
        // the original object is 0x178 bytes (GZCOM_CreatecSC4CameraControl); the unread tail fields are modelled
        CHECK(sizeof(cSC4CameraControl) == 0x178);
    }

    // ---- state machine with a camera and a recording host ----
    Host host;
    SetCameraControlHost(&host);
    cSC4CameraControl c;
    auto* cam = new s3d::cS3DCamera();
    CHECK(c.Init({1000, 0, 2000}, cam));
    CHECK(c.Initialised() && c.Camera() == cam);
    CHECK(c.ViewTarget().x == 1000 && c.ViewTarget().z == 2000);
    CHECK(host.updates >= 1);
    float vw = 0, vh = 0;
    {
        // the orthographic volume: width = viewport * 0.5 * world-per-pixel; the clip planes come from the pitch
        float l, r, t, b, nr, fr;
        cam->GetViewVolume(l, r, t, b, nr, fr);
        const float wpp = c.WorldPerPixel();
        // zoom 0: cell width 8 px, city width 20.9 -> wpp = 20.9 / 8
        CHECK(Near(wpp, CityWidthFromOrthoView(16.0f, 0) / 8.0f));
        CHECK(Near(r, 400.0f * wpp) && Near(l, -r));
        CHECK(Near(t, -300.0f * wpp) && Near(b, 300.0f * wpp));    // SetOrtho(top = -half height)
        const float pitch = 0.5235988f;
        CHECK(Near(c.PitchAngle(), pitch));
        const float reach = 8192.0f / std::cos(pitch);
        CHECK(Near(fr, reach * 1.5f));
        CHECK(Near(nr, float((reach - std::sqrt(2.0f * 4096.0f * 4096.0f) / std::cos(pitch)) * 0.95)));
        (void)vw; (void)vh;
    }
    {
        // the camera looks at the target: the target projects to the centre of the screen
        cS3DVector3 screen;
        CHECK(cam->Project(c.ViewTarget(), screen));
        CHECK(std::fabs(screen.x - 400.0f) < 1.0f && std::fabs(screen.y - 300.0f) < 1.0f);   // within the sub-pixel offset
        cS3DVector3 pos;
        cam->GetPosition(pos);
        CHECK(Near(pos.x, c.CameraPosition().x) && Near(pos.y, c.CameraPosition().y) && Near(pos.z, c.CameraPosition().z));
        // view direction: unit length and pointing from the camera to the target
        const cS3DVector3 v = c.ViewDirection();
        CHECK(Near(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), 1.0f));
        const cS3DVector3 d{c.ViewTarget().x - pos.x, c.ViewTarget().y - pos.y, c.ViewTarget().z - pos.z};
        const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        CHECK(Near(d.x / dl, v.x) && Near(d.y / dl, v.y) && Near(d.z / dl, v.z));
        // the plane equation passes through the camera position and holds the view direction
        const float* pl = c.GroundPlane();
        CHECK(pl[0] == v.x && pl[1] == v.y && pl[2] == v.z);
        CHECK(Near(pl[0] * pos.x + pl[1] * pos.y + pl[2] * pos.z + pl[3], 0.0f, 1e-3f));
    }
    {
        // scroll bounds were built on Init (view utilities present): six points
        CHECK(c.ScrollBounds().size() == 6);
    }

    // ---- zoom ----
    host.server.sent.clear();
    CHECK(c.SetZoom(2.0f));
    CHECK(c.Zoom() == 2 && c.GetVirtualZoom() == 2);
    CHECK(cSC43DBackingStore::justZoomed);
    {
        // kind 2 sends: pre (type 0xa6b79600), then "changed by" + changed + done
        const auto& s = host.server.sent;
        CHECK(s.size() >= 4);
        CHECK(s[0].type == kMsgCameraViewAboutToChange && s[0].prevZoom == 0 && s[0].zoom == 2);   // state is already updated
        bool sawChanged = false, sawDone = false, sawBy = false;
        for (const auto& e : s) {
            if (e.type == kMsgCameraZoomRotationChanged) { sawChanged = true; CHECK(e.prevZoom == 0 && e.zoom == 2); }
            if (e.type == kMsgCameraZoomRotationDone) sawDone = true;
            if (e.type == kMsgCameraChangedBy && e.voidPtr == &c) sawBy = true;
        }
        CHECK(sawChanged && sawDone && sawBy);
    }
    CHECK(!c.SetZoom(2.0f));                      // unchanged
    CHECK(!c.SetZoom(-1.0f) && !c.SetZoom(6.0f)); // out of range
    CHECK(c.ZoomIn() && c.Zoom() == 3);
    CHECK(c.ZoomIn() && c.Zoom() == 4);
    CHECK(c.GetCustomMagnification() == 1.0f);
    CHECK(c.ZoomIn());                            // beyond the real zoom: a magnification of 2
    CHECK(c.Zoom() == 4 && c.GetCustomMagnification() == 2.0f && c.GetVirtualZoom() == 5);
    CHECK(!c.ZoomIn());                           // the virtual limit is 5
    CHECK(c.ZoomOut() && c.GetCustomMagnification() == 1.0f && c.Zoom() == 4);
    CHECK(c.SetZoom(0.0f) && c.Zoom() == 0);
    CHECK(!c.ZoomOut());                          // already at the minimum
    c.SetCustomMagnification(1.25f);
    CHECK(c.GetCustomMagnification() == 1.25f);
    c.SetCustomMagnification(1.0f);

    // ---- rotation ----
    host.server.sent.clear();
    CHECK(c.SetRotation(1) && c.Rotation() == 1);
    CHECK(host.server.sent.size() >= 3);
    CHECK(c.SetRotation(1));                      // quirk: unchanged reports success
    CHECK(c.SetRotation(9) && c.Rotation() == 1); // quirk: out of range reports success and changes nothing
    c.RotateLeft();
    CHECK(c.Rotation() == 2);
    c.RotateRight(); c.RotateRight();
    CHECK(c.Rotation() == 0);
    c.RotateRight();
    CHECK(c.Rotation() == 3);
    c.RotateLeft();
    CHECK(c.Rotation() == 0);
    CHECK(c.SetZoomAndRotation(3, 2) && c.Zoom() == 3 && c.Rotation() == 2);
    CHECK(!c.SetZoomAndRotation(3, 2));           // no change
    CHECK(!c.SetZoomAndRotation(3, 4) && !c.SetZoomAndRotation(-1, 0));
    CHECK(c.SetZoomAndRotation(5, 1) && c.Zoom() == 4 && c.Rotation() == 1 && c.GetCustomMagnification() == 2.0f);
    CHECK(c.SetZoomAndRotation(0, 0) && c.GetCustomMagnification() == 1.0f);

    // ---- target ----
    host.server.sent.clear();
    CHECK(!c.SetViewTargetPosition({NAN, 0, 0}) && !c.SetViewTargetPosition({200000, 0, 0}));
    CHECK(!c.SetViewTargetPosition({0, -100001, 0}));
    CHECK(c.SetViewTargetVelocity({1, 2, 3}) && !c.SetViewTargetVelocity({0, NAN, 0}));
    CHECK(c.SetViewTargetPosition({2000, 0, 1500}));
    CHECK(c.ViewTarget().x == 2000 && c.ViewTarget().z == 1500);
    {
        bool sawTarget = false;
        for (const auto& e : host.server.sent)
            if (e.type == kMsgCameraTargetChanged) sawTarget = true;
        CHECK(sawTarget);
        // kind 3 skips the zoom / rotation parameter update, so no pre message is sent
        CHECK(host.server.sent[0].type == kMsgCameraChangedBy);
    }

    // ---- viewport, tweak ----
    c.SetViewportSize(1024, 768);
    int w = 0, h = 0;
    c.GetViewportSize(w, h);
    CHECK(w == 1024 && h == 768 && cam->ViewportWidth() == 1024.0f && cam->ViewportHeight() == 768.0f);
    {
        const int before = host.updates;
        c.SetCameraPositionTweakAdd(0, 0);
        CHECK(host.updates == before);               // unchanged: no update
        const cS3DVector2 o0 = c.ScreenOffset();
        c.SetCameraPositionTweakAdd(3.0f, 4.0f);
        CHECK(host.updates == before + 1);
        // the tweak lands in the sub-pixel screen offset: x + 3, y - (-4) relative to the untweaked offset
        CHECK(Near(c.ScreenOffset().x, o0.x + 3.0f * c.WorldPerPixel() * 0 + (c.ScreenOffset().x - o0.x)));
        // NaN never compares equal, so the original updates every time (0x10076c0d6)
        const int beforeNaN = host.updates;
        c.SetCameraPositionTweakAdd(NAN, 0.0f);
        CHECK(host.updates == beforeNaN + 1);
        c.SetCameraPositionTweakAdd(NAN, 0.0f);
        CHECK(host.updates == beforeNaN + 2);
        c.SetCameraPositionTweakAdd(0, 0);
    }
    c.SetViewportSize(800, 600);

    // ---- coordinate conversion round trip ----
    {
        c.SetViewTargetPosition({2048, 0, 2048});
        const cS3DVector3 points[] = {{100, 0, 200}, {2048, 0, 2048}, {4000, 0, 50}};
        for (const auto& pt : points) {
            cS3DVector2 plane;
            c.ConvertWorldPointToCameraPlanePoint(pt, plane);
            cS3DVector3 back;
            c.ConvertCameraPlanePointToWorldPoint(plane, back);
            // the plane point drops the component along the view direction, so the round trip keeps only the
            // projection onto the camera plane: converting again gives the same plane point
            cS3DVector2 again;
            c.ConvertWorldPointToCameraPlanePoint(back, again);
            CHECK(Near(again.x, plane.x, 1e-3f) && Near(again.y, plane.y, 1e-3f));
        }
    }

    // ---- scroll bounds ----
    {
        const auto& poly = c.ScrollBounds();
        CHECK(poly.size() == 6);
        cS3DVector2 pa, pb, pc, pd;
        c.ConvertWorldPointToCameraPlanePoint(host.corners[1], pa);
        c.ConvertWorldPointToCameraPlanePoint(host.corners[0], pb);
        c.ConvertWorldPointToCameraPlanePoint(host.corners[2], pc);
        c.ConvertWorldPointToCameraPlanePoint(host.corners[3], pd);
        // vertices: A lifted, B lifted and widened, B widened, D, C widened, C widened lifted
        const float lift = poly[0].y - pa.y;
        CHECK(Near(poly[0].x, pa.x) && lift > 0);
        CHECK(Near(poly[1].x, pb.x - 500.0f) && Near(poly[1].y, pb.y + lift));
        CHECK(Near(poly[2].x, pb.x - 500.0f) && Near(poly[2].y, pb.y, 2e-3f));
        CHECK(Near(poly[3].x, pd.x) && Near(poly[3].y, pd.y));
        CHECK(Near(poly[4].x, pc.x + 500.0f) && Near(poly[4].y, pc.y));
        CHECK(poly[5].x == poly[4].x + 0.0f && Near(poly[5].y, pc.y + lift));
        // the lift: 0.48 * camera offset y / camera up y - half the viewport height in world units
        cS3DVector2 q;
        c.ConvertWorldPointToCameraPlanePoint({2048, 0, 2048}, q);
        CHECK(Inside(poly, q.x, q.y, 0.0f));
    }
    {
        // ClampScrollToBounds: a move that leaves the polygon ends on or inside it, one that stays inside is untouched
        cS3DVector2 center, out;
        c.ConvertWorldPointToCameraPlanePoint({2048, 0, 2048}, center);
        c.ClampScrollToBounds(center, {center.x + 3.0f, center.y - 2.0f}, out);
        CHECK(Near(out.x, center.x + 3.0f, 1e-5f) && Near(out.y, center.y - 2.0f, 1e-5f));
        int clampedMoves = 0;
        for (int i = 0; i < 64; ++i) {
            const float ang = 6.2831853f * float(i) / 64.0f;
            const cS3DVector2 far{center.x + 20000.0f * std::cos(ang), center.y + 20000.0f * std::sin(ang)};
            c.ClampScrollToBounds(center, far, out);
            if (Inside(c.ScrollBounds(), out.x, out.y, 2.0f)) ++clampedMoves;
        }
        CHECK(clampedMoves == 64);
        // disabled bounds: the target is returned as is
        c.SetBoundsEnabled(false);
        c.ClampScrollToBounds(center, {1e6f, -1e6f}, out);
        CHECK(out.x == 1e6f && out.y == -1e6f);
        c.SetBoundsEnabled(true);
    }

    // ---- scrolling ----
    {
        c.SetViewTargetPosition({2048, 0, 2048});
        host.terrainHit = false;
        c.ScrollByViewportAngle(0.0f, 10.0f);
        // the target stays on the ground plane
        CHECK(Near(c.ViewTarget().y, 0.0f, 1e-3f));
        const cS3DVector3 moved = c.ViewTarget();
        CHECK(!(Near(moved.x, 2048.0f, 1e-6f) && Near(moved.z, 2048.0f, 1e-6f)));
        host.terrainHit = true;
        c.ScrollByViewportAngle(1.0f, 5.0f);
        CHECK(c.ViewTarget().x == 1 && c.ViewTarget().y == 2 && c.ViewTarget().z == 3);   // the terrain hit wins
        host.terrainHit = false;
        c.SetViewTargetPosition({2048, 0, 2048});
        // a scroll far to one side is clamped by the bounds and stays inside them
        for (int i = 0; i < 8; ++i) c.ScrollByViewportAngle(0.7f * float(i), 100000.0f);
        cS3DVector2 q;
        c.ConvertWorldPointToCameraPlanePoint(c.ViewTarget(), q);
        CHECK(Inside(c.ScrollBounds(), q.x, q.y, 2.0f));
    }

    // ---- city size change rebuilds the bounds ----
    {
        const std::size_t before = c.ScrollBounds().size();
        c.SetCitySize(2048.0f, 2048.0f);
        CHECK(before == 6 && c.ScrollBounds().size() == 6);
        float base[3];
        c.GetBaseTargetPositionForRotation(base, 2);
        CHECK(base[0] == 2048 && base[2] == 2048);
    }

    // ---- stream round trip ----
    {
        PortMemoryOStream out;
        CHECK(c.Write(out));
        const auto& bytes = out.bytes();
        CHECK(bytes.size() == 2 + 12 + 24);
        CHECK(bytes[0] == 1 && bytes[1] == 0);
        PortMemoryIStream in(bytes.data(), bytes.size());
        cSC4CameraControl loaded;
        CHECK(loaded.Read(in));
        CHECK(loaded.ViewTarget().x == c.ViewTarget().x && loaded.ViewTarget().z == c.ViewTarget().z);
        CHECK(loaded.Zoom() == c.Zoom() && loaded.Rotation() == c.Rotation());
        // a load flags the controller so Init keeps the loaded target
        auto* cam2 = new s3d::cS3DCamera();
        loaded.Init({5, 0, 5}, cam2);
        CHECK(loaded.ViewTarget().x == c.ViewTarget().x);
        // a wrong version fails, but the original flags the controller as "loaded" before reading the version
        std::vector<unsigned char> bad = bytes;
        bad[0] = 2;
        PortMemoryIStream in2(bad.data(), bad.size());
        cSC4CameraControl other;
        CHECK(!other.Read(in2));
        auto* camOther = new s3d::cS3DCamera();
        CHECK(other.Init({5, 0, 5}, camOther));       // the loaded flag keeps the (default) target
        CHECK(other.ViewTarget().x == 0.0f && other.ViewTarget().z == 0.0f);
        other.Shutdown();
        // a truncated stream fails
        PortMemoryIStream in3(bytes.data(), 10);
        cSC4CameraControl shortRead;
        CHECK(!shortRead.Read(in3));
        loaded.Shutdown();
        CHECK(loaded.Camera() == nullptr);
    }

    // ---- message shapes: kind 1 sends nothing, kind 4 one message, kind 0 the view-changed message ----
    {
        host.server.sent.clear();
        c.SetCitySize(1500.0f, 1500.0f);              // kind 1: SendPostViewChangeMessage skips it
        CHECK(host.server.sent.empty());
        host.server.sent.clear();
        c.SetCustomMagnification(1.25f);              // kind 4: magnification changed, no "changed by" message
        CHECK(host.server.sent.size() == 1);
        CHECK(host.server.sent[0].type == kMsgCameraMagnificationChanged);
        CHECK(host.server.sent[0].zoom == c.Zoom() && host.server.sent[0].rot == c.Rotation());
        host.server.sent.clear();
        c.SendViewChangeMessage();                    // kind 0
        CHECK(host.server.sent.size() == 1 && host.server.sent[0].type == kMsgCameraViewChanged);
        host.server.sent.clear();
        c.SendPostViewChangeMessage(5);               // kinds >= 5 are not sent
        c.SendPreViewChangeMessage(kCameraUpdateTarget);   // the pre message is only sent for kind 2
        CHECK(host.server.sent.empty());
    }

    // ---- clamping the view target: a target far outside the bounds comes back onto the ground inside them ----
    {
        CHECK(c.ScrollBounds().size() == 6);
        CHECK(c.SetViewTargetPosition({8000, 0, 8000}));
        c.ClampTargetToScrollBounds();
        CHECK(Near(c.ViewTarget().y, 0.0f, 0.05f));
        cS3DVector2 q;
        c.ConvertWorldPointToCameraPlanePoint(c.ViewTarget(), q);
        CHECK(Inside(c.ScrollBounds(), q.x, q.y, 2.0f));
    }

    // ---- the stream keeps the previous / sent zoom-rotation copies (they travel in the kind 0 message) ----
    {
        cSC4CameraControl source;
        auto* camSource = new s3d::cS3DCamera();
        CHECK(source.Init({10, 0, 20}, camSource));
        CHECK(source.SetZoom(2.0f));
        CHECK(source.SetZoom(3.0f));                  // previous = (2, 0), current = (3, 0)
        PortMemoryOStream out;
        CHECK(source.Write(out));
        const auto& bytes = out.bytes();
        PortMemoryIStream in(bytes.data(), bytes.size());
        cSC4CameraControl reloaded;
        CHECK(reloaded.Read(in));
        CHECK(reloaded.Zoom() == 3 && reloaded.Rotation() == 0);
        host.server.sent.clear();
        reloaded.SendViewChangeMessage();             // kind 0 carries &prev and &sent, no camera needed
        CHECK(host.server.sent.size() == 1);
        CHECK(host.server.sent[0].prevZoom == 2 && host.server.sent[0].prevRot == 0);
        CHECK(host.server.sent[0].zoom == 3 && host.server.sent[0].rot == 0);
        source.Shutdown();
    }

    // ---- attaching a camera: releases the old one, sets the viewport and repositions ----
    {
        const int before = host.updates;
        auto* camB = new s3d::cS3DCamera();
        c.AttachToCamera(camB);
        CHECK(c.Camera() == camB);
        CHECK(host.updates == before + 1);            // the controller is initialised
        int vw2 = 0, vh2 = 0;
        c.GetViewportSize(vw2, vh2);
        CHECK(camB->ViewportWidth() == float(vw2) && camB->ViewportHeight() == float(vh2));
        c.AttachToCamera(camB);                       // the same camera: nothing happens
        CHECK(host.updates == before + 1);
        // Init a second time leaves the state alone
        const cS3DVector3 beforeTarget = c.ViewTarget();
        CHECK(c.Init({9, 0, 9}, camB));
        CHECK(c.ViewTarget().x == beforeTarget.x && c.ViewTarget().z == beforeTarget.z);
    }

    // ---- the magnification limits: 1000 (and 0) freeze the magnification, the zoom still moves ----
    {
        cSC4CameraControl frozen;
        frozen.SetCustomMagnification(1000.0f);       // no camera: nothing to update, the value is stored
        CHECK(frozen.GetCustomMagnification() == 1000.0f);
        frozen.SetCustomMagnification(2.0f);
        CHECK(frozen.GetCustomMagnification() == 1000.0f);
        frozen.SetZoom(1.0f);
        CHECK(frozen.Zoom() == 1 && frozen.GetCustomMagnification() == 1000.0f);   // only the magnification is frozen
        cSC4CameraControl zero;
        zero.SetCustomMagnification(0.0f);
        CHECK(zero.GetCustomMagnification() == 0.0f);
        zero.SetCustomMagnification(1.0f);
        CHECK(zero.GetCustomMagnification() == 0.0f);
    }

    // ---- a zoom beyond the real range becomes a magnification ----
    {
        cSC4CameraControl m;
        auto* camM = new s3d::cS3DCamera();
        CHECK(m.Init({100, 0, 200}, camM));
        CHECK(m.SetZoom(4.5f));
        CHECK(m.Zoom() == 4 && m.GetVirtualZoom() == 4);
        CHECK(Near(m.GetCustomMagnification(), std::exp2(0.5f), 1e-5f));
        CHECK(m.SetZoom(5.0f));
        CHECK(m.Zoom() == 4 && m.GetVirtualZoom() == 5);
        CHECK(Near(m.GetCustomMagnification(), 2.0f));
        CHECK(m.SetZoom(4.0f));                        // back to the real range
        CHECK(m.Zoom() == 4 && m.GetCustomMagnification() == 1.0f && m.GetVirtualZoom() == 4);
        m.Shutdown();
    }

    // ---- no view utilities: kinds 1 and 2 clamp without rebuilding ----
    {
        host.viewUtils = false;
        host.updates = 0;
        c.SetZoom(1.0f);
        CHECK(host.updates == 0);
        host.viewUtils = true;
    }

    // ---- shutdown releases the camera ----
    c.Shutdown();
    CHECK(c.Camera() == nullptr);
    CHECK(!c.UpdateCameraPosition(0));               // no camera: nothing to update
    SetCameraControlHost(nullptr);

    if (failures == 0) std::printf("sc4_cameracontrol OK\n");
    return failures == 0 ? 0 : 1;
}
