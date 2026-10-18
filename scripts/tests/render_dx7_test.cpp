// The OpenGL-backed DX7 shim: pixel formats, surfaces (kinds, locking, readback), vertex buffers, the device's state
// cache and draw path, display selection and the preferences store — all against a recording IGLApi / fake display platform.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "gl_trace.h"
#include "render/D3DDevice7.h"
#include "render/D3DVertexBuffer7.h"
#include "render/DirectDraw7.h"
#include "render/DDSurface7.h"
#include "render/DX7Host.h"
#include "render/FastATIVertexProgram.h"
#include "render/MacDisplay.h"
#include "render/MacOpenGL.h"

using namespace sgl;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a); long long vb_ = (long long)(b); if (va_ != vb_) { std::printf("FAIL line %d: %s == %s (%lld vs %lld)\n", __LINE__, #a, #b, va_, vb_); ++failures; } } while (0)

namespace {
class MemoryPrefs final : public IPreferencesStore {
public:
    std::map<std::string, std::string> values;
    bool CopyString(const char* key, std::string& out) override {
        auto it = values.find(key);
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    void SetString(const char* key, const char* value) override { values[key] = value; }
    void Synchronize() override { ++syncs; }
    int syncs = 0;
};

class FakePlatform final : public IDisplayPlatform {
public:
    std::vector<std::string> log;
    std::vector<Mode> modes;
    std::vector<std::uint32_t> displays{7};
    CGRect bounds{0, 0, 1920, 1200};
    std::string renderer = "Fake GPU";
    bool hardware = true;
    int windowW = 0, windowH = 0;
    float opacity = 0.0f;
    int alertButton = 0x67;
    bool mainThread = true;
    bool glContextOk = true;
    int exited = -1;
    std::uint32_t MainDisplayID() override { return 3; }
    CGRect DisplayBounds(std::uint32_t) override { return bounds; }
    bool ActiveDisplays(std::vector<std::uint32_t>& out) override { out = displays; return !out.empty(); }
    std::vector<Mode> DisplayModes(std::uint32_t) override { return modes; }
    int DisplayPixelsWide(std::uint32_t) override { return 1920; }
    int DisplayPixelsHigh(std::uint32_t) override { return 1200; }
    std::int32_t CurrentDisplayDepth(std::uint32_t) override { return 32; }
    bool DisplayHasHardwareRenderer(std::uint32_t) override { return hardware; }
    bool QueryPCIDisplayInfo(std::uint32_t display, ASLPCIDisplayInfo_t& out) override {
        log.push_back("pci(" + std::to_string(display) + ")");
        out = {};
        out.deviceID = 0x1234;
        out.vendorID = 0x10de;
        out.vramMB = 2048;
        std::strcpy(out.name, "Fake Board");
        return true;
    }
    bool IsMacOSSierraOrLater() override { return true; }
    const char* GLRendererString() override { return renderer.c_str(); }
    void* CreateGLContext(void*, bool fullscreen, int w, int h) override {
        log.push_back("ctx(" + std::to_string(fullscreen) + "," + std::to_string(w) + "x" + std::to_string(h) + ")");
        return glContextOk ? this : nullptr;
    }
    void DeleteGLContext(void*) override { log.push_back("deletectx"); }
    void MakeCurrent(void*, void*) override { log.push_back("current"); }
    void SwapWindow(void*) override { log.push_back("swap"); }
    void FlushGL() override { log.push_back("flush"); }
    void EnableMultithreadedGL(bool) override { log.push_back("mtgl"); }
    void* CreateWindow(const char*, bool windowed, int w, int h, const CGRect&) override {
        log.push_back("window(" + std::to_string(windowed) + "," + std::to_string(w) + "x" + std::to_string(h) + ")");
        windowW = w; windowH = h; opacity = 1.0f;
        return this;
    }
    void DestroyWindow(void*) override { log.push_back("destroywindow"); }
    void SetWindowPosition(void*, int x, int y) override { log.push_back("pos(" + std::to_string(x) + "," + std::to_string(y) + ")"); }
    void SetWindowSize(void*, int w, int h) override { log.push_back("size(" + std::to_string(w) + "x" + std::to_string(h) + ")"); windowW = w; windowH = h; }
    void GetWindowSize(void*, int& w, int& h) override { w = windowW; h = windowH; }
    float WindowOpacity(void*) override { return opacity; }
    void HideWindow(void*) override { log.push_back("hide"); }
    void ShowWindow(void*) override { log.push_back("show"); }
    void CenterWindow(void*) override { log.push_back("center"); }
    void SetMenuBarVisible(bool v) override { log.push_back(v ? "menu+" : "menu-"); }
    int ShowAlertBox(void*, const char*, const char* text) override { log.push_back(std::string("alert:") + text); return alertButton; }
    bool IsMainThread() override { return mainThread; }
    void FlushInputEvents() override { log.push_back("flushevents"); }
    void Exit(int code) override { exited = code; }
};

_DDPIXELFORMAT Pf(std::uint32_t flags, std::uint32_t bits, std::uint32_t r = 0, std::uint32_t g = 0, std::uint32_t b = 0,
                  std::uint32_t a = 0, std::uint32_t fourcc = 0) {
    return _DDPIXELFORMAT{0x20, flags, fourcc, bits, r, g, b, a};
}
const std::uint32_t kDXT1 = 0x31545844, kDXT2 = 0x32545844, kDXT3 = 0x33545844, kDXT4 = 0x34545844, kDXT5 = 0x35545844;

_DDSURFACEDESC2 Desc(std::uint32_t w, std::uint32_t h, std::uint32_t caps, _DDPIXELFORMAT pf, std::uint32_t mips = 0) {
    _DDSURFACEDESC2 d{};
    d.dwSize = 0x88;
    d.dwFlags = 0x100f | (mips ? 0x20000 : 0);
    d.dwWidth = w;
    d.dwHeight = h;
    d.dwMipMapCount = mips;
    d.ddpfPixelFormat = pf;
    d.ddsCaps.dwCaps = caps;
    return d;
}
const _GUID kKey{};
}  // namespace

namespace sgl { IPreferencesStore* DefaultPreferencesStore() { static MemoryPrefs prefs; return &prefs; } }

static void TestFormats() {
    CHECK(MacOpenGL::GetOpenGLFormat(Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)).internalFormat == 0x8058);
    auto rgb = MacOpenGL::GetOpenGLFormat(Pf(0x40, 32, 0xff0000, 0xff00, 0xff));
    CHECK(rgb.internalFormat == 0x8051 && rgb.format == 0x80e1 && rgb.type == 0x8367);
    auto a4 = MacOpenGL::GetOpenGLFormat(Pf(0x41, 16, 0xf00, 0xf0, 0xf, 0xf000));
    CHECK(a4.internalFormat == 0x8056 && a4.type == 0x8365);
    auto a1 = MacOpenGL::GetOpenGLFormat(Pf(0x41, 16, 0x7c00, 0x3e0, 0x1f, 0x8000));
    CHECK(a1.internalFormat == 0x8057 && a1.type == 0x8366);
    auto r565 = MacOpenGL::GetOpenGLFormat(Pf(0x40, 16, 0xf800, 0x7e0, 0x1f));
    CHECK(r565.internalFormat == 0x1908 && r565.format == 0x1908 && r565.type == 0x1401);
    auto z24 = MacOpenGL::GetOpenGLFormat(Pf(0x400, 24));
    CHECK(z24.internalFormat == 0x1902 && z24.type == 0x1406);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(0x400, 8)).internalFormat, 0x1908);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, kDXT1)).internalFormat, 0x83f1);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, kDXT2)).internalFormat, 0x83f1);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, kDXT3)).internalFormat, 0x83f2);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, kDXT5)).internalFormat, 0x83f3);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, kDXT4)).internalFormat, 0x1908);
    CHECK_EQ(MacOpenGL::GetOpenGLFormat(Pf(4, 0, 0, 0, 0, 0, 0x12345678)).internalFormat, 0x1908);

    MacOpenGL::OpenGLTextureFormat dxt1{0x83f1, 0, 0}, dxt3{0x83f2, 0, 0}, rgba{0x8058, 0x80e1, 0x8367}, small{0x8056, 0x80e1, 0x8365};
    CHECK_EQ(dxt1.GetImageSizeInBytes(8, 8), 32u);
    CHECK_EQ(dxt3.GetImageSizeInBytes(8, 8), 64u);
    CHECK_EQ(dxt1.GetImageSizeInBytes(1, 1), 8u);
    CHECK_EQ(rgba.GetImageSizeInBytes(4, 4), 64u);
    CHECK_EQ(small.GetImageSizeInBytes(4, 4), 32u);
    CHECK_EQ((MacOpenGL::OpenGLTextureFormat{0x1908, 0x1908, 0x1401}.GetImageSizeInBytes(4, 4)), 0u);
    CHECK_EQ((MacOpenGL::OpenGLTextureFormat{0x1902, 0x1902, 0x1406}.GetImageSizeInBytes(2, 2)), 16u);
}

static void TestSurfaces(TraceGL& gl) {
    SetGL(&gl);
    // kinds from caps
    struct { std::uint32_t caps; int kind; bool pixels; } cases[] = {
        {0x2200, 1, false}, {0x22000, 2, false}, {0x2004, 3, true}, {0x40, 5, true}, {0x20000, 6, true},
        {0x2000, 0, false}, {0, 0, true}, {0x1000, 4, true}};
    for (auto& c : cases) {
        CDirectDrawSurface7 s(Desc(16, 16, c.caps, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));
        CHECK_EQ(s.kind, c.kind);
        CHECK((s.pixels != nullptr) == c.pixels);
        CHECK_EQ(s.refCount, 1);
        s.refCount = 0;   // (the destructor of a stack object must not be paired with Release)
    }
    // texture: private data holds the default GL texture state
    CDirectDrawSurface7 tex(Desc(8, 8, 0x1000, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000), 3));
    CHECK_EQ(tex.kind, 4);
    CHECK_EQ(tex.GetImageSizeInBytes(), 256u + 64u + 16u);
    OpenGLTextureState ts{};
    CHECK_EQ(tex.GetPrivateData(kKey, &ts, nullptr), 0u);
    CHECK(ts.id == 0 && ts.dirty == 0 && ts.tex.wrapS == 0x2901 && ts.tex.magFilter == 0x2601 && ts.tex.minFilter == 0x2702 &&
          ts.tex.maxLevel == 1000);
    // lock: flags, pitch, level and rect offsets
    _DDSURFACEDESC2 out{};
    out.dwSize = 0x88;
    CHECK_EQ(tex.Lock(nullptr, &out, 0, nullptr), 0u);
    CHECK_EQ(out.dwFlags & 0x808, 0x808u);
    CHECK_EQ(out.lPitch, 32);
    CHECK(out.lpSurface == tex.pixels->aligned);
    tex.GetPrivateData(kKey, &ts, nullptr);
    CHECK_EQ(ts.dirty, 1u);
    tagRECT rect{2, 1, 6, 3};
    tex.Lock(&rect, &out, 0, nullptr);
    CHECK(out.lpSurface == tex.pixels->aligned + 40);
    tex.lockLevel = 1;
    tex.Lock(nullptr, &out, 0, nullptr);
    CHECK(out.lpSurface == tex.pixels->aligned + 256);
    CHECK_EQ(out.lPitch, 16);
    tex.Unlock(nullptr);
    CHECK_EQ(tex.lockLevel, 0);
    // compressed surface
    CDirectDrawSurface7 dxt(Desc(8, 8, 0, Pf(4, 0, 0, 0, 0, 0, kDXT1)));
    CHECK_EQ(dxt.GetImageSizeInBytes(), 32u);
    dxt.Lock(nullptr, &out, 0, nullptr);
    CHECK_EQ(out.dwFlags & 0x80800, 0x80800u);
    CHECK_EQ(out.lPitch, 32);
    CHECK_EQ(dxt.GetLevelSizeInBytes(5, 5, Pf(4, 0, 0, 0, 0, 0, kDXT5)), 64u);
    CHECK_EQ(dxt.GetLevelSizeInBytes(4, 4, Pf(4, 0, 0, 0, 0, 0, 0x4b4d4f4b)), 0u);
    CHECK_EQ(dxt.GetLevelSizeInBytes(4, 4, Pf(2, 8)), 16u);   // luminance
    // private data grows, then shrinking requests keep the larger block
    CDirectDrawSurface7 plain(Desc(4, 4, 0, Pf(0x40, 32)));
    char data[0x40];
    std::memset(data, 7, sizeof data);
    plain.SetPrivateData(kKey, data, 8, 0);
    CHECK_EQ(plain.privateData->size, 8u);
    plain.SetPrivateData(kKey, data, 0x40, 0);
    CHECK_EQ(plain.privateData->size, 0x40u);
    plain.SetPrivateData(kKey, data, 4, 0);
    CHECK_EQ(plain.privateData->size, 0x40u);
    plain.FreePrivateData(kKey);
    CHECK(plain.privateData == nullptr);
    // destruction of a texture with a GL object frees it
    gl.clear();
    {
        auto* t2 = new CDirectDrawSurface7(Desc(4, 4, 0x1000, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));
        OpenGLTextureState s2{};
        t2->GetPrivateData(kKey, &s2, nullptr);
        s2.id = 55;
        s2.pixelBuffer = 66;
        t2->SetPrivateData(kKey, &s2, sizeof s2, 0);
        CHECK_EQ(t2->Release(), 0);
    }
    CHECK(gl.has("DeleteTextures(1,p)") && gl.has("DeleteBuffers(1,p)"));
    // back-buffer lock reads GL pixels and flips the rows
    gl.clear();
    CDirectDrawSurface7 back(Desc(4, 4, 0x2004, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));
    CHECK_EQ(back.kind, 3);
    gl.onReadPixels = [&](GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
        CHECK(x == 0 && y == 0 && w == 4 && h == 4 && format == 0x80e1 && type == 0x8367);
        for (int row = 0; row < 4; ++row) std::memset(static_cast<char*>(data) + row * 16, 'A' + row, 16);   // GL rows run bottom up
    };
    back.Lock(nullptr, &out, 0, nullptr);
    gl.onReadPixels = nullptr;
    auto* px = static_cast<char*>(out.lpSurface);
    CHECK(px[0] == 'D' && px[16] == 'C' && px[32] == 'B' && px[48] == 'A');
    CHECK(gl.has("PixelStorei(3330,4)") || gl.has("PixelStorei(3330,4)"));   // PACK_ROW_LENGTH? (0xd02 = 3330)
    CHECK(gl.has("DepthMask(0)") && gl.has("StencilMask(0)") && gl.has("ColorMask(1,1,1,1)"));
    // the GL state touched during the read is put back
    CHECK(gl.count("ActiveTexture") == 5);
    SetGL(nullptr);
}

static void TestBlt(TraceGL& gl) {
    SetGL(&gl);
    CDirectDrawSurface7 src(Desc(8, 8, 0x40, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));   // front buffer (kind 5)
    CDirectDrawSurface7 back(Desc(8, 8, 0x2004, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));
    tagRECT dest{1, 1, 5, 5}, srcRect{2, 2, 6, 6};
    MacOpenGL::uiFrameBufferBltTransferMethod = 2;
    gl.clear();
    back.Blt(&dest, &src, &srcRect, 0, nullptr);
    CHECK(gl.has("DrawPixels(4,4,32993,33639,p)"));
    CHECK(gl.has("PopAttrib()"));
    CHECK(gl.has("PixelStorei(3317,1)") && gl.has("PixelStorei(3333,1)"));
    MacOpenGL::uiFrameBufferBltTransferMethod = 0;
    gl.clear();
    back.Blt(&dest, &src, &srcRect, 0, nullptr);
    CHECK(gl.count("CopyPixels(2,2,4,4,6144)") == 1);
    gl.clear();
    src.Blt(&dest, &back, &srcRect, 0, nullptr);   // front buffer: GL -> memory
    CHECK(gl.count("CopyPixels(2,2,4,4,6144)") == 1 && gl.has("DrawBuffer(1033)"));
    SetGL(nullptr);
}

static void TestVertexBuffer(TraceGL& gl) {
    SetGL(&gl);
    const std::uint32_t xyz = 2, normal = 0x10, diffuse = 0x40;
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz), 12);
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(4), 16);
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz | normal), 24);
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz | diffuse), 16);
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz | 0x80), 16);                        // specular
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz | 0x100), 12 + 8);                   // one 2-float set
    CHECK_EQ(CDirect3DVertexBuffer7::GetFVFSize(xyz | 0x200 | 0x30000), 12 + 4 + 8);    // set 0: bits 3 -> one float; set 1: bits 0 -> two
    std::vector<std::uint32_t> sizes(2);
    CDirect3DVertexBuffer7::GetTexCoordSizes(sizes, 0x200 | (1u << 16) | (2u << 18));
    CHECK(sizes[0] == 3 && sizes[1] == 4);
    gl.clear();
    _D3DVERTEXBUFFERDESC d{16, 0, xyz | diffuse | 0x200 | (1u << 18), 4};
    auto* vb = new CDirect3DVertexBuffer7(&d, 0);
    CHECK_EQ(vb->refCount, 1);
    CHECK_EQ(vb->sizeBytes, 4u * (12 + 4 + 8 + 12));
    CHECK_EQ(vb->diffuseOffset, 12);
    CHECK(vb->texOffset[0] == 16 && vb->texSize[0] == 2);
    CHECK(vb->texOffset[1] == 24 && vb->texSize[1] == 3);
    CHECK(vb->storage != nullptr && (reinterpret_cast<std::uintptr_t>(vb->storage) & 0xfff) == 0);
    CHECK(gl.has("GenBuffers(1,p)") && gl.has("BufferData(34962,208,null,35040)") == false);   // size is not 208
    CHECK(gl.count("BufferData(34962,") == 1 && gl.has("BufferParameteriAPPLE(34962,35347,0)"));
    // lock: discard re-specifies the store, serialized-modify follows NOOVERWRITE
    gl.clear();
    void* data = nullptr;
    vb->Lock(0x2000, &data, nullptr);
    CHECK(data == gl.mapped.data());
    CHECK(gl.count("BufferData(34962,") == 1);
    CHECK(gl.has("BufferParameteriAPPLE(34962,35346,1)"));
    gl.clear();
    vb->Lock(0x1000, &data, nullptr);
    CHECK(gl.has("BufferParameteriAPPLE(34962,35346,0)") && gl.count("BufferData") == 0);
    gl.clear();
    g_MacVBOffsetToFlush = 16; g_MacVBSizeToFlush = 32;
    vb->Unlock();
    CHECK(gl.has("FlushMappedBufferRangeAPPLE(34962,16,32)") && gl.has("UnmapBuffer(34962)"));
    CHECK(g_MacVBOffsetToFlush == 0 && g_MacVBSizeToFlush == 0);
    _D3DVERTEXBUFFERDESC back{};
    vb->GetVertexBufferDesc(&back);
    CHECK(back.dwFVF == d.dwFVF && back.dwNumVertices == 4);
    gl.clear();
    CHECK_EQ(vb->Release(), 0);
    CHECK(gl.has("DeleteBuffers(1,p)"));
    SetGL(nullptr);
}

static _D3DRENDERSTATETYPE RS(int v) { return static_cast<_D3DRENDERSTATETYPE>(v); }
static _D3DTEXTURESTAGESTATETYPE TS(int v) { return static_cast<_D3DTEXTURESTAGESTATETYPE>(v); }

static void TestDevice(TraceGL& gl) {
    SetGL(&gl);
    FakePlatform platform;
    SetDisplayPlatform(&platform);
    _DDSURFACEDESC2 rt = Desc(64, 64, 0x2004, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000));
    _DDSURFACEDESC2 zb = Desc(64, 64, 0x22000, Pf(0x400, 32));
    auto* target = new CDirectDrawSurface7(rt);
    auto* depth = new CDirectDrawSurface7(zb);
    _D3DDeviceDesc7 desc{};
    desc.bytes[0] = 0x42;
    std::vector<_DDPIXELFORMAT> formats{Pf(0x41, 16, 0xf00, 0xf0, 0xf, 0xf000), Pf(4, 0, 0, 0, 0, 0, kDXT1)};
    gl.clear();
    auto* dev = new CDirect3DDevice7(target, depth, &desc, &formats);
    CHECK_EQ(dev->refCount_, 1);
    // layout of the original object
    auto off = [&](const void* p) { return static_cast<std::size_t>(static_cast<const char*>(p) - reinterpret_cast<const char*>(dev)); };
    CHECK_EQ(off(&dev->refCount_), 8u);
    CHECK_EQ(off(&dev->desc_), 0x20u);
    CHECK_EQ(off(&dev->matrixMode_), 0x114u);
    CHECK_EQ(off(&dev->viewport_), 0x118u);
    CHECK_EQ(off(dev->diffuse_), 0x130u);
    CHECK_EQ(off(dev->ambient_), 0x140u);
    CHECK_EQ(off(&dev->viewDirty_), 0x174u);
    CHECK_EQ(off(dev->view_), 0x178u);
    CHECK_EQ(off(&dev->projDirty_), 0x1b8u);
    CHECK_EQ(off(dev->proj_), 0x1bcu);
    CHECK_EQ(off(&dev->activeTexture_), 0x1fcu);
    CHECK_EQ(off(&dev->stages_[0]), 0x208u);
    CHECK_EQ(off(&dev->stages_[1]), 0x420u);
    CHECK_EQ(off(&dev->stages_[0].coords), 0x208u + 0x58);
    CHECK_EQ(off(&dev->stages_[0].rgbScale), 0x208u + 0x78);
    CHECK_EQ(off(&dev->stages_[0].gl), 0x208u + 0x80);
    CHECK_EQ(off(dev->stages_[0].state), 0x208u + 0x194);
    CHECK_EQ(off(&dev->vertexArray_), 0x638u);
    CHECK_EQ(off(&dev->colorArray_), 0x658u);
    CHECK_EQ(off(&dev->formats_), 0x678u);
    CHECK_EQ(off(&dev->gl_), 0x690u);
    CHECK_EQ(off(&dev->blendDirty_), 0x828u);
    CHECK_EQ(off(dev->renderState_), 0x830u);
    CHECK_EQ(sizeof(CDirect3DDevice7), 0xc30u);
    // construction sets up both units and the stock render states
    CHECK_EQ(dev->matrixMode_, 0x1700u);
    CHECK(gl.has("PixelStorei(3317,1)") && gl.has("PixelStorei(34226,1)"));
    CHECK(gl.has("TexEnvi(8960,8704,34160)"));
    CHECK_EQ(dev->renderState_[7], 1u);
    CHECK_EQ(dev->renderState_[0x16], 3u);
    CHECK_EQ(dev->stages_[0].state[1], 4u);
    CHECK_EQ(dev->stages_[1].state[1], 1u);
    CHECK_EQ(dev->stages_[1].enabled, 0);
    CHECK_EQ(dev->stages_[0].enabled, 0);
    CHECK_EQ(dev->view_[0], 1.0f);
    CHECK_EQ(dev->view_[5], 1.0f);
    CHECK_EQ(dev->diffuse_[0], -1.0f);
    // GetCaps / EnumTextureFormats / GetRenderTarget
    _D3DDeviceDesc7 caps{};
    dev->GetCaps(&caps);
    CHECK_EQ(caps.bytes[0], 0x42);
    static int enumerated;
    enumerated = 0;
    dev->EnumTextureFormats([](_DDPIXELFORMAT*, void*) { ++enumerated; return 1; }, nullptr);
    CHECK_EQ(enumerated, 2);
    IDirectDrawSurface7* rtOut = nullptr;
    dev->GetRenderTarget(&rtOut);
    CHECK(rtOut == target);
    rtOut->Release();
    // Clear
    gl.clear();
    dev->Clear(0, nullptr, 7, 0xff804020, 0.5f, 3);
    CHECK(gl.has("PushAttrib(17664)") && gl.has("Clear(17664)") && gl.has("ClearDepth(0.5)") && gl.has("ClearStencil(3)"));
    CHECK(gl.has("ClearColor(0.501961,0.12549,0.0627451,1)") || gl.count("ClearColor(0.50") == 1);
    gl.clear();
    dev->Clear(0, nullptr, 2, 0, 1.0f, 0);
    CHECK(gl.has("Clear(256)") && gl.count("ClearColor") == 0);
    // render states: cache, GL passthrough, deferred groups
    gl.clear();
    CHECK_EQ(dev->SetRenderState(RS(7), 1), 0u);   // unchanged
    CHECK(gl.log.empty());
    dev->SetRenderState(RS(7), 0);
    CHECK(gl.has("Disable(2929)"));
    dev->SetRenderState(RS(0x16), 2);
    CHECK(gl.has("Enable(2884)") && gl.has("FrontFace(2305)") && gl.has("CullFace(1029)"));
    dev->SetRenderState(RS(0x16), 1);
    CHECK(gl.has("Disable(2884)"));
    dev->SetRenderState(RS(8), 2);
    CHECK(gl.has("PolygonMode(1032,6913)"));
    dev->SetRenderState(RS(8), 1);
    CHECK(gl.has("PolygonMode(1032,6912)"));
    dev->SetRenderState(RS(8), 3);
    CHECK(gl.has("PolygonMode(1032,6914)"));
    gl.clear();
    dev->SetRenderState(RS(9), 1);
    CHECK(gl.has("ShadeModel(7424)"));
    dev->SetRenderState(RS(9), 2);
    CHECK(gl.has("ShadeModel(7425)"));
    dev->SetRenderState(RS(0x17), 3);
    CHECK(gl.has("DepthFunc(514)"));
    dev->SetRenderState(RS(0xe), 0);
    CHECK(gl.has("DepthMask(0)"));
    gl.clear();
    dev->SetRenderState(RS(0x1b), 1);
    dev->SetRenderState(RS(0x13), 5);
    dev->SetRenderState(RS(0x14), 6);
    CHECK(gl.log.empty());   // blend is applied by UpdateRenderingState
    dev->SetRenderState(RS(0x22), 0x80ff4000);
    CHECK(gl.count("Fogfv(2918,p)") == 1);
    dev->SetRenderState(RS(0x2f), 0);
    dev->SetRenderState(RS(0x2f), 5);
    dev->SetRenderState(RS(0x89), 0);
    CHECK(gl.has("Disable(2896)"));
    dev->SetRenderState(RS(0x8c), 4);
    CHECK(gl.has("Fogi(2917,9729)"));
    // material sources: both 1 -> ambient and diffuse
    gl.clear();
    dev->SetRenderState(RS(0x91), 0);
    CHECK(gl.has("Disable(2903)") || gl.has("ColorMaterial(1032,4608)"));
    gl.clear();
    dev->SetRenderState(RS(0x93), 0);
    CHECK(gl.has("Disable(2903)"));
    gl.clear();
    dev->SetRenderState(RS(0x93), 1);
    CHECK(gl.has("ColorMaterial(1032,4608)") && gl.has("Enable(2903)"));
    gl.clear();
    dev->SetRenderState(RS(0x91), 1);
    CHECK(gl.has("ColorMaterial(1032,5634)") && gl.has("Enable(2903)"));
    // GetRenderState maps booleans and the cull mode
    std::uint32_t value = 99;
    dev->GetRenderState(RS(7), &value);
    CHECK_EQ(value, 0u);
    dev->SetRenderState(RS(7), 4);
    dev->GetRenderState(RS(7), &value);
    CHECK_EQ(value, 1u);
    dev->GetRenderState(RS(0x16), &value);
    CHECK_EQ(value, 0u);
    value = 99;
    dev->GetRenderState(RS(0x17), &value);
    CHECK_EQ(value, 99u);   // untouched: not a boolean state
    dev->GetRenderState(RS(0x40), &value);
    CHECK_EQ(value, 99u);
    // transforms are lazy
    gl.clear();
    _D3DMATRIX m{};
    m.m[0] = 2.0f;
    dev->SetTransform(static_cast<_D3DTRANSFORMSTATETYPE>(1), &m);
    CHECK(gl.log.empty() && dev->viewDirty_ == 1);
    dev->UpdateRenderingState();
    CHECK(gl.has("LoadMatrixf(p)") && dev->viewDirty_ == 0);
    CHECK(gl.count("Materialfv") == 2);
    CHECK(gl.has("BlendFunc(770,771)") && gl.has("Enable(3042)"));
    gl.clear();
    dev->UpdateRenderingState();
    CHECK(gl.count("LoadMatrixf") == 0 && gl.count("BlendFunc") == 0);
    // alpha test
    dev->SetRenderState(RS(0xf), 1);
    dev->SetRenderState(RS(0x18), 128);
    dev->SetRenderState(RS(0x19), 5);
    gl.clear();
    dev->UpdateRenderingState();
    CHECK(gl.has("AlphaFunc(516,0.501961)") && gl.has("Enable(3008)"));
    // materials
    gl.clear();
    _D3DMATERIAL7 mat{};
    mat.diffuse = {1, 0, 0, 1};
    mat.ambient = {0, 1, 0, 1};
    dev->SetMaterial(&mat);
    CHECK(gl.count("Materialfv(1032,4608,p)") == 1 && gl.count("Materialfv(1032,4609,p)") == 1);
    gl.clear();
    dev->SetMaterial(&mat);
    CHECK(gl.log.empty());
    // texture stage states
    gl.clear();
    dev->SetTextureStageState(0, TS(1), 5);   // MODULATE2X
    CHECK(gl.has("TexEnvi(8960,34161,8448)") && gl.has("TexEnvf(8960,34163,2)"));
    dev->SetTextureStageState(0, TS(4), 6);   // alpha MODULATE4X
    CHECK(gl.has("TexEnvi(8960,34162,8448)") && gl.has("TexEnvf(8960,3356,4)"));
    dev->SetTextureStageState(0, TS(2), 3);
    dev->SetTextureStageState(0, TS(3), 3);
    CHECK(gl.has("TexEnvi(8960,34176,34166)") && gl.has("TexEnvi(8960,34177,34166)"));
    dev->SetTextureStageState(1, TS(1), 13);
    CHECK(gl.has("TexEnvi(8960,34178,5890)") && gl.has("TexEnvi(8960,34194,770)"));
    dev->SetTextureStageState(0, TS(0xb), 0x20000);
    CHECK(dev->stages_[0].cameraSpace == 1 && dev->stages_[0].matrixDirty == 1);
    dev->SetTextureStageState(0, TS(0xb), 0x10000);
    CHECK(dev->stages_[0].cameraSpace == 1);   // other values leave the flag alone
    dev->SetTextureStageState(0, TS(0xb), 0);
    CHECK(dev->stages_[0].cameraSpace == 0);
    dev->SetTextureStageState(5, TS(1), 4);    // out-of-range unit
    // textures: binding, filters through the surface's private data
    auto* tex = new CDirectDrawSurface7(Desc(4, 4, 0x1000, Pf(0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000)));
    gl.clear();
    dev->SetTexture(0, tex);
    CHECK_EQ(dev->stages_[0].enabled, 1);
    CHECK(gl.count("GenTextures") == 1 && gl.count("BindTexture") == 2);
    OpenGLTextureState ts{};
    tex->GetPrivateData(kKey, &ts, nullptr);
    CHECK(ts.id != 0 && ts.dirty == 1 && ts.uploaded == 0 && ts.internalFormat == 0x8058);
    CHECK_EQ(tex->refCount, 2);
    gl.clear();
    dev->SetTextureStageState(0, TS(0x10), 2);   // MAGFILTER linear (the device starts on point)
    CHECK(gl.has("TexParameteri(3553,10240,9729)"));
    dev->SetTextureStageState(0, TS(0xd), 3);    // address U clamp
    CHECK(gl.has("TexParameteri(3553,10242,10496)"));
    // a vertex-buffer draw: state flushed first, pointers bound, pixels uploaded once
    _D3DVERTEXBUFFERDESC vd{16, 0, 2 | 0x100, 3};
    auto* vb = new CDirect3DVertexBuffer7(&vd, 0);
    gl.clear();
    MacOpenGL::bIsVertexArraySupported = false;
    dev->SetTextureStageState(0, TS(0x12), 1);
    gl.clear();
    CHECK_EQ(dev->DrawPrimitiveVB(static_cast<_D3DPRIMITIVETYPE>(4), vb, 0, 3, 0), 0u);
    CHECK(gl.has("TexImage2D(3553,0,32856,4,4,0,32993,33639,p)"));
    CHECK(gl.count("EnableClientState(32884)") == 1 && gl.has("EnableClientState(32888)"));
    CHECK(gl.has("DrawArrays(4,0,3)"));
    tex->GetPrivateData(kKey, &ts, nullptr);
    CHECK(ts.dirty == 0 && ts.uploaded == 1);
    gl.clear();
    dev->DrawPrimitiveVB(static_cast<_D3DPRIMITIVETYPE>(4), vb, 0, 3, 0);
    CHECK(gl.count("TexImage2D") == 0 && gl.count("TexSubImage2D") == 0 && gl.count("VertexPointer") == 0);
    CHECK(gl.has("DrawArrays(4,0,3)"));
    // locking again re-uploads, this time as a sub-image through the unpack buffer
    _DDSURFACEDESC2 locked{};
    tex->Lock(nullptr, &locked, 0, nullptr);
    tex->Unlock(nullptr);
    gl.clear();
    std::uint16_t indices[3] = {0, 1, 2};
    dev->DrawIndexedPrimitiveVB(static_cast<_D3DPRIMITIVETYPE>(4), vb, 0, 3, indices, 3, 0);
    CHECK(gl.has("TexSubImage2D(3553,0,0,0,4,4,32993,33639,null)") && gl.has("BindBuffer(35052,0)"));
    CHECK(gl.has("DrawElements(4,3,5123,p)"));
    // unbinding a texture
    dev->SetTexture(0, nullptr);
    CHECK_EQ(dev->stages_[0].enabled, 0);
    CHECK_EQ(tex->refCount, 1);
    vb->Release();
    dev->Release();
    tex->Release();
    depth->Release();
    target->Release();
    SetDisplayPlatform(nullptr);
    SetGL(nullptr);
}

static void TestPlatform(TraceGL& gl) {
    SetGL(&gl);
    FakePlatform platform;
    SetDisplayPlatform(&platform);
    MemoryPrefs* prefs = static_cast<MemoryPrefs*>(DefaultPreferencesStore());
    prefs->values.clear();
    // preferences
    MacPreferences::PutInteger("k", 42);
    CHECK_EQ(MacPreferences::GetInteger("k", 7), 42);
    CHECK_EQ(MacPreferences::GetInteger("missing", 7), 7);
    MacPreferences::PutBoolean("b", true);
    CHECK(MacPreferences::GetBoolean("b", false) && !MacPreferences::GetBoolean("none", false));
    CGRect r{1.5, 2.5, 800, 600}, back{};
    MacPreferences::PutRect("r", r);
    MacPreferences::GetRect("r", back);
    CHECK(back.x == 1.5 && back.y == 2.5 && back.width == 800 && back.height == 600);
    CHECK(prefs->values.count("r.origin.x") && prefs->values.count("r.size.height"));
    char buf[8];
    MacPreferences::PutString("long", "abcdef");
    MacPreferences::GetString("long", buf, 4, "dflt");
    CHECK(std::string(buf) == "dflt");
    // display modes: scaled / small / 16-bit modes are dropped, stock sizes are added
    platform.modes = {{1920, 1200, 1920, 1200, 32, 60}, {1680, 1050, 1680, 1050, 32, 60}, {1280, 800, 2560, 1600, 32, 60},
                      {640, 480, 640, 480, 32, 60}, {1024, 768, 1024, 768, 16, 60}, {1440, 900, 1440, 900, 32, 60}};
    CHECK_EQ(MacDisplay::Initialize(), 0);
    CHECK_EQ(MacDisplay::GetDisplayID(), 3u);   // nothing saved: the main display
    int w, h, d;
    int count = MacDisplay::GetNumModes();
    CHECK_EQ(count, 3 + 4);
    MacDisplay::GetNthMode(0, w, h, d);
    CHECK(w == 1440 && h == 900 && d == 32);   // real modes sorted; the stock sizes follow
    MacDisplay::GetNthMode(count - 1, w, h, d);
    CHECK(w == 1600 && h == 1200);
    MacDisplay::GetNthNativeResolution(0, w, h);
    CHECK(w >= 1600);
    CHECK(prefs->values.count("display.index") && prefs->syncs >= 1);
    // window and context
    CHECK_EQ(MacDisplay::SetupDisplay(1, 1024, 768), 0);
    CHECK(MacDisplay::IsWindowMode() && MacDisplay::GetMainWindow() != nullptr);
    float sx, sy;
    MacDisplay::GetDisplayScale(&sx, &sy);
    CHECK(sx == 1.0f && sy == 1.0f);
    CHECK(MacDisplay::PtInDisplay((100 << 16) | 50) && !MacDisplay::PtInDisplay((2000 << 16) | 50) && !MacDisplay::PtInDisplay(-1));
    tagRECT rect{};
    CHECK_EQ(MacDisplay::GetClientRect(MacDisplay::GetMainWindow(), &rect), 1);
    CHECK(rect.right == 1024 && rect.bottom == 768);
    MacDisplay::SetMode(1024, 768, 32);   // same size, window visible: nothing happens
    // GL start-up through MacOpenGL
    gl.extensions = "GL_SGIS_texture_lod GL_APPLE_vertex_array_object GL_APPLE_fence GL_ARB_texture_compression";
    prefs->values["blt-method"] = "1";
    IDirect3DDevice7* device = nullptr;
    IDirectDrawSurface7 *primary = nullptr, *zbuffer = nullptr, *backBuffer = nullptr;
    SetDX7Host(DefaultDX7Host());
    CDirectDraw7 dd;
    CHECK_EQ(MacOpenGL::InitializeOpenGL(0, 640, 480, 32, 0, &dd, device, primary, zbuffer, backBuffer), 0u);
    CHECK(device && primary && zbuffer && backBuffer);
    CHECK(MacOpenGL::bIsTextureLODSupported && !MacOpenGL::bIsVertexArraySupported && !MacOpenGL::bIsWindowPosSupported);
    CHECK_EQ(MacOpenGL::GetFrameBufferBltTransferMethod(), 1u);
    auto* p7 = static_cast<CDirectDrawSurface7*>(primary);
    CHECK(p7->kind == 1 && static_cast<CDirectDrawSurface7*>(zbuffer)->kind == 2 && static_cast<CDirectDrawSurface7*>(backBuffer)->kind == 3);
    CHECK_EQ(p7->desc.lPitch, 640 * 4);
    CHECK_EQ(static_cast<CDirectDrawSurface7*>(zbuffer)->desc.lPitch, 640 * 4);
    CHECK_EQ(static_cast<CDirectDrawSurface7*>(backBuffer)->desc.dwBackBufferCount, 1u);
    static int formatCount;
    formatCount = 0;
    device->EnumTextureFormats([](_DDPIXELFORMAT*, void*) { ++formatCount; return 1; }, nullptr);
    CHECK_EQ(formatCount, 2 + 2 + 4);   // 16-bit pair, 32-bit pair, four DXT formats
    primary->Flip(nullptr, 0);
    CHECK(platform.log.back() == "swap");
    MacOpenGL::ShutdownOpenGL(device, primary, zbuffer, backBuffer);
    CHECK(!device && !primary && !zbuffer && !backBuffer);
    // no context -> error code
    platform.glContextOk = false;
    CHECK_EQ(MacOpenGL::InitializeOpenGL(0, 640, 480, 32, 0, &dd, device, primary, zbuffer, backBuffer), 0x80000008u);
    // identifier query goes through the PCI info
    std::uint32_t vendor = 0, deviceId = 0;
    std::string name;
    CHECK(DefaultDX7Host()->PCIDisplayInfo(vendor, deviceId, name));
    CHECK(vendor == 0x10de && deviceId == 0x1234 && name == "Fake Board");
    ASLPCIDisplayInfo_t info;
    GetPCIDisplayInfo(0, &info);   // cached for the display: only one platform query
    int queries = 0;
    for (auto& l : platform.log) if (l.rfind("pci(", 0) == 0) ++queries;
    CHECK_EQ(queries, 1 + 0 + 0);
    // alert box answers
    platform.log.clear();
    platform.alertButton = 0x66;
    CHECK_EQ(MacDebug::MessageToAlert("oops %d", 5), 2);
    platform.alertButton = 0x67;
    CHECK_EQ(MacDebug::MessageToAlert("fine"), 0);
    platform.alertButton = 0x65;
    MacDebug::MessageToAlert("quit");
    CHECK_EQ(platform.exited, 0);
    platform.mainThread = false;
    CHECK_EQ(MacDebug::MessageToAlert("thread"), 2);
    CHECK(!MacGlobals::IsSystemLocked());
    MacGlobals::LockSystem();
    CHECK(MacGlobals::IsSystemLocked());
    MacGlobals::UnlockSystem();
    MacGlobals::UnlockSystem();
    CHECK(!MacGlobals::IsSystemLocked());
    // vertex programs
    gl.clear();
    FastATIVertexProgram::Init();
    CHECK_EQ(gl.count("GenProgramsARB"), 4);
    CHECK_EQ(gl.count("ProgramStringARB(34336,34933"), 4);
    FastATIVertexProgram::Shutdown();
    CHECK_EQ(gl.count("DeleteProgramsARB"), 4);
    MacDisplay::ReleaseDisplay();
    SetDisplayPlatform(nullptr);
    SetDX7Host(nullptr);
    SetGL(nullptr);
}

int main() {
    TraceGL gl;
    TestFormats();
    TestSurfaces(gl);
    TestBlt(gl);
    TestVertexBuffer(gl);
    TestDevice(gl);
    TestPlatform(gl);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("OK");
    return 0;
}
