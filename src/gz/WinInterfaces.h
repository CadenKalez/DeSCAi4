// The GZ window system interfaces (cIGZWin, cIGZWinMgr and the small collaborators), laid out in the original
// vtable order (recovered from the vtables of cGZWin 0x100a78950 and cGZWinMgrW32 0x100ad8960).
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZPoint.h"

class cIGZWin;
class cIGZWinMgr;

constexpr std::uint32_t kGZIID_cIGZWin = 0x22ba0121;       // also accepted by cGZWin::QueryInterface: 0xe98b2f57, 1
constexpr std::uint32_t kGZIID_cGZWinAlt = 0xe98b2f57;

// cGZMessage: type + three words. Mouse messages pack x / y / flags into the words as 16-bit values.
struct cGZMessage {
    std::uint32_t type = 0;          // +0
    std::uint64_t param1 = 0;        // +8
    std::uint64_t param2 = 0;        // +0x10
    std::uint64_t param3 = 0;        // +0x18
};
enum GZMessageType : std::uint32_t {
    kGZMsgDeleteChild = 1, kGZMsgCommand = 3, kGZMsgCharacter = 4, kGZMsgKeyDown = 5, kGZMsgKeyUp = 6,
    kGZMsgMouseDownL = 7, kGZMsgMouseDownR = 8, kGZMsgMouseUpL = 10, kGZMsgMouseUpR = 11, kGZMsgMouseMove = 13,
    kGZMsgMouseWheel = 14, kGZMsgKillFocus = 16, kGZMsgSetFocus = 17, kGZMsgCaptureChanged = 18,   // 16 goes to the window that LOSES the focus (cGZWin::DoMessage -> vslot 0x428 GZOnKillFocus), 17 to the one that gets it (0x420 GZOnSetFocus); the names were swapped before
    kGZMsgMouseEnter = 19, kGZMsgMouseExit = 20, kGZMsgPaint = 0x16,
};

// cIGZWin::tWinFlag bits (cGZWin +0x140; initial 0x8903)
enum GZWinFlag : std::uint32_t {
    kWinVisible = 0x1, kWinEnabled = 0x2, kWinFlag4 = 0x4, kWinUseShadeColor = 0x8, kWinFade = 0x20,
    kWinFlag100 = 0x100, kWinFlag800 = 0x800, kWinInitialized = 0x4000, kWinPrivateBuffer = 0x10000,
    kWinPrivateBufferTransparent = 0x20000, kWinHitTestTransparency = 0x80000, kWinIgnoreCursor = 0x200000,
    kWinFlag400000 = 0x400000, kWinPlotted = 0x4000000, kWinDesktop = 0x8000000,
};

// The mouse cursors (cRZCursor, cRZSystemCursorW95; iid 0x834c471e) and their manager (cRZCursorManager; iid 0xa34c498a).
// The members past the bare unknown all have do-nothing defaults so test doubles only override what they use.
class cIGZBuffer;
class cIGZCursor : public cIGZUnknown {
public:
    virtual bool Init() { return true; }                                        // [3]
    virtual bool Shutdown() { return true; }                                    // [4]
    virtual void SetCursor(bool apply) { (void)apply; }                         // [5] makes it the pointer
    virtual void ShowCursor(bool show) { (void)show; }                          // [6]
    virtual void GetCursorHotSpot(int& x, int& y) { x = y = 0; }                // [7]
    virtual void SetCursorHotSpot(int x, int y) { (void)x; (void)y; }           // [8]
    virtual bool LoadCursorA(const cIGZString& path) { (void)path; return false; }   // [9] a .cur file
    virtual void SetCursorHandle(void* handle) { (void)handle; }                // [10]
    virtual void SetCursorBuffer(cIGZBuffer* buffer) { (void)buffer; }          // [11]
    virtual bool SystemHasCursorSupport() { return false; }                     // [12]
    virtual cIGZBuffer* GetCursorBuffer() { return nullptr; }                   // [13]
    virtual bool IsLoaded() { return false; }                                   // [14]
};
class cIGZCursorManager : public cIGZUnknown {
public:
    virtual cIGZCursor* LoadCursor(std::uint32_t id) = 0;          // vslot 0x38 (the original calls it GetCursor(id))
    virtual void SetCursor(cIGZCursor* cursor) = 0;                // vslot 0x50
    virtual bool Init() { return true; }                           // [3]
    virtual bool Shutdown() { return true; }                       // [4]
    virtual bool AddCursor(cIGZCursor* cursor, std::uint32_t id) { (void)cursor; (void)id; return false; }   // [5]
    virtual bool RemoveCursor(std::uint32_t id) { (void)id; return false; }                // [6]
    virtual cIGZCursor* GetCursor() { return nullptr; }            // [8] the current cursor
    virtual bool SetCursor(std::uint32_t id) { (void)id; return false; }                   // [9]
    virtual bool PushAutoCursor(std::uint32_t id) { (void)id; return false; }              // [11]
    virtual bool PopAutoCursor() { return false; }                 // [12]
    virtual void AddCursor(const cIGZString& path, std::uint32_t id) { (void)path; (void)id; }   // [13]
    virtual void ShowCursor() {}                                   // [14]
    virtual void HideCursor() {}                                   // [15]
};
class cIGZKeyboard : public cIGZUnknown {};
// The keyboard accelerator table windows ask first (iid 0xe2c1b3c4; cRZWinKeyAccelerator implements it). A key is the
// virtual key code with the modifier bits (shifted to bit 16) and "key up" (bit 28); an entry maps it to a message.
class cIGZWinKeyAccelerator : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                  // [3]
    virtual bool Shutdown() = 0;                                                              // [4]
    virtual bool RegisterAccelerator(std::uint32_t key, std::uint32_t command) = 0;           // [5] a message of that type, no payload
    virtual bool RegisterAccelerator(std::uint32_t key, const cGZMessage& message) = 0;       // [6]
    virtual bool UnregisterAccelerator(std::uint32_t key) = 0;                                // [7]
    virtual bool IsAcceleratorPresent(std::uint32_t key, bool withModifiers, bool withDirection) = 0;   // [8]
    virtual bool TestForAccelerator(const cGZMessage& keyMessage, std::uint32_t& command) = 0;   // [9]
    virtual bool TestForAccelerator(const cGZMessage& keyMessage, cGZMessage& result) = 0;       // [10]
    virtual bool TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, std::uint32_t& command) = 0;   // [11]
    virtual bool TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, cGZMessage& result) = 0;       // [12]
    virtual int GetKeysForAccelerator(std::uint32_t command, std::uint32_t* keys, std::uint32_t& count) = 0;   // [13]
    virtual bool ProcessAccelerator(const cGZMessage& keyMessage) = 0;                        // [14] test, then send the result
};
class cIGZWinMessageFilter : public cIGZUnknown {
public:
    virtual bool DoMessage(cIGZWin* window, cGZMessage& message) = 0;    // vslot 0x18; true = handled
};

class cIGZWin : public cIGZUnknown {
public:
    using EnumChildFn = bool (*)(cIGZWin* parent, std::uint32_t id, void* child, void* context);
    using SortFn = bool (*)(cIGZWin* a, cIGZWin* b, void* context);
    using EnumParamFn = bool (*)(cIGZWin* window, std::uint32_t key, cIGZVariant* value, void* context);

    virtual bool DoMessage(cGZMessage& message) = 0;                                     // [3]
    virtual bool Init() = 0;                                                             // [4]
    virtual bool Shutdown() = 0;                                                         // [5]
    virtual cIGZWinMgr* GetWindowManager() = 0;                                          // [6]
    virtual bool SetWindowManager(cIGZWinMgr* mgr) = 0;                                  // [7]
    virtual cIGZKeyboard* GetKeyboard() = 0;                                             // [8]
    virtual bool SetKeyboard(cIGZKeyboard* keyboard) = 0;                                // [9]
    virtual cIGZWin* GetMainWindow() = 0;                                                // [10]
    virtual cIGZWin* GetParentWin() const = 0;                                           // [11]
    virtual bool SetParentWin(cIGZWin* parent) = 0;                                      // [12]
    virtual std::uint32_t GetChildCount() const = 0;                                     // [13]
    virtual bool ChildAdd(cIGZWin* child) = 0;                                           // [14]
    virtual bool ChildRemove(cIGZWin* child) = 0;                                        // [15]
    virtual bool ChildDelete(cIGZWin* child) = 0;                                        // [16]
    virtual bool ChildDeleteAbsolute(cIGZWin* child) = 0;                                // [17]
    virtual bool ChildDeleteAll() = 0;                                                   // [18]
    virtual bool ChildExists(cIGZWin* child) = 0;                                        // [19]
    virtual bool ChildExists(std::uint32_t id) = 0;                                      // [20]
    virtual bool IsWinInParentChain(cIGZWin* window) = 0;                                // [21]
    virtual bool IsWinInChildChain(cIGZWin* window) = 0;                                 // [22]
    virtual bool PullToFront() = 0;                                                      // [23]
    virtual bool SendToBack() = 0;                                                       // [24]
    virtual bool ChildToFront(cIGZWin* child) = 0;                                       // [25]
    virtual bool ChildToFront(std::uint32_t id) = 0;                                     // [26]
    virtual bool ChildToBack(cIGZWin* child) = 0;                                        // [27]
    virtual bool ChildStepFront(cIGZWin* child) = 0;                                     // [28]
    virtual bool ChildStepBack(cIGZWin* child) = 0;                                      // [29]
    virtual bool MoveRelativeTo(cIGZWin* other, bool before) = 0;                        // [30]
    virtual bool ChildMoveRelative(cIGZWin* child, cIGZWin* other, bool before) = 0;     // [31]
    virtual bool EnumChildren(std::uint32_t iid, EnumChildFn fn, void* context) = 0;     // [32]
    virtual bool SortChildren(SortFn fn, void* context, bool includeFixed) = 0;          // [33]
    virtual cIGZWin* GetChildWindowFromID(std::uint32_t id) = 0;                         // [34]
    virtual cIGZWin* GetChildWindowFromIDRecursive(std::uint32_t id) = 0;                // [35]
    virtual bool GetChildAs(std::uint32_t id, std::uint32_t iid, void** out) = 0;        // [36]
    virtual bool GetChildAsRecursive(std::uint32_t id, std::uint32_t iid, void** out) = 0;   // [37]
    virtual cIGZWin* GetWindowFromPoint(int x, int y) = 0;                               // [38]
    virtual cIGZWin* GetChildWindowFromPoint(int x, int y) = 0;                          // [39]
    virtual cIGZWin* GetChildWindowFromCursorPoint(int x, int y) = 0;                    // [40]
    virtual int GetW() const = 0;                                                        // [41]
    virtual int GetH() const = 0;                                                        // [42]
    virtual int GetL() const = 0;                                                        // [43]
    virtual int GetT() const = 0;                                                        // [44]
    virtual int GetR() const = 0;                                                        // [45]
    virtual int GetB() const = 0;                                                        // [46]
    virtual const cRZRect& GetArea() const = 0;                                          // [47]
    virtual void GetArea(cRZRect& out) const = 0;                                        // [48]
    virtual const cRZRect& GetAreaAbsolute() const = 0;                                  // [49]
    virtual void GetAreaAbsolute(cRZRect& out) const = 0;                                // [50]
    virtual void SetW(int w) = 0;                                                        // [51]
    virtual void SetH(int h) = 0;                                                        // [52]
    virtual void SetSize(int w, int h) = 0;                                              // [53]
    virtual void SetArea(int l, int t, int r, int b) = 0;                                // [54]
    virtual void SetArea(const cRZRect& area) = 0;                                       // [55]
    virtual void GZWinMoveTo(int x, int y) = 0;                                          // [56]
    virtual void GZWinOffset(int dx, int dy) = 0;                                        // [57]
    virtual bool FitRectToWindow(cRZRect& rect, int margin) = 0;                         // [58]
    virtual void ScreenToWindowCoordinates(int& x, int& y) const = 0;                    // [59]
    virtual void WindowToScreenCoordinates(int& x, int& y) const = 0;                    // [60]
    virtual bool WindowToWindowCoordinates(cIGZWin* other, int& x, int& y) const = 0;    // [61]
    virtual bool IsPointInWindowScreenCoordinates(int x, int y) const = 0;               // [62]
    virtual std::uint32_t GetID() const = 0;                                             // [63]
    virtual void SetID(std::uint32_t id) = 0;                                            // [64]
    virtual std::uint32_t GetInstanceID() const = 0;                                     // [65]
    virtual bool SetInstanceID(std::uint32_t id) = 0;                                    // [66]
    virtual bool GetFlag(std::uint32_t flag) const = 0;                                  // [67]
    virtual std::uint32_t SetFlag(std::uint32_t flag, bool on) = 0;                      // [68]
    virtual void ShowWindow() = 0;                                                       // [69]
    virtual void HideWindow() = 0;                                                       // [70]
    virtual bool IsVisible() const = 0;                                                  // [71]
    virtual bool IsEnabled() const = 0;                                                  // [72]
    virtual const cIGZString& GetCaption() const = 0;                                    // [73]
    virtual void SetCaption(const cIGZString& caption) = 0;                              // [74]
    virtual cIGZWinKeyAccelerator* GetKeyboardAccelerator() const = 0;                   // [75]
    virtual void SetKeyboardAccelerator(cIGZWinKeyAccelerator* accelerator) = 0;         // [76]
    virtual bool AccelerateKeyboardMsg(cGZMessage& message) = 0;                         // [77]
    virtual std::uint32_t GetKeyEquivalent() = 0;                                        // [78]
    virtual void SetKeyEquivalent(std::uint32_t key) = 0;                                // [79]
    virtual bool CheckKeyEquivalent(std::uint32_t key, std::uint32_t modifiers) = 0;     // [80]
    virtual std::uint32_t MakeKeyEquivalent(std::uint32_t key, std::uint32_t modifiers) = 0;   // [81]
    virtual cIGZWin* IsChildKeyEquivalent(std::uint32_t key, std::uint32_t modifiers) = 0;     // [82]
    virtual bool ProcessCursorMessage(cGZMessage& message) = 0;                          // [83]
    virtual void UpdateCursor() = 0;                                                     // [84]
    virtual void SetCursor(cIGZCursor* cursor, bool updateNow) = 0;                      // [85]
    virtual void SetNotificationTarget(cIGZWin* target) = 0;                             // [86]
    virtual cIGZWin* GetNotificationTarget() const = 0;                                  // [87]
    virtual bool GZPaint() = 0;                                                          // [88]
    virtual bool Plot() = 0;                                                             // [89]
    virtual void CalcAbsoluteArea() = 0;                                                 // [90]
    virtual void InvalidateSelf() = 0;                                                   // [91]
    virtual void InvalidateSelfAndParents() = 0;                                         // [92]
    virtual void* GetDrawContext() = 0;                                                  // [93]
    virtual void* GetBufferToDrawTo() = 0;                                               // [94]
    virtual void SetBufferToDrawTo() = 0;                                                // [95]
    virtual void SetBufferToDrawToRecursive() = 0;                                       // [96]
    virtual void SetAreaToDrawTo() = 0;                                                  // [97]
    virtual void SetAreaToDrawToRecursive() = 0;                                         // [98]
    virtual const cRZRect& GetAreaToDrawTo() = 0;                                        // [99]
    virtual bool PrivateBuffer(bool enable) = 0;                                         // [100]
    virtual void* GetPrivateBuffer() = 0;                                                // [101]
    virtual void GetFillColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const = 0;   // [102]
    virtual void SetFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;       // [103]
    virtual std::uint32_t GetFillColor() const = 0;                                      // [104]
    virtual void SetFillColor(std::uint32_t nativeColor) = 0;                            // [105]
    virtual bool GetFillColor(std::uint32_t& rgb) const = 0;                             // [106]
    virtual bool SetFillColorColor(std::uint32_t rgb) = 0;                               // [107] SetFillColor(cRZColor)
    virtual std::uint32_t MakeFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;   // [108]
    virtual void SetFadeEffectPeriod(int fadeIn, int fadeOut) = 0;                       // [109]
    virtual void GetFadeEffectPeriod(int& fadeIn, int& fadeOut) const = 0;               // [110]
    virtual void SetShadeColor(std::uint32_t color) = 0;                                 // [111]
    virtual void GetShadeColor(std::uint32_t& color) const = 0;                          // [112]
    virtual bool GetParam(std::uint32_t key, cIGZVariant** out) const = 0;               // [113]
    virtual bool SetParam(std::uint32_t key, cIGZVariant* value) = 0;                    // [114]
    virtual bool EnumParams(EnumParamFn fn, void* context) = 0;                          // [115]
    virtual bool AddMessageFilter(cIGZWinMessageFilter* filter) = 0;                     // [116]
    virtual bool RemoveMessageFilter(cIGZWinMessageFilter* filter) = 0;                  // [117]
    virtual void SetSize(const cRZPoint& size) = 0;                                      // [118]
    virtual void CenterWindowInRect(const cRZRect* rect) = 0;                            // [119]
    virtual void CenterWindowInRect(const cRZRect& rect) = 0;                            // [120]
    virtual bool IsPointInWindowWindowCoordinates(int x, int y) const = 0;               // [121]
    virtual bool IsPointInWindowParentCoordinates(int x, int y) const = 0;               // [122]
    virtual bool PlotComposite() = 0;                                                    // [123]
    virtual bool PlotPresent() = 0;                                                      // [124]
    virtual void SetFillColorRGB(std::uint32_t rgb) = 0;                                 // [125]
    virtual std::uint32_t GetFillColorRGB() const = 0;                                   // [126]
    virtual std::uint32_t ConvertPackedRGBToNative(std::uint32_t rgb) const = 0;         // [127]
    virtual std::uint32_t ConvertNativeToPackedRGB(std::uint32_t native) const = 0;      // [128]
    virtual bool GZOnCharacter(char c) = 0;                                              // [129]
    virtual bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) = 0;            // [130]
    virtual bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) = 0;              // [131]
    virtual bool GZOnSetFocus(cIGZWin* previous) = 0;                                    // [132]
    virtual bool GZOnKillFocus(cIGZWin* next) = 0;                                       // [133]
    virtual bool GZOnMouseDownL(int x, int y, std::uint32_t flags) = 0;                  // [134]
    virtual bool GZOnMouseDownR(int x, int y, std::uint32_t flags) = 0;                  // [135]
    virtual bool GZOnMouseUpL(int x, int y, std::uint32_t flags) = 0;                    // [136]
    virtual bool GZOnMouseUpR(int x, int y, std::uint32_t flags) = 0;                    // [137]
    virtual bool GZOnMouseMove(int x, int y, std::uint32_t flags) = 0;                   // [138]
    virtual bool GZOnMouseWheel(int x, int y, std::uint32_t flags, int delta) = 0;       // [139]
    virtual bool GZOnCaptureChanged(cIGZWin* lost, cIGZWin* gained) = 0;                 // [140]
    virtual bool GZOnMouseEnter(cIGZWin* from) = 0;                                      // [141]
    virtual bool GZOnMouseExit(cIGZWin* to) = 0;                                         // [142]
    virtual bool GZOnCommand(std::uint32_t command, std::uint64_t param) = 0;            // [143]
    virtual bool SendMsg(cIGZWin* target, cGZMessage& message) = 0;                      // [144]
    virtual void SendMsg(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) = 0;   // [145]
    virtual bool PostMsg(cIGZWin* target, cGZMessage& message) = 0;                      // [146]
    virtual void PostMsg(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) = 0;   // [147]
    // [148],[149] destructors
    virtual bool is_point_transparent(int x, int y) const = 0;                           // [150]
    virtual bool create_private_buffer(int w, int h) = 0;                                // [151]
};

// cIGZWinMgr (cGZWinMgrW32 vtable group 0). Only the slots the window code calls are named here.
class cIGZWinMgr : public cIGZUnknown {
public:
    virtual cIGZWin* GetMainWindow() = 0;                                // [3]
    // [9] +0x48: a dialog-like window finished (code 0xf from the list box and the generic window; result is the choice).
    virtual void EndDialog(std::uint32_t code, cIGZWin* window, std::int64_t result) { (void)code; (void)window; (void)result; }
    virtual void AppendToPlotStrategy(cIGZWin* window) = 0;              // [7]
    virtual void Plot() = 0;                                             // [6]
    virtual void PostMessageToTarget(cIGZWin* target, const cGZMessage& message) = 0;   // [14]
    virtual void AddWindowToValidList(cIGZWin* window) = 0;              // [20]
    virtual void RemoveWindowFromValidList(cIGZWin* window) = 0;         // [21]
    virtual void CleanUpWindowReferences(cIGZWin* window) = 0;           // [22]
    virtual bool DestroyWindow(cIGZWin* window) = 0;                     // [23]
    virtual bool IsWindowValid(cIGZWin* window) = 0;                     // [24]
    virtual cIGZWin* GetWindowFromPoint(int x, int y) = 0;               // [25]
    // [34] +0x110: the mouse position in `window`'s coordinates.
    // [33] +0x108: the mouse position in screen coordinates.
    virtual void GetMousePosition(int& x, int& y) { x = 0; y = 0; }
    virtual void GetCursorPos(cIGZWin* window, int& x, int& y) { (void)window; x = 0; y = 0; }
    // [41] +0x148: shows `popup` modally under the mouse and answers the index the user chose (-1 when none).
    virtual int RunPopup(cIGZWin* popup) { (void)popup; return -1; }
    virtual void SetCurrentMouseWin(cIGZWin* window) = 0;                // [26]
    virtual cIGZWin* GZGetCapture() = 0;                                 // [28]
    virtual void GZSetCapture(cIGZWin* window) = 0;                      // [29]
    virtual void GZReleaseCapture(cIGZWin* window) = 0;                  // [30]
    virtual cIGZCursorManager* GetCursorManager() = 0;                   // [32]
    virtual cIGZWin* GZGetFocus() = 0;                                   // [36]
    virtual void GZSetFocus(cIGZWin* window) = 0;                        // [37]
    virtual cIGZKeyboard* GetKeyboard() = 0;                             // [38]
    // [44] +0x160: the game's own message box (cGZWinMgrBase::GZMsgBox @ 0x100623722); answers the button id
    // (gz::kMsgBoxYes 0x5301814c and friends) or 0 when no box could be shown. Default: no box.
    virtual int GZMsgBox(const cIGZString& text, const cIGZString& title, std::uint32_t style, bool unused1,
                         std::uint32_t unused2) {
        (void)text;
        (void)title;
        (void)style;
        (void)unused1;
        (void)unused2;
        return 0;
    }
};
