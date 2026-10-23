#include "sc4/EstablishCityWinProc.h"

#include "gz/WinUtils.h"
#include "rz/cRZString.h"

namespace sc4 {

namespace {
IEstablishCityHost* g_host = nullptr;
constexpr std::uint32_t kCityNameEdit = 0xaa4149bf, kMayorNameEdit = 0xaa4149d2, kOkButton = 0x2a4144be;
constexpr std::uint32_t kFirstDifficultyButton = 0x2ab6c373, kDifficultyCount = 3;
constexpr std::uint32_t kSenderButton = 0x287259f6;
constexpr std::uint32_t kCancelA = 0x8a414491, kCancelB = 0x42b7c353, kOkB = 0x42b7c356;
constexpr std::uint32_t kMessageTextChanged = 0x9b1552d8;
constexpr std::uint32_t kEndDialogCode = 0xf;

// A name is usable when it holds a character above the space.
bool HasVisibleCharacter(gz::cIGZWinTextEdit* edit) {
    cRZString text;
    edit->GetText(text);
    for (const char* p = text.ToChar(); *p; ++p)
        if (static_cast<unsigned char>(*p) > 0x20) return true;
    return false;
}
}  // namespace

void SetEstablishCityHost(IEstablishCityHost* host) { g_host = host; }

cSC4EstablishCityWinProc::cSC4EstablishCityWinProc(gz::cIGZWinGen* dialog) : dialog_(dialog) {
    if (dialog_) dialog_->AddRef();
    InitDialog();
}

cSC4EstablishCityWinProc::~cSC4EstablishCityWinProc() {
    if (dialog_) dialog_->Release();
}

bool cSC4EstablishCityWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != gz::kGZIID_cIGZWinProc) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4EstablishCityWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void cSC4EstablishCityWinProc::InitDialog() {
    if (!dialog_) return;                  // (the original does not check)
    difficulty_ = g_host->DifficultyLevel();
    cIGZWin* window = dialog_->AsIGZWin();
    nsGZWinUtils::SetWindowNotificationTargetRecursive(window, window);
    nsGZWinUtils::ExcludeButtonByRange(window, kFirstDifficultyButton, kDifficultyCount, kFirstDifficultyButton + difficulty_);
    ValidateCityAndMayorNames();
}

bool cSC4EstablishCityWinProc::ValidateCityAndMayorNames() {
    cIGZWin* window = dialog_->AsIGZWin();
    gz::cIGZWinTextEdit *city = nullptr, *mayor = nullptr;
    bool valid = true;
    if (window->GetChildAsRecursive(kCityNameEdit, gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&city))) {
        if (window->GetChildAsRecursive(kMayorNameEdit, gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&mayor))) {
            if (cIGZWin* ok = window->GetChildWindowFromIDRecursive(kOkButton)) {
                valid = HasVisibleCharacter(city) && HasVisibleCharacter(mayor);
                ok->SetFlag(2, valid);          // enabled
            }
        }
    }
    if (mayor) mayor->Release();
    if (city) city->Release();
    return valid;
}

bool cSC4EstablishCityWinProc::GZOnCharacter(char c) {
    if (c == '\r') {
        if (ValidateCityAndMayorNames()) EndDialog(true);
        return true;
    }
    if (c != '\t') return false;
    cIGZWin* window = dialog_->AsIGZWin();
    gz::cIGZWinTextEdit *city = nullptr, *mayor = nullptr;
    if (window->GetChildAsRecursive(kCityNameEdit, gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&city))) {
        if (window->GetChildAsRecursive(kMayorNameEdit, gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&mayor))) {
            gz::cIGZWinTextEdit* entered;
            gz::cIGZWinTextEdit* left;
            if (g_host->FocusedWindow() == city->AsIGZWin()) { entered = mayor; left = city; }
            else { entered = city; left = mayor; }
            entered->SetSelection(0, entered->GetTextLength());
            g_host->SetFocus(entered->AsIGZWin());
            left->SetSelection(0, 0);
            mayor->Release();
        }
        city->Release();
    }
    return true;
}


bool cSC4EstablishCityWinProc::DoCommand(const cGZMessage& message) {
    if (message.param1 != kSenderButton) return false;
    const std::uint32_t id = static_cast<std::uint32_t>(message.param2);
    if (id - kFirstDifficultyButton < kDifficultyCount) {
        difficulty_ = static_cast<int>(id - kFirstDifficultyButton);
        nsGZWinUtils::ExcludeButtonByRange(dialog_->AsIGZWin(), kFirstDifficultyButton, kDifficultyCount, id);
        return true;
    }
    if (id == kOkButton || id == kOkB) {
        if (ValidateCityAndMayorNames()) EndDialog(true);
        return true;
    }
    if (id == kCancelA || id == kCancelB) {
        EndDialog(false);
        return true;
    }
    return false;
}

void cSC4EstablishCityWinProc::EndDialog(bool ok) {
    if (!dialog_) return;
    AddRef();
    dialog_->SetWinProc(nullptr);
    cIGZWin* window = dialog_->AsIGZWin();
    cIGZWinMgr* manager = window->GetWindowManager();
    if (!window->GetFlag(0x1000)) {
        window->GetParentWin()->ChildDelete(window);
    } else {
        manager->EndDialog(kEndDialogCode, window, !ok);
    }
    gz::cIGZWinGen* dialog = dialog_;
    dialog_ = nullptr;
    dialog->Release();
    Release();
}

bool cSC4EstablishCityWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    if (dialog_->AsIGZWin() != window) return false;
    switch (message.type) {
        case gz::kWinProcInit:
        case gz::kWinProcShutdown:
            return true;
        case kMessageTextChanged:
            ValidateCityAndMayorNames();
            return true;
        case kGZMsgCommand:
            return DoCommand(message);
        case kGZMsgCharacter:
            return GZOnCharacter(static_cast<char>(message.param1));
        // message 0x11: the window got the focus (this case was named KillFocus while the enum names were swapped)
        case kGZMsgSetFocus: {
            gz::cIGZWinTextEdit* city = nullptr;
            if (window->GetChildAsRecursive(kCityNameEdit, gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&city))) {
                g_host->SetFocus(city->AsIGZWin());
                city->Release();
            }
            return true;
        }
        default:
            return false;
    }
}

}  // namespace sc4
