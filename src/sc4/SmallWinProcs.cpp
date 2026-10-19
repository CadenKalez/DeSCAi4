#include "sc4/SmallWinProcs.h"

#include <cmath>

namespace sc4 {

PanelServices& GetPanelServices() {
    static PanelServices services;
    return services;
}

// ---- cSC4PanelUIWinProc -------------------------------------------------------------------------------

bool cSC4PanelUIWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != gz::kGZIID_cIGZWinProc && iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4PanelUIWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4PanelUIWinProc::DoWinProcMessage(cIGZWin*, cGZMessage& message) {
    if (message.type != kGZMsgCommand || message.param1 != 0x287259f6) return false;
    PanelServices& services = GetPanelServices();
    if (!services.hasView3D || !services.hasView3D()) return false;
    std::uint32_t command;
    switch (message.param2) {
        case 0x26c10a3d: command = 0x6a935e36; break;
        case 0x26c10a3e: command = 0x6a935e3c; break;
        case 0x8a1da655: command = 0x6a935e4b; break;
        case 0xca1da670: command = 0x6a935e45; break;
        case 0xc98f4f48: command = 0x6a935e39; break;
        default: return false;
    }
    if (services.executeCommand) services.executeCommand(command);
    return true;
}

// ---- cSC4RegionSignpostAnimatorWinProc ------------------------------------------------------------------

bool cSC4RegionSignpostAnimatorWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != gz::kGZIID_cIGZWinProc && iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4RegionSignpostAnimatorWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4RegionSignpostAnimatorWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    PanelServices& services = GetPanelServices();
    switch (message.type) {
        case kMessageSignpostTick:
            if (signpost_) {
                const std::uint32_t now = services.nowMs ? services.nowMs() : 0;
                const double phase = std::sin((static_cast<double>((now - startMs_) % 1500u) / 1500.0) * 6.28318);
                const int range = signpost_->GetParentWin()->GetH() - signpost_->GetH();
                signpost_->GZWinMoveTo(0, static_cast<int>(static_cast<double>(range) * (1.0 - std::fabs(phase))));
            }
            return false;
        case gz::kWinProcShutdown: {
            cIGZWin* old = signpost_;
            signpost_ = nullptr;
            if (old) old->Release();
            return true;
        }
        case gz::kWinProcInit: {
            startMs_ = services.nowMs ? services.nowMs() : 0;
            cIGZWin* found = window->GetChildWindowFromIDRecursive(0x4a630000);
            if (signpost_ != found) {
                if (found) found->AddRef();
                cIGZWin* old = signpost_;
                signpost_ = found;
                if (old) old->Release();
            }
            return true;
        }
        default:
            return false;
    }
}

}  // namespace sc4
