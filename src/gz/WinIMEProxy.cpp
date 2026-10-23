#include "gz/WinIMEProxy.h"

#include "gz/FontSys.h"
#include "gz/GZCOMDirector.h"
#include "gz/WinCtrlMgr.h"
#include "gz/WinFont.h"
#include "gz/WinMgrBase.h"
#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"

namespace gz {

namespace {

IIMEProxyHost* g_host = nullptr;

// cRZSysServPtr<...>::operator=(T*): AddRefs the new service before releasing the old one. The binary writes this
// sequence inline at every +0x48 / +0x60 / +0x68 store (0x1001f7cd9..0x1001f7d67).
template <class T>
void AssignService(T*& slot, T* value) {
    if (slot == value) return;
    if (value) value->AddRef();
    T* old = slot;
    slot = value;
    if (old) old->Release();
}

}  // namespace

IIMEProxyHost* IMEProxyHost() { return g_host; }
void SetIMEProxyHost(IIMEProxyHost* host) { g_host = host; }

cGZWinIMEProxy::cGZWinIMEProxy()   // @ 0x1001f7822
    : cRZSystemService(kGZSID_cGZWinIMEProxy, kGZWinIMEProxyPriority) {
    refCount_ = 0;
    serviceIdCopy_ = kGZSID_cGZWinIMEProxy;   // +0x38: the original stores the service id a second time; nothing reads it
    initialized_ = false;
    tickCounter_ = 0;
    messageServer_ = nullptr;
    compositionPosX_ = kGZIMEProxyNoPosition;
    compositionPosY_ = kGZIMEProxyNoPosition;
    focusProbeSuppressed_ = false;
    imeServer_ = nullptr;
    winMgr_ = nullptr;
    previousFocus_ = nullptr;
    compositionWinEnabled_ = true;
    compositionWin_ = nullptr;
    compositionFont_ = nullptr;
    candidateListWinEnabled_ = true;
    candidateListWin_ = nullptr;
    candidateListFont_ = nullptr;
}

cGZWinIMEProxy::~cGZWinIMEProxy() {   // @ 0x1001f7912
    // The binary releases the members in this order and never nulls them (the object is going away).
    if (candidateListFont_) candidateListFont_->Release();   // +0xa0
    if (candidateListWin_) candidateListWin_->Release();     // +0x98
    if (compositionFont_) compositionFont_->Release();       // +0x88
    if (compositionWin_) compositionWin_->Release();         // +0x80
    if (winMgr_) winMgr_->Release();                         // +0x68
    if (imeServer_) imeServer_->Release();                   // +0x60
    if (messageServer_) messageServer_->Release();           // +0x48
}

bool cGZWinIMEProxy::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1001f7af8
    if (iid == kGZIID_cGZWinAlt) {                 // 0xe98b2f57: the cIGZMessageTarget view at +8
        *out = static_cast<cIGZMessageTarget*>(this);
    } else if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZWinIMEProxy) {
        *out = static_cast<cIGZWinIMEProxy*>(this);
    } else {
        return cRZSystemService::QueryInterface(iid, out);   // 0x287fb697 and whatever the base refuses
    }
    AddRef();
    return true;
}

std::uint32_t cGZWinIMEProxy::AddRef() {   // @ 0x1001f7b5c
    return ++refCount_;
}

std::uint32_t cGZWinIMEProxy::Release() {   // @ 0x1001f7b94
    // Quirk: a count below 2 shuts the service down and deletes the object (so a fresh, count-0 proxy dies on the
    // first Release); the binary calls Shutdown (vslot 0x20) then the deleting destructor (vslot 0x80).
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cGZWinIMEProxy::Init() {   // @ 0x1001f7c86
    if (initialized_) return true;
    initialized_ = true;

    {
        cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
        AssignService(messageServer_, server.get());
    }
    {
        cRZSysServPtr<cIGZWinIMEServer, kGZSID_cGZWinIMEServer, kGZIID_cIGZWinIMEServer> server;
        cIGZWinIMEServer* value = server.get();
        if (value) {
            AssignService(imeServer_, value);
            imeServer_->SetEnabled(true);   // vslot 0x38 (the binary re-reads +0x60, so the old service also gets this)
        }
    }
    {
        cRZSysServPtr<cIGZWinMgr, kGZSID_cGZWinMgrBase, kGZIID_cIGZWinMgr> mgr;
        cIGZWinMgr* value = mgr.get();
        // Unlike the two services above, a missing manager does not clear the cached one.
        if (value && winMgr_ != value) AssignService(winMgr_, value);
    }
    {
        cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
        if (server) {
            cIGZMessageTarget* target = static_cast<cIGZMessageTarget*>(this);
            server->AddNotification(target, kGZMessage_IMEStartComposition);   // vslot 0x28
            server->AddNotification(target, kGZMessage_IMEEndComposition);
            server->AddNotification(target, kGZMessage_IMEStartCandidateList);
            server->AddNotification(target, kGZMessage_IMEEndCandidateList);
        }
    }
    if (imeServer_ && imeServer_->IsIMEAvailable() /* vslot 0x28 */) {
        if (cIGZFrameWork* framework = RZGetFramework())
            framework->AddToTick(static_cast<cIGZSystemService*>(this));   // vslot 0x48
    }
    return true;
}

bool cGZWinIMEProxy::Shutdown() {   // @ 0x1001f7ef8
    if (!initialized_) return true;
    initialized_ = false;

    if (cIGZFrameWork* framework = RZGetFramework())
        framework->RemoveFromTick(static_cast<cIGZSystemService*>(this));   // vslot 0x50

    if (messageServer_) { cIGZMessageServer* old = messageServer_; messageServer_ = nullptr; old->Release(); }   // +0x48
    if (imeServer_) { cIGZWinIMEServer* old = imeServer_; imeServer_ = nullptr; old->Release(); }                 // +0x60
    if (winMgr_) { cIGZWinMgr* old = winMgr_; winMgr_ = nullptr; old->Release(); }                               // +0x68
    previousFocus_ = nullptr;                                                                                    // +0x70
    if (compositionFont_) { cIGZFont* old = compositionFont_; compositionFont_ = nullptr; old->Release(); }      // +0x88
    if (candidateListFont_) { cIGZFont* old = candidateListFont_; candidateListFont_ = nullptr; old->Release(); }   // +0xa0
    // Quirk: the two floating windows (+0x80 / +0x98) are NOT released here; only the destructor does that.

    {
        cRZSysServPtr<cIGZWinIMEServer, kGZSID_cGZWinIMEServer, kGZIID_cIGZWinIMEServer> server;
        if (server) server->SetEnabled(false);   // vslot 0x38 (a fresh lookup, not the cached server)
    }
    {
        cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
        if (server) {
            cIGZMessageTarget* target = static_cast<cIGZMessageTarget*>(this);
            server->RemoveNotification(target, kGZMessage_IMEStartComposition);   // vslot 0x30
            server->RemoveNotification(target, kGZMessage_IMEEndComposition);
            server->RemoveNotification(target, kGZMessage_IMEStartCandidateList);
            server->RemoveNotification(target, kGZMessage_IMEEndCandidateList);
        }
    }
    return true;
}

void cGZWinIMEProxy::SetupDefaultFonts() {   // @ 0x1001f80c6
    if (compositionFont_ && candidateListFont_) return;
    cRZSysServPtr<cIGZFontSys, kGZFontSysDefaultServiceID, kGZIID_cIGZFontSys> fontSys;
    cRZSysServPtr<cIGZWinCtrlMgr, kGZSID_cGZWinCtrlMgr, kGZIID_cIGZWinCtrlMgr> ctrlMgr;
    if (!fontSys || !ctrlMgr) return;
    IWinFont* styleFont = ctrlMgr->SystemFont(kWinDefaultFontStyle);   // vslot 0x28
    cRZString name;
    if (styleFont) {
        if (cIGZFont* font = styleFont->AsFont()) font->GetName(name);   // cIGZFont vslot 0x28
    }
    // Two separate acquires: the text size is 0x12, the style 0 and the reuse flag set (0x1001f81bc / 0x1001f81eb).
    if (!compositionFont_) compositionFont_ = fontSys->FontAcquire(name, 0, 0x12, 0, true);   // vslot 0x48
    if (!candidateListFont_) candidateListFont_ = fontSys->FontAcquire(name, 0, 0x12, 0, true);
}

bool cGZWinIMEProxy::DoMessage(cGZMessage& message) {   // @ 0x1001f825e
    if (message.type == kGZMessage_IMEStartComposition) {
        StartComposition();
    } else if (message.type == kGZMessage_IMEEndComposition) {
        EndComposition();
    } else if (message.type == kGZMessage_IMEStartCandidateList) {
        StartCandidateList();
    } else if (message.type == kGZMessage_IMEEndCandidateList) {
        EndCandidateList();
    }
    return true;   // the binary always answers true and never forwards to a base class
}

bool cGZWinIMEProxy::OnTick(std::uint32_t elapsedMs) {   // @ 0x1001f8832
    (void)elapsedMs;
    ++tickCounter_;
    if ((tickCounter_ & 3) != 0) return true;
    if (!winMgr_ || !imeServer_) return true;
    cIGZWin* focus = winMgr_->GZGetFocus();   // vslot 0x120
    if (focus == previousFocus_) {
        if ((tickCounter_ & 7) != 0) return true;   // the same focus is only re-examined every eighth tick
    }
    previousFocus_ = focus;
    compositionPosX_ = kGZIMEProxyNoPosition;
    compositionPosY_ = kGZIMEProxyNoPosition;
    if (focusProbeSuppressed_) return true;

    cIGZWinTextEdit* textEdit = nullptr;
    cIGZWinIMEComposition* composition = nullptr;
    cIGZWinIMECandidateList* candidates = nullptr;
    bool probeAnswered = false;
    if (focus) {
        probeAnswered = focus->QueryInterface(kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&textEdit));
        if (!probeAnswered) {
            probeAnswered = focus->QueryInterface(kGZIID_cIGZWinIMEComposition, reinterpret_cast<void**>(&composition));
            if (!probeAnswered)
                probeAnswered = focus->QueryInterface(kGZIID_cIGZWinIMECandidateList, reinterpret_cast<void**>(&candidates));
        }
    }
    // Released in the binary's order: candidate list, composition, text edit.
    if (candidates) candidates->Release();
    if (composition) composition->Release();
    if (textEdit) textEdit->Release();
    if (probeAnswered) return true;

    // The composition position request: p1 = 0, p2 = p3 = 2 (0x1001f8921..0x1001f893d). A listener that answers
    // (p1 != 0) supplies the position in p2 / p3, which the next composition window is moved to.
    cGZMessage message{};
    message.type = kGZMsgIMEProxyPositionProbe;
    message.param1 = 0;
    message.param2 = 2;
    message.param3 = 2;
    if (messageServer_) messageServer_->MessageSend(message);   // vslot 0x18; the binary dereferences without a check
    if (message.param1 == 0) {
        if (imeServer_->GetCompositionEnabled() /* vslot 0x40 */) imeServer_->SetCompositionEnabled(false);   // 0x48
    } else {
        compositionPosX_ = static_cast<std::uint32_t>(message.param2);
        compositionPosY_ = static_cast<std::uint32_t>(message.param3);
    }
    return true;
}

void cGZWinIMEProxy::StartComposition() {   // @ 0x1001f82a6
    if (!compositionWinEnabled_ || compositionWin_) return;
    cRZSysServPtr<cIGZWinMgr, kGZSID_cGZWinMgrBase, kGZIID_cIGZWinMgr> winMgr;
    cIGZWin* mainWindow = winMgr ? winMgr->GetMainWindow() : nullptr;   // vslot 0x18
    // The binary releases a window left over from a previous run before creating the new one (0x1001f831e); the
    // guard above makes that impossible here, but the sequence is kept.
    if (compositionWin_) { cIGZWinIMEComposition* old = compositionWin_; compositionWin_ = nullptr; old->Release(); }
    bool created = false;
    if (cIGZCOM* com = GZCOM())
        created = com->CreateInstance(kGZCLSID_cGZWinIMEComposition, kGZIID_cIGZWinIMEComposition,
                                      reinterpret_cast<void**>(&compositionWin_));   // vslot 0x18
    if (!created) {
        // 0x1001f834e: operator new(0x438) + cGZWinIMEComposition::cGZWinIMEComposition() + AddRef into +0x80.
        auto* window = new cGZWinIMEComposition(winMgr.get());
        window->AddRef();
        compositionWin_ = window;
    }
    if (!compositionWin_) return;
    SetupDefaultFonts();
    compositionWin_->SetFont(compositionFont_);        // vslot 0x38
    cIGZWin* window = compositionWin_->AsIGZWin();     // vslot 0x18
    if (!window) return;
    if (mainWindow) mainWindow->ChildAdd(window);      // vslot 0x70 (the binary dereferences the manager unchecked)
    window->PullToFront();                             // vslot 0xb8
    if (compositionPosX_ != kGZIMEProxyNoPosition)
        window->GZWinMoveTo(static_cast<int>(compositionPosX_), static_cast<int>(compositionPosY_));   // vslot 0x1c0
    window->SetFlag(0x800, false);                     // vslot 0x220
}

void cGZWinIMEProxy::EndComposition() {   // @ 0x1001f84e4
    if (!compositionWin_) return;
    cRZSysServPtr<cIGZWinMgr, kGZSID_cGZWinMgrBase, kGZIID_cIGZWinMgr> winMgr;
    cIGZWin* window = compositionWin_->AsIGZWin();         // vslot 0x18
    if (winMgr && window) winMgr->DestroyWindow(window);   // vslot 0xb8 (the binary dereferences the manager unchecked)
    cIGZWinIMEComposition* saved = compositionWin_;        // 0x1001f853e
    compositionWin_ = nullptr;
    saved->Release();
}

void cGZWinIMEProxy::StartCandidateList() {   // @ 0x1001f8588
    if (!candidateListWinEnabled_ || candidateListWin_) return;
    cRZSysServPtr<cIGZWinMgr, kGZSID_cGZWinMgrBase, kGZIID_cIGZWinMgr> winMgr;
    cIGZWin* mainWindow = winMgr ? winMgr->GetMainWindow() : nullptr;   // vslot 0x18
    if (candidateListWin_) { cIGZWinIMECandidateList* old = candidateListWin_; candidateListWin_ = nullptr; old->Release(); }
    bool created = false;
    if (cIGZCOM* com = GZCOM())
        created = com->CreateInstance(kGZCLSID_cGZWinIMECandidateList, kGZIID_cIGZWinIMECandidateList,
                                      reinterpret_cast<void**>(&candidateListWin_));   // vslot 0x18
    if (!created) {
        // 0x1001f8634: operator new(0x208) + cGZWinIMECandidateList::cGZWinIMECandidateList() + AddRef into +0x98.
        // That class is not recovered, so this host stands in for the construction.
        if (IIMEProxyHost* host = IMEProxyHost()) candidateListWin_ = host->CreateCandidateListWindow(winMgr.get());
    }
    if (!candidateListWin_) return;
    SetupDefaultFonts();
    candidateListWin_->SetFont(candidateListFont_);    // vslot 0x38
    cIGZWin* window = candidateListWin_->AsIGZWin();   // vslot 0x18
    if (!window) return;
    if (mainWindow) mainWindow->ChildAdd(window);      // vslot 0x70
    window->PullToFront();                             // vslot 0xb8
    window->SetFlag(0x800, false);                     // vslot 0x220 (no position, unlike the composition window)
}

void cGZWinIMEProxy::EndCandidateList() {   // @ 0x1001f877c
    if (!candidateListWin_) return;
    cRZSysServPtr<cIGZWinMgr, kGZSID_cGZWinMgrBase, kGZIID_cIGZWinMgr> winMgr;
    cIGZWin* window = candidateListWin_->AsIGZWin();       // vslot 0x18
    if (winMgr && window) winMgr->DestroyWindow(window);   // vslot 0xb8
    cIGZWinIMECandidateList* saved = candidateListWin_;    // 0x1001f8751
    candidateListWin_ = nullptr;
    saved->Release();
}

bool cGZWinIMEProxy::GetCompositionWinEnabled() const { return compositionWinEnabled_; }   // @ 0x1001f8b44

void cGZWinIMEProxy::SetCompositionWinEnabled(bool enabled) {   // @ 0x1001f8b4e
    compositionWinEnabled_ = enabled;
}

cIGZWinIMEComposition* cGZWinIMEProxy::GetCompositionWin() const { return compositionWin_; }   // @ 0x1001f8b64

cIGZFont* cGZWinIMEProxy::GetCompositionFont() const { return compositionFont_; }   // @ 0x1001f8b72

void cGZWinIMEProxy::SetCompositionFont(cIGZFont* font) {   // @ 0x1001f8b80
    cIGZFont* old = compositionFont_;
    if (old == font) return;
    if (font) font->AddRef();
    compositionFont_ = font;
    if (old) old->Release();
}

bool cGZWinIMEProxy::GetCandidateListWinEnabled() const { return candidateListWinEnabled_; }   // @ 0x1001f8bd4

void cGZWinIMEProxy::SetCandidateListWinEnabled(bool enabled) {   // @ 0x1001f8be0
    candidateListWinEnabled_ = enabled;
}

cIGZWinIMECandidateList* cGZWinIMEProxy::GetCandidateListWin() const { return candidateListWin_; }   // @ 0x1001f8bfc

cIGZFont* cGZWinIMEProxy::GetCandidateListFont() const { return candidateListFont_; }   // @ 0x1001f8c0a

void cGZWinIMEProxy::SetCandidateListFont(cIGZFont* font) {   // @ 0x1001f8c18
    cIGZFont* old = candidateListFont_;
    if (old == font) return;
    if (font) font->AddRef();
    candidateListFont_ = font;
    if (old) old->Release();
}

}  // namespace gz
