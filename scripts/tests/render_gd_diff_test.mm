// Render diff harness (RENDER_ARCH.md 8.5): the same cIGZGDriver call sequence ("scene") is run on the native Metal driver
// and on the reference (GDriverOnGL over the MetalGL scaffolding); frames are read back, written to work/shots/ as PNGs and
// compared. Some behaviours that the reference cannot reproduce (scissored clears) are checked against analytic pixels.
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "gdriver_on_gl.h"
#include "render/MetalGL.h"
#include "render/gd/MetalGDriver.h"
#include "render/gd/IBackingStore.h"
#include "render/gd/TextureTable.h"
#include "render/gd/VideoModes.h"

using namespace gd;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

constexpr std::uint32_t W = 128, H = 128;

struct Vtx { float x, y, z; std::uint8_t r, g, b, a; float u, v; };   // V3F_C4UB_T2F, 24 bytes
using Frame = std::vector<std::uint8_t>;                              // BGRA, top-left origin

static void WritePNG(const std::string& path, const Frame& bgra) {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(const_cast<std::uint8_t*>(bgra.data()), W, H, 8, W * 4, cs,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst); CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
}

struct Diff { int maxDiff = 0; int badPixels = 0; };
static Diff Compare(const Frame& a, const Frame& b, int tolerance, Frame* diffOut) {
    Diff d;
    if (diffOut) diffOut->assign(a.size(), 0);
    for (std::size_t i = 0; i < a.size(); i += 4) {
        int worst = 0;
        for (int c = 0; c < 4; ++c) worst = std::max(worst, std::abs(int(a[i + c]) - int(b[i + c])));
        d.maxDiff = std::max(d.maxDiff, worst);
        if (worst > tolerance) {
            ++d.badPixels;
            if (diffOut) { (*diffOut)[i + 2] = 255; (*diffOut)[i + 3] = 255; }
        }
    }
    return d;
}

static Frame RunNative(const std::function<void(cIGZGDriver&)>& scene) {
    auto drv = cMetalGDriver::Create(nullptr, W, H);
    Frame f; std::uint32_t w, h;
    if (!drv) return f;
    scene(*drv);
    drv->ReadBack(f, w, h);
    if (!drv->UnsupportedCalls().empty()) {
        for (auto& [name, n] : drv->UnsupportedCalls()) std::printf("  native: unsupported %s x%d\n", name.c_str(), n);
        ++failures;
    }
    return f;
}

static Frame RunReference(const std::function<void(cIGZGDriver&)>& scene) {
    auto gl = sgl::MetalGL::Create(nullptr, W, H);
    Frame f;
    if (!gl) return f;
    GDriverOnGL drv(gl.get());
    scene(drv);
    std::vector<std::uint8_t> bottomUp(W * H * 4);
    gl->ReadPixels(0, 0, W, H, 0x80e1, 0x8367, bottomUp.data());
    f.resize(bottomUp.size());
    for (std::uint32_t y = 0; y < H; ++y) std::memcpy(&f[y * W * 4], &bottomUp[(H - 1 - y) * W * 4], W * 4);
    return f;
}

static void Reset(cIGZGDriver& d) {
    d.SetViewport();
    d.ClearColor(0, 0, 0, 1);
    d.ClearDepth(1.0);
    d.Clear(kClearColor | kClearDepth);
    d.MatrixMode(0); d.LoadIdentity();
    d.MatrixMode(1); d.LoadIdentity();
    d.DepthMask(true);
    d.DepthFunc(test::LEqual);
    d.Disable(state::DepthTest); d.Disable(state::Blend); d.Disable(state::AlphaTest); d.Disable(state::CullFace);
}

static void Quad(cIGZGDriver& d, float x0, float y0, float x1, float y1, float z, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const Vtx q[4] = {{x0, y0, z, r, g, b, a, 0, 0}, {x1, y0, z, r, g, b, a, 1, 0}, {x1, y1, z, r, g, b, a, 1, 1}, {x0, y1, z, r, g, b, a, 0, 1}};
    d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), q);
    d.DrawArrays(prim::Quads, 0, 4);
}

static std::uint32_t MakeTexture(cIGZGDriver& d, std::uint32_t w, std::uint32_t h, const std::vector<std::uint32_t>& argb, std::uint32_t format = kFmtARGB8888) {
    const std::uint32_t id = d.CreateTexture(format, w, h, 1, 0);
    d.LoadTextureLevel(id, 0, 0, 0, w, h, 3, 1, 0, argb.data());   // (BGRA, unsigned byte) = 0xAARRGGBB words
    return id;
}
static void UseTexture(cIGZGDriver& d, std::uint32_t id, std::uint32_t env, std::uint32_t mag = 0, std::uint32_t wrap = 3) {
    d.TexStage(0);
    d.SetTexture(id, 0);
    d.Enable(state::Texturing);
    d.TexEnv(0, 0, env);
    d.TexParameter(0, 0, mag);
    d.TexParameter(0, 1, mag);
    d.TexParameter(0, 2, wrap);
    d.TexParameter(0, 3, wrap);
}
static void TexQuad(cIGZGDriver& d, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::uint8_t r = 255, std::uint8_t g = 255, std::uint8_t b = 255, std::uint8_t a = 255) {
    const Vtx q[4] = {{x0, y0, 0.5f, r, g, b, a, u0, v1}, {x1, y0, 0.5f, r, g, b, a, u1, v1}, {x1, y1, 0.5f, r, g, b, a, u1, v0}, {x0, y1, 0.5f, r, g, b, a, u0, v0}};
    d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), q);
    d.DrawArrays(prim::Quads, 0, 4);
}

struct Scene { const char* name; int tolerance; std::function<void(cIGZGDriver&)> run; };

int main() {
    @autoreleasepool {
        [[NSFileManager defaultManager] createDirectoryAtPath:@"work/shots" withIntermediateDirectories:YES attributes:nil error:nil];
        const std::vector<Scene> scenes = {
            {"clear", 0, [](cIGZGDriver& d) {
                d.ClearColor(0.2f, 0.4f, 0.6f, 1.0f);
                d.Clear(kClearColor | kClearDepth);
            }},
            {"solid_quad", 0, [](cIGZGDriver& d) {
                Reset(d);
                Quad(d, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 255, 0, 0, 255);
            }},
            {"two_quads_depth", 0, [](cIGZGDriver& d) {
                Reset(d);
                d.Enable(state::DepthTest);
                Quad(d, -0.6f, -0.6f, 0.3f, 0.3f, 0.3f, 0, 255, 0, 255);   // nearer, drawn first
                Quad(d, -0.3f, -0.3f, 0.6f, 0.6f, 0.7f, 0, 0, 255, 255);   // farther, must be hidden where they overlap
            }},
            {"blend_alpha", 2, [](cIGZGDriver& d) {
                Reset(d);
                Quad(d, -0.8f, -0.8f, 0.2f, 0.2f, 0.5f, 255, 0, 0, 255);
                d.Enable(state::Blend);
                d.BlendFunc(blend::SrcAlpha, blend::InvSrcAlpha);
                Quad(d, -0.2f, -0.2f, 0.8f, 0.8f, 0.5f, 0, 0, 255, 128);
            }},
            {"alpha_test", 0, [](cIGZGDriver& d) {
                Reset(d);
                d.Enable(state::AlphaTest);
                d.AlphaFunc(test::Greater, 0.5f);
                const Vtx tri[6] = {{-0.9f, -0.9f, 0.5f, 255, 255, 0, 255, 0, 0}, {0.0f, -0.9f, 0.5f, 255, 255, 0, 255, 0, 0}, {-0.9f, 0.0f, 0.5f, 255, 255, 0, 255, 0, 0},
                                    {0.1f, 0.1f, 0.5f, 255, 255, 0, 100, 0, 0}, {0.9f, 0.1f, 0.5f, 255, 255, 0, 100, 0, 0}, {0.1f, 0.9f, 0.5f, 255, 255, 0, 100, 0, 0}};
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), tri);
                d.DrawArrays(prim::Triangles, 0, 6);
            }},
            {"cull", 0, [](cIGZGDriver& d) {
                Reset(d);
                d.Enable(state::CullFace);
                const Vtx ccw[3] = {{-0.8f, -0.8f, 0.5f, 255, 0, 0, 255, 0, 0}, {0.0f, -0.8f, 0.5f, 255, 0, 0, 255, 0, 0}, {-0.4f, 0.0f, 0.5f, 255, 0, 0, 255, 0, 0}};
                const Vtx cw[3] = {{0.2f, 0.2f, 0.5f, 0, 255, 0, 255, 0, 0}, {0.6f, 0.9f, 0.5f, 0, 255, 0, 255, 0, 0}, {0.9f, 0.2f, 0.5f, 0, 255, 0, 255, 0, 0}};
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), ccw);
                d.DrawArrays(prim::Triangles, 0, 3);
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), cw);
                d.DrawArrays(prim::Triangles, 0, 3);
            }},
            {"strip_and_fan", 0, [](cIGZGDriver& d) {
                Reset(d);
                const Vtx strip[5] = {{-0.9f, -0.9f, 0.5f, 255, 0, 0, 255, 0, 0}, {-0.9f, -0.2f, 0.5f, 255, 0, 0, 255, 0, 0}, {-0.5f, -0.9f, 0.5f, 255, 128, 0, 255, 0, 0},
                                      {-0.5f, -0.2f, 0.5f, 255, 128, 0, 255, 0, 0}, {-0.1f, -0.9f, 0.5f, 255, 255, 0, 255, 0, 0}};
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), strip);
                d.DrawArrays(prim::TriangleStrip, 0, 5);
                const Vtx fan[5] = {{0.5f, 0.5f, 0.5f, 255, 255, 255, 255, 0, 0}, {0.9f, 0.5f, 0.5f, 0, 0, 255, 255, 0, 0}, {0.8f, 0.9f, 0.5f, 0, 255, 0, 255, 0, 0},
                                    {0.2f, 0.9f, 0.5f, 255, 0, 0, 255, 0, 0}, {0.1f, 0.5f, 0.5f, 0, 255, 255, 255, 0, 0}};
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), fan);
                d.DrawArrays(prim::TriangleFan, 0, 5);
            }},
            {"elements", 0, [](cIGZGDriver& d) {
                Reset(d);
                const Vtx v[4] = {{-0.5f, -0.5f, 0.5f, 255, 0, 0, 255, 0, 0}, {0.5f, -0.5f, 0.5f, 0, 255, 0, 255, 0, 0}, {0.5f, 0.5f, 0.5f, 0, 0, 255, 255, 0, 0}, {-0.5f, 0.5f, 0.5f, 255, 255, 255, 255, 0, 0}};
                const std::uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
                d.InterleavedArrays(vf::V3F_C4UB_T2F, sizeof(Vtx), v);
                d.DrawElements(prim::Triangles, 6, 3, idx);
            }},
            {"projection", 1, [](cIGZGDriver& d) {
                Reset(d);
                const float scale[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0.25f, 0, 0, 1};   // row-vector matrix: scale 0.5, shift +0.25 in x
                d.MatrixMode(1);
                d.LoadMatrix(scale);
                Quad(d, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 200, 100, 50, 255);
            }},
        };
        const std::vector<std::uint32_t> checker = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00,
                                                    0xff00ffff, 0xffff00ff, 0xff808080, 0xffffffff,
                                                    0xff804020, 0xff208040, 0xff402080, 0xff102030,
                                                    0xffa0b0c0, 0xff0a0b0c, 0xffc0b0a0, 0xff123456};
        const std::vector<std::uint32_t> sprite = {0xffff0000, 0x00000000, 0xff00ff00, 0x00000000,
                                                   0x00000000, 0xff0000ff, 0x00000000, 0xffffff00,
                                                   0xffff00ff, 0x00000000, 0xff00ffff, 0x00000000,
                                                   0x00000000, 0xffffffff, 0x00000000, 0x80ff8000};
        const std::vector<Scene> texScenes = {
            {"tex_modulate", 0, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker), 1);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1, 255, 255, 255, 255);
            }},
            {"tex_modulate_tint", 1, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker), 1);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1, 200, 150, 255, 128);
            }},
            {"tex_replace", 0, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker), 0);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1, 10, 20, 30, 255);
            }},
            {"tex_sprite_alpha_test", 0, [&](cIGZGDriver& d) {
                Reset(d);
                d.Enable(state::AlphaTest);
                d.AlphaFunc(test::Greater, 0.5f);
                UseTexture(d, MakeTexture(d, 4, 4, sprite), 1);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1);
            }},
            {"tex_sprite_blend", 2, [&](cIGZGDriver& d) {
                Reset(d);
                Quad(d, -0.9f, -0.9f, 0.9f, 0.9f, 0.9f, 40, 40, 160, 255);
                d.Enable(state::Blend);
                d.BlendFunc(blend::SrcAlpha, blend::InvSrcAlpha);
                UseTexture(d, MakeTexture(d, 4, 4, sprite), 1);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1);
            }},
            {"tex_linear_clamp", 2, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker), 0, 1, 2);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, -0.25f, -0.25f, 1.25f, 1.25f);
            }},
            {"tex_repeat", 0, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker), 0, 0, 3);
                TexQuad(d, -0.9f, -0.9f, 0.9f, 0.9f, 0, 0, 3, 2);
            }},
            {"tex_565_quantised", 0, [&](cIGZGDriver& d) {
                Reset(d);
                UseTexture(d, MakeTexture(d, 4, 4, checker, kFmtRGB565), 0);
                TexQuad(d, -0.8f, -0.8f, 0.8f, 0.8f, 0, 0, 1, 1);
            }},
        };
        std::vector<Scene> allScenes = scenes;
        allScenes.insert(allScenes.end(), texScenes.begin(), texScenes.end());
        for (const Scene& s : allScenes) {
            const Frame native = RunNative(s.run);
            const Frame ref = RunReference(s.run);
            if (native.empty() || ref.empty()) { std::printf("no Metal device: skipping the harness\n"); return 0; }
            Frame diff;
            const Diff d = Compare(native, ref, s.tolerance, &diff);
            const std::string base = std::string("work/shots/gd_") + s.name;
            WritePNG(base + ".native.png", native);
            WritePNG(base + ".ref.png", ref);
            WritePNG(base + ".diff.png", diff);
            bool drew = s.name == std::string("clear");
            for (std::size_t i = 0; i < native.size() && !drew; i += 4) drew = (native[i] | native[i + 1] | native[i + 2]) != 0;
            CHECK(drew);   // 'equal to the reference' must not mean 'both empty'
            std::printf("%-18s max diff %3d, pixels over %d: %d  %s\n", s.name, d.maxDiff, s.tolerance, d.badPixels, d.badPixels == 0 ? "ok" : "MISMATCH");
            if (d.badPixels != 0) ++failures;
        }

        // ---- sanity: the scenes really drew what they claim (so 'equal to the reference' is not 'both empty') ----
        {
            auto at = [](const Frame& f, int x, int y, int c) { return f[(y * W + x) * 4 + c]; };   // c: 0 b, 1 g, 2 r
            const Frame solid = RunNative(scenes[1].run);
            CHECK(at(solid, 64, 64, 2) == 255 && at(solid, 64, 64, 1) == 0 && at(solid, 5, 5, 2) == 0);
            const Frame depthScene = RunNative(scenes[2].run);
            CHECK(at(depthScene, 64, 64, 1) == 255 && at(depthScene, 64, 64, 0) == 0);    // overlap shows the nearer (green) quad
            CHECK(at(depthScene, 100, 28, 0) == 255);                                      // far quad visible outside the overlap
            const Frame culled = RunNative(scenes[5].run);
            CHECK(at(culled, 38, 98, 2) == 255);                                            // the counter-clockwise (front) triangle is drawn
            CHECK(at(culled, 100, 36, 1) == 0);                                             // the clockwise one is culled
        }
        // ---- texture handles / errors (observable driver behaviour) ----
        {
            auto drv = cMetalGDriver::Create(nullptr, W, H);
            std::uint32_t ids[3];
            drv->GenTextures(3, ids);
            CHECK(ids[0] == 1 && ids[1] == 2 && ids[2] == 3);     // texture 0 is the built-in white texel
            CHECK(drv->IsTexture(0) && drv->IsTexture(2) && !drv->IsTexture(9));
            drv->DeleteTextures(2, ids + 1);                       // frees 2 then 3
            CHECK(!drv->IsTexture(2) && !drv->IsTexture(3));
            std::uint32_t again[2];
            drv->GenTextures(2, again);
            CHECK(again[0] == 3 && again[1] == 2);                 // most recently freed first
            CHECK(drv->CreateTexture(kFmtARGB8888, 5000, 4, 1, 0) != 0 && drv->GetError() == 3);   // too wide: handle still returned, error 3
            CHECK(drv->GetError() == 0);                           // reading the error clears it
            drv->TexStage(2);
            CHECK(drv->GetError() == 2);                           // only two stages
            drv->BindTexture(1, 5);
            CHECK(drv->GetError() == 1);                           // only target 0
            drv->SetTexture(7, 1);
            CHECK(drv->GetTexture(1) == 7);
            drv->TexStage(0);
            drv->BindTexture(0, 1);
            drv->TexParameter(0, 3, 9);                            // bind mode: persisted on the texture
            drv->BindTexture(0, 1);
            drv->TexParameter(0, 9, 1);                            // unknown parameter type on a bound texture -> error 1
            CHECK(drv->GetError() == 1);
        }
        {   // video modes through the driver slots, and mode selection resizing the targets
            struct Src final : IModeSource {
                int NumModes() override { return 2; }
                void NthMode(int n, int& w, int& h, int& d) override { w = n ? 64 : 32; h = n ? 48 : 24; d = 32; }
                void WindowMaxSizes(int& w, int& h) override { w = 9999; h = 9999; }
            } src;
            auto drv = cMetalGDriver::Create(nullptr, W, H, &src);
            CHECK(drv->CountVideoModes() == 4 + 12);               // 2 modes x 2 entries + the 12 stock windowed sizes
            sGDMode m{};
            CHECK(drv->GetVideoModeInfo(2, m) && m.width == 64 && m.height == 48 && m.id == 2);
            CHECK(!drv->GetVideoModeInfo(99, m) && drv->GetError() == 2);
            drv->GetVideoModeInfo(m);
            CHECK(m.id == 0 && m.width == W && m.height == H);    // before any SetVideoMode the drawable is the current mode (the boot code reads it)
            CHECK(drv->SetVideoMode(2, nullptr, true, false));
            drv->GetVideoModeInfo(m);
            CHECK(m.id == 2 && m.width == 64);
            drv->ClearColor(0, 1, 0, 1); drv->Clear(kClearColor | kClearDepth);
            std::vector<std::uint8_t> px; std::uint32_t w, h;
            drv->ReadBack(px, w, h);
            CHECK(w == 64 && h == 48 && px[1] == 255);
            CHECK(!drv->SetVideoMode(500, nullptr, true, false) && drv->GetError() == 2);
            CHECK(drv->SetVideoMode(-1, nullptr, true, false));
        }
        {   // backing store: colour AND depth survive a clear, and blocks can be moved while restoring
            auto drv = cMetalGDriver::Create(nullptr, W, H);
            IBackingStore* store = drv->BackingStore();
            CHECK(!store->Exists() && store->Create(W, H) && store->Exists());
            Reset(*drv);
            drv->Enable(state::DepthTest);
            Quad(*drv, -0.9f, -0.9f, 0.1f, 0.1f, 0.3f, 255, 0, 0, 255);          // red, near
            store->SaveFromFrame(0, 0, W, H, 0, 0);
            drv->ClearColor(0, 0, 1, 1);
            drv->Clear(kClearColor | kClearDepth);                                   // wipe the frame
            store->RestoreToFrame(0, 0, W, H, 0, 0);
            Quad(*drv, -0.5f, -0.5f, 0.5f, 0.5f, 0.6f, 0, 255, 0, 255);             // green, farther: must lose the depth test where red is
            std::vector<std::uint8_t> px; std::uint32_t w, h;
            drv->ReadBack(px, w, h);
            auto at = [&](int x, int y, int c) { return px[(y * W + x) * 4 + c]; };
            CHECK(at(40, 90, 2) == 255 && at(40, 90, 1) == 0);                      // red restored (left-bottom area)
            CHECK(at(64, 64, 2) == 255 && at(64, 64, 1) == 0);                      // overlap: red still wins -> depth was restored
            CHECK(at(83, 64, 1) == 255 && at(83, 64, 2) == 0);                    // green visible where red never was
            CHECK(at(110, 120, 1) == 0 && at(110, 120, 2) == 0);                    // untouched area is the saved background, not the later blue clear
            // moving restore: copy a 20x20 block from (30,70) to (90,10)
            drv->ClearColor(0, 0, 0, 1); drv->Clear(kClearColor | kClearDepth);
            store->RestoreToFrame(30, 70, 20, 20, 90, 10);
            drv->ReadBack(px, w, h);
            CHECK(at(95, 15, 2) == 255 && at(85, 15, 2) == 0 && at(95, 35, 2) == 0);
            store->Destroy();
            CHECK(!store->Exists());
        }
        // ---- analytic checks the reference cannot do ----
        {   // clear colour quantisation: ARGB 8-bit, round-half-even(f*256) with 256 -> 255
            const Frame f = RunNative([](cIGZGDriver& d) { d.ClearColor(0.2f, 0.4f, 0.6f, 1.0f); d.Clear(kClearColor | kClearDepth); });
            const std::uint32_t q = PackClearColor(0.2f, 0.4f, 0.6f, 1.0f);
            CHECK(f[2] == (q >> 16 & 0xff) && f[1] == (q >> 8 & 0xff) && f[0] == (q & 0xff) && f[3] == 255);
            CHECK(f[0] == 154 && f[1] == 102 && f[2] == 51);   // 0.6, 0.4, 0.2 -> b, g, r
        }
        {   // scissored clear touches only the rectangle (GL coordinates, origin bottom-left)
            const Frame f = RunNative([](cIGZGDriver& d) {
                d.ClearColor(0, 0, 0, 1); d.SetViewport(); d.Clear(kClearColor | kClearDepth);
                d.ClearColor(1, 0, 0, 1);
                d.SetViewport(16, 32, 40, 20);   // x 16..56, y(bottom-up) 32..52  -> rows 76..96 from the top
                d.Clear(kClearColor);
            });
            auto px = [&](int x, int y) { return f[(y * W + x) * 4 + 2]; };   // red channel
            CHECK(px(20, 80) == 255);          // inside
            CHECK(px(15, 80) == 0);            // just left
            CHECK(px(20, 96) == 0);            // just below (row 96 is outside: rows 76..95)
            CHECK(px(20, 75) == 0);            // just above
            CHECK(px(55, 90) == 255 && px(56, 90) == 0);
        }
        {   // viewport readback
            auto drv = cMetalGDriver::Create(nullptr, W, H);
            std::uint32_t v[4];
            drv->SetViewport(5, 6, 20, 30);
            drv->GetViewport(v);
            CHECK(v[0] == 5 && v[1] == 6 && v[2] == 25 && v[3] == 36);
            drv->SetViewport();
            drv->GetViewport(v);
            CHECK(v[0] == 0 && v[1] == 0 && v[2] == W && v[3] == H);
        }
        {   // pipeline cache: identical state reuses the pipeline, a new blend state adds one
            auto drv = cMetalGDriver::Create(nullptr, W, H);
            Reset(*drv);
            for (int i = 0; i < 5; ++i) Quad(*drv, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 255, 0, 0, 255);
            CHECK(drv->PipelineCount() == 1 && drv->DrawCalls() == 5);
            drv->Enable(state::Blend);
            drv->BlendFunc(blend::SrcAlpha, blend::InvSrcAlpha);
            Quad(*drv, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 255, 0, 0, 255);
            CHECK(drv->PipelineCount() == 2);
        }
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
