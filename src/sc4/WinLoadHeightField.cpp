#include "sc4/WinLoadHeightField.h"

namespace sc4 {

namespace {
ILoadHeightFieldHost* g_host = nullptr;
constexpr std::uint32_t kEndDialogCode = 0xf;
}
ILoadHeightFieldHost* LoadHeightFieldHost() { return g_host; }
void SetLoadHeightFieldHost(ILoadHeightFieldHost* host) { g_host = host; }

bool cSC4WinLoadHeightField::DoMessage(cGZMessage& message) {
    if (message.type == kGZMsgCommand) {
        if (message.param1 == kLoadHeightFieldMsgCommandSender) {
            if (message.param2 == kFileBrowserCtlOk) {
                if (!browser_) return cGZWin::DoMessage(message);
                FileBrowserSelection selection;
                browser_->GetSelection(selection);
                if (selection.kind == 2) {
                    GetWindowManager()->EndDialog(kEndDialogCode, this, 1);
                    return true;
                }
            }
            if (message.param2 == kFileBrowserCtlCancel && browser_) {
                GetWindowManager()->EndDialog(kEndDialogCode, this, 0);
                return true;
            }
        }
        return cGZWin::DoMessage(message);
    }
    if (message.type != kFileBrowserMsgSelectionChosen) return cGZWin::DoMessage(message);
    // the browser says the user picked something: finish when it is a file
    if (!browser_) return true;
    FileBrowserSelection selection;
    browser_->GetSelection(selection);
    if (selection.kind != 2) return true;
    GetWindowManager()->EndDialog(kEndDialogCode, this, 1);   // the original returns the manager's answer; assumed true
    return true;
}

bool cSC4WinLoadHeightField::GetSelection(FileBrowserSelection& out) {
    return browser_ ? browser_->GetSelection(out) : false;
}

void cSC4WinLoadHeightField::GetInitialDirectory(cRZString& out) {
    if (browser_) browser_->GetCurrentDirectoryLocation(directory_);
    out = directory_;
}

void cSC4WinLoadHeightField::SetInitialDirectory(const cRZString& directory) {
    directory_ = directory;
    if (browser_) browser_->SetCurrentDirectoryLocation(directory_);
}

bool cSC4WinLoadHeightField::Init() {
    if (get_init_flag()) return true;
    cGZWin::Init();
    SetArea(0x14, 0x14, 0x1a8, 0x120);
    PrivateBuffer(true);
    SetFillColor(0x19, 3, 0xdc);
    if (!g_host) return false;
    browser_ = g_host->CreateFileBrowser();
    if (!browser_) return false;
    cIGZWin* browserWindow = browser_->AsIGZWin();
    browserWindow->AddRef();
    browserWindow->SetID(kLoadHeightFieldBrowserId);
    browserWindow->SetArea(8, 8, GetW() - 8, GetH() - 8);
    g_host->SetBrowserGutters(browser_, 4, 4, 4);
    for (const char* pattern : {"*.bmp", "*.png", "*.gif", "*.jpg", "*.jpeg", "*.tga"}) {
        browser_->AddFilter(cRZString(pattern), nullptr, 0, 0);
    }
    browser_->AddFilterSetDescription(cRZString(g_host->FilterSetTitle().c_str()), 0);
    browser_->SetCurrentFilterSet(0);
    browser_->SetDefaultDirectoryLocation(directory_);
    ChildAdd(browserWindow);
    browserWindow->Release();
    return true;
}

bool cSC4WinLoadHeightField::Shutdown() {
    if (browser_) {
        browser_->GetCurrentDirectoryLocation(directory_);
        browser_->Release();
        browser_ = nullptr;
    }
    return true;   // quirk kept: does not call cGZWin::Shutdown
}

bool cSC4WinLoadHeightField::GZPaint() {
    if (drawBuffer_ && drawBuffer_->Lock(0x8001)) {
        drawBuffer_->Fill(GetFillColor(), drawArea_);
        drawBuffer_->Unlock(0x8001);
    }
    return true;
}

bool cSC4WinLoadHeightField::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    if (key == 0x1b) {
        GetWindowManager()->EndDialog(kEndDialogCode, this, 0);
        return true;
    }
    return cGZWin::GZOnKeyDown(key, modifiers);
}

}  // namespace sc4
