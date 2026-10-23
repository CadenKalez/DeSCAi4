// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// The concrete window manager: cGZWinMgrBase supplies everything but the platform half. The original's W32 manager turns
// raw window-system messages into cGZMessages and runs the modal loop; this one does the same for the Cocoa pump.
#pragma once
#include <cstdint>
#include <functional>

#include "gz/WinMgrBase.h"
#include "gz/cGZMessageServer.h"

namespace exphost {

class HostWinMgr final : public gz::cGZWinMgrBase, private gz::IWinMgrBaseHost {
public:
    HostWinMgr();
    ~HostWinMgr() override;

    // the platform half
    void PostWinMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) override;
    std::uint64_t RunModalLoop(cIGZWin* window, cIGZFrameWork* framework, std::uint32_t noResult, std::uint32_t abortCode) override;

    // FINDING (early test): cGZWin::Plot ends with mgr->Plot() for every window drawing to the screen buffer, and
    // cGZWinMgrBase::Plot has no re-entry guard, so a plain call recurses until the stack ends. This host adds the guard
    // here instead of touching the recovered classes; whether the original guards elsewhere is still to be checked.
    void Plot() override { if (plotting_) return; plotting_ = true; gz::cGZWinMgrBase::Plot(); plotting_ = false; }

    // called by the Cocoa pump each frame between events: nothing queued is ever dropped
    void Pump() { ProcessPostedMessages(); }
    // the modal loop calls this to let the host keep painting / pumping events
    std::function<void()> modalPump;

private:
    // IWinMgrBaseHost
    cIGZFrameWork* Framework() override { return nullptr; }
    bool GetCOMObject(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZMessageServer* MessageServer() override { return &messages_; }
    int GraphicSystemShowCursor(bool) override { return 0; }

    cGZMessageServer messages_;
    bool plotting_ = false;
    bool modalDone_ = false;
    std::uint64_t modalResult_ = 0;
};

}  // namespace exphost
