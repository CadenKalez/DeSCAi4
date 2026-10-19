// cGZCanvasW32 — the application's window as the framework sees it (88 functions, 0x100619430..0x10061bbe0; service
// 0xa3460deb, priority 1,257,199). It owns the window rectangle, turns native window events (mouse, wheel, keys,
// characters, activation, paint) into cGZMouseMessage / cGZKeyMessage / cGZCanvasMessage objects and sends them through
// the message server 2, and asks the driver for the video mode that matches the canvas size when the window is created.
// The native events arrive through the On* methods (on this platform the SDL layer calls them).
//
// Quirks kept: mouse positions are clamped to the canvas; numeric keypad keys are reported as the digit / '-' / '.' / '/'
// keys; Enter, Tab, Backspace and Delete are sent as character events plus (unless the key is extended) a key-down;
// input generation is counted (mouse and keyboard separately) and the mouse side also gates the message-server hook.
#pragma once
#include <functional>
#include <list>
#include <string>

#include "gz/IGraphicSystemControl.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZInputMessages.h"
#include "gz/cGZMessageServer.h"
#include "gz/cGZMessageServer2.h"
#include "rz/cRZCOMDllDirector.h"
#include "rz/cRZSystemService.h"
#include "rz/cRZPoint.h"

constexpr std::uint32_t kGZSID_cGZCanvasW32 = 0xa3460deb;
constexpr std::int32_t kGZCanvasW32Priority = 0x1312cf;
constexpr std::uint32_t kGZIID_cIGZCanvas = 0x66b217a1, kGZIID_cIGZCanvasW32 = 0xe46397db, kGZIID_cIGZCanvasMessageTarget = 0x3ac7a210;

// A filter that sees window procedure messages before the canvas does (cIGZWinProcFilterW32).
class cIGZWinProcFilterW32 : public cIGZUnknown {};

// The canvas events of cGZCanvasMessage.
enum GZCanvasEvent : std::uint32_t {
    kCanvasInitialized = 0, kCanvasShutdown = 1, kCanvasDeactivate = 2, kCanvasActivate = 3, kCanvasPaint = 4,
    kCanvasUninitialize = 6, kCanvasRestore = 7, kCanvasWindowCreated = 8, kCanvasWindowDestroyed = 9,
};

class cGZCanvasW32 : public cRZSystemService {
public:
    cGZCanvasW32();                                                 // @ 0x100619430
    ~cGZCanvasW32() override = default;                              // @ 0x100619b2c

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10061a2ee
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10061a384
    std::uint32_t Release() override;                              // @ 0x10061a3e6

    // service life cycle
    bool Init() override;                                           // @ 0x100619b9c
    bool Shutdown() override;                                       // @ 0x10061a122

    // canvas interface
    bool Init(const cRZRect& area);                                 // @ 0x100619da6
    // @ 0x10061a0f4: stores the window style (default 0x1b) and initialises a (0, 0, width, height) canvas
    void Init(std::uint32_t windowStyle, std::uint32_t width, std::uint32_t height);
    bool IsInitialized() const { return initialized_; }             // @ 0x10061bb30
    bool Uninitialize();                                            // @ 0x10061a686
    bool Restore();                                                 // @ 0x10061a728
    void SetArea(int left, int top, int right, int bottom);         // @ 0x10061a5d8
    cRZRect GetArea() const { return area_; }                       // @ 0x10061bb3a
    void ShowWindow() {}                                            // @ 0x10061a820
    void HideWindow() {}                                            // @ 0x10061a826
    void SetCaption(const cIGZString&) {}                           // @ 0x10061a81a
    void Paint() { ForceFullSystemRedraw(); }                       // @ 0x10061bb4c
    void ForceFullSystemRedraw();                                   // @ 0x10061a5f2 (also OnPaint @ 0x10061a5fc)
    int LastCursorPositionX() const { return lastCursorX_; }        // @ 0x10061bb58
    int LastCursorPositionY() const { return lastCursorY_; }        // @ 0x10061bb62
    bool FlushInputMessageQueue() { return false; }                 // @ 0x10061a82c
    bool PostSystemMessage(cIGZVariant* message);                   // @ 0x10061a834
    void* GetSystemCanvasIdentifier() { return &handle_; }          // @ 0x10061bb6c
    void CenterWindowInRect(cRZRect*) {}                            // @ 0x10061a680
    int GetW() const { return area_.right - area_.left; }           // @ 0x10061bb76
    int GetH() const { return area_.bottom - area_.top; }           // @ 0x10061bb86
    int GetL() const { return area_.left; }                         // @ 0x10061bb96
    int GetT() const { return area_.top; }                          // @ 0x10061bba0
    int GetR() const { return area_.right; }                        // @ 0x10061bbaa
    int GetB() const { return area_.bottom; }                       // @ 0x10061bbb6
    bool GetInputGenerationEnabled(std::uint32_t which) const;      // @ 0x10061a88c
    void SetInputGenerationEnabled(std::uint32_t which, bool enable);   // @ 0x10061a8b4
    cIGZMessageServer2* MessageServer() const { return messageServer2_.get(); }   // @ 0x10061bbc2
    void* Handle() const { return handle_; }                        // @ 0x10061a94c
    int Class() const { return 0; }                                 // @ 0x10061bbd0
    bool AddWinProcFilter(cIGZWinProcFilterW32* filter, bool add);  // @ 0x10061a960
    bool DoMessage(cGZMessage&) { return true; }                    // @ 0x10061a4fe
    bool DoMessage(cIGZMessage2* message);                          // @ 0x10061a50e
    bool FinalRelease();                                            // @ 0x10061a4d0

    // native window events (the window procedure's handlers)
    void OnMouseMove(int x, int y, std::uint32_t flags);                    // @ 0x10061aac6
    void OnMouseWheel(int x, int y, std::uint32_t flags);                   // @ 0x10061ac60 (flags: wheel delta in the high word)
    void OnLButtonDown(int x, int y, std::uint32_t flags);                  // @ 0x10061ae08
    void OnRButtonDown(int x, int y, std::uint32_t flags);                  // @ 0x10061af7a
    void OnLButtonUp(int x, int y, std::uint32_t flags);                    // @ 0x10061b0ec
    void OnRButtonUp(int x, int y, std::uint32_t flags);                    // @ 0x10061b25e
    void OnKeyDown(std::uint32_t virtualKey, int repeat, std::uint32_t flags);   // @ 0x10061b3d0
    void OnKeyUp(std::uint32_t virtualKey, int repeat, std::uint32_t flags);     // @ 0x10061b6cc
    void OnChar(char character, int repeat, std::uint32_t flags);           // @ 0x10061b7f0
    void OnActivate(std::uint32_t state, void* other, int minimised);       // @ 0x10061ba9a

    // host seams
    void SetGraphicSystem(IGraphicSystemControl* graphics) { graphics_ = graphics; }
    void SetMessageServers(cIGZMessageServer* server1, cIGZMessageServer2* server2) { messageServer1_ = server1; messageServer2_ = server2; }
    std::function<std::uint32_t()> mouseButtonState;                // NCT_GetMouseButtonState: bit 0 left, bit 1 right
    std::function<std::string(const std::string&, int codePage)> convertToFramework;   // multi-byte characters -> framework code page
    std::function<int()> frameworkCodePage;                         // GetCurrentFrameworkCodePage
    std::function<int(char lead, int codePage)> followByteCount;    // GetFollowByteCount
    std::function<void(cGZCanvasW32&)> realizeHandle;               // bind the native window (sets the handle)
    void SetCodePage(int codePage) { codePage_ = codePage; }
    void SetHandle(void* handle) { handle_ = handle; }

private:
    bool InitWindow(bool create);                                   // init_windows32_window @ 0x100619df2
    static void StaticWindowProc();                                 // @ 0x10061a5c4
    void PostCanvas(std::uint32_t event);
    void PostMouse(std::uint32_t event, int x, int y, std::uint32_t flags, std::int32_t wheel);
    void PostKey(std::uint32_t event, std::uint32_t key, std::uint32_t flags, std::uint32_t repeat);
    cRZPoint Clamp(int x, int y) const;
    static std::uint32_t Modifiers(std::uint32_t nativeFlags, bool includeButtons);

    std::uint32_t refCount_ = 0;                                    // +0x4c
    bool initialized_ = false;                                      // +0x50
    bool uninitializing_ = false;                                   // +0x51
    bool pendingRestore_ = false;                                   // +0x52
    void* surface_ = nullptr;                                       // +0x58 (freed on window destruction)
    int lastCursorX_ = 0, lastCursorY_ = 0;                         // +0x60 / +0x64
    int field68_ = -1;
    std::uint32_t windowStyle_ = 0x1b;                              // +0x88
    void* handle_ = nullptr;                                        // +0x70
    cRZRect area_;                                                  // +0x78
    int mouseDisabled_ = 0;                                         // +0x90 (input generation: mouse)
    int keyboardDisabled_ = 0;                                      // +0x8c (input generation: keyboard)
    std::string pendingBytes_;                                      // +0xa0 (a multi-byte character being assembled)
    int expectedBytes_ = 0;                                         // +0xc0
    int codePage_ = 0;                                              // +0xf0
    IGraphicSystemControl* graphics_ = nullptr;                     // +0xf8
    cRZAutoRefCount<cIGZMessageServer> messageServer1_;             // +0x100
    cRZAutoRefCount<cIGZMessageServer2> messageServer2_;            // +0x108
    cRZAutoRefCount<gz::cGZCanvasMessage> canvasPrototype_;         // +0x110
    cRZAutoRefCount<gz::cGZMouseMessage> mousePrototype_;           // +0x118
    cRZAutoRefCount<gz::cGZKeyMessage> keyPrototype_;               // +0x120
    std::list<cRZAutoRefCount<cIGZWinProcFilterW32>> filters_;      // +0x128
};
