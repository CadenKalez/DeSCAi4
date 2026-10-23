// cSC4AdvisorPopupTracker — the advisor popup queue (13 member functions, 17 symbols with the four non-virtual thunks,
// 0x10032f2ac..0x1003303ca, object 0x38 bytes, two vtable views, no class id: cSC4View3DWin::AddGUI allocates it with
// operator new, stores it at the view's +0x4d0, takes one reference and calls Init; the port's View3DWin still calls
// that field tipManager_ - it is this class, see the to-do row APT-6).
//
// It listens for the advisor system's "advice activated" notification (0x8a09fa64), queues every advice whose 0x6000
// flags are clear, and shows the queued advices one at a time, in activation order: each activation posts the tick
// 0xea83607b to itself, and the tick pops the first advice and builds its dialog from the UI script
// "instance 0x4a5a89d5 - (mood < 3)". The cooldown rules (frequency / timeout, next day it may fire again) live in
// cSC4Advice and cSC4Advisor; this class only decides *when* a queued advice may be put on screen.
//
// Original layout (0x38 bytes):
//   +0x00 cIGZMessageTarget2 vptr (iid 0x452294aa; DoMessage at slot 3)
//   +0x08 cIGZWinProc vptr       (DoWinProcMessage at slot 3, DoWinMsg at slot 4)
//   +0x10 int  reference count        +0x14 bool subscribed to the three notifications
//   +0x18 std::list<cRZAutoRefCount<cISC4Advice>> pending advices (libc++: sentinel +0x18 / +0x20, size +0x28)
//   +0x30 cSC4AdvisorUIImageWinProc* the image window procedure of the dialog that is on screen (counted)
// The original's *primary* vtable is [0] QueryInterface, [1] AddRef, [2] Release, [3] DoMessage, [4]/[5] the
// complete / deleting destructor, [6] Init (+0x30, called by AddGUI), [7] Shutdown (+0x38), [8] DoWinProcMessage
// (the same function the cIGZWinProc view uses; the class declares it, so it gets a slot in both groups). This port
// declares the same members in that order, so the primary slots line up too; `clang -fdump-vtable-layouts` produces the
// same nine-entry primary group and the same five-entry cIGZWinProc group.
//
// The three messages it routes:
//   0x8a09fa64 an advice was activated (the message's unknown is QueryInterface'd for cISC4Advice 0x8a09f5f5):
//              when its 0x6000 flags are clear the advice is queued, its 0x4000 flag is set and the tick is posted.
//   0xea83607b the show tick: pops the first advice when the game may show a popup right now, otherwise re-posts
//              itself while the queue is non-empty (see the quirks).
//   0x26d31ec2 drop the queue (the city was closed / restarted).
// The queue is dropped without touching the advices' 0x4000 flag, so an advice that was queued when the clear
// arrives keeps the "queued" bit set for the rest of its life - the original does not clear it either.
//
// Quirks kept:
//  * QueryInterface answers iid 0x2a5a754d, not the cIGZWinProc iid 0x22e85d8e every other window procedure of the
//    game accepts (0x10032f4d1). A caller that asks for 0x22e85d8e gets "no"; the window manager never does.
//  * QueryInterface stores the *primary* pointer for every accepted iid, including the window-procedure one - it does
//    not adjust to the +8 subobject (0x10032f4d9).
//  * The show tick pops the advice and runs the dialog only when the simulator is not paused and the modal-dialog
//    "safe" test passed *before* the pop; a paused / unsafe game leaves the advice in the queue and re-posts the tick,
//    so the message server keeps one tick in flight until the game allows the popup (0x10032f6d6 / 0x10032f6e3).
//  * The popup is suppressed (advice popped and dropped) while any tutorial is running (city vslot 0x290 -> vslot
//    0x40) or while the vehicle manager reports its paused state (vslot 0x68), and when the advice's priority is
//    below the advisor system's threshold (city vslot 0x288 -> vslot 0xa8), so a low-priority advice never pops.
//  * The dialog is only built when both SC4GetPopupModalDialogsEnabled and the preference byte +0xf0b of the
//    preference blob are set - the preference is not the same flag the "safe" test above uses.
//  * The dialog's script instance is one less for a bad mood (mood < 3): 0x4a5a89d4 instead of 0x4a5a89d5.
//  * SetupDialog reads the *right* and *top* edges of the HTML window's rectangle to compute the extra height
//    (0x10032fe79) - a bug that happens to work because the advisor pages are laid out with top 0. The port keeps it.
//  * SetupDialog adds 0x14 to the document height and clamps the dialog height so it stays <= 0x1c2 pixels
//    (0x10032fe52 / 0x10032fea0); a taller dialog shrinks the extra height, it is not scrolled.
//  * Both font-name strings are constructed with the literal "Arta" before the face's own name is read into them
//    (0x10032fb16 / 0x10032fb51) - leftover of the original source, kept; when the font is missing the fallback sizes
//    are 4 (title) and 3 (message, not bold).
//  * A missing font falls back to size 4 / 3 and the "Arta" name; the size is (GetSize()*4 + 8) / 18, an integer
//    division of the pixel size into the HTML "SIZE=" scale (0x10032fb9b).
//  * The link colour and the text colour both depend on the mood (red tones / blue tones) and the page background is
//    the HTML window's fill colour (0x10032fc3c..0x10032fc92).
//  * The advice is stored in the dialog's window parameter 0xea9f13c0 as a cRZVariant that is *not* released after
//    SetParam (SetParam takes its own reference; the initial reference count is 0), so the parameter owns it.
//  * DoWinProcMessage closes the dialog when the stored advice is no longer "current" (flag 0x2) or when it carries
//    the no-link flag (0x8), and *also* closes it after following a link; the return value is the negation of the
//    link handler's answer when a link was followed (0x1003302f1), true for the window-proc install / remove
//    messages (0xa2bf8ad5 / 0xa2bf8ad6) and for a command from sender 0x287259f6, false otherwise.
//  * EndDialog disables the image window procedure and drops the tracker's reference *before* it removes the window
//    parameter, ends the dialog (window manager code 0xf) and destroys the window (0x10033034e). The original
//    dereferences the image window procedure unconditionally; the port adds the null guard.
//  * Init posts the show tick even though the queue is empty (0x10032f3ed); the tick returns immediately then.
//  * Shutdown only unregisters the notifications; it leaves the queue and the image window procedure alone (the
//    destructor releases the procedure and clears the queue).
//
// Everything the original reaches through SL::sp* / RZ:: / GZCOM() goes through IAdvisorPopupTrackerHost; the advice,
// message and window types are the recovered ones (sc4/Advice.h, gz/cIGZMessage2.h, gz/WinInterfaces.h,
// gz/WinHTML.h, gz/WinGen.h). A missing host is treated as absent - the original dereferences spMS2 / RZ::WM()
// unconditionally.
#pragma once
#include <cstdint>
#include <list>

#include "gz/WinInterfaces.h"
#include "gz/WinProc.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZAutoRefCount.h"
#include "sc4/Advice.h"

class cSLWinLocationSaver;
namespace gz {
class cIGZWinGen;
class cIGZWinHTML;
}  // namespace gz

namespace sc4 {

constexpr std::uint32_t kGZIID_cSC4AdvisorPopupTrackerWinProc = 0x2a5a754d;   // 0x10032f4d1 (not 0x22e85d8e)
constexpr std::uint32_t kAdvisorPopupIIDMessageTarget2 = 0x452294aa;         // the +0x00 view

// The three notifications / messages Init subscribes to and DoMessage routes.
constexpr std::uint32_t kAdvisorPopupMessageAdviceActivated = 0x8a09fa64;   // data: unknown = the advice
constexpr std::uint32_t kAdvisorPopupMessageShow = 0xea83607b;             // the tick the tracker posts to itself
constexpr std::uint32_t kAdvisorPopupMessageClear = 0x26d31ec2;            // drop the queue

// The dialog the tick builds (SC4CreateWindowFromScript) and the children SetupDialog works on.
constexpr std::uint32_t kAdvisorPopupWindowId = 0x4a9db60c;
constexpr std::uint32_t kAdvisorPopupScriptGroup = 0x96a006b0;
constexpr std::uint32_t kAdvisorPopupScriptInstance = 0x4a5a89d5;          // one less for a bad mood
constexpr std::uint32_t kAdvisorPopupParamAdvice = 0xea9f13c0;            // the window parameter holding the advice
constexpr std::uint32_t kAdvisorPopupHtmlId = 2;                          // child id of the cIGZWinHTML page
constexpr std::uint32_t kAdvisorPopupImageId = 4;                         // child id of the advisor image window
constexpr std::uint32_t kAdvisorPopupLayoutId = 5;                        // child moved down with the dialog
constexpr const char* kAdvisorPopupBaseUrl = "sc4://";                    // the page's base URL
constexpr std::uint32_t kAdvisorPopupTitleFontId = 0x4a809914;            // RZ::WCM() system font ids
constexpr std::uint32_t kAdvisorPopupMessageFontId = 0x4a809915;
constexpr const char* kAdvisorPopupDefaultFace = "Arta";                  // the face the advisor pages are written for
constexpr std::uint32_t kAdvisorPopupFontSizeDivisor = 0x12;              // pixel size -> HTML "SIZE=" (18)
constexpr std::uint32_t kAdvisorPopupFontSizeBias = 8;                    // (size*4 + 8) / 18
constexpr std::uint32_t kAdvisorPopupDefaultTitleSize = 4;                // when the title font is missing
constexpr std::uint32_t kAdvisorPopupDefaultMessageSize = 3;              // when the message font is missing
constexpr int kAdvisorPopupDocHeightPadding = 0x14;                       // added to the document height
constexpr int kAdvisorPopupMaxDialogHeight = 0x1c2;                       // 450 pixels
constexpr int kAdvisorPopupMoodThreshold = 3;                             // below: the red / bad-mood palette

// Advice flags beyond the ones sc4/Advice.h names (see the to-do row about them).
constexpr std::uint32_t kAdvisorPopupAdviceFlagOnScreen = 0x2000;   // read through the 0x6000 test only
constexpr std::uint32_t kAdvisorPopupAdviceFlagQueued = 0x4000;     // set while the advice waits in the queue
constexpr std::uint32_t kAdvisorPopupAdviceFlagPopupMask = 0x6000;  // already on screen / queued: do not queue again
constexpr std::uint32_t kAdvisorPopupAdviceFlagNoLinksWhenDone = 0x1000;   // set no-link (0x8) after the dialog closed

// The window-proc install / detach messages cGZWinGen sends through cIGZWinProc (see gz/WinGen.h).
constexpr std::uint32_t kAdvisorPopupWinProcAttach = 0xa2bf8ad5;
constexpr std::uint32_t kAdvisorPopupWinProcDetach = 0xa2bf8ad6;

// cSC4AdvisorUIImageWinProc (unrecovered: ctor @ 0x1002dcd0c, 0x118 bytes, iids 1 / 0x22e85d8e): the window procedure
// that draws the advisor's head inside the popup. The tracker only creates it, enables it, tells it the mood and
// releases it; the object's own vtable slots are [0] QueryInterface, [1] AddRef, [2] Release, [3] DoWinProcMessage,
// [4] DoWinMsg, [5]/[6] destructor, [7] DoMessage, [8] Enable, [9] SetOffset, [10] SetMood, [11] ForceMood,
// [12] PlaceHead, [13] LoadModels, [14] DoPaint, [15] DoAnimationTick.
// The object is also a window procedure (its +8 group is the cIGZWinProc one), so the interface derives from it and
// the tracker can hand it to cIGZWinGen::SetWinProc unchanged.
class IAdvisorUIImageWinProc : public gz::cIGZWinProc {
public:
    virtual void Enable(bool enable) = 0;   // vslot 0x40, called with 1 by SetupDialog and 0 by EndDialog
    virtual void ForceMood(int mood) = 0;   // vslot 0x58
};

// The metrics SetupDialog reads from a system font: RZ::WCM() vslot 0x28 (the font registered for a style id, borrowed)
// followed by cIGZFont vslot 0x28 (GetName), 0x38 (GetStyle(1), the bold bit) and 0x50 (GetSize, the pixel height the
// HTML "SIZE=" is derived from).
struct AdvisorPopupFont {
    cRZString name{kAdvisorPopupDefaultFace};   // the face name (the original pre-loads the "Arta" default)
    bool bold = false;
    std::uint32_t size = 0;                     // pixels
};

// What cSC4AdvisorPopupTracker needs from the running game (one method per original global / engine call).
class IAdvisorPopupTrackerHost {
public:
    virtual ~IAdvisorPopupTrackerHost() = default;
    // SL::spMS2 (borrowed): AddNotification (vslot 0x28), RemoveNotification (0x30) and
    // GeneralMessagePostToTarget (0x38). May be null.
    virtual cIGZMessageServer2* Messages() = 0;
    // SC4GetPopupModalDialogsSafe @ 0x100038d8e: the game may open a modal dialog right now.
    virtual bool PopupModalDialogsSafe() = 0;
    // SC4GetPopupModalDialogsEnabled @ 0x100038e6b (SL::spSC4 vslot 0xb8): the player allows modal dialogs at all.
    virtual bool PopupModalDialogsEnabled() = 0;
    // SL::spPreferences byte +0xf0b: the advisor popups are enabled in the preferences (assumed meaning).
    virtual bool AdvisorPopupsPreferenceEnabled() = 0;
    // SL::spCity vslot 0x288 -> cISC4AdvisorSystem vslot 0xa8 (GetAdvicePopupPriorityThreshold). False when there is
    // no city or the city has no advisor system (the original returns early then).
    virtual bool AdvicePopupPriorityThreshold(int& threshold) = 0;
    // SL::spCity vslot 0x290 -> cISC4TutorialSystem vslot 0x40 (IsTutorial): any tutorial suppresses the popups.
    virtual bool TutorialRunning() = 0;
    // SL::spVehicleManager vslot 0x68 (false when the manager is absent): a paused vehicle manager suppresses them too.
    virtual bool VehicleManagerPaused() = 0;
    // RZGetFramework() vslot 0x28(0x46956659, 0x8695664e) -> cISC4Simulator vslot 0x78 (IsPaused). False when the
    // service is absent (headless), which is also the original's behaviour (no simulator = not paused).
    virtual bool SimulatorPaused() = 0;
    // SC4CreateWindowFromScript(key, parent, id, iid, out) @ 0x100039bf7 with a null parent.
    virtual bool CreateWindowFromScript(const cGZPersistResourceKey& key, std::uint32_t windowId,
                                        std::uint32_t iid, void** out) = 0;
    // cSC4AutoGamePause(false, false, true, true) around the modal dialog (SC4GamePause @ 0x10003903e).
    virtual void BeginGamePause() = 0;
    virtual void EndGamePause() = 0;
    // SL::spWinLocationSaver (borrowed): RestoreWindowLocation (vslot 0x48) / SaveWindowLocation (vslot 0x28).
    virtual cSLWinLocationSaver* WindowLocationSaver() = 0;
    // RZ::WM() (borrowed): RunPopup (vslot 0x148), GZSetFocus (0x128), EndDialog (0x48), DestroyWindow (0xb8).
    virtual cIGZWinMgr* WindowManager() = 0;
    // RZ::WM() vslot 0x128(SL::spView3DWin): the keyboard focus goes back to the 3D view after the dialog.
    virtual void FocusView3D() = 0;
    // operator new(0x118) + cSC4AdvisorUIImageWinProc::cSC4AdvisorUIImageWinProc(advisorId): a new object with a
    // reference count of 0 (the tracker AddRefs it when it stores it).
    virtual IAdvisorUIImageWinProc* CreateImageWinProc(int advisorId) = 0;
    // RZ::WCM() vslot 0x28 -> cIGZFont vslot 0x28 / 0x38 / 0x50: the name, bold bit and pixel size of the system font
    // registered for `fontId`. False when the window control manager has no such font (the original's null check).
    virtual bool SystemFont(std::uint32_t fontId, AdvisorPopupFont& out) = 0;
    // cSC4HTMLLinkHandler::DoURL @ 0x1001f22b6 (static; the original also passes an unused cIGZString scratch):
    // true when the link was followed.
    virtual bool DoURL(cIGZString* link) = 0;
};
IAdvisorPopupTrackerHost* AdvisorPopupTrackerHost();
void SetAdvisorPopupTrackerHost(IAdvisorPopupTrackerHost* host);

class cSC4AdvisorPopupTracker : public cIGZMessageTarget2, public gz::cIGZWinProc {
public:
    cSC4AdvisorPopupTracker();                          // @ 0x10032f2ac
    virtual ~cSC4AdvisorPopupTracker();                 // @ 0x10032f2f4 (wrapper 0x10032f354, deleting 0x10032f35e)

    // cIGZUnknown of the primary view (the id list and the pointer quirk are in the header comment).
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10032f4c0 (thunk 0x10032f4ea)
    std::uint32_t AddRef() override;                               // @ 0x10032f4f8 (thunk 0x10032f506)
    std::uint32_t Release() override;                              // @ 0x10032f514 (thunk 0x10032f530)

    // cIGZMessageTarget2
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x10032f550

    // The class's own interface: the original's primary slots 6 and 7 (AddGUI calls them through the vtable).
    virtual bool Init();                                           // @ 0x10032f38c
    virtual bool Shutdown();                                       // @ 0x10032f460

    // cIGZWinProc (the original's slot 8 in the primary group and slot 3 of the +8 group)
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;   // @ 0x1003301f2 (thunk 0x1003303ca)

    // helpers (the original's file-static / non-virtual members)
    void SetupDialog(cIGZWin* dialog, cISC4Advice* advice);        // @ 0x10032fa3c
    void EndDialog(cIGZWin* window);                               // @ 0x10033034e

    // test / glue accessors
    std::list<cRZAutoRefCount<cISC4Advice>>& Pending() { return pending_; }        // +0x18
    std::uint32_t RefCount() const { return refCount_; }                           // +0x10
    bool Initialized() const { return initialized_; }                              // +0x14
    IAdvisorUIImageWinProc* ImageWinProc() const { return imageWinProc_; }         // +0x30

private:
    // The queue is only touched by DoMessage / the destructor; the layout (and therefore the offsets) is the
    // original's: the two vptrs, the count, the flag, the list and the image window procedure.
    std::uint32_t refCount_ = 0;                                 // +0x10
    bool initialized_ = false;                                   // +0x14
    std::list<cRZAutoRefCount<cISC4Advice>> pending_;            // +0x18
    IAdvisorUIImageWinProc* imageWinProc_ = nullptr;             // +0x30 (one reference while set)

    friend struct AdvisorPopupTrackerTestAccess;                 // the layout assertions of the unit test
};

}  // namespace sc4
