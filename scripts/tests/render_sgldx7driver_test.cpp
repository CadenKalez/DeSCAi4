// Tests for nSGLDX7::cGDriver (src/render/gd/SGLDX7Driver.{h,cpp}): the recovered interface surface, state mirror and
// capability/format bookkeeping. Every D3D7/GL/manager neighbour is a fake seam here, so the test exercises the driver
// logic itself, not a renderer.
#include "render/gd/SGLDX7Driver.h"

#include "render/gd/TextureTable.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace nSGLDX7;

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Fakes

struct FakeSurface final : ID3D7Surface {
    sGDPixelFormat format;
    bool lost = false;
    int lockCalls = 0, unlockCalls = 0, bltCalls = 0, restoreCalls = 0;
    long GetPixelFormat(sGDPixelFormat& out) override {
        out = format;
        return 0;
    }
    long GetSurfaceDesc(void* desc) override {
        (void)desc;
        return 0;
    }
    long Blt(const void*, ID3D7Surface*, const void*, std::uint32_t, void*) override {
        ++bltCalls;
        return 0;
    }
    long IsLost() override { return lost ? static_cast<long>(0x887601c2u) : 0; }
    long Lock(const void*, void*, std::uint32_t, void*) override {
        ++lockCalls;
        return 0;
    }
    long Restore() override {
        ++restoreCalls;
        return 0;
    }
    long Unlock(const void*) override {
        ++unlockCalls;
        return 0;
    }
};

struct FakeDevice final : ID3D7Device {
    std::uint8_t caps[caps::kDeviceDescSize] = {};
    FakeSurface* renderTarget = nullptr;
    std::map<std::uint32_t, std::uint32_t> renderStates;
    std::vector<std::array<std::uint32_t, 3>> stageStateLog;   // stage, type, value
    std::vector<std::uint32_t> textures;        // per stage
    std::vector<std::uint32_t> transforms;      // transform states seen
    std::vector<sGDLight> lights;
    std::vector<std::uint32_t> lightEnables;
    sGDMaterial material;
    int materialCalls = 0;
    int beginSceneCalls = 0, endSceneCalls = 0;
    std::uint32_t clearFlags = 0xffffffffu, clearColor = 0, clearStencil = 0;
    float clearDepth = -1.0f;
    int clearCalls = 0;
    std::uint32_t viewport[4] = {10, 20, 640, 480};
    bool formatCompressedMode = true;

    FakeDevice() {
        std::uint16_t stageCount = 2, maxTexture = 0x800;
        std::uint32_t hasStencil = 1, renderBits = 0x20, zBits = 0x10, fog = 4;
        std::memcpy(caps + caps::kStageCount, &stageCount, 2);
        std::memcpy(caps + caps::kMaxTextureSize, &maxTexture, 2);
        std::memcpy(caps + caps::kHasStencil, &hasStencil, 4);
        std::memcpy(caps + caps::kRenderBitDepth, &renderBits, 4);
        std::memcpy(caps + caps::kZBufferBitDepth, &zBits, 4);
        std::memcpy(caps + caps::kFog, &fog, 4);
        textures.assign(8, 0);
    }
    long GetCaps(std::uint8_t* out) override {
        std::memcpy(out, caps, caps::kDeviceDescSize);
        return 0;
    }
    long EnumTextureFormats(int (*callback)(sGDPixelFormat*, void*), void* context) override {
        sGDPixelFormat formats[4] = {};
        formats[0].dwFlags = 0x41;   // RGB | ALPHAPIXELS
        formats[0].dwRGBBitCount = 32;
        formats[0].dwRGBAlphaBitMask = 0xff000000;
        formats[0].dwRBitMask = 0x00ff0000;
        formats[0].dwGBitMask = 0x0000ff00;
        formats[0].dwBBitMask = 0x000000ff;
        formats[1].dwFlags = 0x40;
        formats[1].dwRGBBitCount = 16;
        formats[1].dwRBitMask = 0xf800;
        formats[1].dwGBitMask = 0x07e0;
        formats[1].dwBBitMask = 0x001f;
        formats[2].dwFlags = 0x4;   // FOURCC
        formats[2].dwFourCC = 0x31545844;   // 'DXT1'
        formats[3].dwFlags = 0x40;
        formats[3].dwRGBBitCount = 24;
        formats[3].dwRBitMask = 0xff0000;
        formats[3].dwGBitMask = 0x00ff00;
        formats[3].dwBBitMask = 0x0000ff;   // matches format row 0 (24-bit RGB888)
        const int count = formatCompressedMode ? 4 : 3;
        for (int i = 0; i < count; ++i) callback(&formats[i], context);
        return 0;
    }
    long BeginScene() override {
        ++beginSceneCalls;
        return 0;
    }
    long EndScene() override {
        ++endSceneCalls;
        return 0;
    }
    ID3D7Surface* GetRenderTarget() override { return renderTarget; }
    long Clear(const void*, std::uint32_t flags, std::uint32_t color, float z, std::uint32_t stencil) override {
        ++clearCalls;
        clearFlags = flags;
        clearColor = color;
        clearDepth = z;
        clearStencil = stencil;
        return 0;
    }
    long SetTransform(std::uint32_t state, const float* matrix) override {
        transforms.push_back(state);
        (void)matrix;
        return 0;
    }
    long GetViewport(std::uint32_t out[4]) override {
        std::memcpy(out, viewport, sizeof(viewport));
        return 0;
    }
    long SetMaterial(const sGDMaterial& m) override {
        material = m;
        ++materialCalls;
        return 0;
    }
    long SetLight(std::uint32_t index, const sGDLight& light) override {
        if (lights.size() <= index) lights.resize(index + 1);
        lights[index] = light;
        return 0;
    }
    long SetRenderState(std::uint32_t state, std::uint32_t value) override {
        renderStates[state] = value;
        return 0;
    }
    long GetRenderState(std::uint32_t state, std::uint32_t* value) override {
        auto it = renderStates.find(state);
        *value = it == renderStates.end() ? 0 : it->second;
        return 0;
    }
    long SetTexture(std::uint32_t stage, std::uint32_t texture) override {
        if (textures.size() <= stage) textures.resize(stage + 1, 0);
        textures[stage] = texture;
        return 0;
    }
    long SetTextureStageState(std::uint32_t stage, std::uint32_t type, std::uint32_t value) override {
        stageStateLog.push_back({stage, type, value});
        return 0;
    }
    long LightEnable(std::uint32_t index, bool enable) override {
        lightEnables.push_back(index << 1 | (enable ? 1u : 0u));
        return 0;
    }
    bool HasStageState(std::uint32_t stage, std::uint32_t type, std::uint32_t value) const {
        for (const auto& v : stageStateLog)
            if (v[0] == stage && v[1] == type && v[2] == value) return true;
        return false;
    }
};

struct FakeEnvironment final : ID3D7Environment {
    FakeDevice device;
    FakeSurface renderSurface, backBuffer, depthStencil, thirdSurface, regionSurface;
    bool contextCreated = false;
    bool contextDestroyed = false;
    bool displaySet = false;
    bool cooperative = true;
    bool restoreOk = true;
    int createContextCalls = 0;
    int windowRectCalls = 0;
    std::int32_t setModeWidth = 0, setModeHeight = 0;
    bool setModeWindowed = false;
    FakeEnvironment() {
        renderSurface.format.dwFlags = 0x41;
        renderSurface.format.dwRGBBitCount = 32;
        renderSurface.format.dwRGBAlphaBitMask = 0xff000000;
        renderSurface.format.dwRBitMask = 0x00ff0000;
        renderSurface.format.dwGBitMask = 0x0000ff00;
        renderSurface.format.dwBBitMask = 0x000000ff;
        device.renderTarget = &renderSurface;
    }
    bool CreateContext(void*, bool windowed, bool vsync, std::uint32_t bitsPerPixel, std::int32_t width,
                       std::int32_t height) override {
        (void)windowed;
        (void)vsync;
        (void)bitsPerPixel;
        (void)width;
        (void)height;
        ++createContextCalls;
        contextCreated = true;
        return true;
    }
    void DestroyContext() override { contextDestroyed = true; }
    bool CreateContextInfoOnly() override { return true; }
    bool GetDriverInfo(sGDDriverInfo& info) override {
        info.driverName = "Test GDriver";
        info.versionLow = 0x00010002;
        info.versionHigh = 0x00030004;
        info.vendorId = 0x10de;
        info.deviceId = 0x0407;
        info.subSysId = 0x1234;
        info.revision = 0x00a1;
        for (int i = 0; i < 16; ++i) info.guid[i] = static_cast<std::uint8_t>(i + 1);
        return true;
    }
    ID3D7Device* Device() override { return &device; }
    ID3D7Surface* RenderTarget() override { return &renderSurface; }
    ID3D7Surface* BackBuffer() override { return &backBuffer; }
    ID3D7Surface* DepthStencil() override { return &depthStencil; }
    ID3D7Surface* CreateRegionSurface(std::int32_t type, std::int32_t width, std::int32_t height) override {
        (void)type;
        (void)width;
        (void)height;
        return &regionSurface;
    }
    ID3D7Surface* BufferRegionTarget(std::int32_t type) override {
        if (type == 1) return &backBuffer;
        if (type == 2) return &thirdSurface;
        return &renderSurface;
    }
    void ReleaseDisplay() override {}
    bool TestCooperativeLevel() override { return cooperative; }
    bool RestoreDevice() override { return restoreOk; }
    bool SetDisplayMode(bool windowed, std::int32_t width, std::int32_t height) override {
        displaySet = true;
        setModeWindowed = windowed;
        setModeWidth = width;
        setModeHeight = height;
        return true;
    }
    void GetWindowRect(std::int32_t rect[4]) override {
        ++windowRectCalls;
        rect[0] = 0;
        rect[1] = 0;
        rect[2] = 640;
        rect[3] = 480;
    }
};

struct FakePrimitives final : IPrimitiveManagerHost {
    bool started = true;
    int flushCalls = 0, flushSceneCalls = 0, restartCalls = 0, stopCalls = 0;
    std::vector<std::uint16_t> indexPool = std::vector<std::uint16_t>(4096, 0);
    struct QueueCall {
        std::uint32_t format, prim, stride, base, vertexCount, indexCount;
        const void* vertices;
        const std::uint16_t* indices;
    };
    std::vector<QueueCall> queued;
    bool IsStarted() override { return started; }
    long Flush() override {
        ++flushCalls;
        return 0;
    }
    long FlushScene() override {
        ++flushSceneCalls;
        return 0;
    }
    long Queue(std::uint32_t vertexFormat, std::uint32_t primitiveType, std::uint32_t stride, std::uint32_t baseVertex,
               std::uint32_t vertexCount, const void* vertices, std::uint32_t indexCount,
               const std::uint16_t* indices) override {
        queued.push_back({vertexFormat, primitiveType, stride, baseVertex, vertexCount, indexCount, vertices, indices});
        return 0;
    }
    std::uint16_t* GetIndices(int) override { return indexPool.data(); }
    long Restart(void*, ID3D7Device*) override {
        ++restartCalls;
        started = true;
        return 0;
    }
    void Stop() override {
        ++stopCalls;
        started = false;
    }
    bool Init(void*, ID3D7Device*) override {
        started = true;
        return true;
    }
    void Shutdown() override { started = true; }   // the shipped manager is restarted by Init
};

struct FakeTextures final : ITextureManagerHost {
    std::map<std::uint32_t, sGDTextureParams> params;
    std::map<std::uint32_t, bool> alpha;
    std::map<std::uint32_t, bool> valid;
    std::map<std::uint32_t, bool> resident;
    std::map<std::uint32_t, std::uint32_t> hints;
    std::map<std::uint32_t, float> priorities;
    std::map<std::uint32_t, std::uint32_t> paramSet;
    std::vector<std::uint32_t> created, uploaded;
    std::uint32_t nextHandle = 1;
    int initDeviceCalls = 0, shutdownCalls = 0;
    FakeTextures() {
        valid[0] = true;
        params[0] = sGDTextureParams{};
    }
    bool GetTextureParams(std::uint32_t texture, sGDTextureParams& out) override {
        auto it = params.find(texture);
        if (it == params.end() || !valid[texture]) return false;
        out = it->second;
        return true;
    }
    bool HasAlpha(std::uint32_t texture) override { return alpha[texture]; }
    std::uint32_t NewTexture() override {
        const std::uint32_t handle = nextHandle++;
        valid[handle] = true;
        return handle;
    }
    bool CreateTexture(std::uint32_t texture, std::uint32_t hint, std::uint32_t, std::uint32_t, std::uint32_t,
                       std::uint32_t) override {
        valid[texture] = true;
        hints[texture] = hint;
        created.push_back(texture);
        return true;
    }
    bool LoadTextureLevel(std::uint32_t texture, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                          std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const void*) override {
        uploaded.push_back(texture);
        return valid[texture];
    }
    bool HasTexture(std::uint32_t texture) override { return valid[texture]; }
    std::uint32_t TextureHint(std::uint32_t texture) override {
        auto it = hints.find(texture);
        return it == hints.end() ? 1 : it->second;   // cTextureHandle +0x18 defaults to 1
    }
    void FreeTexture(std::uint32_t texture) override { valid[texture] = false; }
    bool IsValid(std::uint32_t texture) override { return valid[texture]; }
    bool IsResident(std::uint32_t texture) override { return resident[texture]; }
    void SetPriority(std::uint32_t texture, float priority) override { priorities[texture] = priority; }
    bool SetTextureParam(std::uint32_t texture, std::uint32_t type, std::uint32_t value) override {
        paramSet[texture << 8 | type] = value;
        return true;
    }
    void InitDevice(ID3D7Device*) override { ++initDeviceCalls; }
    void ShutdownDevice() override {}
    void Init(void*, ID3D7Device*, const std::vector<cDXTextureFormatInfo>&) override {}
    void Shutdown() override { ++shutdownCalls; }
};

struct FakeGL final : IDriverGL {
    struct Call {
        char kind;
        std::uint32_t a, b, c, d;
    };
    std::vector<Call> calls;
    void Viewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override {
        calls.push_back({'v', x, y, w, h});
    }
    void Scissor(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override {
        calls.push_back({'s', x, y, w, h});
    }
    void Enable(std::uint32_t cap) override { calls.push_back({'e', cap, 0, 0, 0}); }
    void Disable(std::uint32_t cap) override { calls.push_back({'d', cap, 0, 0, 0}); }
};

struct FakeModes final : gd::IModeSource {
    struct Mode {
        int w, h, d;
    };
    std::vector<Mode> modes = {{800, 600, 32}, {1024, 768, 32}};
    int maxW = 1280, maxH = 1024;
    int NumModes() override { return static_cast<int>(modes.size()); }
    void NthMode(int n, int& w, int& h, int& d) override {
        w = modes[static_cast<std::size_t>(n)].w;
        h = modes[static_cast<std::size_t>(n)].h;
        d = modes[static_cast<std::size_t>(n)].d;
    }
    void WindowMaxSizes(int& w, int& h) override {
        w = maxW;
        h = maxH;
    }
};

struct Fixture {
    FakeEnvironment env;
    FakeModes modes;
    FakePrimitives prims;
    FakeTextures textures;
    FakeGL gl;
    cGDriver driver{env, modes, prims, textures, gl};
};

// Expected video-mode count of cVideoModeList for the two display modes above and 1280x1024 max window.
int ExpectedModeCount() {
    const std::uint32_t stockW[12] = {0x320, 0x400, 0x500, 0x500, 0x500, 0x540, 0x540, 0x5a0, 0x5a0, 0x640, 0x640, 0x690};
    const std::uint32_t stockH[12] = {0x258, 0x300, 0x320, 0x3c0, 0x400, 0x2f4, 0x3f0, 0x384, 0x438, 0x384, 0x4b0, 0x41a};
    int count = 0;
    bool covered[12] = {};
    for (const auto& m : std::vector<std::pair<int, int>>{{800, 600}, {1024, 768}}) {
        ++count;   // full screen
        ++count;   // full-screen repeat or windowed entry
        for (int k = 0; k < 12; ++k)
            if (static_cast<std::uint32_t>(m.first) == stockW[k] && static_cast<std::uint32_t>(m.second) == stockH[k])
                covered[k] = true;
    }
    for (int k = 0; k < 12; ++k)
        if (!covered[k] && static_cast<int>(stockW[k]) < 1280 && static_cast<int>(stockH[k]) < 1024) ++count;
    ++count;   // the max-window entry
    return count;
}

void TestLayoutAndFreeHelpers() {
    static_assert(sizeof(sGDLight) == 0x68);
    static_assert(sizeof(sGDMaterial) == 0x44);
    static_assert(sizeof(cDXTextureFormatInfo) == 0x28);
    static_assert(sizeof(sGDPixelFormat) == 0x20);
    // cTextureStageData keeps the 0x80-byte original record's fields; the recovered struct has no tail padding.
    static_assert(offsetof(cTextureStageData, alphaScale) == 0x60);

    CHECK(IsIdentityMatrix(std::vector<float>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}.data()));
    CHECK(!IsIdentityMatrix(std::vector<float>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2}.data()));

    std::vector<cDXTextureFormatInfo> formats;
    sGDTextureFormatContext ctx;
    ctx.formats = &formats;
    sGDPixelFormat rgb;
    rgb.dwFlags = 0x40;
    rgb.dwRGBBitCount = 16;
    rgb.dwRBitMask = 0xf800;
    rgb.dwGBitMask = 0x07e0;
    rgb.dwBBitMask = 0x001f;
    CHECK(SurfaceFormatEnumerator(&rgb, &ctx) == 1);
    CHECK(formats.size() == 1);
    CHECK(formats[0].formatIndex == 3);   // RGB565
    CHECK(formats[0].colorBits == 16);
    CHECK(formats[0].alphaBits == 0);
    CHECK(ctx.compressed == 0);

    sGDPixelFormat dxt;
    dxt.dwFlags = 0x4;
    dxt.dwFourCC = 0x33545844;   // 'DXT3'
    CHECK(SurfaceFormatEnumerator(&dxt, &ctx) == 1);
    CHECK(ctx.compressed == 1);
    CHECK(formats.size() == 2);
    CHECK(formats[1].formatIndex == 0xb);

    sGDPixelFormat ignored;
    ignored.dwFlags = 0x40;
    ignored.dwRGBBitCount = 16;
    ignored.dwRBitMask = 0x1234;
    CHECK(SurfaceFormatEnumerator(&ignored, &ctx) == 1);
    CHECK(formats.size() == 2);
}

void TestInterfaceAndIdentity() {
    Fixture f;
    CHECK(f.driver.GetGZCLSID() == 0xbadb6906u);
    CHECK(f.driver.IsDeviceReady());
    CHECK(f.driver.Init());
    CHECK(f.driver.Init());   // idempotent
    CHECK(f.driver.currentStage == 0);
    CHECK(f.driver.stageCount == 0);
    CHECK(f.driver.reserved184 == 7);
    CHECK(f.driver.modeIndex == -1);
    CHECK(!f.driver.driverInfo.empty());
    CHECK(f.driver.driverInfo.find("Maxis 3D GDriver") == 0);
    CHECK(f.driver.driverInfo.find("Vendor: 10de, Device: 0407, Board: 1234, Chipset: 00a1") != std::string::npos);
    CHECK(std::string(static_cast<const char*>(f.driver.GetDriverInfo())) == f.driver.driverInfo);

    void* out = nullptr;
    CHECK(f.driver.QueryInterface(0xa4554849, &out) && out != nullptr);
    out = nullptr;
    CHECK(f.driver.QueryInterface(0x87e2b87d, &out) && out != nullptr);
    out = nullptr;
    CHECK(f.driver.QueryInterface(0xe69bfe2a, &out) && out != nullptr);
    out = nullptr;
    CHECK(f.driver.QueryInterface(1, &out) && out != nullptr);
    out = nullptr;
    CHECK(f.driver.QueryInterface(0x669565fe, &out) && out != nullptr);
    out = nullptr;
    CHECK(f.driver.QueryInterface(0x9cd86f9, &out) && out == &f.prims);
    out = nullptr;
    CHECK(!f.driver.QueryInterface(0xdeadbeef, &out));
    CHECK(!f.driver.QueryInterface(1, nullptr));
    CHECK(f.driver.Shutdown());
}

void TestRefCount() {
    FakeEnvironment env;
    FakeModes modes;
    FakePrimitives prims;
    FakeTextures textures;
    FakeGL gl;
    cGDriver* driver = cGDriver::Create(env, modes, prims, textures, gl);
    CHECK(driver != nullptr);
    driver->AddRef();
    CHECK(driver->Release() == 1);
    driver->Release();   // deletes itself (FinalRelease)
}

void TestClearAndState() {
    Fixture f;
    f.driver.modeIndex = 0;
    f.modes.modes = {{800, 600, 32}};
    std::vector<cDXTextureFormatInfo> formats;
    f.driver.CountVideoModes();
    CHECK(f.driver.GetDeviceProperties(formats));
    CHECK(f.driver.GetVideoModeInfo(0, f.driver.currentMode));

    f.driver.ClearColor(0.5f, 0.25f, 0.75f, 1.0f);
    CHECK(f.driver.clearColor == gd::PackClearColor(0.5f, 0.25f, 0.75f, 1.0f));
    f.driver.ClearDepth(0.25);
    CHECK(f.driver.clearDepth == 0.25f);
    f.driver.ClearStencil(3);
    CHECK(f.driver.clearStencil == 3);
    f.driver.Clear(0x4000);
    CHECK(f.env.device.clearFlags == 1);   // colour
    f.driver.Clear(0x1000);
    CHECK(f.env.device.clearFlags == 2);   // depth
    f.driver.Clear(0x2000);
    CHECK(f.env.device.clearFlags == 4);   // stencil (caps present)
    f.driver.Clear(0x7000);
    CHECK(f.env.device.clearFlags == 7);
    CHECK(f.env.device.clearColor == f.driver.clearColor);
    CHECK(f.env.device.clearDepth == 0.25f);
    CHECK(f.env.device.clearStencil == 3);

    f.driver.DepthFunc(gd::test::LEqual);
    CHECK(f.env.device.renderStates[0x17] == 4);
    f.driver.DepthMask(false);
    CHECK(f.env.device.renderStates[0xe] == 0);
    f.driver.BlendFunc(gd::blend::SrcAlpha, gd::blend::InvSrcAlpha);
    CHECK(f.env.device.renderStates[0x13] == 5);
    CHECK(f.env.device.renderStates[0x14] == 6);
    f.driver.AlphaFunc(gd::test::Greater, 0.5f);
    CHECK(f.env.device.renderStates[0x19] == 5);
    CHECK(f.env.device.renderStates[0x18] == 128);
    f.driver.ShadeModel(1);
    CHECK(f.env.device.renderStates[9] == 2);
    f.driver.StencilFunc(gd::test::Always, 7, 0xff);
    CHECK(f.env.device.renderStates[0x3a] == 0xff);
    CHECK(f.env.device.renderStates[0x38] == 8);
    CHECK(f.env.device.renderStates[0x39] == 7);
    f.driver.StencilMask(0x0f);
    CHECK(f.env.device.renderStates[0x3b] == 0x0f);
    f.driver.StencilOp(0, 1, 3);
    CHECK(f.env.device.renderStates[0x35] == 2);
    CHECK(f.env.device.renderStates[0x36] == 1);
    CHECK(f.env.device.renderStates[0x37] == 4);
    f.driver.PolygonOffset(9);
    CHECK(f.env.device.renderStates[0x2f] == 9);

    // ColourMask is a flush-only no-op.
    f.driver.ColorMask(false);
    CHECK(f.env.device.renderStates.find(0x80) == f.env.device.renderStates.end());

    f.driver.Enable(gd::state::DepthTest);
    CHECK(f.env.device.renderStates[7] == 1);
    CHECK(f.driver.IsEnabled(gd::state::DepthTest));
    f.driver.Disable(gd::state::DepthTest);
    CHECK(f.env.device.renderStates[7] == 0);
    CHECK(!f.driver.IsEnabled(gd::state::DepthTest));
    CHECK(f.driver.IsEnabled(static_cast<gd::eGDDriverState>(7)));

    // Cull-face: disabling selects clockwise culling (quirk), enabling selects counter-clockwise.
    f.driver.Enable(gd::state::CullFace);
    CHECK(f.env.device.renderStates[0x16] == 2);
    f.driver.Disable(gd::state::CullFace);
    CHECK(f.env.device.renderStates[0x16] == 1);

    // Texturing toggles the current stage record, not the device.
    f.driver.Enable(gd::state::Texturing);
    CHECK(f.driver.stages[0].textureEnabled == 1);
    CHECK(f.driver.IsEnabled(gd::state::Texturing));
    f.driver.Disable(gd::state::Texturing);
    CHECK(f.driver.stages[0].textureEnabled == 0);
    CHECK(!f.driver.IsEnabled(gd::state::Texturing));

    f.driver.Enable(gd::state::Fog);
    CHECK(f.env.device.renderStates[0x1c] == 1);
    f.driver.Disable(gd::state::Fog);
    CHECK(f.env.device.renderStates[0x1c] == 0);

    // Stencil without caps reports error 3.
    FakeEnvironment env2;
    FakeModes modes2;
    FakePrimitives prims2;
    FakeTextures textures2;
    FakeGL gl2;
    cGDriver plain(env2, modes2, prims2, textures2, gl2);
    plain.StencilFunc(gd::test::Always, 1, 1);
    CHECK(plain.GetError() == 3);
    CHECK(plain.GetError() == 0);   // reading clears the error

    // Fog is rejected when the device description has no table fog.
    env2.device.caps[caps::kFog] = 0;
    plain.Fog(static_cast<gd::eGDFogParamType>(0), static_cast<gd::eGDFogParam>(1));
    CHECK(plain.GetError() == 3);

    // Stub entry points report error 3.
    plain.BitBlt(0, 0, 1, 1, 0, 0, nullptr, false, nullptr);
    CHECK(plain.GetError() == 3);
    plain.StretchBlt(0, 0, 1, 1, 1, 1, 0, 0, nullptr, false, nullptr);
    CHECK(plain.GetError() == 3);
    CHECK(!plain.Punt(1, nullptr));
}

void TestDefaultState() {
    Fixture f;
    f.modes.modes = {{800, 600, 32}};
    f.driver.modeIndex = 0;
    f.driver.CountVideoModes();
    std::vector<cDXTextureFormatInfo> formats;
    CHECK(f.driver.GetDeviceProperties(formats));
    f.env.device.renderStates.clear();
    f.env.device.stageStateLog.clear();
    f.driver.SetDefaultD3DDeviceState();
    CHECK(f.env.device.renderStates[0x16] == 1);           // cull clockwise
    CHECK(f.env.device.renderStates[0x1a] == 1);           // dither
    CHECK(f.env.device.renderStates[4] == 1);              // texture perspective
    CHECK(f.env.device.renderStates[0xe] == 1);            // z-write
    CHECK(f.env.device.renderStates[0x17] == 4);           // z-func less-equal
    CHECK(f.env.device.renderStates[8] == 3);              // solid fill
    CHECK(f.env.device.renderStates[0x34] == 0);           // stencil off
    CHECK(f.env.device.renderStates[0x36] == 6);           // stencil z-fail invert
    CHECK(f.env.device.renderStates[0x38] == 8);           // stencil func always
    CHECK(f.env.device.renderStates[0x3c] == 0xffffffffu); // texture factor white
    CHECK(f.env.device.renderStates[0x89] == 0);           // lighting off
    CHECK(f.env.device.renderStates[0x8d] == 1);           // vertex colours on
    CHECK(f.env.device.renderStates[0x93] == 1);
    CHECK(f.env.device.renderStates[0x91] == 1);
    CHECK(f.env.device.textures[0] == 0);
    CHECK(f.env.device.HasStageState(0, 1, 2));            // color op select-arg1
    CHECK(f.env.device.HasStageState(0, 0x10, 1) || f.env.device.HasStageState(0, 0x10, 1));
    CHECK(f.driver.stages.size() == 2);
    CHECK(f.driver.stages[0].dirty == 1);
    CHECK(f.driver.stagesDirty);
}

void TestCapabilities() {
    Fixture f;
    f.modes.modes = {{800, 600, 32}, {1024, 768, 32}};
    f.driver.modeIndex = 0;
    CHECK(f.driver.CountVideoModes() == ExpectedModeCount());
    CHECK(f.driver.GetVideoModeInfo(0, f.driver.currentMode));
    std::vector<cDXTextureFormatInfo> formats;
    CHECK(f.driver.GetDeviceProperties(formats));
    CHECK(f.driver.GetVideoModeInfo(0, f.driver.currentMode));
    CHECK(f.driver.stageCount == 2);
    CHECK(f.driver.stages.size() == 2);
    CHECK(formats.size() == 4);
    CHECK(formats[0].formatIndex == 1);   // ARGB8888
    CHECK(formats[0].alphaBits == 8);
    CHECK(formats[1].formatIndex == 3);   // RGB565
    CHECK(formats[2].formatIndex == 9);   // DXT1
    CHECK(formats[2].fourCC == 0x31545844u);
    CHECK(formats[3].formatIndex == 0);   // 24-bit RGB888
    // The capability bytes land in the current mode entry.
    gd::sGDMode capMode{};
    CHECK(f.driver.GetVideoModeInfo(0, capMode));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&capMode);
    CHECK(bytes[0x21] == 1);   // stencil
    std::uint32_t maxTexture = 0;
    std::memcpy(&maxTexture, bytes + 0x24, 4);
    CHECK(maxTexture == 0x800);
    CHECK(bytes[0x28] == 1);
    CHECK(bytes[0x2c] == 1);   // compressed formats present
    CHECK(bytes[0x2f] == 0);   // zbuffer bit depth 0x10 -> bit 0 clear
    CHECK(bytes[0x30] == 1);
    std::uint32_t alphaMask = 0, redMask = 0, greenMask = 0, blueMask = 0;
    std::memcpy(&alphaMask, bytes + 0x10, 4);
    std::memcpy(&redMask, bytes + 0x14, 4);
    std::memcpy(&greenMask, bytes + 0x18, 4);
    std::memcpy(&blueMask, bytes + 0x1c, 4);
    CHECK(alphaMask == 0xff000000);
    CHECK(redMask == 0x00ff0000);
    CHECK(greenMask == 0x0000ff00);
    CHECK(blueMask == 0x000000ff);
    CHECK(f.driver.HasStencilBuffer());
    CHECK(f.driver.HasFog());
    CHECK(f.driver.HasCombine());

    // Error bookkeeping when the mode index is out of range.
    f.driver.modeIndex = 99;
    CHECK(!f.driver.GetDeviceProperties(formats));
}

void TestTextureStages() {
    Fixture f;
    f.modes.modes = {{800, 600, 32}};
    f.driver.modeIndex = 0;
    f.driver.CountVideoModes();
    std::vector<cDXTextureFormatInfo> formats;
    CHECK(f.driver.GetDeviceProperties(formats));
    CHECK(f.driver.GetVideoModeInfo(0, f.driver.currentMode));

    f.textures.valid[7] = true;
    f.textures.alpha[7] = true;
    f.textures.params[7] = sGDTextureParams{1, 2, 3, 2};
    f.driver.BindTexture(0, 7);
    CHECK(f.driver.textureParamsValid);
    CHECK(f.driver.stages[0].textureId == 7);
    CHECK(f.driver.stages[0].magFilter == 1);
    CHECK(f.driver.stages[0].minFilter == 2);
    CHECK(f.driver.stages[0].wrapS == 3);
    CHECK(f.driver.stages[0].wrapT == 2);
    CHECK(f.driver.stages[0].textureHasAlpha == 1);
    CHECK(f.driver.stages[0].dirty == 1);
    CHECK(f.driver.stagesDirty);
    // Unknown texture falls back to texture 0's parameters.
    f.driver.BindTexture(0, 99);
    CHECK(f.driver.stages[0].magFilter == 0);
    // Only 2D targets exist.
    f.driver.BindTexture(1, 7);
    CHECK(f.driver.GetError() == 1);

    f.driver.SetTexture(7, 0);
    CHECK(!f.driver.textureParamsValid);
    CHECK(f.driver.stages[0].textureId == 7);
    f.driver.SetTexture(8, 0);
    CHECK(f.driver.stages[0].textureId == 8);
    CHECK(f.driver.GetTexture(0) == 8);
    CHECK(f.driver.GetTexture(9) == 0);

    // TexParameter stores 0..3 in the record.
    f.driver.TexParameter(0, 0, 0);
    CHECK(f.driver.stages[0].magFilter == 0);
    f.driver.TexParameter(0, 1, 7);
    CHECK(f.driver.stages[0].minFilter == 7);
    f.driver.TexParameter(0, 2, 2);
    CHECK(f.driver.stages[0].wrapS == 2);
    f.driver.TexParameter(0, 3, 4);
    CHECK(f.driver.stages[0].wrapT == 4);

    // TexEnv selects the environment mode.
    f.driver.TexEnv(0, 0, 1);
    CHECK(f.driver.stages[0].envMode == 1);
    // TexEnv(float[4]) packs the texture factor into render state 0x3c.
    const float factor[4] = {0.0f, 0.5f, 1.0f, 0.25f};
    f.driver.TexEnv(0, 0, factor);
    CHECK(f.env.device.renderStates[0x3c] == gd::PackClearColor(0.0f, 0.5f, 1.0f, 0.25f));

    // Stage selection is bounds-checked against the caps count, not the vector size.
    f.driver.TexStage(1);
    CHECK(f.driver.currentStage == 1);
    CHECK(f.driver.GetError() == 0);
    f.driver.TexStage(2);
    CHECK(f.driver.GetError() == 2);
    f.driver.TexStage(0);

    // Combiner state is only accepted when currentMode byte +0x29 is set.
    f.driver.TexStageCombine(static_cast<gd::eGDTextureStageCombineParamType>(0),
                             static_cast<gd::eGDTextureStageCombineModeParam>(4));
    CHECK(f.driver.stages[0].rgbCombine == 4);
    f.driver.TexStageCombine(static_cast<gd::eGDTextureStageCombineSourceParamType>(1),
                             static_cast<gd::eGDTextureStageCombineSourceParam>(2));
    CHECK(f.driver.stages[0].rgbSource[1] == 2);
    f.driver.TexStageCombine(static_cast<gd::eGDTextureStageCombineOperandParamType>(5),
                             static_cast<gd::eGDBlend>(5));
    CHECK(f.driver.stages[0].alphaOperand[1] == 5);
    f.driver.TexStageCombine(static_cast<gd::eGDTextureStageCombineScaleParamType>(1),
                             static_cast<gd::eGDTextureStageCombineScaleParam>(3));
    CHECK(f.driver.stages[0].alphaScale == 3);

    // SetCombiner: byte 0 = colour op, byte 4 = alpha op, low nibbles = sources, high nibbles = operands.
    alignas(4) std::uint8_t combiner[16] = {4, 0x02, 0x13, 0x22, 5, 0x01, 0x13, 0x23, 2, 3, 0, 0, 0, 0, 0, 0};
    f.driver.SetCombiner(*reinterpret_cast<const gd::cGDCombiner*>(combiner), 0);
    CHECK(f.driver.stages[0].envMode == 4);
    CHECK(f.driver.stages[0].rgbCombine == 4);
    CHECK(f.driver.stages[0].alphaCombine == 5);
    CHECK(f.driver.stages[0].rgbSource[0] == 2);
    CHECK(f.driver.stages[0].rgbSource[1] == 3);
    CHECK(f.driver.stages[0].rgbSource[2] == 2);
    CHECK(f.driver.stages[0].alphaSource[0] == 1);
    CHECK(f.driver.stages[0].alphaSource[1] == 3);
    CHECK(f.driver.stages[0].alphaSource[2] == 3);
    CHECK(f.driver.stages[0].rgbOperand[0] == lookup::kColorOperand[0]);
    CHECK(f.driver.stages[0].rgbOperand[1] == lookup::kColorOperand[1]);
    CHECK(f.driver.stages[0].rgbOperand[2] == lookup::kColorOperand[2]);
    CHECK(f.driver.stages[0].alphaOperand[0] == lookup::kAlphaOperand[0]);
    CHECK(f.driver.stages[0].alphaOperand[1] == lookup::kAlphaOperand[1]);
    CHECK(f.driver.stages[0].alphaOperand[2] == lookup::kAlphaOperand[2]);
    CHECK(f.driver.stages[0].rgbScale == 2);
    CHECK(f.driver.stages[0].alphaScale == 3);

    // ApplyTextureState binds the stage texture and sets the environment states.
    f.textures.valid[8] = true;
    f.textures.alpha[8] = true;
    f.driver.stages[0].textureHasAlpha = 1;
    f.driver.stages[0].textureEnabled = 1;
    f.driver.stages[0].envMode = 1;   // modulate
    f.driver.stages[0].appliedEnvMode = 0xffffffffu;
    f.driver.stages[0].dirty = 1;
    f.driver.stagesDirty = true;
    f.env.device.stageStateLog.clear();
    f.driver.ApplyTextureState();
    CHECK(!f.driver.stagesDirty);
    CHECK(f.driver.stages[0].dirty == 0);
    CHECK(f.driver.stages[0].appliedEnvMode == 1);
    CHECK(f.env.device.textures[0] == 8);
    CHECK(f.env.device.HasStageState(0, 1, 4));   // color op MODULATE
    CHECK(f.env.device.HasStageState(0, 5, 2));   // alpha arg1 TEXTURE

    // TexImage2D creates a texture surface when needed and uploads with the unpack row length.
    f.textures.valid[8] = false;
    f.driver.PixelStore(0, 64);
    int dummy = 0;
    f.driver.TexImage2D(0, 0, gd::kFmtARGB8888, 64, 32, 0, 0, 0, &dummy);
    CHECK(f.textures.valid[8]);
    CHECK(f.textures.hints[8] == 1);
    CHECK(f.textures.uploaded.back() == 8);
    // Invalid size.
    f.driver.TexImage2D(0, 0, gd::kFmtARGB8888, 0, 32, 0, 0, 0, &dummy);
    CHECK(f.driver.GetError() == 2);

    // CreateTexture/LoadTextureLevel/GenTextures/DeleteTextures.
    const std::uint32_t created = f.driver.CreateTexture(gd::kFmtARGB8888, 4, 4, 0, 0);
    CHECK(created == f.textures.nextHandle - 1);
    CHECK(f.textures.valid[created]);
    f.driver.LoadTextureLevel(created, 0, 0, 0, 4, 4, 0, 0, 4, &dummy);
    CHECK(f.textures.uploaded.back() == created);
    std::uint32_t ids[3] = {};
    f.driver.GenTextures(3, ids);
    CHECK(ids[0] != 0 && ids[1] != 0 && ids[2] != 0);
    CHECK(f.driver.IsTexture(ids[0]));
    CHECK(!f.driver.IsTexture(0xffff));
    f.driver.texturesReady = true;
    f.driver.DeleteTextures(3, ids);
    CHECK(!f.textures.valid[ids[0]]);

    // AreTexturesResident skips residence[0] (quirk).
    f.textures.resident[ids[0]] = true;
    f.textures.resident[ids[1]] = false;
    bool residence[4] = {true, true, true, true};
    CHECK(f.driver.AreTexturesResident(2, ids, residence));
    CHECK(residence[0] == true);    // untouched
    CHECK(residence[1] == true);
    CHECK(residence[2] == false);
    f.driver.PrioritizeTextures(1, ids, std::vector<float>{0.75f}.data());
    CHECK(f.textures.priorities[ids[0]] == 0.75f);

    // Matrix mode: 0 -> transform state 1, 1 -> 3, anything else ignored.
    f.driver.MatrixMode(0);
    f.driver.LoadIdentity();
    CHECK(f.env.device.transforms.back() == 1);
    f.driver.MatrixMode(1);
    f.driver.LoadIdentity();
    CHECK(f.env.device.transforms.back() == 3);
    const std::size_t before = f.env.device.transforms.size();
    f.driver.MatrixMode(2);
    f.driver.LoadIdentity();
    CHECK(f.env.device.transforms.size() == before);

    // TexStageMatrix: identity clears the texture matrix; a real matrix sets it and the coordinate index.
    f.env.device.stageStateLog.clear();
    f.driver.TexStageMatrix(nullptr, 0, 0, 0);
    CHECK(f.env.device.HasStageState(0, 0x18, 0));
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    m[12] = 0.5f;
    f.driver.TexStageMatrix(m, 0, 1, static_cast<gd::eGDTexMatFlags>(2));
    CHECK(f.env.device.HasStageState(0, 0x18, 0x101));
    CHECK(f.env.device.transforms.back() == 0x10);
    f.env.device.stageStateLog.clear();
    f.driver.TexStageCoord(static_cast<gd::eGDTexCoordSource>(0x10));
    CHECK(f.env.device.HasStageState(0, 0xb, 0x20000));   // projection-space texture coordinates
    f.driver.TexStageCoord(static_cast<gd::eGDTexCoordSource>(0x40));
    CHECK(f.driver.GetError() == 2);
}

void TestDrawing() {
    Fixture f;
    f.driver.InterleavedArrays(gd::vf::V3F_C4UB_2T2F, 32, reinterpret_cast<const void*>(0x1000));
    CHECK(f.driver.vertexFormat == gd::vf::V3F_C4UB_2T2F);
    CHECK(f.driver.vertexStride == 32);
    CHECK(f.driver.standardStride == 32);
    CHECK(f.driver.interleavedSet);
    CHECK(f.driver.vertexData == reinterpret_cast<const void*>(0x1000));
    f.driver.InterleavedArrays(gd::vf::V3F_C4UB_2T2F, 0, reinterpret_cast<const void*>(0x2000));
    CHECK(f.driver.vertexStride == f.driver.standardStride);

    f.prims.queued.clear();
    std::vector<unsigned char> vertices(4 * 32);
    f.driver.InterleavedArrays(gd::vf::V3F_C4UB_2T2F, 32, vertices.data());
    f.driver.DrawArrays(gd::prim::Quads, 1, 4);
    CHECK(f.prims.queued.size() == 1);
    if (!f.prims.queued.empty()) {
        const auto& q = f.prims.queued[0];
        CHECK(q.prim == 4);
        CHECK(q.format == gd::vf::V3F_C4UB_2T2F);
        CHECK(q.stride == 32);
        CHECK(q.base == 0);
        CHECK(q.vertexCount == 4);
        CHECK(q.indexCount == 6);
        CHECK(q.vertices == vertices.data() + 32);
        CHECK(f.prims.indexPool[0] == 0 && f.prims.indexPool[1] == 1 && f.prims.indexPool[2] == 2);
        CHECK(f.prims.indexPool[3] == 0 && f.prims.indexPool[4] == 2 && f.prims.indexPool[5] == 3);
    }

    f.prims.queued.clear();
    f.driver.DrawArrays(gd::prim::TriangleStrip, 0, 4);
    CHECK(f.prims.indexPool[0] == 0 && f.prims.indexPool[1] == 1 && f.prims.indexPool[2] == 2);
    CHECK(f.prims.indexPool[3] == 2 && f.prims.indexPool[4] == 1 && f.prims.indexPool[5] == 3);
    CHECK(f.prims.queued[0].indexCount == 6);

    f.prims.queued.clear();
    f.driver.DrawArrays(gd::prim::TriangleFan, 0, 4);
    CHECK(f.prims.indexPool[0] == 0 && f.prims.indexPool[1] == 1 && f.prims.indexPool[2] == 2);
    CHECK(f.prims.indexPool[3] == 0 && f.prims.indexPool[4] == 2 && f.prims.indexPool[5] == 3);

    f.prims.queued.clear();
    f.driver.DrawArrays(gd::prim::Triangles, 0, 3);
    CHECK(f.prims.queued.size() == 1);
    CHECK(f.prims.queued[0].indexCount == 0);
    CHECK(f.prims.queued[0].indices == nullptr);
    CHECK(f.prims.queued[0].prim == 4);

    // Out-of-range / empty draws are dropped, and a stopped manager ignores everything.
    f.prims.queued.clear();
    f.driver.DrawArrays(static_cast<gd::eGDPrimType>(8), 0, 3);
    f.driver.DrawArrays(gd::prim::Triangles, 0, 0);
    CHECK(f.prims.queued.empty());
    f.prims.started = false;
    f.driver.DrawArrays(gd::prim::Triangles, 0, 3);
    CHECK(f.prims.queued.empty());
    f.prims.started = true;

    // Indexed quads (16-bit).
    const std::uint16_t quad16[4] = {0, 1, 2, 3};
    f.prims.queued.clear();
    f.driver.DrawElements(gd::prim::Quads, 4, 3, quad16);
    CHECK(f.prims.queued.size() == 1);
    CHECK(f.prims.queued[0].indexCount == 6);
    CHECK(f.prims.queued[0].base == 0);
    CHECK(f.prims.queued[0].vertexCount == 4);
    CHECK(f.prims.indexPool[0] == 0 && f.prims.indexPool[1] == 1 && f.prims.indexPool[2] == 2);
    CHECK(f.prims.indexPool[3] == 0 && f.prims.indexPool[4] == 2 && f.prims.indexPool[5] == 3);

    // Indexed triangle strip (32-bit).
    const std::uint32_t strip32[4] = {2, 3, 4, 5};
    f.prims.queued.clear();
    f.driver.DrawElements(gd::prim::TriangleStrip, 4, 5, strip32);
    CHECK(f.prims.indexPool[0] == 2 && f.prims.indexPool[1] == 3 && f.prims.indexPool[2] == 4);
    CHECK(f.prims.indexPool[3] == 4 && f.prims.indexPool[4] == 3 && f.prims.indexPool[5] == 5);
    CHECK(f.prims.queued[0].base == 2);
    CHECK(f.prims.queued[0].vertexCount == 4);

    // Indexed triangle fan (8-bit).
    const std::uint8_t fan8[4] = {1, 2, 3, 4};
    f.prims.queued.clear();
    f.driver.DrawElements(gd::prim::TriangleFan, 4, 1, fan8);
    CHECK(f.prims.indexPool[0] == 1 && f.prims.indexPool[1] == 2 && f.prims.indexPool[2] == 3);
    CHECK(f.prims.indexPool[3] == 1 && f.prims.indexPool[4] == 3 && f.prims.indexPool[5] == 4);

    // Plain triangles convert the indices and report the touched range.
    const std::uint32_t tris[3] = {7, 8, 9};
    f.prims.queued.clear();
    f.driver.DrawElements(gd::prim::Triangles, 3, 5, tris);
    CHECK(f.prims.queued[0].base == 7);
    CHECK(f.prims.queued[0].vertexCount == 3);
    CHECK(f.prims.queued[0].indexCount == 3);

    // Vertex-format helpers delegate to nRZSimGL.
    CHECK(f.driver.VertexFormatStride(gd::vf::V3F_C4UB_2T2F) == 32);
    CHECK(f.driver.MakeVertexFormat(gd::vf::V3F_C4UB_2T2F) == static_cast<gd::eGDVertexFormat>(-0x7fff7effu));
}

void TestVideoModes() {
    Fixture f;
    CHECK(f.driver.Init());   // idempotent; clears the mode list and rebuilds the driver info
    f.driver.GetError();
    const int count = f.driver.CountVideoModes();
    CHECK(count == ExpectedModeCount());
    CHECK(f.driver.CountVideoModes() == count);   // the list is built once
    gd::sGDMode mode{};
    CHECK(f.driver.GetVideoModeInfo(0, mode));
    CHECK(mode.flags[0] == gd::kModeFullScreen);
    CHECK(mode.width == 800);
    CHECK(mode.height == 600);
    CHECK(mode.bitsPerPixel == 32);
    CHECK(f.driver.GetVideoModeInfo(1, mode));
    CHECK(mode.flags[0] == gd::kModeWindowed);
    CHECK(f.driver.GetVideoModeInfo(static_cast<std::uint32_t>(count), mode) == false);
    CHECK(f.driver.GetError() == 2);

    gd::sGDMode current{};
    current.id = 42;
    f.driver.currentMode = current;
    CHECK(f.driver.GetVideoModeInfo(mode));
    CHECK(mode.id == 42);

    // SetVideoMode goes through SetDisplayMode + SetupViewport.
    void* window = reinterpret_cast<void*>(0x1);
    CHECK(f.driver.SetVideoMode(1, window, true, false));
    CHECK(f.env.displaySet);
    CHECK(f.env.setModeWindowed);
    CHECK(f.env.setModeWidth == 800);
    CHECK(f.env.setModeHeight == 600);
    CHECK(f.env.contextCreated);
    CHECK(f.driver.texturesReady);
    CHECK(f.driver.modeIndex == 1);
    CHECK(f.env.windowRectCalls >= 1);

    // An out-of-range mode reports error 2.
    CHECK(!f.driver.SetVideoMode(999, window, false, false));
    CHECK(f.driver.GetError() == 2);

    // Teardown.
    CHECK(f.driver.SetVideoMode(-1, window, false, false));
    CHECK(f.driver.modeIndex == -1);
    CHECK(f.env.contextDestroyed);
}

void TestBufferRegions() {
    Fixture f;
    f.driver.deviceViewport[0] = 0;
    f.driver.deviceViewport[1] = 0;
    f.driver.deviceViewport[2] = 640;
    f.driver.deviceViewport[3] = 480;
    CHECK(f.driver.BufferRegionEnabled());
    CHECK(f.driver.CanDoPartialRegionWrites());
    CHECK(f.driver.CanDoOffsetReads());

    CHECK(f.driver.NewBufferRegion(0) == 1);
    CHECK(f.driver.IsBufferRegion(1));
    CHECK(!f.driver.IsBufferRegion(2));
    CHECK(f.driver.NewBufferRegion(1) == 2);
    CHECK(f.driver.NewBufferRegion(9) == 0);   // unsupported type
    CHECK(f.driver.DeleteBufferRegion(1));
    CHECK(!f.driver.IsBufferRegion(1));
    CHECK(!f.driver.DeleteBufferRegion(1));

    // Read/draw bounds checks.
    CHECK(f.driver.ReadBufferRegion(2, 0, 0, 640, 480, 0, 0));
    CHECK(!f.driver.ReadBufferRegion(2, 0, 0, 641, 480, 0, 0));
    CHECK(f.driver.DrawBufferRegion(2, 0, 0, 640, 480, 0, 0));
    CHECK(!f.driver.DrawBufferRegion(2, 0, 0, 641, 480, 0, 0));
    CHECK(f.env.backBuffer.bltCalls >= 1);          // region type 1 blits to the back buffer
    CHECK(f.env.regionSurface.bltCalls >= 1);       // ReadBufferRegion blits into the region surface

    CHECK(f.driver.DeleteAllBufferRegions());
    // Quirk: DeleteAllBufferRegions leaves an entry whose index is 0, so region id 1 still answers true.
    CHECK(f.driver.IsBufferRegion(1));
    CHECK(f.driver.NewBufferRegion(2) == 2);   // appended, not reused

    // Lost device disables region creation.
    f.driver.deviceLost = true;
    CHECK(f.driver.NewBufferRegion(0) == 0);
    f.driver.deviceLost = false;

    // CopyColorBuffer is partial (needs cIGZBuffer); it still checks the render target.
    int destination = 0;
    CHECK(f.driver.CopyColorBuffer(0, 0, 4, 4, &destination) == &destination);
    CHECK(f.driver.copyColorChecked && f.driver.copyColorAvailable);
}

void TestLighting() {
    Fixture f;
    const float ambient[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float diffuse[4] = {0.5f, 0.6f, 0.7f, 0.8f};
    const float specular[4] = {0.9f, 0.8f, 0.7f, 0.6f};
    f.driver.LightColor(2, 0, ambient);
    CHECK(f.driver.lights.size() == 3);
    CHECK(f.driver.lights[2].ambient[0] == 0.1f);
    CHECK(f.env.device.lights.size() == 3);
    CHECK(f.env.device.lights[2].ambient[3] == 0.4f);
    f.driver.LightColor(3, 1, diffuse);
    CHECK(f.driver.lights.size() == 4);
    CHECK(f.driver.lights[3].diffuse[2] == 0.7f);
    f.driver.LightColor(1, ambient, diffuse, specular);
    CHECK(f.driver.lights[1].ambient[1] == 0.2f);
    CHECK(f.driver.lights[1].diffuse[1] == 0.6f);
    CHECK(f.driver.lights[1].specular[1] == 0.8f);
    // Position/direction are no-ops.
    const float position[3] = {1, 2, 3};
    f.driver.LightPosition(0, position);
    f.driver.LightDirection(0, position);
    CHECK(f.driver.lights[0].position[0] == 0.0f);

    f.driver.MaterialColor(0, ambient);
    CHECK(f.driver.material.ambient[3] == 0.4f);
    f.driver.MaterialColor(1, diffuse);
    CHECK(f.driver.material.diffuse[2] == 0.7f);
    f.driver.MaterialColor(2, specular);
    CHECK(f.driver.material.specular[0] == 0.9f);
    f.driver.MaterialColor(3, ambient);
    CHECK(f.driver.material.emissive[1] == 0.2f);
    const float power = 12.5f;
    f.driver.MaterialColor(4, &power);
    CHECK(f.driver.material.power == 12.5f);
    CHECK(f.env.device.material.power == 12.5f);
    f.driver.MaterialColor(ambient, diffuse, specular, ambient, -1.0f);
    CHECK(f.driver.material.power == 12.5f);   // negative power ignored
    CHECK(f.driver.material.ambient[0] == 0.1f);

    f.driver.EnableLighting(true);
    CHECK(f.driver.lightingEnabled);
    CHECK(f.env.device.renderStates[0x89] == 1);
    f.driver.EnableLighting(true);   // no change -> no call
    f.driver.EnableLight(2, true);
    CHECK(f.env.device.lightEnables.back() == (2u << 1 | 1u));
    f.driver.EnableLight(2, false);
    CHECK(f.env.device.lightEnables.back() == (2u << 1));

    f.driver.LightModelAmbient(1, 0.5f, 0.25f, 1);
    CHECK(f.env.device.renderStates[0x8b] == gd::PackClearColor(1, 0.5f, 0.25f, 1));

    // ColorMultiplier enables lighting, sets the ambient material to white and the ambient render colour.
    f.driver.EnableLighting(false);
    f.driver.ColorMultiplier(0.25f, 0.5f, 0.75f);
    CHECK(f.driver.lightingEnabled);
    CHECK(f.driver.material.ambient[0] == 1.0f && f.driver.material.ambient[1] == 1.0f &&
          f.driver.material.ambient[2] == 1.0f);
    CHECK(f.env.device.renderStates[0x8b] == gd::PackClearColor(0.25f, 0.5f, 0.75f, 1));

    f.driver.EnableVertexColors(false, true);
    CHECK(!f.driver.vertexColorEnabled);
    CHECK(f.driver.vertexAlphaEnabled);
    CHECK(f.env.device.renderStates[0x8d] == 1);
    CHECK(f.env.device.renderStates[0x93] == 0);
    CHECK(f.env.device.renderStates[0x91] == 1);
    f.driver.AlphaMultiplier(0.5f);
    CHECK(f.driver.material.diffuse[3] == 0.5f);
    CHECK(f.env.device.renderStates[0x91] == 0);
    f.driver.AlphaMultiplier(1.0f);
    CHECK(f.env.device.renderStates[0x91] == 1);
}

void TestSceneAndFlush() {
    Fixture f;
    f.modes.modes = {{800, 600, 32}};
    f.driver.modeIndex = 0;
    f.driver.CountVideoModes();
    std::vector<cDXTextureFormatInfo> formats;
    CHECK(f.driver.GetDeviceProperties(formats));

    f.driver.stages[0].textureId = 9;
    f.driver.stages[0].envMode = 3;
    CHECK(f.driver.BeginScene());
    CHECK(f.driver.inScene);
    CHECK(f.env.device.beginSceneCalls == 1);
    CHECK(f.driver.stages[0].textureId == 0);
    CHECK(f.driver.stages[0].envMode == 0);
    f.driver.EndScene();
    CHECK(!f.driver.inScene);
    CHECK(f.env.device.endSceneCalls == 1);
    // Calling EndScene again does nothing.
    f.driver.EndScene();
    CHECK(f.env.device.endSceneCalls == 1);

    // Flush: flush the scene, end it, restart the manager if it stopped, then begin a new scene.
    f.prims.flushSceneCalls = 0;
    f.prims.restartCalls = 0;
    f.prims.started = false;
    f.driver.Flush();
    CHECK(f.prims.flushSceneCalls == 1);
    CHECK(f.prims.restartCalls == 1);
    CHECK(f.driver.inScene);
    CHECK(!f.driver.deviceLost);

    // Lost device: stop the managers and mark the driver lost; the next Flush restores.
    f.env.cooperative = false;
    f.driver.Flush();
    CHECK(f.driver.deviceLost);
    CHECK(f.prims.stopCalls == 1);
    f.env.cooperative = true;
    f.prims.started = false;
    f.driver.Flush();
    CHECK(!f.driver.deviceLost);
    CHECK(f.textures.initDeviceCalls == 1);
    CHECK(f.prims.restartCalls == 2);
}

void TestGetSetMisc() {
    Fixture f;
    std::memcpy(f.driver.deviceDescription, f.env.device.caps, caps::kDeviceDescSize);
    f.driver.GetInteger(0, nullptr);   // no crash
    bool flag = true;
    f.driver.GetBoolean(0, &flag);
    CHECK(flag);
    float value = 3.0f;
    f.driver.GetFloat(0, &value);
    CHECK(value == 3.0f);

    f.driver.SetViewport(1, 2, 30, 40);
    CHECK(f.driver.viewport[0] == 1 && f.driver.viewport[1] == 2 && f.driver.viewport[2] == 30 &&
          f.driver.viewport[3] == 40);
    std::uint32_t vp[4] = {};
    f.driver.GetViewport(vp);
    CHECK(vp[0] == 1 && vp[1] == 2 && vp[2] == 31 && vp[3] == 42);
    CHECK(f.gl.calls.size() == 3);
    CHECK(f.gl.calls[0].kind == 'v' && f.gl.calls[1].kind == 's' && f.gl.calls[2].kind == 'e');
    CHECK(f.gl.calls[2].a == 0xc11);

    f.driver.deviceViewport[2] = 640;
    f.driver.deviceViewport[3] = 480;
    f.driver.SetViewport();
    CHECK(f.driver.viewport[0] == 0 && f.driver.viewport[1] == 0 && f.driver.viewport[2] == 640 &&
          f.driver.viewport[3] == 480);
    CHECK(f.gl.calls.back().kind == 'd' && f.gl.calls.back().a == 0xc11);

    f.driver.Fog(static_cast<gd::eGDFogParamType>(0), static_cast<gd::eGDFogParam>(2));
    CHECK(f.env.device.renderStates[0x8c] == 3);
    f.driver.Fog(static_cast<gd::eGDFogParamType>(1), static_cast<gd::eGDFogParam>(0));
    CHECK(f.driver.GetError() == 1);   // unsupported fog parameter
    const float fogColor[4] = {1, 0, 0, 1};
    f.driver.Fog(static_cast<gd::eGDFogParamType>(1), fogColor);
    CHECK(f.env.device.renderStates[0x22] == gd::PackClearColor(1, 0, 0, 1));
    const float fogValue[1] = {0.5f};
    f.driver.Fog(static_cast<gd::eGDFogParamType>(3), fogValue);
    std::uint32_t halfBits = 0;
    std::memcpy(&halfBits, fogValue, 4);
    CHECK(f.env.device.renderStates[0x24] == halfBits);
    f.driver.Fog(static_cast<gd::eGDFogParamType>(0), fogValue);
    CHECK(f.driver.GetError() == 1);
}

}  // namespace

int main() {
    TestLayoutAndFreeHelpers();
    TestInterfaceAndIdentity();
    TestRefCount();
    TestClearAndState();
    TestDefaultState();
    TestCapabilities();
    TestTextureStages();
    TestDrawing();
    TestVideoModes();
    TestBufferRegions();
    TestLighting();
    TestSceneAndFlush();
    TestGetSetMisc();
    if (g_failures == 0) {
        std::printf("render_sgldx7driver_test: all checks passed\n");
        return 0;
    }
    std::printf("render_sgldx7driver_test: %d failure(s)\n", g_failures);
    return 1;
}
