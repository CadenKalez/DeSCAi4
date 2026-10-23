// cGZGraphicSystemW32 — the Windows-shaped graphics system the draw context and the window layer talk to (35 function
// symbols, 0x1006178a2..0x100619174; service 0xc416025c, priority 1,500,000). It derives from cGZGraphicSystem (not
// recovered, 0x1e8 bytes) and adds the window handle at +0x1f8. The base owns the driver list, the current driver, the
// canvas (+0x178), the screen buffer (+0x168) and the display state; those are reached through IGraphicSystemHost.
//
// What this class itself does: screen-mode queries (MacDisplay), the video-mode list over the current driver, switching
// drivers, pausing the draw, cursor support, the canvas life cycle (the driver's current mode is recorded and the screen
// buffer is initialised with the ARGB4444 / ARGB8888 colour type the driver's bit depth selects) and the COM interface
// map. The DirectDraw / GDI / Windows entry points the macOS build never had (DirectDraw, DirectDrawCaps,
// SetCooperativeLevel, SetWindowHandle, ForceFullSystemRedraw, MessageBoxA, the driver-information lists) return the
// original's constants behind the same names so the callers stay unchanged.
//
// Quirks kept:
//   * IsModeAvailable ignores everything except the driver index; it answers whether that driver object exists.
//   * SwitchToMode returns canvas->IsInitialized() after replacing the driver, and it resets the canvas rectangle to
//     0,0,1024,768 before installing the new driver (the original movaps at 0x1006183e4).
//   * VideoMemory asks MacDisplay for the (free, total) pair and maps type 0 to total and type 2 to free; every other
//     type is 0. The bool argument is unused (0x100617ea8).
//   * OnCanvasInit clears mbDesiredPreferFlipping when the display is windowed, but leaves it alone when the
//     full-screen request is consumed and SetFullScreenMode(true) runs.
#pragma once
#include <cstdint>
#include <string>

#include "gz/StreamOps.h"          // cRZRect
#include "gz/cIGZFrameWork.h"      // cIGZFrameWorkWindow
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

class cGZScreenBuffer;
class cIGZDrawSystem;
struct HWND__;

namespace gd {
class cIGZGDriver;
}

constexpr std::uint32_t kGZSID_cGZGraphicSystem = 0xc416025c;
constexpr std::int32_t kGZGraphicSystemPriority = 1500000;

// The interface ids cGZGraphicSystemW32::QueryInterface maps (the class names behind them are the original GUIDs; only
// the groups the recovered code calls are modelled).
constexpr std::uint32_t kGZIID_cIGZGraphicSystem = 0x0073283c;                 // vtable group +0x28
constexpr std::uint32_t kGZIID_cIGZGraphicSystem2 = 0xc47b747c;                // group +0x30
constexpr std::uint32_t kGZIID_cIGZGraphicSystemHooks = 0x03fa40bf;            // group +0x38
constexpr std::uint32_t kGZIID_cIGZGraphicSystemMessageTarget = 0xe98b2f57;    // group +0x40
constexpr std::uint32_t kGZIID_cIGZGraphicSystemW32Modes = 0x5ad24c36;         // group +0x1f0
constexpr std::uint32_t kGZIID_cIGZCanvasW32View = 0xe46397db;                 // cIGZCanvasW32, group +8 of the canvas

// cGZDisplayMetrics: what GetScreenMetrics / the pre-init setters pass around (three u32s: width, height, depth).
struct cGZDisplayMetrics {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};

// cIGZGraphicSystem::eVideoMemoryType, as used by VideoMemory.
enum eVideoMemoryType : std::uint32_t {
    kVideoMemoryType0 = 0,   // total (the second MacDisplay value)
    kVideoMemoryType1 = 1,   // always 0
    kVideoMemoryType2 = 2,   // free (the first MacDisplay value)
};

// ---- the interface groups whose W32 methods are recovered -------------------------------------------------------
// Group +0x28 (iid 0x73283c, cIGZGraphicSystem). The base supplies the rest of the group; only the W32 overrides are
// declared here. Slots are the original's.
class IGZGraphicSystem : public cIGZUnknown {
public:
    virtual cGZDisplayMetrics GetScreenMetrics() = 0;                                        // [10] 0x50
    virtual void PauseDraw() = 0;                                                            // [18] 0x90
    virtual bool MessageBoxA(const cIGZString& title, const cIGZString& text) = 0;           // [21] 0xa8
    virtual std::uint32_t VideoMemory(eVideoMemoryType type, bool unused) = 0;               // [24] 0xc0
    virtual bool DoesSystemHaveCursorSupport() = 0;                                          // [32] 0x100
    virtual bool GetCursorPosition(int& x, int& y) = 0;                                      // [33] 0x108
    virtual void OnCanvasInit() = 0;                                                         // [66] 0x210
    virtual void OnCanvasShutdown() = 0;                                                     // [67] 0x218
    virtual void OnCanvasReInit() = 0;                                                       // [68] 0x220
};

// Group +0x30 (iid 0xc47b747c, cIGZGraphicSystem2). The driver-control methods W32 overrides.
class IGZGraphicSystem2 : public cIGZUnknown {
public:
    virtual void SetDefaultDriverClassID(std::uint32_t driverClass) = 0;                     // [8] 0x40
    virtual void SetDriverSettingsIniFilePath(const cIGZString& path) = 0;                   // [9] 0x48
};

// Group +0x38 (iid 0x3fa40bf) and +0x40 (iid 0xe98b2f57): the base's frame-work-hook and message-target views. Their
// methods are cGZGraphicSystem's (not recovered); the interface identity is all W32 QueryInterface promises.
class IGZGraphicSystemHooks : public cIGZUnknown {};
class IGZGraphicSystemMessageTarget : public cIGZUnknown {};

// Group +0x1f0 (iid 0x5ad24c36): the driver / video-mode view cGZGraphicSystemW32 adds.
class IGZGraphicSystemDriverModes : public cIGZUnknown {
public:
    virtual std::uint32_t GetDriverCount() const = 0;                                        // [3] 0x18
    virtual bool GetDriverSignature(std::uint32_t index, cIGZString& signature) = 0;         // [4] 0x20
    virtual bool IsModeAvailable(std::uint32_t driverIndex, std::uint32_t modeIndex, std::uint32_t width,
                                 std::uint32_t height, bool fullScreen, std::uint32_t& mode) = 0;   // [5] 0x28
    virtual bool SwitchToMode(std::uint32_t driverIndex, std::uint32_t modeIndex) = 0;       // [6] 0x30
};

// Group +0x1e8 (the legacy Windows view; no QueryInterface iid maps to it in the original). cGZGraphicSystemW32
// overrides every method; the DirectDraw / GDI ones are constants on the macOS build.
class IGZGraphicSystemLegacy : public cIGZUnknown {
public:
    virtual IGZGraphicSystem* AsIGZGraphicSystem() = 0;                                      // [3] 0x18
    virtual bool IsHardware2DAccellerated() = 0;                                             // [4] 0x20
    virtual bool IsHardware3DAccellerated() = 0;                                             // [5] 0x28
    virtual bool DoesSystemHaveCursorSupport() = 0;                                          // [6] 0x30
    virtual void* DirectDraw() = 0;                                                          // [7] 0x38
    virtual void* DirectDrawCaps() = 0;                                                      // [8] 0x40
    virtual void SetCooperativeLevel() = 0;                                                  // [9] 0x48
    virtual void SetCooperativeLevel(bool exclusive) = 0;                                    // [10] 0x50
    virtual bool IsGraphicSystemUsingPrimaryDisplay() = 0;                                   // [11] 0x58
    virtual bool IsVideo3Dfx() = 0;                                                          // [12] 0x60
    virtual void ForceFullSystemRedraw() = 0;                                                // [13] 0x68
    virtual void SetDefaultDriverClassID(std::uint32_t driverClass) = 0;                     // [14] 0x70
    virtual void SetDriverSettingsIniFilePath(const cIGZString& path) = 0;                   // [15] 0x78
    virtual void SetWindowHandle(HWND__* window) = 0;                                        // [16] 0x80
    virtual HWND__* GetWindowHandle() const = 0;                                             // [17] 0x88
    virtual void* GetDriverInformationList() = 0;                                            // [18] 0x90
    virtual void* GetDriverInformationCurrent() = 0;                                         // [19] 0x98
};

// The canvas views W32 touches. The primary view (iid 0x66b217a1) only needs IsInitialized (cGZCanvasW32 primary
// vslot 0x28); the +8 view (iid 0xe46397db) needs Handle (vslot 0x20) and Class (vslot 0x28), with AsIGZCanvas keeping
// the original slot numbers.
class IGZCanvas : public cIGZUnknown {
public:
    virtual bool IsInitialized() = 0;                                                       // cGZCanvasW32 [5] 0x28
};
class IGZCanvasW32 : public cIGZUnknown {
public:
    virtual IGZCanvas* AsIGZCanvas() = 0;                                                        // cGZCanvasW32 group +8 [3] 0x18
    virtual void* Handle() const = 0;                                                       // [4] 0x20
    virtual int Class() const = 0;                                                          // [5] 0x28
};

// What the unrecovered cGZGraphicSystem base (and the platform / framework glue W32 reaches) provides. Each method
// mirrors one original field read or vtable call; the comment carries its slot / address.
class IGraphicSystemHost {
public:
    virtual ~IGraphicSystemHost() = default;

    // ---- cGZGraphicSystem base state (0x10061c100 is the base constructor; none of the base methods are recovered)
    virtual std::uint32_t DriverCount() const = 0;                             // +0xc8 (base GetGDriverCount @ 0x10061e68c; W32 reads it at 0x100618270)
    virtual gd::cIGZGDriver* DriverByIndex(std::uint32_t index) const = 0;     // base vslot 0x2e0 @ 0x10061e6a4
    virtual gd::cIGZGDriver* CurrentDriver() const = 0;                        // base vslot 0x2d8 @ 0x100619062 (+0x100)
    virtual void SetDriver(gd::cIGZGDriver* driver) = 0;                       // base vslot 0x2f0 @ 0x10061da06
    virtual void SetFullScreenMode(bool fullScreen) = 0;                       // base vslot 0x140 @ 0x10061e0da
    virtual bool ScreenBufferEnabled() const = 0;                              // +0x69 (cleared by DisableScreenBuffer @ 0x10061908e)
    virtual bool CursorSupport() const = 0;                                    // +0x164
    virtual cGZScreenBuffer* ScreenBuffer() = 0;                               // +0x168
    virtual IGZCanvas* Canvas() = 0;                                           // +0x178 (base GetCanvas() const @ 0x10061db2c, vslot 0x1f8)
    virtual cGZDisplayMetrics DesiredStartupMetrics() const = 0;               // 0x100b117b8 (mDesiredStartupDisplayMetrics)
    virtual bool PreferFlipping() const = 0;                                   // 0x100b117b1 (mbDesiredPreferFlipping)
    virtual void SetPreferFlipping(bool prefer) = 0;                           // the same static, written by OnCanvasInit

    // ---- framework glue (RZGetFramework() @ 0x10002ab30)
    virtual cIGZFrameWorkWindow* MainWindowInterface() = 0;                    // RZGetFramework() @ 0x10002ab30 -> QI 0x23ea70a1 (counted reference)
    virtual cIGZDrawSystem* DrawSystem() = 0;                                  // RZGetFramework()->GetSystemService(0xeb2ea3cb, 0x4b2ea3b3) (counted reference)
};

void SetGraphicSystemHost(IGraphicSystemHost* host);
IGraphicSystemHost* GetGraphicSystemHost();

class cGZGraphicSystemW32 final : public cRZSystemService,
                                  public IGZGraphicSystem,
                                  public IGZGraphicSystem2,
                                  public IGZGraphicSystemHooks,
                                  public IGZGraphicSystemMessageTarget,
                                  public IGZGraphicSystemDriverModes,
                                  public IGZGraphicSystemLegacy {
public:
    explicit cGZGraphicSystemW32(IGraphicSystemHost* host = nullptr);    // @ 0x1006178a2
    ~cGZGraphicSystemW32() override;                                     // @ 0x100617960 / 0x100617a9c

    void SetHost(IGraphicSystemHost* host) { host_ = host; }

    // ---- COM
    bool QueryInterface(std::uint32_t iid, void** out) override;         // @ 0x100617b6a
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }       // @ 0x100617cd6
    std::uint32_t Release() override { return cRZSystemService::Release(); }     // @ 0x100617d58

    // ---- cIGZGraphicSystem view (+0x28)
    cGZDisplayMetrics GetScreenMetrics() override;                       // @ 0x100617dda (MacDisplay::GetCurrentMode)
    void PauseDraw() override;                                           // @ 0x100617e76
    bool MessageBoxA(const cIGZString& title, const cIGZString& text) override;   // @ 0x100617e98: always false
    std::uint32_t VideoMemory(eVideoMemoryType type, bool unused) override;       // @ 0x100617ea8
    bool DoesSystemHaveCursorSupport() override;                         // @ 0x100619012 (base +0x164)
    bool GetCursorPosition(int& x, int& y) override;                     // @ 0x100617f08 (SDL_GetMouseState)
    void OnCanvasInit() override;                                        // @ 0x100617f30
    void OnCanvasShutdown() override;                                    // @ 0x10061813a
    void OnCanvasReInit() override;                                      // @ 0x100618220

    // ---- cIGZGraphicSystem2 view (+0x30)
    void SetDefaultDriverClassID(std::uint32_t driverClass) override;    // @ 0x100619070 (base +0xd0)
    void SetDriverSettingsIniFilePath(const cIGZString& path) override;  // @ 0x10061907c (base +0xd8)

    // ---- driver / mode view (+0x1f0)
    std::uint32_t GetDriverCount() const override;                       // @ 0x100618270 (base +0xc8)
    bool GetDriverSignature(std::uint32_t index, cIGZString& signature) override;   // @ 0x100618288
    bool IsModeAvailable(std::uint32_t driverIndex, std::uint32_t modeIndex, std::uint32_t width,
                         std::uint32_t height, bool fullScreen, std::uint32_t& mode) override;   // @ 0x1006182fc
    bool SwitchToMode(std::uint32_t driverIndex, std::uint32_t modeIndex) override;      // @ 0x10061837e

    // ---- legacy / Windows view (+0x1e8)
    IGZGraphicSystem* AsIGZGraphicSystem() override;                      // @ 0x100619112: this + 0x28
    bool IsHardware2DAccellerated() override;                            // @ 0x10061911c: false
    bool IsHardware3DAccellerated() override;                            // @ 0x100619124: false
    void* DirectDraw() override;                                         // @ 0x10061912c: null
    void* DirectDrawCaps() override;                                     // @ 0x100619134: null
    void SetCooperativeLevel() override {}                               // @ 0x10061913c: empty
    void SetCooperativeLevel(bool) override {}                           // @ 0x100619142: empty
    bool IsGraphicSystemUsingPrimaryDisplay() override;                  // @ 0x100619148: true
    bool IsVideo3Dfx() override;                                         // @ 0x100619150: false
    void ForceFullSystemRedraw() override {}                             // @ 0x100619158: empty
    void SetWindowHandle(HWND__*) override {}                            // @ 0x10061915e: empty
    HWND__* GetWindowHandle() const override;                            // @ 0x100619164: null
    void* GetDriverInformationList() override;                           // @ 0x10061916c: null
    void* GetDriverInformationCurrent() override;                        // @ 0x100619174: null

    // ---- state the base normally sets, visible for the test / the future base recovery
    void SetFullScreenRequested(bool fullScreen) { fullScreenRequested_ = fullScreen; }   // base +0x10a
    std::uint32_t DisplayState() const { return displayState_; }                          // base +0x54
    std::uint32_t PauseDrawCount() const { return pauseDrawCount_; }                      // base +0x90
    const cRZRect& CanvasRect() const { return canvasRect_; }                             // base +0x80..0x8c
    std::uint32_t DefaultDriverClassID() const { return defaultDriverClassID_; }          // base +0xd0
    const cRZString& DriverSettingsIniFilePath() const { return driverSettingsIniPath_; } // base +0xd8
    const std::uint8_t* ScreenFormatFlags() const { return screenFormatFlags_; }          // base +0x108 / +0x109
    const std::uint32_t* ScreenPixelFormat() const { return screenPixelFormat_; }         // base +0x148..0x158
    HWND__* MainWindowHandle() const { return mainWindowHandle_; }                        // this +0x1f8

private:
    // Per-instance host, else the process-wide one installed with SetGraphicSystemHost.
    IGraphicSystemHost* Host() const;

    IGraphicSystemHost* host_ = nullptr;

    // cGZGraphicSystem fields W32 reads or writes directly. The base constructor sets state 1 and the desired startup
    // metrics; the port leaves the metrics to the host (see IGraphicSystemHost).
    std::uint32_t displayState_ = 1;                       // base +0x54: 1 = created, 0 = canvas up, 2 = canvas down
    cRZRect canvasRect_{};                                 // base +0x80..0x8c
    std::uint32_t pauseDrawCount_ = 0;                     // base +0x90
    std::uint8_t screenFormatFlags_[2] = {0, 0};           // base +0x108 / +0x109
    std::uint8_t fullScreenRequested_ = 0;                 // base +0x10a (set by the base Init)
    std::uint32_t screenPixelFormat_[4] = {0, 0, 0, 0};    // base +0x148..0x158 (copied from the driver's mode +0x10)
    std::uint32_t defaultDriverClassID_ = 0;               // base +0xd0
    cRZString driverSettingsIniPath_;                      // base +0xd8
    HWND__* mainWindowHandle_ = nullptr;                   // this +0x1f8 (OnCanvasInit stores the canvas handle)
};
