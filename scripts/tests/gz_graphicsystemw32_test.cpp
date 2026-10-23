// cGZGraphicSystemW32: the interface map, screen metrics / video memory / cursor over the platform, the driver and
// video-mode view (driver signatures, mode availability, driver switching), the canvas life cycle (window handle,
// full-screen preference, driver mode -> screen pixel format, screen buffer colour type and shutdown / re-init), and
// the constants the Windows entry points answer on macOS.
//
// The unrecovered cGZGraphicSystem base is IGraphicSystemHost (driver list, current driver, canvas, screen buffer,
// display / full-screen / cursor state, framework window and draw system). MacDisplay runs over a fake
// IDisplayPlatform and SDL_GetMouseState is the test's own definitions, so no game files or frameworks are touched.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gz/DrawSystem.h"
#include "gz/GraphicSystemW32.h"
#include "gz/ScreenBuffer.h"
#include "render/MacDisplay.h"
#include "render/MacPreferences.h"
#include "render/gd/GDriverBase.h"
#include "render/gd/VideoModes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// The CFPreferences side of MacPreferences is not linked in this test (no CoreFoundation); a memory store stands in.
namespace sgl {
namespace {
class MemoryPreferences final : public IPreferencesStore {
public:
    bool CopyString(const char*, std::string&) override { return false; }
    void SetString(const char*, const char*) override {}
    void Synchronize() override {}
};
}  // namespace
IPreferencesStore* DefaultPreferencesStore() {
    static MemoryPreferences store;
    return &store;
}
}  // namespace sgl

namespace {
int g_mouseX = 0, g_mouseY = 0;

struct FakePlatform final : sgl::IDisplayPlatform {
    int width = 1920, height = 1080, depth = 32;
    std::uint32_t vramMB = 512;
    std::uint32_t MainDisplayID() override { return 1; }
    sgl::CGRect DisplayBounds(std::uint32_t) override { return {0, 0, 1920, 1080}; }
    bool ActiveDisplays(std::vector<std::uint32_t>& out) override { out.push_back(1); return true; }
    std::vector<Mode> DisplayModes(std::uint32_t) override { return {}; }
    int DisplayPixelsWide(std::uint32_t) override { return width; }
    int DisplayPixelsHigh(std::uint32_t) override { return height; }
    std::int32_t CurrentDisplayDepth(std::uint32_t) override { return depth; }
    bool DisplayHasHardwareRenderer(std::uint32_t) override { return true; }
    bool QueryPCIDisplayInfo(std::uint32_t, sgl::ASLPCIDisplayInfo_t& out) override {
        out.vramMB = vramMB;
        return true;
    }
    bool IsMacOSSierraOrLater() override { return true; }
    const char* GLRendererString() override { return "test"; }
    void* CreateGLContext(void*, bool, int, int) override { return nullptr; }
    void DeleteGLContext(void*) override {}
    void MakeCurrent(void*, void*) override {}
    void SwapWindow(void*) override {}
    void FlushGL() override {}
    void EnableMultithreadedGL(bool) override {}
    void* CreateWindow(const char*, bool, int, int, const sgl::CGRect&) override { return nullptr; }
    void DestroyWindow(void*) override {}
    void SetWindowPosition(void*, int, int) override {}
    void SetWindowSize(void*, int, int) override {}
    void GetWindowSize(void*, int& w, int& h) override { w = width; h = height; }
    float WindowOpacity(void*) override { return 0.0f; }
    void HideWindow(void*) override {}
    void ShowWindow(void*) override {}
    void CenterWindow(void*) override {}
    void SetMenuBarVisible(bool) override {}
    int ShowAlertBox(void*, const char*, const char*) override { return 0; }
    bool IsMainThread() override { return true; }
    void FlushInputEvents() override {}
    void Exit(int) override {}
};

struct FakeWindow final : cIGZFrameWorkWindow {
    void* main = nullptr;
    int refs = 0, setCalls = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    cIGZFrameWork* AsIGZFrameWork() override { return nullptr; }
    void* GetWindowsInstance() const override { return nullptr; }
    void* GetMainHWND() override { return main; }
    void SetMainHWND(void* handle) override { main = handle; ++setCalls; }
};

struct FakeDrawSystem final : cIGZDrawSystem {
    int refs = 0, flushCalls = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    void AddEngine(cIGZDrawContextEngineFactory*) override {}
    bool CreateEngine(cIGZUnknown*, gz::cIGZDrawEngine**) override { return false; }
    bool CreatePrivateBuffer(cIGZBuffer**, int, int, std::int32_t) override { return false; }
    void RegisterBufferCache(cIGZBufferHardwareCache*) override {}
    void UnregisterBufferCache(cIGZBufferHardwareCache*) override {}
    void FlushOldBufferCaches(std::uint32_t) override {}
    void FlushBufferCaches() override { ++flushCalls; }
    std::uint32_t GetBufferCacheClock() override { return 0; }
};

// The primary canvas view plus the +8 cIGZCanvasW32 view. Release never deletes: the object is a stack fake.
struct FakeCanvas final : IGZCanvas, IGZCanvasW32 {
    bool initialized = true;
    bool allowView = true;
    void* handle = reinterpret_cast<void*>(0x1234);
    int refs = 0, viewQueries = 0, released = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZCanvasW32View || !allowView) return false;
        *out = static_cast<IGZCanvasW32*>(this);
        AddRef();
        ++viewQueries;
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { ++released; return static_cast<std::uint32_t>(--refs); }
    bool IsInitialized() override { return initialized; }
    IGZCanvas* AsIGZCanvas() override { return static_cast<IGZCanvas*>(this); }
    void* Handle() const override { return handle; }
    int Class() const override { return 42; }
};

struct FakeDriver final : gd::cGDriverBase {
    const char* signature = "";
    int modeCount = 0;
    std::uint32_t bitsPerPixel = 32;
    std::uint8_t modeByte2D = 0, modeByte2E = 0;
    std::uint32_t format[4] = {0, 0, 0, 0};
    int modeQueries = 0, modeCountQueries = 0;
    const void* GetDriverInfo() override { return signature; }
    int CountVideoModes() override { ++modeCountQueries; return modeCount; }
    bool GetVideoModeInfo(gd::sGDMode& mode) override {
        ++modeQueries;
        mode = {};
        mode.bitsPerPixel = bitsPerPixel;
        auto* bytes = reinterpret_cast<std::uint8_t*>(&mode);
        bytes[0x2d] = modeByte2D;
        bytes[0x2e] = modeByte2E;
        std::memcpy(bytes + 0x10, format, sizeof(format));
        return true;
    }
    bool GetVideoModeInfo(std::uint32_t, gd::sGDMode& mode) override { return GetVideoModeInfo(mode); }
};

struct FakeHost final : IGraphicSystemHost {
    std::vector<gd::cIGZGDriver*> drivers;
    gd::cIGZGDriver* current = nullptr;
    std::vector<gd::cIGZGDriver*> setDriverHistory;
    int setFullScreenCalls = 0;
    bool fullScreen = false;
    bool screenBufferEnabled = true;
    bool cursorSupport = false;
    cGZScreenBuffer* screenBuffer = nullptr;
    FakeCanvas* canvas = nullptr;
    cGZDisplayMetrics desired{800, 600, 32};
    bool preferFlipping = true;
    int setPreferCalls = 0;
    FakeWindow window;
    FakeDrawSystem drawSystem;

    std::uint32_t DriverCount() const override { return static_cast<std::uint32_t>(drivers.size()); }
    gd::cIGZGDriver* DriverByIndex(std::uint32_t index) const override {
        return index < drivers.size() ? drivers[index] : nullptr;
    }
    gd::cIGZGDriver* CurrentDriver() const override { return current; }
    void SetDriver(gd::cIGZGDriver* driver) override {
        setDriverHistory.push_back(driver);
        current = driver;
    }
    void SetFullScreenMode(bool fullScreenMode) override {
        ++setFullScreenCalls;
        fullScreen = fullScreenMode;
    }
    bool ScreenBufferEnabled() const override { return screenBufferEnabled; }
    bool CursorSupport() const override { return cursorSupport; }
    cGZScreenBuffer* ScreenBuffer() override { return screenBuffer; }
    IGZCanvas* Canvas() override { return canvas; }
    cGZDisplayMetrics DesiredStartupMetrics() const override { return desired; }
    bool PreferFlipping() const override { return preferFlipping; }
    void SetPreferFlipping(bool prefer) override {
        ++setPreferCalls;
        preferFlipping = prefer;
    }
    cIGZFrameWorkWindow* MainWindowInterface() override {
        window.AddRef();   // the original's framework query hands out a counted reference
        return &window;
    }
    cIGZDrawSystem* DrawSystem() override {
        drawSystem.AddRef();
        return &drawSystem;
    }
};

cGZGraphicSystemW32* MakeSystem(FakeHost& host) {
    auto* system = new cGZGraphicSystemW32(&host);
    system->AddRef();
    return system;
}
void FreeSystem(cGZGraphicSystemW32* system) { system->Release(); }

void TestComAndState() {
    FakeHost host;
    host.cursorSupport = false;
    cGZGraphicSystemW32* system = MakeSystem(host);

    CHECK(system->GetServiceID() == kGZSID_cGZGraphicSystem);
    CHECK(system->GetServicePriority() == kGZGraphicSystemPriority);
    CHECK(system->DisplayState() == 1);
    CHECK(system->PauseDrawCount() == 0);
    system->PauseDraw();
    system->PauseDraw();
    CHECK(system->PauseDrawCount() == 2);

    const cGZDisplayMetrics metrics = system->GetScreenMetrics();
    CHECK(metrics.width == 1920 && metrics.height == 1080 && metrics.depth == 32);

    CHECK(system->VideoMemory(kVideoMemoryType0, false) == 512);
    CHECK(system->VideoMemory(kVideoMemoryType2, false) == 512);
    CHECK(system->VideoMemory(kVideoMemoryType1, false) == 0);
    CHECK(system->VideoMemory(kVideoMemoryType1, true) == 0);

    g_mouseX = 321;
    g_mouseY = 654;
    int x = -1, y = -1;
    CHECK(system->GetCursorPosition(x, y));
    CHECK(x == 321 && y == 654);

    CHECK(!system->MessageBoxA(cRZString("title"), cRZString("text")));
    CHECK(!system->DoesSystemHaveCursorSupport());
    host.cursorSupport = true;
    CHECK(system->DoesSystemHaveCursorSupport());

    system->SetDefaultDriverClassID(0xbadb6906u);
    CHECK(system->DefaultDriverClassID() == 0xbadb6906u);
    system->SetDriverSettingsIniFilePath(cRZString("Graphics Rules.ini"));
    CHECK(std::string(system->DriverSettingsIniFilePath().ToChar()) == "Graphics Rules.ini");

    CHECK(system->AsIGZGraphicSystem() == static_cast<IGZGraphicSystem*>(system));
    CHECK(system->AsIGZGraphicSystem()->GetScreenMetrics().height == 1080);
    CHECK(!system->IsHardware2DAccellerated());
    CHECK(!system->IsHardware3DAccellerated());
    CHECK(system->DirectDraw() == nullptr);
    CHECK(system->DirectDrawCaps() == nullptr);
    CHECK(system->IsGraphicSystemUsingPrimaryDisplay());
    CHECK(!system->IsVideo3Dfx());
    CHECK(system->GetWindowHandle() == nullptr);
    CHECK(system->GetDriverInformationList() == nullptr);
    CHECK(system->GetDriverInformationCurrent() == nullptr);
    system->SetWindowHandle(nullptr);
    system->ForceFullSystemRedraw();
    system->SetCooperativeLevel();
    system->SetCooperativeLevel(false);

    void* out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZGraphicSystem, &out));
    CHECK(out == static_cast<IGZGraphicSystem*>(system));
    CHECK(static_cast<IGZGraphicSystem*>(out)->GetScreenMetrics().width == 1920);
    system->Release();
    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZGraphicSystem2, &out));
    CHECK(out == static_cast<IGZGraphicSystem2*>(system));
    static_cast<IGZGraphicSystem2*>(out)->SetDefaultDriverClassID(0x7aca35c6u);
    CHECK(system->DefaultDriverClassID() == 0x7aca35c6u);
    static_cast<IGZGraphicSystem2*>(out)->Release();   // Release through the secondary vtable

    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZGraphicSystemHooks, &out));
    CHECK(out == static_cast<IGZGraphicSystemHooks*>(system));
    system->Release();
    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZGraphicSystemMessageTarget, &out));
    CHECK(out == static_cast<IGZGraphicSystemMessageTarget*>(system));
    system->Release();
    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZGraphicSystemW32Modes, &out));
    CHECK(out == static_cast<IGZGraphicSystemDriverModes*>(system));
    CHECK(static_cast<IGZGraphicSystemDriverModes*>(out)->GetDriverCount() == 0);
    static_cast<IGZGraphicSystemDriverModes*>(out)->Release();

    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZSystemService, &out));
    CHECK(out != nullptr);
    system->Release();
    out = nullptr;
    CHECK(system->QueryInterface(kGZIID_cIGZUnknown, &out));
    CHECK(out != nullptr);
    system->Release();
    CHECK(!system->QueryInterface(0xdeadbeefu, &out));

    FreeSystem(system);
}

void TestDriversAndModes() {
    FakeHost host;
    FakeDriver driver0, driver1;
    driver0.signature = "driver-zero";
    driver0.modeCount = 3;
    driver1.signature = "driver-one";
    driver1.modeCount = 0;
    host.drivers = {&driver0, &driver1};
    FakeCanvas canvas;
    host.canvas = &canvas;

    cGZGraphicSystemW32* system = MakeSystem(host);
    CHECK(system->GetDriverCount() == 2);

    cRZString signature;
    CHECK(system->GetDriverSignature(0, signature));
    CHECK(std::string(signature.ToChar()) == "driver-zero");
    CHECK(system->GetDriverSignature(1, signature));
    CHECK(std::string(signature.ToChar()) == "driver-one");
    CHECK(!system->GetDriverSignature(2, signature));
    CHECK(std::string(signature.ToChar()) == "driver-one");

    std::uint32_t mode = 7;
    CHECK(system->IsModeAvailable(0, 99, 640, 480, false, mode));   // mode arguments are ignored
    CHECK(system->IsModeAvailable(1, 0, 0, 0, true, mode));
    CHECK(!system->IsModeAvailable(2, 0, 640, 480, false, mode));
    CHECK(mode == 7);

    CHECK(system->SwitchToMode(2, 0) == false);
    CHECK(host.setDriverHistory.empty());
    CHECK(system->SwitchToMode(1, 0) == false);                     // driver 1 has no modes
    CHECK(host.setDriverHistory.empty());
    CHECK(system->SwitchToMode(0, 3) == false);                     // mode index out of range
    CHECK(host.setDriverHistory.empty());

    CHECK(system->SwitchToMode(0, 2) == true);
    CHECK(host.setDriverHistory.size() == 2);
    CHECK(host.setDriverHistory[0] == nullptr);
    CHECK(host.setDriverHistory[1] == &driver0);
    CHECK(system->CanvasRect().left == 0 && system->CanvasRect().top == 0);
    CHECK(system->CanvasRect().right == 1024 && system->CanvasRect().bottom == 768);
    CHECK(driver0.modeCountQueries == 2);   // the failed 3 and the accepted 2

    host.setDriverHistory.clear();
    canvas.initialized = false;
    CHECK(system->SwitchToMode(0, 0) == false);                     // the answer is the canvas state
    CHECK(host.setDriverHistory.size() == 2);

    FreeSystem(system);
}

struct CanvasFixture {
    FakeHost host;
    FakeCanvas canvas;
    FakeDriver driver;
    cGZScreenBuffer buffer;
    cGZGraphicSystemW32* system = nullptr;

    CanvasFixture() {
        canvas.handle = reinterpret_cast<void*>(0x1234);
        canvas.initialized = true;
        host.canvas = &canvas;
        host.screenBuffer = &buffer;
        driver.modeCount = 1;
        driver.bitsPerPixel = 32;
        driver.modeByte2D = 0x2d;
        driver.modeByte2E = 0x2e;
        driver.format[0] = 0x00ff0000u;
        driver.format[1] = 0x0000ff00u;
        driver.format[2] = 0x000000ffu;
        driver.format[3] = 0xff000000u;
        host.drivers = {&driver};
        host.current = &driver;
        system = MakeSystem(host);
    }
    ~CanvasFixture() { FreeSystem(system); }
};

void TestGlobalHostFallback() {
    FakeHost host;
    FakeDriver driver;
    driver.signature = "global-driver";
    driver.modeCount = 1;
    host.drivers = {&driver};
    SetGraphicSystemHost(&host);
    auto* system = new cGZGraphicSystemW32();   // no per-instance host
    system->AddRef();
    CHECK(GetGraphicSystemHost() == &host);
    CHECK(system->GetDriverCount() == 1);
    cRZString signature;
    CHECK(system->GetDriverSignature(0, signature));
    CHECK(std::string(signature.ToChar()) == "global-driver");
    system->Release();
    SetGraphicSystemHost(nullptr);
}

void TestOnCanvasInit32() {
    CanvasFixture f;
    f.system->SetFullScreenRequested(false);
    f.system->OnCanvasInit();

    CHECK(f.host.window.main == reinterpret_cast<void*>(0x1234));
    CHECK(f.host.window.setCalls == 1);
    CHECK(f.host.window.refs == 0);
    CHECK(f.canvas.viewQueries == 1);
    CHECK(f.canvas.refs == 0);
    CHECK(f.system->MainWindowHandle() == reinterpret_cast<HWND__*>(0x1234));
    CHECK(f.system->GetWindowHandle() == nullptr);

    CHECK(f.system->DisplayState() == 0);
    CHECK(f.host.setFullScreenCalls == 0);
    CHECK(f.host.setPreferCalls == 1);
    CHECK(!f.host.preferFlipping);
    CHECK(f.driver.modeQueries == 1);

    CHECK(f.system->ScreenFormatFlags()[0] == 0x2e);                // driver mode +0x2e
    CHECK(f.system->ScreenFormatFlags()[1] == 0x2d);                // driver mode +0x2d
    CHECK(f.system->ScreenPixelFormat()[0] == 0x00ff0000u);
    CHECK(f.system->ScreenPixelFormat()[1] == 0x0000ff00u);
    CHECK(f.system->ScreenPixelFormat()[2] == 0x000000ffu);
    CHECK(f.system->ScreenPixelFormat()[3] == 0xff000000u);

    CHECK(f.buffer.IsInitialized());
    CHECK(f.buffer.GetColorType().type == 9);                        // depth 32 -> ARGB8888
    CHECK(f.buffer.GetColorType().bitsPerPixel == 2);
    cRZRect window{};
    f.buffer.GetAppWindow(window);
    CHECK(window.left == 0 && window.top == 0 && window.right == 800 && window.bottom == 600);
}

void TestOnCanvasInitFullScreenAndDepth16() {
    CanvasFixture f;
    f.driver.bitsPerPixel = 16;
    f.host.preferFlipping = true;
    f.system->SetFullScreenRequested(true);
    f.system->OnCanvasInit();

    CHECK(f.host.setFullScreenCalls == 1);
    CHECK(f.host.fullScreen);
    CHECK(f.host.setPreferCalls == 0);
    CHECK(f.host.preferFlipping);                                    // left alone in the full-screen path
    CHECK(f.buffer.IsInitialized());
    CHECK(f.buffer.GetColorType().type == 7);                        // depth 16 -> ARGB4444
    CHECK(f.buffer.GetColorType().bitsPerPixel == 1);
    CHECK(f.system->DisplayState() == 0);
}

void TestOnCanvasInitWithoutFlips() {
    CanvasFixture f;
    f.host.screenBufferEnabled = false;
    f.host.current = nullptr;                                        // no driver: 32-bpp assumption, no mode query
    f.system->OnCanvasInit();

    CHECK(f.driver.modeQueries == 0);
    CHECK(!f.buffer.IsInitialized());
    CHECK(f.system->DisplayState() == 0);
    CHECK(f.host.window.main == reinterpret_cast<void*>(0x1234));
}

void TestOnCanvasInitNoCanvasOrNoHandle() {
    {
        CanvasFixture f;
        f.host.canvas = nullptr;
        f.system->OnCanvasInit();
        CHECK(f.system->DisplayState() == 1);
        CHECK(f.host.window.setCalls == 0);
        CHECK(f.host.window.refs == 0);
    }
    {
        CanvasFixture f;
        f.canvas.allowView = false;                                  // QueryInterface refuses the +8 view
        f.system->OnCanvasInit();
        CHECK(f.system->DisplayState() == 1);
        CHECK(f.host.window.setCalls == 0);
        CHECK(f.host.window.refs == 0);
    }
    {
        CanvasFixture f;
        f.canvas.handle = nullptr;                                   // a canvas without a window still initialises
        f.system->OnCanvasInit();
        CHECK(f.host.window.main == nullptr);
        CHECK(f.host.window.setCalls == 0);
        CHECK(f.system->MainWindowHandle() == nullptr);
        CHECK(f.system->DisplayState() == 0);
        CHECK(f.buffer.IsInitialized());
    }
}

void TestOnCanvasShutdownAndReInit() {
    {
        CanvasFixture f;
        f.system->OnCanvasInit();
        const int setCallsBefore = f.host.window.setCalls;
        f.host.drawSystem.flushCalls = 0;
        f.system->OnCanvasShutdown();
        CHECK(f.system->DisplayState() == 2);
        CHECK(f.host.window.setCalls == setCallsBefore + 1);
        CHECK(f.host.window.main == nullptr);
        CHECK(f.host.setFullScreenCalls == 1);
        CHECK(!f.host.fullScreen);
        CHECK(f.host.drawSystem.flushCalls == 1);
        CHECK(f.host.window.refs == 0);
        CHECK(f.host.drawSystem.refs == 0);
        CHECK(f.system->MainWindowHandle() == reinterpret_cast<HWND__*>(0x1234));   // +0x1f8 is not cleared
        CHECK(f.system->GetWindowHandle() == nullptr);
    }
    {
        CanvasFixture f;
        f.system->OnCanvasReInit();
        CHECK(f.system->DisplayState() == 0);                        // shutdown (2) followed by init (0)
        CHECK(f.host.window.setCalls == 2);                               // null on shutdown, handle on init
        CHECK(f.host.setFullScreenCalls == 1);
        CHECK(f.host.drawSystem.flushCalls == 1);
        CHECK(f.buffer.IsInitialized());
    }
    {
        CanvasFixture f;
        f.system->SetFullScreenRequested(false);
        f.system->OnCanvasShutdown();                                // no canvas work needed for the shutdown itself
        CHECK(f.system->DisplayState() == 2);
        CHECK(f.host.window.setCalls == 1);
        CHECK(f.host.drawSystem.flushCalls == 1);
    }
}

}  // namespace

extern "C" std::uint32_t SDL_GetMouseState(int* x, int* y) {
    if (x) *x = g_mouseX;
    if (y) *y = g_mouseY;
    return 0;
}

int main() {
    FakePlatform platform;
    sgl::SetDisplayPlatform(&platform);
    sgl::SetPreferencesStore(nullptr);

    TestComAndState();
    TestDriversAndModes();
    TestGlobalHostFallback();
    TestOnCanvasInit32();
    TestOnCanvasInitFullScreenAndDepth16();
    TestOnCanvasInitWithoutFlips();
    TestOnCanvasInitNoCanvasOrNoHandle();
    TestOnCanvasShutdownAndReInit();

    if (failures == 0) std::printf("gz_graphicsystemw32_test: all checks passed\n");
    else std::printf("gz_graphicsystemw32_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
