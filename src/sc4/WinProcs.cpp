#include "sc4/WinProcs.h"

namespace sc4 {

static IWinProcDirector* g_director = nullptr;
void SetWinProcDirector(IWinProcDirector* director) { g_director = director; }

// ---- dummy ---------------------------------------------------------------------------------------------

bool cGZDummyWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != gz::kGZIID_cIGZWinProc) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZDummyWinProc::AddRef() {
    if (g_director) g_director->AddRef();
    return ++refCount_;
}

std::uint32_t cGZDummyWinProc::Release() {
    if (g_director) g_director->OnProcReleased();
    if (refCount_ != 0) {
        --refCount_;
        if (refCount_ != 0) return refCount_;
    }
    delete this;
    return 0;
}

// ---- generic dialog ------------------------------------------------------------------------------------

bool cSC4GenericDialogWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != gz::kGZIID_cIGZWinProc) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4GenericDialogWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4GenericDialogWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    if (message.type == kGZMsgCommand && message.param1 == gz::kWinCommandSender) {
        window->GetWindowManager()->EndDialog(0xf, window, static_cast<std::int64_t>(message.param2));
        return true;
    }
    return false;
}

// ---- legend dialog -------------------------------------------------------------------------------------

bool cSC4LegendDialogWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != gz::kGZIID_cIGZWinProc) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4LegendDialogWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4LegendDialogWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    constexpr std::uint32_t kButtonA = 0xabb0f65f, kButtonB = 0xebb0f666;
    if (message.type == gz::kWinProcInit && window->GetID() == 0xbb0f5e7) {
        view_->SetButtonToggleState(window, kButtonA, true);
        view_->SetButtonToggleState(window, kButtonB, false);
        return false;
    }
    if (message.type != kGZMsgCommand || message.param1 != gz::kWinCommandSender) return false;
    switch (message.param2) {
    case 1: case 0x2bb0f616: case 0x2bb9131f: case 0x4bb0f628: case 0x4bb91326: case 0x4c081783:
    case 0x6bb91318: case 0xabb0f639: case 0xebb0f630:
        break;
    case 0x4bb92c1f:
        if (view_->LegendPanel() && view_->LegendMinimizedPanel()) {
            view_->LegendPanel()->ShowWindow();
            view_->LegendMinimizedPanel()->HideWindow();
        }
        return false;
    case kButtonA:
        view_->SetButtonToggleState(window, kButtonA, true);
        view_->SetButtonToggleState(window, kButtonB, false);
        break;
    case kButtonB:
        view_->SetButtonToggleState(window, kButtonA, false);
        view_->SetButtonToggleState(window, kButtonB, true);
        break;
    case 0xebb91356:
        view_->SetLegendMinimized(true);
        return false;
    default:
        return false;
    }
    view_->UpdatePaths();
    return false;
}

// ---- compass -------------------------------------------------------------------------------------------

bool cSC4CompassWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == gz::kGZIID_cIGZWinProc) *out = static_cast<gz::cIGZWinProc*>(this);
    else if (iid == 0x452294aa) *out = static_cast<cIGZMessageTarget2*>(this);
    else return false;
    AddRef();
    return true;
}

bool cSC4CompassWinProc::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    if (message.type == gz::kWinProcShutdown) {
        window_ = nullptr;
        host_->RemoveNotification(this, kMessageCameraViewChanged);
        host_->RemoveNotification(this, kMessageCameraRotated);
        return true;
    }
    if (message.type != gz::kWinProcInit) return false;
    window_ = window;
    host_->AddNotification(this, kMessageCameraRotated);
    host_->AddNotification(this, kMessageCameraViewChanged);
    return true;
}

bool cSC4CompassWinProc::DoMessage(cIGZMessage2* message) {
    const std::uint32_t type = message->GetType();
    if (type != kMessageCameraRotated && type != kMessageCameraViewChanged) return false;
    auto* payload = static_cast<cIGZMessage2Standard*>(message);
    const auto* before = static_cast<const int*>(payload->GetVoid1());
    const auto* after = static_cast<const int*>(payload->GetVoid2());
    if (type == kMessageCameraRotated || before[1] != after[1]) UpdateRotation();
    return true;
}

// Children id+1, id+2, ... are the compass faces; only the one whose index equals the rotation is visible.
void cSC4CompassWinProc::UpdateRotation() {
    int rotation = host_->CameraRotation();
    const std::uint32_t id = window_->GetID();
    std::uint32_t next = id + 1;
    for (cIGZWin* child = window_->GetChildWindowFromID(next); child; child = window_->GetChildWindowFromID(++next)) {
        child->SetFlag(kWinVisible, rotation == 0);
        --rotation;
    }
}

}  // namespace sc4
