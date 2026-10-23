// cSC4AdvisorPopupTracker — see sc4/AdvisorPopupTracker.h for the object layout, the message routes, the popup rules
// and the quirks kept.
#include "sc4/AdvisorPopupTracker.h"

#include <string>

#include "gz/WinGen.h"
#include "gz/WinHTML.h"
#include "gz/WinProc.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZVariant.h"
#include "sc4/WinLocationSaver.h"

namespace sc4 {

namespace {

IAdvisorPopupTrackerHost* g_host = nullptr;

// The messages the message server hands DoMessage are the standard message (the original reads the payload slots
// 0x100 / 0x20 through the cIGZMessage2* it was given).
cIGZMessage2Standard& StandardMessage(cIGZMessage2* message) {
    return *static_cast<cIGZMessage2Standard*>(message);
}

// The page: text colour / link colour follow the mood, the background is the HTML window's fill colour.
const char* kAdvisorPopupHtmlFormat =
    "<HTML><HEAD></HEAD><BODY TEXT=\"#%02x%02x%02x\" BGCOLOR=\"#%02x%02x%02x\" "
    "LINK=\"#%02x%02x%02x\"><P><FONT FACE=\"%s\" SIZE=%d>%s%s%s</FONT></P>"
    "<P><FONT FACE=\"%s\" SIZE=%d>%s%s%s</FONT></P></BODY></HTML>";

}  // namespace

IAdvisorPopupTrackerHost* AdvisorPopupTrackerHost() { return g_host; }
void SetAdvisorPopupTrackerHost(IAdvisorPopupTrackerHost* host) { g_host = host; }

// @ 0x10032f2ac: the two vtable views, the zero reference count, the "not subscribed" byte, the empty queue and no
// image window procedure.
cSC4AdvisorPopupTracker::cSC4AdvisorPopupTracker() = default;

// @ 0x10032f2f4 (+ the wrapper 0x10032f354 and the deleting destructor 0x10032f35e): drops the image window procedure
// and the queue. The original releases the procedure before it clears the list; kept.
cSC4AdvisorPopupTracker::~cSC4AdvisorPopupTracker() {
    if (imageWinProc_) {
        IAdvisorUIImageWinProc* proc = imageWinProc_;
        imageWinProc_ = nullptr;
        proc->Release();
    }
    pending_.clear();
}

// @ 0x10032f4c0: answers iid 1, the message-target iid and 0x2a5a754d (the window-procedure iid of this class, not the
// 0x22e85d8e every other window procedure answers); every accepted id stores the primary pointer, not the +8 view.
bool cSC4AdvisorPopupTracker::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kAdvisorPopupIIDMessageTarget2 && iid != kGZIID_cSC4AdvisorPopupTrackerWinProc) return false;
    if (!out) return false;   // the original stores through the out pointer without checking it
    *out = this;
    AddRef();
    return true;
}

// @ 0x10032f4f8: the count lives at +0x10 (the +8 thunk increments the same word).
std::uint32_t cSC4AdvisorPopupTracker::AddRef() { return ++refCount_; }

// @ 0x10032f514: at one reference (or zero) the deleting destructor runs; otherwise the count drops by one.
std::uint32_t cSC4AdvisorPopupTracker::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// @ 0x10032f38c: subscribe to the three messages, then post the show tick to itself even though the queue is empty.
bool cSC4AdvisorPopupTracker::Init() {
    if (initialized_) return true;
    IAdvisorPopupTrackerHost* host = g_host;
    cIGZMessageServer2* messages = host ? host->Messages() : nullptr;   // SL::spMS2
    if (messages) {
        messages->AddNotification(this, kAdvisorPopupMessageAdviceActivated);
        messages->AddNotification(this, kAdvisorPopupMessageShow);
        messages->AddNotification(this, kAdvisorPopupMessageClear);
    }
    initialized_ = true;
    cRZMessage2Standard* tick = new cRZMessage2Standard();
    tick->AddRef();
    tick->SetType(kAdvisorPopupMessageShow);
    if (messages) messages->GeneralMessagePostToTarget(tick, this);
    tick->Release();
    return initialized_;
}

// @ 0x10032f460: unregister the three messages. The queue and the image window procedure are left alone (the
// destructor drops them).
bool cSC4AdvisorPopupTracker::Shutdown() {
    if (initialized_) {
        IAdvisorPopupTrackerHost* host = g_host;
        cIGZMessageServer2* messages = host ? host->Messages() : nullptr;
        if (messages) {
            messages->RemoveNotification(this, kAdvisorPopupMessageAdviceActivated);
            messages->RemoveNotification(this, kAdvisorPopupMessageShow);
            messages->RemoveNotification(this, kAdvisorPopupMessageClear);
        }
        initialized_ = false;
    }
    return true;
}

// @ 0x10032f550: the clear / activation / show-tick routes.
bool cSC4AdvisorPopupTracker::DoMessage(cIGZMessage2* message) {
    IAdvisorPopupTrackerHost* host = g_host;
    cIGZMessageServer2* messages = host ? host->Messages() : nullptr;   // SL::spMS2
    const std::uint32_t type = message->GetType();

    // 0x26d31ec2: drop the queue (the advices keep their 0x4000 flag; the original does not clear it either).
    if (type == kAdvisorPopupMessageClear) {
        pending_.clear();
        return true;
    }

    // 0x8a09fa64: an advice was activated. Queue it unless it is already on screen / queued, and post the show tick.
    if (type == kAdvisorPopupMessageAdviceActivated) {
        cIGZUnknown* object = StandardMessage(message).GetIGZUnknown();   // cIGZMessage2Standard slot 32 (+0x100)
        if (!object) return true;
        cISC4Advice* advice = nullptr;
        if (object->QueryInterface(kGZIID_cISC4Advice, reinterpret_cast<void**>(&advice))) {
            if (!advice->CheckFlags(kAdvisorPopupAdviceFlagPopupMask)) {
                pending_.emplace_back(advice);   // the queue holds one reference
                advice->SetFlags(kAdvisorPopupAdviceFlagQueued, true);
                cRZMessage2Standard* tick = new cRZMessage2Standard();
                tick->AddRef();
                tick->SetType(kAdvisorPopupMessageShow);
                if (messages) messages->GeneralMessagePostToTarget(tick, this);
                tick->Release();
            }
            advice->Release();   // the QueryInterface reference
        }
        return true;
    }

    if (type != kAdvisorPopupMessageShow) return true;

    // ---- 0xea83607b: the show tick ----
    if (pending_.empty()) return true;
    int threshold = 0;
    if (!host || !host->AdvicePopupPriorityThreshold(threshold)) return true;   // no city / no advisor system
    // A paused simulator or an unsafe moment leaves the advice in the queue and re-posts the tick (the original jumps
    // to the re-post before it pops the front; quirk kept - the queue keeps one tick in flight).
    if (host->SimulatorPaused() || !host->PopupModalDialogsSafe()) {
        if (messages) messages->GeneralMessagePostToTarget(message, this);
        return true;
    }

    // Pop the front advice (the reference is taken before the pop, as the original does).
    cRZAutoRefCount<cISC4Advice> advice(pending_.front().get());
    pending_.pop_front();

    bool show = host->PopupModalDialogsEnabled() && host->AdvisorPopupsPreferenceEnabled();
    if (show && advice->GetPriority() < threshold) show = false;   // below the difficulty threshold: never pops
    if (show && host->TutorialRunning()) show = false;
    if (show && host->VehicleManagerPaused()) show = false;

    if (show) {
        // A bad mood uses the previous script instance (the red-tinted page with the angry advisor).
        const bool badMood = advice->GetMood() < kAdvisorPopupMoodThreshold;
        const cGZPersistResourceKey key{0, kAdvisorPopupScriptGroup,
                                       kAdvisorPopupScriptInstance - (badMood ? 1u : 0u)};
        gz::cIGZWinGen* gen = nullptr;
        if (host->CreateWindowFromScript(key, kAdvisorPopupWindowId, gz::kGZIID_cIGZWinGen,
                                         reinterpret_cast<void**>(&gen))) {
            host->BeginGamePause();   // cSC4AutoGamePause(false, false, true, true)
            gen->SetWinProc(this);    // the +8 cIGZWinProc view
            SetupDialog(gen->AsIGZWin(), advice.get());
            cSLWinLocationSaver* saver = host->WindowLocationSaver();
            if (saver) saver->RestoreWindowLocation(gen->AsIGZWin(), true);
            if (cIGZWinMgr* manager = host->WindowManager()) manager->RunPopup(gen->AsIGZWin());
            if (saver) saver->SaveWindowLocation(gen->AsIGZWin());
            host->FocusView3D();      // RZ::WM() vslot 0x128(SL::spView3DWin)
            if (advice->CheckFlags(kAdvisorPopupAdviceFlagNoLinksWhenDone)) advice->SetFlags(kAdviceFlagNoLinks, true);
            host->EndGamePause();
        }
        if (gen) gen->Release();
    }

    advice->SetFlags(kAdvisorPopupAdviceFlagQueued, false);
    // Re-post while something is still waiting (the original posts the very message it was handed).
    if (!pending_.empty() && messages) messages->GeneralMessagePostToTarget(message, this);
    return true;
}

// @ 0x1003301f2: the dialog's window procedure. It closes the dialog when the advice it shows is no longer current or
// has the no-link flag, follows the HTML view's links, and treats a command from sender 0x287259f6 as "close".
bool cSC4AdvisorPopupTracker::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    IAdvisorPopupTrackerHost* host = g_host;
    cIGZVariant* variant = nullptr;
    bool closed = false;
    // The advice the dialog was built for is kept in the window parameter 0xea9f13c0 (the show tick stores it).
    if (window->GetParam(kAdvisorPopupParamAdvice, &variant)) {
        cISC4Advice* advice = nullptr;
        if (variant->GetValUnknown(kGZIID_cISC4Advice, reinterpret_cast<void**>(&advice))) {
            // Not current any more, or the advice asked for no link handling: the dialog closes on this message.
            if (!advice->CheckFlags(kAdviceFlagCurrent) || advice->CheckFlags(kAdviceFlagNoLinks)) {
                EndDialog(window);
                closed = true;
            }
            advice->Release();   // the variant handed out a counted reference
        }
    }
    // The original releases the parameter reference at the very end, whatever path was taken.
    bool result = false;
    if (closed) {
        result = true;
    } else if (message.type == kAdvisorPopupWinProcAttach || message.type == kAdvisorPopupWinProcDetach) {
        // The window procedure was just installed or is being removed: nothing to do, but the message is handled.
        result = true;
    } else if (message.type == kWinHtmlMsgLinkJump) {
        // param2 is the cRZString the HTML view released to us; the original also passes an unused cIGZString.
        const bool followed = host && host->DoURL(reinterpret_cast<cIGZString*>(message.param2));
        EndDialog(window);
        result = !followed;   // a link that was not followed counts as unhandled
    } else if (message.type == kGZMsgCommand && message.param1 == gz::kWinCommandSender) {
        EndDialog(window);
        result = true;
    }
    if (variant) variant->Release();
    return result;
}

// @ 0x10033034e: disable the image window procedure, drop the tracker's reference, remove the advice parameter, then
// end the dialog (window manager code 0xf) and destroy the window.
void cSC4AdvisorPopupTracker::EndDialog(cIGZWin* window) {
    // The original dereferences the procedure without a null check; the port keeps the guard (see the header).
    if (imageWinProc_) {
        imageWinProc_->Enable(false);
        IAdvisorUIImageWinProc* proc = imageWinProc_;
        imageWinProc_ = nullptr;
        proc->Release();
    }
    window->SetParam(kAdvisorPopupParamAdvice, nullptr);
    if (IAdvisorPopupTrackerHost* host = g_host) {
        if (cIGZWinMgr* manager = host->WindowManager()) {
            manager->EndDialog(0xf, window, 0);
            manager->DestroyWindow(window);
        }
    }
}

// @ 0x10032fa3c: build the popup's page (HTML text from the advice's title and message, the two system fonts by
// style id, the mood palette), size the dialog to the document, store the advice in the window parameter and create
// the advisor image window procedure on child 4.
void cSC4AdvisorPopupTracker::SetupDialog(cIGZWin* dialog, cISC4Advice* advice) {
    if (!dialog || !advice) return;
    IAdvisorPopupTrackerHost* host = g_host;
    cIGZWinHTML* html = nullptr;
    // Child 2 of the script window is the HTML page (iid 0x4a11fd4a).
    if (dialog->GetChildAsRecursive(kAdvisorPopupHtmlId, kGZIID_cIGZWinHTML, reinterpret_cast<void**>(&html))) {
        // The two faces come from the window control manager's system font table. A missing face leaves the
        // "Arta" default name and the fallback HTML size (4 for the title, 3 for the message, not bold).
        AdvisorPopupFont titleFont, messageFont;
        const bool hasTitleFace = host && host->SystemFont(kAdvisorPopupTitleFontId, titleFont);
        const bool hasMessageFace = host && host->SystemFont(kAdvisorPopupMessageFontId, messageFont);
        const cRZString& titleName = titleFont.name;
        const cRZString& messageName = messageFont.name;
        const bool titleBold = hasTitleFace && titleFont.bold;
        const bool messageBold = hasMessageFace && messageFont.bold;
        // The HTML "SIZE=" the original derives from the pixel size: (size*4 + 8) / 18 (0x10032fb9b).
        const std::uint32_t titleSize = hasTitleFace
            ? (titleFont.size * 4 + kAdvisorPopupFontSizeBias) / kAdvisorPopupFontSizeDivisor
            : kAdvisorPopupDefaultTitleSize;
        const std::uint32_t messageSize = hasMessageFace
            ? (messageFont.size * 4 + kAdvisorPopupFontSizeBias) / kAdvisorPopupFontSizeDivisor
            : kAdvisorPopupDefaultMessageSize;
        // The page's colours: the background is the HTML window's fill colour, the text and link colours follow the
        // advice's mood (red for a bad mood, blue otherwise).
        cIGZWin* htmlWindow = html->AsIGZWin();
        std::uint8_t fillR = 0, fillG = 0, fillB = 0;
        htmlWindow->GetFillColor(fillR, fillG, fillB);
        html->ClearFlag(kHtmlOutline);   // the original clears the outline flag (0x10032fc3c)
        const bool badMood = advice->GetMood() < kAdvisorPopupMoodThreshold;
        const std::uint32_t textR = badMood ? 0x72 : 0x39;
        const std::uint32_t textG = badMood ? 0x39 : 0x44;
        const std::uint32_t textB = badMood ? 0x39 : 0x72;
        const std::uint32_t linkR = badMood ? 0xff : 0;
        const std::uint32_t linkB = badMood ? 0 : 0xff;
        cRZString page;
        page.Sprintf(kAdvisorPopupHtmlFormat, textR, textG, textB, static_cast<std::uint32_t>(fillR),
                     static_cast<std::uint32_t>(fillG), static_cast<std::uint32_t>(fillB), linkR, 0u, linkB,
                     titleName.ToChar(), static_cast<int>(titleSize), titleBold ? "<B>" : "",
                     advice->GetTextTitle().ToChar(), titleBold ? "</B>" : "", messageName.ToChar(),
                     static_cast<int>(messageSize), messageBold ? "<B>" : "", advice->GetTextMessage().ToChar(),
                     messageBold ? "</B>" : "");
        html->SetDocument(std::string(page.ToChar(), page.Strlen()), cRZString(kAdvisorPopupBaseUrl));

        // Grow the dialog by the document height (plus the padding) and clamp it to the maximum dialog height. The
        // original measures the page with the rectangle's right and top edges; kept (see the header).
        gz::cRZSize documentSize;
        html->GetDocumentSize(documentSize);
        documentSize.height += kAdvisorPopupDocHeightPadding;
        const cRZRect& htmlArea = htmlWindow->GetArea();
        const int htmlHeight = htmlArea.right - htmlArea.top;
        int extra = documentSize.height - htmlHeight;
        if (extra > 0) {
            const int left = htmlArea.left;
            const int bottom = htmlArea.bottom;
            if (dialog->GetH() + extra > kAdvisorPopupMaxDialogHeight)
                extra = kAdvisorPopupMaxDialogHeight - dialog->GetH();
            dialog->SetSize(dialog->GetW(), dialog->GetH() + extra);
            htmlWindow->SetSize(bottom - left, htmlHeight + extra);
            if (cIGZWin* layout = dialog->GetChildWindowFromIDRecursive(kAdvisorPopupLayoutId))
                layout->GZWinMoveTo(layout->GetL(), layout->GetT() + extra);
        }
    }
    // Centre the dialog in its parent, then keep the advice in the window parameter (the variant is created with a
    // reference count of 0 and SetParam takes its own reference, so it is not released here).
    if (cIGZWin* parent = dialog->GetParentWin()) {
        dialog->GZWinMoveTo((parent->GetW() - dialog->GetW()) / 2, (parent->GetH() - dialog->GetH()) / 2);
    }
    dialog->SetParam(kAdvisorPopupParamAdvice, new cRZVariant(advice));
    // Child 4 is the image window: the advisor's head. The tracker owns its window procedure while the dialog lives.
    gz::cIGZWinGen* imageWindow = nullptr;
    if (dialog->GetChildAsRecursive(kAdvisorPopupImageId, gz::kGZIID_cIGZWinGen,
                                    reinterpret_cast<void**>(&imageWindow))) {
        IAdvisorUIImageWinProc* proc = host ? host->CreateImageWinProc(advice->GetAdvisorID()) : nullptr;
        if (proc) {
            if (proc != imageWinProc_) {
                proc->AddRef();
                IAdvisorUIImageWinProc* old = imageWinProc_;
                imageWinProc_ = proc;
                if (old) old->Release();
                proc = imageWinProc_;
            }
            proc->Enable(true);
            proc->ForceMood(advice->GetMood());
            imageWindow->SetWinProc(proc);
        }
        imageWindow->Release();
    }
    if (html) html->Release();
}

}  // namespace sc4
