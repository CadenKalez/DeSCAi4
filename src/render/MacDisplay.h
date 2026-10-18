// MacDisplay and its helpers (0x10063dbb3..0x10064004x, 0x100641a46..0x100641b5c, 0x1004edc2e): display enumeration and
// validation, the SDL window and GL context of the primary display, the list of display modes the game offers, plus the
// small platform odds and ends that sit beside it (PCI display info, message alerts, the system lock, the log file).
// All the operating-system work (CoreGraphics, CGL, IOKit, SDL, the message box) sits behind IDisplayPlatform.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

#include "render/DX7Types.h"
#include "render/MacPreferences.h"

namespace sgl {

struct tagRECT;

// What GetPCIDisplayInfo answers (0x54 bytes).
struct ASLPCIDisplayInfo_t {
    std::uint32_t deviceID, vendorID, revisionID, subsystemID, vramMB;
    char name[0x40];
};
static_assert(sizeof(ASLPCIDisplayInfo_t) == 0x54);

// The platform's side of MacDisplay. Windows and GL contexts are opaque handles here.
class IDisplayPlatform {
public:
    struct Mode {
        std::int32_t width, height, pixelWidth, pixelHeight;
        std::int32_t depth;          // 32 ("--------RRRRRRRRGGGGGGGGBBBBBBBB"), 16 ("-RRRRRGGGGGBBBBB"), 8 or 0 (anything else)
        double refreshRate;
    };
    virtual ~IDisplayPlatform() = default;
    // CoreGraphics
    virtual std::uint32_t MainDisplayID() = 0;
    virtual CGRect DisplayBounds(std::uint32_t display) = 0;
    virtual bool ActiveDisplays(std::vector<std::uint32_t>& out) = 0;               // false when the query fails or finds none
    virtual std::vector<Mode> DisplayModes(std::uint32_t display) = 0;
    virtual int DisplayPixelsWide(std::uint32_t display) = 0;
    virtual int DisplayPixelsHigh(std::uint32_t display) = 0;
    virtual std::int32_t CurrentDisplayDepth(std::uint32_t display) = 0;             // 32 / 16 / 8, else 0
    virtual bool DisplayHasHardwareRenderer(std::uint32_t display) = 0;              // an accelerated renderer with the 0x400 bit
    virtual bool QueryPCIDisplayInfo(std::uint32_t display, ASLPCIDisplayInfo_t& out) = 0;   // the IOKit registry walk
    virtual bool IsMacOSSierraOrLater() = 0;
    // OpenGL
    virtual const char* GLRendererString() = 0;                                      // glGetString(GL_RENDERER)
    virtual void* CreateGLContext(void* window, bool fullscreen, int width, int height) = 0;   // null on failure
    virtual void DeleteGLContext(void* context) = 0;
    virtual void MakeCurrent(void* window, void* context) = 0;
    virtual void SwapWindow(void* window) = 0;
    virtual void FlushGL() = 0;
    virtual void EnableMultithreadedGL(bool enable) = 0;
    // SDL window
    virtual void* CreateWindow(const char* title, bool windowed, int width, int height, const CGRect& displayRect) = 0;
    virtual void DestroyWindow(void* window) = 0;
    virtual void SetWindowPosition(void* window, int x, int y) = 0;
    virtual void SetWindowSize(void* window, int w, int h) = 0;
    virtual void GetWindowSize(void* window, int& w, int& h) = 0;
    virtual float WindowOpacity(void* window) = 0;
    virtual void HideWindow(void* window) = 0;
    virtual void ShowWindow(void* window) = 0;
    virtual void CenterWindow(void* window) = 0;
    virtual void SetMenuBarVisible(bool visible) = 0;
    // Message box with Debug (0x64) / Quit (0x65) / Ignore (0x66) / Continue (0x67); returns the chosen id.
    virtual int ShowAlertBox(void* window, const char* title, const char* text) = 0;
    virtual bool IsMainThread() = 0;
    virtual void FlushInputEvents() = 0;
    virtual void Exit(int code) = 0;
};
IDisplayPlatform* DisplayPlatform();
void SetDisplayPlatform(IDisplayPlatform* platform);

// A GL context handle: the platform context, plus a flag that makes Sync swap rather than flush (+0x14).
struct OpaqueContextRef {
    void* context = nullptr;      // +0
    bool swapOnSync = false;      // +0x14
    ~OpaqueContextRef();          // @ 0x10063ebc8
    void Sync();                  // @ 0x10063ebf6
};

struct MacResItem {               // 12 bytes
    std::int32_t width, height, depth;
    struct byResolution { bool operator()(const MacResItem& a, const MacResItem& b) const { return a.width < b.width || a.height < b.height; } };
    struct byAspectDelta {
        double aspect;
        bool operator()(const MacResItem& a, const MacResItem& b) const;
    };
};

struct MacDisplay {
    static void SwapContext(OpaqueContextRef* context);                        // @ 0x10063ec18
    static std::int32_t Initialize();                                          // @ 0x10063ec49
    static std::int32_t SetupDisplay(std::uint8_t windowed, int width, int height);   // @ 0x10063f734
    static void ReleaseDisplay();                                              // @ 0x10063f8d5
    static void* GetMainWindow();                                              // @ 0x10063f907
    static void GetDisplayScale(float* x, float* y);                           // @ 0x10063f914
    static std::uint32_t GetDisplayID();                                       // @ 0x10063f93c
    static bool IsWindowMode();                                                // @ 0x10063f948
    static bool PtInDisplay(std::int32_t point);                               // @ 0x10063f955 (Point: v in the high half, h in the low)
    static void GetCurrentMode(int& width, int& height, int& depth);           // @ 0x10063f9a1
    static void SetMode(int width, int height, int depth);                     // @ 0x10063fa87
    static int GetNumModes();                                                  // @ 0x10063fb30
    static void GetNthMode(int n, int& width, int& height, int& depth);        // @ 0x10063fb4e
    static void GetNthNativeResolution(int n, int& width, int& height);        // @ 0x10063fb77
    static OpaqueContextRef* CreateScreenContext(int display, std::uint8_t& fastFlag);   // @ 0x10063fb99
    static void ReleaseContext(OpaqueContextRef*& context);                    // @ 0x10063fcbe
    static void SetCurrentContext(OpaqueContextRef* context);                  // @ 0x10063fcff
    static void GetVideoMemoryInfo(std::uint32_t& a, std::uint32_t& b);        // @ 0x10063fd16
    static std::int32_t GetClientRect(void* window, tagRECT* rect);            // @ 0x10063fd7a
};

// Free helpers of MacDisplay.cp / DisplayPicker.cp.
std::int32_t GetPCIDisplayInfo(std::uint32_t display, ASLPCIDisplayInfo_t* out);                  // @ 0x10063dbb3
void BuildDisplayList();                                                                          // @ 0x10063e1b6
std::int32_t ValidateDisplay(std::uint32_t index, const CGRect* rect, std::uint32_t* displayOut);   // @ 0x10063e2f8
bool IsDisplayValid(std::uint32_t display);                                                       // @ 0x10063e459
void NCT_SetWindowBorderSizes(int left, int top, int right, int bottom);                           // @ 0x10063eb80
void NCT_GetWindowMaxSizes(int* width, int* height);                                               // @ 0x10063eb96

// MacGlobals (0x100641a46..0x100641a6a), MacDebug (0x10063e89c / 0x10063e966), the log file (0x100641a7d..0x100641b48).
struct MacGlobals {
    static void LockSystem();      // @ 0x100641a46
    static void UnlockSystem();    // @ 0x100641a52
    static bool IsSystemLocked();  // @ 0x100641a6a
};
void MacLogToConsole(const char* text);                                         // @ 0x1007f9fb1 (NSLog)
struct CMacLogFile {
    std::FILE* file = nullptr;                                                  // +0 (never opened in the shipped build)
    ~CMacLogFile();                                                              // @ 0x100641b48
    void WriteMessage(const char* text);                                         // @ 0x100641af4
};
CMacLogFile& GetMacLogFile();                                                   // @ 0x100641a7d
struct MacLogFile { static void WriteMessage(const char* text); };              // @ 0x100641ad1
struct MacDebug {
    static void MessageToConsole(const char* format, ...);                      // @ 0x10063e89c
    static std::int32_t MessageToAlert(const char* format, ...);                // @ 0x10063e966
};

}  // namespace sgl
