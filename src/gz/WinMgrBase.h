// cGZWinMgrBase — the window manager every concrete manager (cGZWinMgrW32 and friends) derives from
// (0x100621a70..0x100623f4c, service id 0xa417445e, priority 0x1312d0, tick priority 0x4c4b41, interface iid 0x5a4).
// It owns the window bookkeeping: the main (desktop) window, the valid-window set, keyboard focus, mouse capture, the
// window under the mouse, the modal window and its nesting count, the posted-message queue, the plot strategy, the
// condemned-window list and the key-filter list. What a platform manager adds is the OS event pump: the two pure
// virtuals below (the post of a raw window-system message and the modal loop).
//
// Layout (0x278 bytes): cRZSystemService (+8), +0x30 posted messages, +0x48 critical section, +0x58 initialised,
// +0x60 capture, +0x68 focus, +0x70 modal window, +0x78 window under the mouse, +0x80 main window, +0x88 valid-window
// hash set (max load factor 1.0 at +0xa8), +0xb0 framework, +0xb8 keyboard, +0x1d8 cursor manager, +0x1e0/+0x1e4
// mouse x/y, +0x1f0 mouse flags, +0x1f4 modal nest count, +0x1f8 plot enabled, +0x200 plot list, +0x218 plotting,
// +0x21c busy count, +0x220 condemned list, +0x238 next instance id (starts at 1), +0x240 deferred EndModal map,
// +0x258 key filters, +0x270..0x276 behaviour switches (all set by the constructor, nothing in this class changes them).
//
// Seams: IWinMgrBaseHost mirrors the four engine services the class reaches (framework, COM class lookup, message
// server, graphic system). The platform half is the pair PostWinMessage / RunModalLoop (vslots 0x48 and 0x220).
//
// Return types: the original returns a bool from several functions whose cIGZWinMgr slot (WinInterfaces.h) is declared
// void; those keep the cIGZWinMgr signature and the original result is available from the Try* / *Ex companions.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <unordered_set>

#include "gz/Win.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZMessageServer.h"
#include "gz/cIGZFrameWork.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZKeyboard.h"
#include "rz/cRZSystemService.h"

namespace gz {

constexpr std::uint32_t kGZSID_cGZWinMgrBase = 0xa417445e;
constexpr std::uint32_t kGZIID_cIGZWinMgr = 0x000005a4;
constexpr std::int32_t kGZWinMgrServicePriority = 0x1312d0;
constexpr std::int32_t kGZWinMgrTickPriority = 0x4c4b41;
constexpr std::uint32_t kGZCLSID_CursorManager = 0x4352fef3, kGZIID_CursorManager = 0xa34c498a;
constexpr std::uint32_t kGZCLSID_WinMsgBox = 0x00000593, kGZIID_WinMsgBox = 0x0000057c;

// Messages the manager itself builds or sends.
constexpr std::uint32_t kWinMgrMsgEndModal = 0x0f;          // EndModal -> the posted message type
constexpr std::uint32_t kWinMgrMsgSetFocusLost = 0x10;      // sent to the window that loses the focus (p1 = new focus)
constexpr std::uint32_t kWinMgrMsgSetFocusGained = 0x11;    // sent to the window that gets it (p1 = old focus)
constexpr std::uint32_t kWinMgrMsgCaptureChanged = 0x12;    // p1 = old capture, p2 = new capture
constexpr std::uint32_t kWinMgrMsgMouseEnter = 0x13;        // p1 = previous window under the mouse
constexpr std::uint32_t kWinMgrMsgMouseExit = 0x14;         // p1 = next window under the mouse
constexpr std::uint32_t kWinMgrMsgKeyPreview = 0x1a;        // offered up the parent chain before the key itself (p1 target, p2 key msg type, p3 &msg)
constexpr std::uint32_t kWinMgrMsgKeyUnhandled = 0x1b;      // offered after the focus window refused the key
constexpr std::uint32_t kWinMgrModalBegin = 0x62ba7b42;     // message-server broadcast when DoModalWin starts (p1 window id, p2 window, p3 nest)
constexpr std::uint32_t kWinMgrModalEnd = 0x62ba7b43;       // ... and when it finished
constexpr std::uint32_t kWinFlagCanFocus = 0x8000;          // cIGZWin flag tested before a window takes the focus
constexpr std::uint32_t kWinFlagModal = 0x1000;             // set on a window while it is the modal window
constexpr std::uint64_t kModalNoResult = 0x80000000;        // DoModalWin's result when nothing ran

class IWinMgrBaseHost {
public:
    virtual ~IWinMgrBaseHost() = default;
    virtual cIGZFrameWork* Framework() = 0;                                              // RZGetFramework() @ 0x10002ab30
    virtual bool GetCOMObject(std::uint32_t classId, std::uint32_t iid, void** out) = 0;  // GZCOM() (0x10003f2d4) vslot 0x18
    virtual cIGZMessageServer* MessageServer() = 0;                                      // RZ::MS() @ 0x100127b18
    virtual int GraphicSystemShowCursor(bool show) = 0;                                  // graphic system (service 0xc416025c, iid 0x73283c) vslot 0xf0
};

// The keyboard sub-object (+0xb8): a cRZKeyboard that also answers as the cIGZKeyboard handed to windows.
class WinMgrKeyboard final : public cIGZKeyboard, public cRZKeyboard {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }       // not counted: lives inside the manager
    std::uint32_t Release() override { return 1; }
};

class cGZWinMgrBase : public cIGZWinMgr, public cRZSystemService {
public:
    // A message waiting in the post queue (cGZWinMgrBase::MessageTargetInfo, 0x38-byte list node).
    struct MessageTargetInfo {
        cIGZWin* target = nullptr;
        cGZMessage message;
    };

    explicit cGZWinMgrBase(IWinMgrBaseHost* host = nullptr);          // @ 0x100621a70
    ~cGZWinMgrBase() override;                                        // @ 0x100621cdc

    void SetHost(IWinMgrBaseHost* host) { host_ = host; }

    // ---- platform half (pure in the original) ----------------------------------------------------------------------
    // vslot 0x48: queue a raw window-system message (type, three words). EndModal and GZWinMsgPost_* go through it.
    virtual void PostWinMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) = 0;
    // vslot 0x220: run the modal event loop for `window` until it ends; (window, framework, 0x80000000, 0x80000001).
    virtual std::uint64_t RunModalLoop(cIGZWin* window, cIGZFrameWork* framework, std::uint32_t noResult, std::uint32_t abortCode) = 0;
    // The message box DoModalWin shows for GZMsgBox: asks COM for class 0x593 (iid 0x57c) and falls back to a stock
    // cGZWinMsgBox, then CreateDlg(text, title, style). Returns the dialog window with one reference, or null.
    // Implemented in WinMgrBaseMsgBox.cpp; a platform manager or a test may override it.
    virtual cIGZWin* CreateMsgBoxWindow(const cIGZString& text, const cIGZString& title, std::uint32_t style);

    // ---- service / unknown ------------------------------------------------------------------------------------------
    bool QueryInterface(std::uint32_t iid, void** out) override;      // @ 0x100621f00 (+ thunks 0x100621f22, 0x100621f48)
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }     // @ 0x100621eb4 (+ thunks)
    std::uint32_t Release() override { return cRZSystemService::Release(); }   // @ 0x100621eda (+ thunks)
    bool Init() override;                                             // @ 0x100621f6e (+ thunk 0x10062208e)
    bool Shutdown() override;                                         // @ 0x1006220a0 (+ thunk 0x1006222ba)
    bool OnTick(std::uint32_t elapsedMs) override;                    // @ 0x1006222cc (+ thunk 0x1006222de): ProcessPostedMessages, always true

    // ---- main window / posting -----------------------------------------------------------------------------------------
    cIGZWin* GetMainWindow() override { return mainWindow_.get(); }   // @ 0x1006222f4
    bool GetMainWinAs(std::uint32_t iid, void** out) const;           // @ 0x100622302
    virtual void GZWinMsgPost_Mouse(std::uint32_t type, int x, int y, std::uint32_t flags);   // @ 0x100622320
    virtual void GZWinMsgPost_Keyboard(std::uint32_t type, std::uint32_t key, std::uint32_t modifiers);   // @ 0x100622340
    void EndDialog(std::uint32_t code, cIGZWin* window, std::int64_t result) override { PostWinMessage(code, std::uint64_t(reinterpret_cast<std::uintptr_t>(window)), std::uint64_t(result), std::uint64_t(result)); }   // slot 0x48
    virtual void EndModal(cIGZWin* window, std::uint64_t result);             // @ 0x1006231a6
    void AddDeferredEndModal(cIGZWin* window, std::uint64_t result);  // @ 0x1006231c6

    // ---- plotting ---------------------------------------------------------------------------------------------------------
    void Plot() override;                                             // @ 0x100622352
    bool AppendToPlotStrategyEx(cIGZWin* window);                     // @ 0x1006223a2 (original returns the bool)
    void AppendToPlotStrategy(cIGZWin* window) override { AppendToPlotStrategyEx(window); }
    bool ExecutePlotStrategy();                                       // @ 0x100622406

    // ---- messages ---------------------------------------------------------------------------------------------------------
    virtual bool SendMessageToTarget(cIGZWin* target, cGZMessage& message);   // @ 0x100622478
    virtual bool SendMessageToTarget(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3);   // @ 0x1006224b4
    virtual bool ProcessMouseMessage(cGZMessage& message);                    // @ 0x100622510
    virtual bool ProcessKeyboardMessage(cGZMessage& message);                 // @ 0x10062275c
    void PostMessageToTarget(cIGZWin* target, const cGZMessage& message) override;   // @ 0x1006228ba
    void PostMessageToTarget(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3);   // @ 0x100622944
    virtual void ProcessPostedMessages();                                     // @ 0x1006229d2
    std::size_t PostedMessageCount() const { return posted_.size(); }

    // ---- key filters ------------------------------------------------------------------------------------------------------
    void GZRegisterKeyFilter(cIGZWinMessageFilter* filter);           // @ 0x100622b2a
    void GZUnregisterKeyFilter(cIGZWinMessageFilter* filter);         // @ 0x100622bdc
    virtual bool RunKeyFilters(cGZMessage& message);                          // @ 0x100622c54

    // ---- capture / cursor / focus -------------------------------------------------------------------------------------
    cIGZWin* GZGetCapture() override { return capture_.get(); }       // @ 0x100622ca2
    bool GZSetCaptureEx(cIGZWin* window);                             // @ 0x100622cac (always true)
    void GZSetCapture(cIGZWin* window) override { GZSetCaptureEx(window); }
    bool GZReleaseCaptureEx(cIGZWin* window);                         // @ 0x100622dcc
    void GZReleaseCapture(cIGZWin* window) override { GZReleaseCaptureEx(window); }
    cIGZCursorManager* GetCursorManager() override { return cursors_.get(); }   // @ 0x100622e18
    void GetCursorRelativePosition(cIGZWin* window, int& x, int& y);  // @ 0x100622e26
    void GetCursorPos(cIGZWin* window, int& x, int& y) override { GetCursorRelativePosition(window, x, y); }   // slot 0x110
    int GZShowCursor(bool show);                                      // @ 0x100622e68
    virtual void GetCursorScreenPosition(int& x, int& y);                     // @ 0x100622ee0
    void GetMousePosition(int& x, int& y) override { GetCursorScreenPosition(x, y); }   // slot 0x108
    void SetCursorPosition(int x, int y);                             // @ 0x100622ef6 (does nothing here; platform managers move the pointer)
    bool SetCurrentMouseWinEx(cIGZWin* window);                       // @ 0x100622efc
    void SetCurrentMouseWin(cIGZWin* window) override { SetCurrentMouseWinEx(window); }
    cIGZWin* GZGetFocus() override { return focus_.get(); }           // @ 0x10062302e
    cIGZWin* GZSetFocusEx(cIGZWin* window);                           // @ 0x100623038 (answers the focus window afterwards)
    void GZSetFocus(cIGZWin* window) override { GZSetFocusEx(window); }
    cIGZKeyboard* GetKeyboard() override { return &keyboard_; }       // @ 0x10062317c

    // ---- modal ----------------------------------------------------------------------------------------------------------------
    bool IsModal() const { return modalNest_ != 0; }                  // @ 0x10062318a
    std::uint32_t GetModalNestCount() const { return modalNest_; }    // @ 0x10062319a
    virtual std::uint64_t DoModalWin(cIGZWin* window);                        // @ 0x1006232a8
    int RunPopup(cIGZWin* popup) override { return int(DoModalWin(popup)); }   // slot 0x148 is DoModalWin
    int GZMsgBox(const cIGZString& text, const cIGZString& title, std::uint32_t style, bool unused1, std::uint32_t unused2) override;   // @ 0x100623722
    bool SetGZMsgBoxCMDID(std::uint32_t, std::uint32_t, bool) { return false; }              // @ 0x100623824
    bool GetGZMsgBoxCMDID(std::uint32_t&, std::uint32_t&, bool&) const { return false; }     // @ 0x10062382c

    // ---- valid-window list ------------------------------------------------------------------------------------------------
    bool TryAddWindowToValidList(cIGZWin* window);                    // @ 0x100623834
    void AddWindowToValidList(cIGZWin* window) override { TryAddWindowToValidList(window); }
    bool TryRemoveWindowFromValidList(cIGZWin* window);               // @ 0x100623ad4
    void RemoveWindowFromValidList(cIGZWin* window) override { TryRemoveWindowFromValidList(window); }
    void CleanUpWindowReferences(cIGZWin* window) override;           // @ 0x100623b14
    bool IsWindowValid(cIGZWin* window) override;                     // @ 0x100623c4e
    cIGZWin* GetWindowFromPoint(int x, int y) override;               // @ 0x100623c80
    virtual void InvalidateAllWindows();                                      // @ 0x100623ca2
    std::size_t ValidWindowCount() const { return validWindows_.size(); }

    // ---- destruction --------------------------------------------------------------------------------------------------------
    virtual bool DestroyWindowAbsolute(cIGZWin* window);                      // @ 0x100623cce
    virtual void IncrementBusyCount() { ++busyCount_; }                       // @ 0x100623d6e
    virtual int DecrementBusyCount();                                         // @ 0x100623d82
    bool DestroyWindow(cIGZWin* window) override;                     // @ 0x100623dc6
    virtual bool DestroyCondemnedWindows();                                   // @ 0x100623e4e
    virtual bool IsWindowOrParentInCondemnedList(cIGZWin* window);            // @ 0x100623ec8
    int BusyCount() const { return busyCount_; }

    // ---- debug gadgets (the base shows none) ----------------------------------------------------------------------------
    void DebugDumpWindowList() {}                                     // @ 0x100623f22
    bool DebugIsFocusGadgetVisible() const { return false; }          // @ 0x100623f28
    void DebugSetFocusGadgetVisible(bool) {}                          // @ 0x100623f30
    bool DebugIsCaptureGadgetVisible() const { return false; }        // @ 0x100623f36
    void DebugSetCaptureGadgetVisible(bool) {}                        // @ 0x100623f3e
    bool DebugIsInvalidateGadgetVisible() const { return false; }     // @ 0x100623f44
    void DebugSetInvalidateGadgetVisible(bool) {}                     // @ 0x100623f4c

    // State the tests and platform managers read.
    cIGZWin* ModalWindow() const { return modal_.get(); }
    cIGZWin* MouseWindow() const { return mouse_.get(); }
    cIGZFrameWork* FrameworkPtr() const { return framework_; }
    bool IsInitialised() const { return initialised_; }
    // The behaviour switches at +0x270..+0x276 (constructor values: 1,0,1,1,1,0,1).
    bool allowCaptureSteal = true;       // +0x270 GZSetCapture may replace an existing capture
    bool moveFallbackToMain = false;     // +0x271 a mouse move nobody took re-targets the window under the cursor
    bool releaseCaptureOnModal = true;   // +0x272 DoModalWin drops a foreign capture
    bool showModalWindow = true;         // +0x273 DoModalWin shows the window
    bool pullModalToFront = true;        // +0x274 DoModalWin raises the window
    bool alwaysRemoveModal = false;      // +0x275 remove the dialog from its parent afterwards
    bool removeOrphanModal = true;       // +0x276 ... only when it had no valid parent

private:
    template <class T>
    static void AssignRef(cRZAutoRefCount<T>& slot, T* value) { slot = value; }

    struct PtrHash {   // cGZWinMgrBase::tIGZWinPtrHash: the pointer shifted right by three
        std::size_t operator()(cIGZWin* w) const noexcept { return reinterpret_cast<std::uintptr_t>(w) >> 3; }
    };

    IWinMgrBaseHost* host_ = nullptr;
    std::list<MessageTargetInfo> posted_;                       // +0x30
    cRZCriticalSection postLock_;                               // +0x48
    bool initialised_ = false;                                  // +0x58
    cRZAutoRefCount<cIGZWin> capture_, focus_, modal_, mouse_, mainWindow_;   // +0x60 .. +0x80
    std::unordered_set<cIGZWin*, PtrHash> validWindows_;        // +0x88
    cIGZFrameWork* framework_ = nullptr;                        // +0xb0
    WinMgrKeyboard keyboard_;                                   // +0xb8
    cRZAutoRefCount<cIGZCursorManager> cursors_;                // +0x1d8
    std::int32_t mouseX_ = 0, mouseY_ = 0;                      // +0x1e0, +0x1e4
    std::int32_t mouseFlags_ = 0;                               // +0x1f0
    std::uint32_t modalNest_ = 0;                               // +0x1f4
    bool plotEnabled_ = true;                                   // +0x1f8
    std::list<cIGZWin*> plotList_;                              // +0x200
    bool plotting_ = false;                                     // +0x218
    int busyCount_ = 0;                                         // +0x21c
    std::list<cIGZWin*> condemned_;                             // +0x220
    std::uint32_t nextInstanceId_ = 1;                          // +0x238
    std::map<cIGZWin*, std::uint64_t> deferredEndModal_;        // +0x240
    std::list<cRZAutoRefCount<cIGZWinMessageFilter>> keyFilters_;   // +0x258
};

}  // namespace gz
