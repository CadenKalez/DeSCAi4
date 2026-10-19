#include "sc4/GamePreferencesWinProc.h"

#include "gz/WinBtn.h"
#include "sc4/DialogWinProcs.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kDialogInstance = 0x0a7df315, kDialogWindowId = 0x2a57db82;
constexpr std::uint32_t kPageA = 0x2a57dc83, kPageB = 0x2a57db84;
constexpr std::uint32_t kTabA = 0xea57db7e, kTabB = 0xea5e99d9, kClose = 0xea57db7f;
constexpr std::uint32_t kOk = 0xea57da59, kCancel = 0x6a57da48, kCredits = 0x0a57da65;
constexpr std::uint32_t kSenderButton = 0x287259f6, kEndDialogCode = 0xf;
constexpr std::uint32_t kMessagePreferencesChanged = 0x231bbf91, kMessageAppClose = 0xac3b3fa6;
constexpr std::uint32_t kWinFlagKeepFocus = 0x1000, kWinFlagVisible = 1, kWinFlagEnabled = 2;

struct CheckBox { std::uint32_t id; std::size_t offset; };
constexpr CheckBox kCheckBoxes[5] = {{0x4a7f19f2, 0xf08}, {0x4a7f19f3, 0xf09}, {0x4a7f19f4, 0x4ec}, {0x4a7f19f5, 0xf0b}, {0x4a7f19f6, 0xf0a}};

bool g_staticInitialized = false;     // mbStaticInitialized
bool g_displayed = false;             // mbDisplayed
GamePreferencesServices g_services;

gz::cIGZWinBtn* FindCheckBox(cIGZWin* window, std::uint32_t id) {
    gz::cIGZWinBtn* button = nullptr;
    if (!window->GetChildAsRecursive(id, gz::kGZIID_cIGZWinBtn, reinterpret_cast<void**>(&button)) || !button) return nullptr;
    button->Release();                  // a borrowed pointer, as in the original's reuse of one slot
    return button;
}
}  // namespace

GamePreferencesServices& GetGamePreferencesServices() { return g_services; }
bool cSC4GamePreferencesWinProc::Displayed() { return g_displayed; }

bool cSC4GamePreferencesWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != 0x22e85d8e) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4GamePreferencesWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    Shutdown();
    if (dialog_) dialog_->Release();
    delete this;
    return 0;
}

bool cSC4GamePreferencesWinProc::Shutdown() {
    preferences_ = nullptr;
    return true;
}

// Modal: the simulator and view are held while the dialog runs; focus returns to whatever had it.
bool cSC4GamePreferencesWinProc::DoModalWin() {
    GamePreferencesServices& s = g_services;
    if (s.gamePause) s.gamePause(true);
    cIGZWinMgr* manager = s.windowManager ? s.windowManager() : nullptr;
    cIGZWin* previousFocus = manager->GZGetFocus();
    cIGZWin* window = dialog_->AsIGZWin();
    if (window) window->AddRef();
    cSLWinLocationSaver* saver = s.locationSaver ? s.locationSaver() : nullptr;
    if (saver) saver->RestoreWindowLocation(window, true);
    manager->GZSetFocus(window);
    manager->RunPopup(window);
    if (saver) saver->SaveWindowLocation(window);
    if (manager->IsWindowValid(previousFocus)) manager->GZSetFocus(previousFocus);
    EndDialog();
    if (window) window->Release();
    if (s.gamePause) s.gamePause(false);
    return true;
}

void cSC4GamePreferencesWinProc::EndDialog() {
    if (!g_displayed) return;
    AddRef();
    if (dialog_) {
        cIGZWin* window = dialog_->AsIGZWin();
        cIGZWinMgr* manager = window->GetWindowManager();
        if (manager) {
            if (window->GetFlag(kWinFlagKeepFocus)) manager->EndDialog(kEndDialogCode, window, 1);
            manager->DestroyWindow(window);
        }
        gz::cIGZWinGen* dialog = dialog_;
        dialog_ = nullptr;
        dialog->Release();
    }
    g_displayed = false;
    Release();
}

bool cSC4GamePreferencesWinProc::ShowDialog(eGameWindowType type) {
    if (!g_staticInitialized) {
        g_staticInitialized = true;
        g_displayed = false;
    } else if (g_displayed) {
        return true;
    }
    if (type != kGameOptions) return false;
    if (dialog_) {
        gz::cIGZWinGen* old = dialog_;
        dialog_ = nullptr;
        old->Release();
    }
    const cGZPersistResourceKey key{0, kUIScriptGroup, kDialogInstance};
    if (!SC4CreateWindowFromScript(key, nullptr, kDialogWindowId, gz::kGZIID_cIGZWinGen, reinterpret_cast<void**>(&dialog_))) return false;
    cIGZWin* window = dialog_->AsIGZWin();
    cIGZWin* parent = window->GetParentWin();
    window->SetW(parent ? parent->GetW() : 0x280);
    window->SetH(parent ? parent->GetH() : 0x1e0);
    window->GetChildWindowFromIDRecursive(kPageA)->SetFlag(kWinFlagVisible, false);
    window->GetChildWindowFromIDRecursive(kPageB)->SetFlag(kWinFlagVisible, true);

    if (!preferences_ && g_services.preferences) preferences_ = g_services.preferences();
    if (preferences_) {
        for (const CheckBox& box : kCheckBoxes)
            if (gz::cIGZWinBtn* button = FindCheckBox(window, box.id)) {
                if (preferences_[box.offset]) button->ToggleOn(); else button->ToggleOff();
            }
        for (std::uint32_t id : {kCancel, kOk, kCredits}) window->GetChildWindowFromIDRecursive(id)->SetNotificationTarget(window);
        if (gz::cIGZWinBtn* tab = FindCheckBox(window, kTabB)) {
            cIGZWin* tabWindow = tab->AsIGZWin();
            if (g_services.cityLoaded && g_services.cityLoaded()) tabWindow->SetFlag(kWinFlagEnabled, false);
            else tabWindow->SetNotificationTarget(window);
        }
        cIGZWin* root = window->GetParentWin();
        root->GetChildWindowFromIDRecursive(kTabA)->SetNotificationTarget(window);
        root->GetChildWindowFromIDRecursive(kClose)->SetNotificationTarget(window);
    }
    g_displayed = true;
    dialog_->SetWinProc(this);
    return false;                                        // original quirk: false on success
}

bool cSC4GamePreferencesWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    if (!dialog_ || dialog_->AsIGZWin() != window || !window->IsVisible() || message.type != kGZMsgCommand) return false;
    if (message.param1 == kSenderButton) {
        const std::uint32_t id = static_cast<std::uint32_t>(message.param2);
        if (id == kTabA || id == kClose || id == kOk || id == kTabB || id == kCancel || id == kCredits) HandleControlMessage(id);
    }
    return true;
}

bool cSC4GamePreferencesWinProc::HandleControlMessage(std::uint32_t id) {
    GamePreferencesServices& s = g_services;
    switch (id) {
        case kOk: {
            if (preferences_ && dialog_) {
                cIGZWin* window = dialog_->AsIGZWin();
                for (const CheckBox& box : kCheckBoxes)
                    if (gz::cIGZWinBtn* button = FindCheckBox(window, box.id)) preferences_[box.offset] = button->IsOn();
            }
            if (s.postMessage) s.postMessage(kMessagePreferencesChanged);
            if (s.appPresent && s.appPresent() && s.savePreferences) s.savePreferences();
            EndDialog();
            return true;
        }
        case kClose:
            if (s.appPresent && s.appPresent() && s.sendAppMessage) {
                s.sendAppMessage(kMessageAppClose);
                EndDialog();
            }
            return true;
        case kTabA:
        case kTabB: {
            cIGZWin* window = dialog_->AsIGZWin();
            const bool first = id == kTabB;           // the second tab shows page A
            window->GetChildWindowFromIDRecursive(kPageA)->SetFlag(kWinFlagVisible, first);
            window->GetChildWindowFromIDRecursive(kPageB)->SetFlag(kWinFlagVisible, !first);
            return true;
        }
        case kCancel:
            EndDialog();
            return true;
        case kCredits:
            if (s.showCredits) s.showCredits();
            return true;
        default:
            return true;
    }
}

}  // namespace sc4
