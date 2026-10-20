// terrain_shot: renders a procedural terrain through cSTETerrainView3D into the native Metal driver (headless) and writes
// work/shots/terrain_shot.png. A quick end-to-end check that the recovered view drives the recovered renderer; it is not the
// game boot path (that still needs the city component classes, see notes/boot path plan).
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "render/gd/MetalGDriver.h"
#include "sc4/PackedRenderState.h"
#include "sc4/cSTETerrain.h"
#include "sc4/cSTETerrainView3D.h"

using namespace sc4;
constexpr std::uint32_t W = 1280, H = 720;

struct Buf : IPixelBuffer {
    int w, h; std::uint32_t c;
    Buf(int w_, int h_, std::uint32_t c_) : w(w_), h(h_), c(c_) {}
    int Width() const override { return w; }
    int Height() const override { return h; }
    std::uint32_t Pixel(int, int) const override { return c; }
};

struct Host : ITerrainViewHost {
    std::vector<std::uint32_t> none;
    const std::vector<std::uint32_t>& TexturesUsedInTerrainType(int) const override { return none; }
    std::shared_ptr<IPixelBuffer> GetBuffer(std::uint32_t id, int) override {
        if (id == 0xc9efb727) return std::make_shared<Buf>(1, 8, 0x1c4a7a);       // water box gradient
        if (id == 0x69efb6f8) return std::make_shared<Buf>(1, 8, 0x2f7fb8);       // water colour by depth
        if (id == 0x69efb7da) return std::make_shared<Buf>(8, 8, 0);              // glare mask
        return std::make_shared<Buf>(0x101, 1, 127);                              // city-edge displacement maps
    }
    bool HasTuningExemplar() override { return true; }
    bool ReadTuningFloat(std::uint32_t id, float& v) override {
        if (id == 0x29ab15c0) { v = 14.0f; return true; }
        if (id == 0x29ab15c1) { v = 90.0f; return true; }
        return false;
    }
    bool ReadTuningFloatVector(std::uint32_t id, std::vector<float>& v) override {
        if (id == 0x29ab15c6) { v = {10.0f, 30.0f}; return true; }
        return false;
    }
    bool HasLighting() const override { return true; }
};

// Row-vector (D3D) matrices.
static void LookAtLH(const float eye[3], const float at[3], float m[16]) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]); for (float& v : z) v /= zl;
    float x[3] = {z[2], 0.0f, -z[0]};                                           // up (0,1,0) x z
    float xl = std::sqrt(x[0] * x[0] + x[2] * x[2]); for (float& v : x) v /= xl;
    float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float M[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                         -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    std::memcpy(m, M, sizeof M);
}
static void PerspectiveLH(float fovY, float aspect, float zn, float zf, float m[16]) {
    const float ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect;
    const float M[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    std::memcpy(m, M, sizeof M);
}

struct DC : ITerrainDrawContext {
    gd::cIGZGDriver* drv; SC4RenderStateCache cache; TerrainFrustumPlanes fp; int pass = 0;
    explicit DC(gd::cIGZGDriver* d) : drv(d), cache(d, SC4DeviceFlags{}) {
        fp.minZ = -1e5f; fp.maxZ = 1e5f; fp.leftOrigin[0] = -1e5f; fp.leftDir[1] = 1; fp.rightOrigin[0] = 1e5f; fp.rightDir[1] = 1;
        fp.planeB = 1; fp.planeD = 1e7f;
    }
    int Zoom() const override { return 5; }
    int Rotation() const override { return 0; }
    int Pass() const override { return pass; }
    const TerrainFrustumPlanes& DrawFrustum() const override { return fp; }
    const TerrainFrustumPlanes& ViewFrustum() const override { return fp; }
    void GetVisibleEdges(int& a, int& b) const override { a = 6; b = 5; }          // -> edges 0 and 1 (the +z and +x faces)
    void SetRenderState(const cSC4PackedRenderState& s) override { cache.SetRenderState(s); drv->Disable(gd::state::Texturing); drv->Disable(gd::state::CullFace); }
    void EnableTextureStateFlag(bool, int) override { drv->Disable(gd::state::Texturing); }
    void EnableBlendStateFlag(bool on) override { on ? drv->Enable(gd::state::Blend) : drv->Disable(gd::state::Blend); }
    void SetBlendFunc(int s, int d) override { drv->BlendFunc(s, d); }
    void EnableCullFaceFlag(bool) override { drv->Disable(gd::state::CullFace); }   // demo camera is not the game camera: no culling
    void EnableDepthMaskFlag(bool on) override { drv->DepthMask(on); }
    void SetDepthFunc(int f) override { drv->DepthFunc(f); }
    // vertex formats: 0xb = V3F_C4UB_2T2F (32 B), 10 = V3F_C4UB_T2F (24 B), 1 = x,y,z,rgba (16 B, widened here)
    int format = 0; const void* data = nullptr; std::vector<std::uint8_t> widened; int count = 0;
    void SetVertexBuffer(int f, int n, const void* d) override { format = f; data = d; count = n; widened.clear(); }
    void SetIndexBuffer(int, const std::uint16_t* i) override { indices = i; }
    const std::uint16_t* indices = nullptr;
    void Bind() {
        if (format == 1) {
            widened.assign(static_cast<std::size_t>(count) * 24, 0);
            for (int i = 0; i < count; ++i) {
                std::memcpy(&widened[static_cast<std::size_t>(i) * 24], static_cast<const std::uint8_t*>(data) + i * 16, 16);
            }
            drv->InterleavedArrays(10, 24, widened.data());
        } else {
            drv->InterleavedArrays(format == 0xb ? 11 : 10, format == 0xb ? 32 : 24, data);
        }
    }
    void DrawPrimsIndexed(int, int first, int n) override { Bind(); drv->DrawElements(gd::prim::Triangles, n, 3, indices + first); }
    void DrawPrims(int mode, int fmt, int n, const void* d) override {
        format = fmt; data = d; count = n; Bind();
        drv->DrawArrays(mode == 6 ? gd::prim::Quads : gd::prim::Triangles, 0, n);
    }
};

int main() {
    auto drv = gd::cMetalGDriver::Create(nullptr, W, H);
    if (!drv) { std::puts("no Metal device"); return 2; }
    Host host;
    cSTETerrainView3D::StaticInit(&host);
    cSTETerrain terrain;
    TerrainTuning tuning; tuning.seaLevel = 38.0f;
    const int N = 64;
    terrain.Init(N, N, tuning);
    for (int z = 0; z <= N; ++z)
        for (int x = 0; x <= N; ++x) {
            const float fx = x / float(N), fz = z / float(N);
            float h = 30.0f + 45.0f * std::sin(fx * 5.0f) * std::cos(fz * 4.0f) + 25.0f * std::sin(fx * 13.0f + fz * 9.0f)
                      + 60.0f * std::exp(-((fx - 0.7f) * (fx - 0.7f) + (fz - 0.3f) * (fz - 0.3f)) * 25.0f)
                      - 70.0f * std::exp(-((fx - 0.3f) * (fx - 0.3f) + (fz - 0.65f) * (fz - 0.65f)) * 30.0f);
            auto& v = terrain.Vertices()[terrain.GetVertIndex(x, z)];
            v.altitude = h;
            std::uint8_t r, g, b;                                                  // colour A: height ramp
            if (h < 40) { r = 194; g = 178; b = 128; }
            else if (h < 70) { r = 70 + int(h - 40); g = 130 + int((h - 40) * 0.5f); b = 60; }
            else if (h < 100) { r = 120 + int(h - 70); g = 100 + int(h - 70); b = 70; }
            else { r = g = b = 235; }
            v.surface[3] = r; v.surface[4] = g; v.surface[5] = b;
        }
    terrain.RecomputeWaterLevel({0, 0, N, N});
    cSTETerrainView3D view(&host);
    view.Init(&terrain);
    DC dc(drv.get());
    const float eye[3] = {1250.0f, 700.0f, 1250.0f}, at[3] = {480.0f, 20.0f, 480.0f};
    float mv[16], proj[16];
    LookAtLH(eye, at, mv);
    PerspectiveLH(0.8f, float(W) / H, 10.0f, 6000.0f, proj);
    drv->ClearColor(0.55f, 0.72f, 0.92f, 1.0f);
    drv->ClearDepth(1.0);
    drv->Clear(gd::kClearColor | gd::kClearDepth);
    drv->SetViewport(0, 0, W, H);
    drv->MatrixMode(0); drv->LoadMatrix(mv);
    drv->MatrixMode(1); drv->LoadMatrix(proj);
    drv->Enable(gd::state::DepthTest);
    dc.pass = 0; view.Update(dc);
    dc.pass = 1; view.Update(dc);
    std::vector<std::uint8_t> bgra; std::uint32_t w = 0, h = 0;
    drv->ReadBack(bgra, w, h);
    for (auto& [name, n] : drv->UnsupportedCalls()) std::printf("unsupported %s x%d\n", name.c_str(), n);
    std::printf("draw calls: %u\n", drv->DrawCalls());
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(bgra.data(), w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    [[NSFileManager defaultManager] createDirectoryAtPath:@"work/shots" withIntermediateDirectories:YES attributes:nil error:nil];
    NSURL* url = [NSURL fileURLWithPath:@"work/shots/terrain_shot.png"];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    std::puts("wrote work/shots/terrain_shot.png");
    return 0;
}
