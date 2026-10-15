#include "sc4/ViewInputControl.h"

namespace sc4 {

static IViewInputHost* g_host = nullptr;
IViewInputHost* ViewInputHost() { return g_host; }
void SetViewInputHost(IViewInputHost* host) { g_host = host; }

bool cSC4ViewInputControl::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cISC4ViewInputControl) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC4ViewInputControl::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

void cSC4ViewInputControl::FinalRelease() {
    Shutdown();
    delete this;
}

bool cSC4ViewInputControl::Init() {
    if (initialized_) return true;
    if (!cursor_ && cursorId_ != 0) {
        if (cIGZCursorManager* manager = g_host->WindowManager()->GetCursorManager()) {
            cursor_ = manager->LoadCursor(cursorId_);
            if (cursor_) cursor_->AddRef();
        }
    }
    if (cursor_ && window_) window_->SetCursor(cursor_, true);
    view3D_ = g_host->View3D();
    if (view3D_) view3D_->AddRef();
    winMgr_ = g_host->WindowManager();
    winMgr_->AddRef();
    initialized_ = true;
    return true;
}

bool cSC4ViewInputControl::Shutdown() {
    if (initialized_) {
        if (winMgr_) { winMgr_->Release(); winMgr_ = nullptr; }
        if (view3D_) { view3D_->Release(); view3D_ = nullptr; }
        if (cursor_) { cursor_->Release(); cursor_ = nullptr; }
        initialized_ = false;
    }
    return true;
}

void cSC4ViewInputControl::SetCursor(cIGZCursor* cursor) {
    cIGZCursor* old = cursor_;
    if (old == cursor) return;
    if (cursor) {
        cursor->AddRef();
        old = cursor_;
    }
    if (old) old->Release();
    cursor_ = cursor;
    if (cursor && initialized_ && window_) window_->SetCursor(cursor, true);
}

void cSC4ViewInputControl::SetCursor(std::uint32_t cursorId) {
    if (cIGZCursorManager* manager = g_host->WindowManager()->GetCursorManager()) {
        if (cIGZCursor* cursor = manager->LoadCursor(cursorId)) {
            cursorId_ = cursorId;
            SetCursor(cursor);
        }
    }
}

void cSC4ViewInputControl::EndInput() {
    if (view3D_) view3D_->EndInput(0);
}

void cSC4ViewInputControl::PlayResultSound(int soundId) {
    if (soundId != 0) g_host->PlaySound(1, static_cast<std::uint32_t>(soundId));
}

bool cSC4ViewInputControl::SetCapture() {
    if (!window_) return false;
    winMgr_->GZSetCapture(window_);
    return true;
}

bool cSC4ViewInputControl::ReleaseCapture() {
    if (!window_) return false;
    winMgr_->GZReleaseCapture(window_);
    return true;
}

bool cSC4ViewInputControl::AmCapturing() {
    return window_ ? winMgr_->GZGetCapture() == window_ : false;
}

bool cSC4ViewInputControl::IsOnTop() { return view3D_->TopControl() == this; }

}  // namespace sc4
