// NativeWinMgr — the platform half of the window manager (cGZWinMgrBase supplies everything else). The original's W32 manager
// turns raw window-system messages into cGZMessages and runs the modal loop; this one does the same for the Cocoa event pump.
// Not game code: there is no recovered counterpart on macOS.
#pragma once
#include <cstdint>
#include <functional>

#include "gz/WinMgrBase.h"
#include "gz/cGZMessageServer.h"

namespace native {

class WinMgr final : public gz::cGZWinMgrBase, private gz::IWinMgrBaseHost {
public:
    WinMgr();
    ~WinMgr() override;

    void PostWinMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) override;
    std::uint64_t RunModalLoop(cIGZWin* window, cIGZFrameWork* framework, std::uint32_t noResult, std::uint32_t abortCode) override;

    // cGZWin::Plot ends with mgr->Plot() for every window drawing to the screen buffer and cGZWinMgrBase::Plot has no re-entry
    // guard, so a plain call recurses; the guard lives here (whether the original guards elsewhere is still to be checked).
    void Plot() override { if (plotting_) return; plotting_ = true; gz::cGZWinMgrBase::Plot(); plotting_ = false; }

    void Pump() { ProcessPostedMessages(); }
    std::function<void()> modalPump;   // keeps the host painting and pumping events while a modal window is up
    bool modalDialogs = false;         // SC4GetPopupModalDialogsEnabled / Safe: true once modalPump can run a dialog

private:
    cIGZFrameWork* Framework() override { return nullptr; }
    bool GetCOMObject(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZMessageServer* MessageServer() override { return &messages_; }
    int GraphicSystemShowCursor(bool) override { return 0; }

    cGZMessageServer messages_;
    bool plotting_ = false;
    bool modalDone_ = false;
    std::uint64_t modalResult_ = 0;
};

// The game's window manager (RZ::WM()): set once by the host at boot, for the services that reach it outside the window tree.
WinMgr* ActiveWinMgr();
void SetActiveWinMgr(WinMgr* manager);

}  // namespace native
