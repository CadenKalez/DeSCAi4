#include "gz/GraphicSystemW32.h"

#include <cstring>

#include "gz/Buffer.h"
#include "gz/DrawSystem.h"
#include "gz/ScreenBuffer.h"
#include "render/MacDisplay.h"
#include "render/gd/IGZGDriver.h"
#include "render/gd/VideoModes.h"

// GetCursorPosition @ 0x100617f08 calls SDL directly; the rest of the platform work is MacDisplay (recovered).
extern "C" std::uint32_t SDL_GetMouseState(int* x, int* y);

namespace {
IGraphicSystemHost* g_host = nullptr;
}  // namespace

void SetGraphicSystemHost(IGraphicSystemHost* host) { g_host = host; }
IGraphicSystemHost* GetGraphicSystemHost() { return g_host; }

cGZGraphicSystemW32::cGZGraphicSystemW32(IGraphicSystemHost* host)
    : cRZSystemService(kGZSID_cGZGraphicSystem, kGZGraphicSystemPriority), host_(host) {
    // The original's base constructor (0x10061c100) initialises the service and the base fields; the port keeps the
    // fields this class touches (above) and reaches the rest through IGraphicSystemHost.
}

cGZGraphicSystemW32::~cGZGraphicSystemW32() = default;

IGraphicSystemHost* cGZGraphicSystemW32::Host() const { return host_ ? host_ : g_host; }

// The interface map is the base's plus 0x5ad24c36; the base (0x10061c584) maps the other four and defers to
// cRZSystemService (0x10003d286), which is exactly the fallback here.
bool cGZGraphicSystemW32::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZGraphicSystemW32Modes) {
        *out = static_cast<IGZGraphicSystemDriverModes*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZGraphicSystem2) {
        *out = static_cast<IGZGraphicSystem2*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZGraphicSystemHooks) {
        *out = static_cast<IGZGraphicSystemHooks*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZGraphicSystemMessageTarget) {
        *out = static_cast<IGZGraphicSystemMessageTarget*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZGraphicSystem) {
        *out = static_cast<IGZGraphicSystem*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

// MacDisplay::GetCurrentMode @ 0x10063f9a1 fills (width, height, depth); the original writes them into the sret record.
cGZDisplayMetrics cGZGraphicSystemW32::GetScreenMetrics() {
    int width = 0, height = 0, depth = 0;
    sgl::MacDisplay::GetCurrentMode(width, height, depth);
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<std::uint32_t>(depth)};
}

void cGZGraphicSystemW32::PauseDraw() { ++pauseDrawCount_; }

bool cGZGraphicSystemW32::MessageBoxA(const cIGZString&, const cIGZString&) { return false; }

// MacDisplay::GetVideoMemoryInfo @ 0x10063fd16 gives (free, total); type 0 reads the total, type 2 the free value and
// every other type answers 0. The bool argument is part of the interface and unused (0x100617ea8..0x100617ed6).
std::uint32_t cGZGraphicSystemW32::VideoMemory(eVideoMemoryType type, bool) {
    std::uint32_t first = 0, second = 0;
    sgl::MacDisplay::GetVideoMemoryInfo(first, second);
    if (type == kVideoMemoryType2) return first;
    if (type == kVideoMemoryType0) return second;
    return 0;
}

bool cGZGraphicSystemW32::DoesSystemHaveCursorSupport() {
    IGraphicSystemHost* host = Host();
    return host ? host->CursorSupport() : false;
}

bool cGZGraphicSystemW32::GetCursorPosition(int& x, int& y) {
    SDL_GetMouseState(&x, &y);   // the button mask the SDL call also returns is ignored
    return true;
}

// 0x100617f30. Order kept: canvas -> framework window -> handle -> full-screen preference -> current mode -> screen
// buffer; the state field +0x54 is cleared last.
void cGZGraphicSystemW32::OnCanvasInit() {
    IGraphicSystemHost* host = Host();
    if (!host) return;
    IGZCanvas* owner = host->Canvas();
    if (!owner) return;

    IGZCanvasW32* canvas = nullptr;
    if (!owner->QueryInterface(kGZIID_cIGZCanvasW32View, reinterpret_cast<void**>(&canvas)) || !canvas) return;

    cIGZFrameWorkWindow* window = host->MainWindowInterface();   // RZGetFramework()->QI 0x23ea70a1
    if (!window) {
        canvas->Release();
        return;
    }

    if (void* handle = canvas->Handle()) {                        // canvas +8 view vslot 0x20
        mainWindowHandle_ = reinterpret_cast<HWND__*>(handle);
        window->SetMainHWND(handle);                              // framework-window vslot 0x30
    }

    if (fullScreenRequested_ != 0) {                              // base +0x10a
        fullScreenRequested_ = 0;
        host->SetFullScreenMode(true);                            // base vslot 0x140
    } else {
        host->SetPreferFlipping(false);                           // cGZGraphicSystem::mbDesiredPreferFlipping = 0
    }

    std::uint32_t depth = 0x20;                                   // no driver: assume 32 bpp
    if (gd::cIGZGDriver* driver = host->CurrentDriver()) {        // base vslot 0x2d8 (+0x100)
        gd::sGDMode mode{};
        driver->GetVideoModeInfo(mode);                           // driver vslot 0x208 (65)
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&mode);
        screenFormatFlags_[0] = bytes[0x2e];                      // base +0x108
        screenFormatFlags_[1] = bytes[0x2d];                      // base +0x109
        std::memcpy(screenPixelFormat_, bytes + 0x10, sizeof(screenPixelFormat_));   // base +0x148..0x158
        depth = mode.bitsPerPixel;
    }

    if (host->ScreenBufferEnabled()) {                            // base +0x69
        if (cGZScreenBuffer* buffer = host->ScreenBuffer()) {     // base +0x168
            // 0x100618082: depth above 16 selects ARGB8888 (type 9), otherwise ARGB4444 (type 7). The second word of
            // the packed cGZBufferColorType is 2 for the 32-bpp driver and 1 for the 16-bpp one (the raw constant).
            const cGZBufferColorType color = depth > 0x10 ? cGZBufferColorType{9, 2} : cGZBufferColorType{7, 1};
            const cGZDisplayMetrics desired = host->DesiredStartupMetrics();
            buffer->Init(desired.width, desired.height, host->PreferFlipping(), color);   // screen buffer vslot 0x198
        }
    }

    displayState_ = 0;                                            // base +0x54
    window->Release();
    canvas->Release();
}

// 0x10061813a: state 2, detach the window, leave full screen, flush the draw system's buffer caches.
void cGZGraphicSystemW32::OnCanvasShutdown() {
    displayState_ = 2;
    IGraphicSystemHost* host = Host();
    if (!host) return;

    if (cIGZFrameWorkWindow* window = host->MainWindowInterface()) {
        window->SetMainHWND(nullptr);                             // framework-window vslot 0x30
        window->Release();
    }
    host->SetFullScreenMode(false);                               // base vslot 0x140

    if (cIGZDrawSystem* drawSystem = host->DrawSystem()) {        // GetSystemService(0xeb2ea3cb, 0x4b2ea3b3)
        drawSystem->FlushBufferCaches();                          // draw-system vslot 0x48
        drawSystem->Release();
    }
}

// 0x100618220 calls the canvas shutdown slot (0x410) first and the init slot (0x408) second.
void cGZGraphicSystemW32::OnCanvasReInit() {
    OnCanvasShutdown();
    OnCanvasInit();
}

void cGZGraphicSystemW32::SetDefaultDriverClassID(std::uint32_t driverClass) { defaultDriverClassID_ = driverClass; }

void cGZGraphicSystemW32::SetDriverSettingsIniFilePath(const cIGZString& path) {
    driverSettingsIniPath_.AssignFromIGZString(path);             // cRZString at base +0xd8
}

std::uint32_t cGZGraphicSystemW32::GetDriverCount() const {
    IGraphicSystemHost* host = Host();
    return host ? host->DriverCount() : 0;
}

// 0x100618288: the driver's GetDriverInfo (vslot 0x268 = 77 in the recovered cIGZGDriver) is the signature string.
bool cGZGraphicSystemW32::GetDriverSignature(std::uint32_t index, cIGZString& signature) {
    if (index >= GetDriverCount()) return false;
    IGraphicSystemHost* host = Host();
    gd::cIGZGDriver* driver = host ? host->DriverByIndex(index) : nullptr;     // base vslot 0x2e0
    if (!driver) return false;
    signature.AssignFromCString(static_cast<const char*>(driver->GetDriverInfo()));
    return true;
}

// 0x1006182fc: the original looks only at the driver index (the mode width / height / flag / out arguments are
// unused); it answers whether that driver object exists.
bool cGZGraphicSystemW32::IsModeAvailable(std::uint32_t driverIndex, std::uint32_t, std::uint32_t, std::uint32_t,
                                          bool, std::uint32_t&) {
    if (driverIndex >= GetDriverCount()) return false;
    IGraphicSystemHost* host = Host();
    return host && host->DriverByIndex(driverIndex) != nullptr;
}

// 0x10061837e: replace the driver (null first, then the candidate), reset the canvas rectangle to 0,0,1024,768 and
// answer the canvas' IsInitialized (primary canvas vslot 0x28). The original does not check the canvas pointer; the
// port answers false instead of faulting.
bool cGZGraphicSystemW32::SwitchToMode(std::uint32_t driverIndex, std::uint32_t modeIndex) {
    IGraphicSystemHost* host = Host();
    if (!host) return false;
    if (driverIndex >= GetDriverCount()) return false;
    gd::cIGZGDriver* driver = host->DriverByIndex(driverIndex);
    if (!driver) return false;
    if (modeIndex >= static_cast<std::uint32_t>(driver->CountVideoModes())) return false;   // driver vslot 0x1f8 (63)

    IGZCanvas* canvas = host->Canvas();                           // base GetCanvas() const = +0x178
    host->SetDriver(nullptr);                                     // base vslot 0x2f0
    canvasRect_ = {0, 0, 1024, 768};                              // movaps at 0x1006183e4
    host->SetDriver(driver);
    return canvas ? canvas->IsInitialized() : false;
}

IGZGraphicSystem* cGZGraphicSystemW32::AsIGZGraphicSystem() { return static_cast<IGZGraphicSystem*>(this); }
bool cGZGraphicSystemW32::IsHardware2DAccellerated() { return false; }
bool cGZGraphicSystemW32::IsHardware3DAccellerated() { return false; }
void* cGZGraphicSystemW32::DirectDraw() { return nullptr; }
void* cGZGraphicSystemW32::DirectDrawCaps() { return nullptr; }
bool cGZGraphicSystemW32::IsGraphicSystemUsingPrimaryDisplay() { return true; }
bool cGZGraphicSystemW32::IsVideo3Dfx() { return false; }
HWND__* cGZGraphicSystemW32::GetWindowHandle() const { return nullptr; }
void* cGZGraphicSystemW32::GetDriverInformationList() { return nullptr; }
void* cGZGraphicSystemW32::GetDriverInformationCurrent() { return nullptr; }
