#include "gz/Win.h"

#include <algorithm>

#include "rz/cRZVariant.h"

static IWinGraphics* g_graphics = nullptr;
static WinClockFn g_clock = nullptr;
IWinGraphics* WinGraphics() { return g_graphics; }
void SetWinGraphics(IWinGraphics* graphics) { g_graphics = graphics; }
void SetWinClock(WinClockFn fn) { g_clock = fn; }

namespace {
constexpr std::uint32_t kFadeAlphaKey = 0xe6e998fb, kFadeStartKey = 0xe6e998fc, kFadePeriodKey = 0xe6e998fd;
}

cGZWin::cGZWin(cIGZWinMgr* mgr, cIGZKeyboard* keyboard)
    : mgr_(mgr), keyboard_(keyboard), children_(new ChildList) {
    if (mgr_) mgr_->AddWindowToValidList(this);
}

cGZWin::~cGZWin() {
    ChildDeleteAll();
    Shutdown();
    mgr_->CleanUpWindowReferences(this);
    mgr_->RemoveWindowFromValidList(this);
    delete children_;
    for (cIGZWinMessageFilter* f : filters_) f->Release();
    for (cIGZWinMessageFilter* f : pendingRemove_) f->Release();
}

bool cGZWin::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZWin::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZWin::Init() {
    if (!GetFlag(kWinInitialized)) {
        SetFlag(kWinInitialized, true);
        if ((flags_ & 0x10001) == 0x10001) PrivateBuffer(true);
        SetBufferToDrawToRecursive();
    }
    return true;
}

bool cGZWin::Shutdown() {
    if (GetFlag(kWinInitialized)) {
        SetFlag(kWinInitialized, false);
        if (accelerator_) {
            accelerator_->Release();
            accelerator_ = nullptr;
        }
        for (cIGZWinMessageFilter* f : filters_) f->Release();
        filters_.clear();
        for (cIGZWinMessageFilter* f : pendingRemove_) f->Release();
        pendingRemove_.clear();
        HideWindow();
        ChildDeleteAll();
        PrivateBuffer(false);
        for (auto& entry : params_)
            if (entry.second) entry.second->Release();
        params_.clear();
    }
    return true;
}

// ---- child list (readers snapshot, writers copy) -------------------------------------------------------------------

void cGZWin::UnlockChildListRead(ChildList* list) const {
    if (--list->locks != 0 || children_ == list) return;
    for (cIGZWin* child : list->items) child->Release();
    delete list;
}

cGZWin::ChildList* cGZWin::LockChildListWrite() {
    if (children_->locks != 0) {
        auto* copy = new ChildList;
        copy->items = children_->items;
        for (cIGZWin* child : copy->items) child->AddRef();
        children_ = copy;
    }
    return children_;
}

std::uint32_t cGZWin::GetChildCount() const {
    ChildList* list = LockChildListRead();
    const std::uint32_t count = static_cast<std::uint32_t>(list->items.size());
    UnlockChildListRead(list);
    return count;
}

bool cGZWin::ChildExists(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child)) return false;
    ChildList* list = LockChildListRead();
    const bool found = std::find(list->items.begin(), list->items.end(), child) != list->items.end();
    UnlockChildListRead(list);
    return found;
}

bool cGZWin::ChildExists(std::uint32_t id) {
    ChildList* list = LockChildListRead();
    bool found = false;
    for (cIGZWin* child : list->items)
        if ((found = child->GetID() == id)) break;
    UnlockChildListRead(list);
    return found;
}

bool cGZWin::SetParentWin(cIGZWin* parent) {
    cIGZWin* old = parent_;
    if (old == parent) return true;
    if (parent == nullptr || old == nullptr) {
        if (notify_ == nullptr) notify_ = parent;
    } else {
        if (!old->ChildRemove(this)) return false;
        if (notify_ == old) notify_ = nullptr;
        if (notify_ == nullptr) notify_ = parent;
    }
    parent_ = parent;
    return true;
}

bool cGZWin::ChildAddAbsolute(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child)) return false;
    {
        ChildList* list = LockChildListRead();
        const bool present = std::find(list->items.begin(), list->items.end(), child) != list->items.end();
        UnlockChildListRead(list);
        if (present) return false;
    }
    if (!child->SetParentWin(this)) return false;
    if (!child->Init()) {
        child->Shutdown();
        child->SetParentWin(nullptr);
        return false;
    }
    ChildList* list = LockChildListWrite();
    list->items.push_front(child);
    child->AddRef();
    child->CalcAbsoluteArea();
    child->SetBufferToDrawToRecursive();
    return true;
}

bool cGZWin::ChildRemoveAbsolute(cIGZWin* child) {
    const bool valid = mgr_->IsWindowValid(child);
    ChildList* list = LockChildListWrite();
    auto it = std::find(list->items.begin(), list->items.end(), child);
    if (it == list->items.end()) return false;
    if (valid) {
        mgr_->CleanUpWindowReferences(child);
        child->SetParentWin(nullptr);
        child->Release();
    }
    list->items.erase(it);
    return true;
}

bool cGZWin::ChildDeleteAll() {
    bool failed = false;
    ChildList* list = LockChildListRead();
    while (!list->items.empty()) {
        cIGZWin* child = list->items.front();
        UnlockChildListRead(list);
        if (!child) return !failed;
        failed |= !mgr_->DestroyWindow(child);
        list = LockChildListRead();
    }
    UnlockChildListRead(list);
    return !failed;
}

bool cGZWin::ChildDeleteAbsolute(cIGZWin* child) {
    bool failed = true;
    if (mgr_->IsWindowValid(child)) {
        child->AddRef();
        failed = !child->Shutdown();
    }
    const bool removed = ChildRemoveAbsolute(child);
    mgr_->DestroyWindow(child);
    child->Release();
    return removed && !failed;
}

bool cGZWin::IsWinInParentChain(cIGZWin* window) {
    for (cIGZWin* p = GetParentWin(); p; p = p->GetParentWin())
        if (p == window) return true;
    return false;
}

bool cGZWin::IsWinInChildChain(cIGZWin* window) {
    ChildList* list = LockChildListRead();
    bool found = false;
    for (cIGZWin* child : list->items) {
        if (child == window || child->IsWinInChildChain(window)) {
            found = true;
            break;
        }
    }
    UnlockChildListRead(list);
    return found;
}

bool cGZWin::ChildToFront(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child)) return false;
    ChildList* list = LockChildListRead();
    auto it = std::find(list->items.begin(), list->items.end(), child);
    const bool found = it != list->items.end();
    const bool alreadyFront = found && it == list->items.begin();
    UnlockChildListRead(list);
    if (!found) return false;
    if (GetParentWin()) GetParentWin()->ChildToFront(this);
    const bool movable = child->GetFlag(kWinFlag800);
    if (alreadyFront) return movable;
    if (!movable) return false;
    ChildList* w = LockChildListWrite();
    w->items.remove(child);
    w->items.push_front(child);
    return true;
}

bool cGZWin::ChildToBack(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child) || !ChildExists(child) || !child->GetFlag(kWinFlag800)) return false;
    ChildList* w = LockChildListWrite();
    w->items.remove(child);
    w->items.push_back(child);
    return true;
}

bool cGZWin::ChildStepFront(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child)) return false;
    ChildList* w = LockChildListWrite();
    auto it = std::find(w->items.begin(), w->items.end(), child);
    if (it == w->items.end()) return false;
    if (it != w->items.begin()) w->items.splice(std::prev(it), w->items, it);
    return true;
}

bool cGZWin::ChildStepBack(cIGZWin* child) {
    if (!mgr_->IsWindowValid(child)) return false;
    ChildList* w = LockChildListWrite();
    auto it = std::find(w->items.begin(), w->items.end(), child);
    if (it == w->items.end()) return false;
    auto next = std::next(it);
    if (next != w->items.end()) w->items.splice(std::next(next), w->items, it);
    return true;
}

bool cGZWin::MoveRelativeTo(cIGZWin* other, bool before) {
    return parent_ ? parent_->ChildMoveRelative(this, other, before) : false;
}

bool cGZWin::ChildMoveRelative(cIGZWin* child, cIGZWin* other, bool before) {
    if (!ChildExists(child) || !ChildExists(other)) return false;
    ChildList* w = LockChildListWrite();
    auto itChild = std::find(w->items.begin(), w->items.end(), child);
    auto itOther = std::find(w->items.begin(), w->items.end(), other);
    if (!before && itOther != w->items.end()) ++itOther;
    if (itChild != itOther) w->items.splice(itOther, w->items, itChild);
    return true;
}

bool cGZWin::EnumChildren(std::uint32_t iid, EnumChildFn fn, void* context) {
    if (!fn) return false;
    ChildList* list = LockChildListRead();
    if (list->items.empty()) {
        UnlockChildListRead(list);
        return false;
    }
    int failures = 0;
    for (cIGZWin* child : list->items) {
        if (iid == kGZIID_cIGZWin) {
            failures += !fn(this, child->GetID(), child, context);
        } else {
            void* typed = nullptr;
            if (child->QueryInterface(iid, &typed)) {
                failures += !fn(this, child->GetID(), typed, context);
                child->Release();
            }
        }
    }
    UnlockChildListRead(list);
    return failures == 0;
}

// Insertion sort that restarts after every move (the original's O(n^2) scheme); windows with flag 0x800 stay
// put unless `includeFixed` is set.
bool cGZWin::SortChildren(SortFn fn, void* context, bool includeFixed) {
    SortFn less = fn ? fn : lessfunc_id;
    ChildList* w = LockChildListWrite();
    if (w->items.size() < 2) return false;
    auto prev = w->items.begin();
    auto cur = std::next(prev);
    while (cur != w->items.end()) {
        cIGZWin* win = *cur;
        bool restarted = false;
        while ((includeFixed || !win->GetFlag(kWinFlag800)) && less(win, *prev, context)) {
            w->items.erase(cur);
            auto where = w->items.begin();
            while (where != w->items.end() && !less(win, *where, context)) ++where;
            w->items.insert(where, win);
            prev = w->items.begin();
            cur = std::next(prev);
            restarted = true;
            if (cur == w->items.end()) return true;
            win = *cur;
        }
        (void)restarted;
        prev = cur;
        ++cur;
    }
    return true;
}

cIGZWin* cGZWin::GetChildWindowFromID(std::uint32_t id) {
    ChildList* list = LockChildListRead();
    cIGZWin* result = nullptr;
    for (cIGZWin* child : list->items)
        if (child->GetID() == id) { result = child; break; }
    UnlockChildListRead(list);
    return result;
}

cIGZWin* cGZWin::GetChildWindowFromIDRecursive(std::uint32_t id) {
    ChildList* list = LockChildListRead();
    cIGZWin* result = nullptr;
    for (cIGZWin* child : list->items)
        if ((result = child->GetChildWindowFromIDRecursive(id))) break;
    UnlockChildListRead(list);
    if (result) return result;
    return GetID() == id ? this : nullptr;
}

bool cGZWin::GetChildAs(std::uint32_t id, std::uint32_t iid, void** out) {
    cIGZWin* child = GetChildWindowFromID(id);
    return child ? child->QueryInterface(iid, out) : false;
}

bool cGZWin::GetChildAsRecursive(std::uint32_t id, std::uint32_t iid, void** out) {
    cIGZWin* child = GetChildWindowFromIDRecursive(id);
    return child ? child->QueryInterface(iid, out) : false;
}

cIGZWin* cGZWin::GetChildWindowFromPoint(int x, int y) {
    ChildList* list = LockChildListRead();
    cIGZWin* result = nullptr;
    for (cIGZWin* child : list->items)
        if (child->GetFlag(kWinVisible) && (result = child->GetChildWindowFromPoint(x, y))) break;
    UnlockChildListRead(list);
    if (result) return result;
    return IsPointInWindowScreenCoordinates(x, y) ? this : nullptr;
}

cIGZWin* cGZWin::GetChildWindowFromCursorPoint(int x, int y) {
    ChildList* list = LockChildListRead();
    cIGZWin* result = nullptr;
    for (cIGZWin* child : list->items)
        if (child->GetFlag(kWinVisible) && !child->GetFlag(kWinIgnoreCursor) &&
            (result = child->GetChildWindowFromCursorPoint(x, y)))
            break;
    UnlockChildListRead(list);
    if (result) return result;
    return IsPointInWindowScreenCoordinates(x, y) ? this : nullptr;
}

cIGZWin* cGZWin::IsChildKeyEquivalent(std::uint32_t key, std::uint32_t modifiers) {
    ChildList* list = LockChildListRead();
    cIGZWin* result = nullptr;
    for (cIGZWin* child : list->items)
        if (child->CheckKeyEquivalent(key, modifiers)) { result = child; break; }
    UnlockChildListRead(list);
    return result;
}

// ---- geometry ----------------------------------------------------------------------------------------------

void cGZWin::SetArea(int l, int t, int r, int b) {
    const bool sameSize = (r - l == area_.right - area_.left) && (b - t == area_.bottom - area_.top);
    area_ = {l, t, r, b};
    if (!sameSize && (flags_ & kWinPrivateBuffer)) PrivateBuffer(true);
    CalcAbsoluteArea();
    SetAreaToDrawToRecursive();
}

void cGZWin::CalcAbsoluteArea() {
    cRZRect abs = GetArea();
    for (cIGZWin* p = GetParentWin(); p; p = p->GetParentWin()) {
        const int l = p->GetL(), t = p->GetT();
        abs.left += l; abs.top += t; abs.right += l; abs.bottom += t;
    }
    absoluteArea_ = abs;
    ChildList* list = LockChildListRead();
    for (cIGZWin* child : list->items) child->CalcAbsoluteArea();
    UnlockChildListRead(list);
}

void cGZWin::CenterWindowInRect(const cRZRect& rect) {
    const int w = GetW(), h = GetH();
    const int x = ((rect.right - rect.left) - w) / 2 + rect.left;
    const int y = ((rect.bottom - rect.top) - h) / 2 + rect.top;
    SetArea(x, y, w + x, h + y);
}

bool cGZWin::FitRectToWindow(cRZRect& rect, int margin) {
    const int w = GetW(), h = GetH();
    if (rect.left < 0) rect.left = margin;
    if (rect.top < 0) rect.top = margin;
    if (w < rect.right) {
        const int d = (w - rect.right) - margin;
        rect.left += d;
        rect.right += d;
    }
    if (h < rect.bottom) {
        const int d = (h - rect.bottom) - margin;
        rect.top += d;
        rect.bottom += d;
    }
    if (w < (rect.right + margin) - rect.left) return false;
    return (rect.bottom + margin) - rect.top <= h;
}

void cGZWin::ScreenToWindowCoordinates(int& x, int& y) const {
    int ox = 0, oy = 0;
    WindowToScreenCoordinates(ox, oy);
    x -= ox;
    y -= oy;
}

bool cGZWin::WindowToWindowCoordinates(cIGZWin* other, int& x, int& y) const {
    if (!other) return false;
    WindowToScreenCoordinates(x, y);
    other->ScreenToWindowCoordinates(x, y);
    return true;
}

bool cGZWin::IsPointInWindowWindowCoordinates(int x, int y) const {
    bool inside = !((x | y) < 0) && y < area_.bottom - area_.top && x < area_.right - area_.left;
    if (inside && GetFlag(kWinHitTestTransparency)) inside = !is_point_transparent(x, y);
    return inside;
}

bool cGZWin::IsPointInWindowParentCoordinates(int x, int y) const {
    return !(x < area_.left || y < area_.top || area_.right <= x) && y < area_.bottom;
}

bool cGZWin::IsPointInWindowScreenCoordinates(int x, int y) const {
    bool inside = !(x < absoluteArea_.left || y < absoluteArea_.top || absoluteArea_.right <= x) && y < absoluteArea_.bottom;
    if (inside && GetFlag(kWinHitTestTransparency)) {
        int wx = x, wy = y;
        ScreenToWindowCoordinates(wx, wy);
        inside = !is_point_transparent(wx, wy);
    }
    return inside;
}

// ---- flags, visibility, focus ----------------------------------------------------------------------------------

std::uint32_t cGZWin::SetFlag(std::uint32_t flag, bool on) {
    flags_ = on ? (flags_ | flag) : (flags_ & ~flag);
    auto refocusToNotifyTarget = [this] {
        if (mgr_->GZGetFocus() == this && mgr_->IsWindowValid(notify_)) mgr_->GZSetFocus(notify_);
    };
    if (flag < 0x10000) {
        if (flag == kWinVisible) {
            if (!GetFlag(kWinPrivateBuffer)) child_private_buffers(on);
            else PrivateBuffer(on);
            if (!on) refocusToNotifyTarget();
            if (parent_) parent_->InvalidateSelfAndParents();
            flags_ &= ~kWinFlag400000;
        } else if (flag == kWinEnabled) {
            if (!on) {
                if (mgr_->GZGetCapture() == this) mgr_->GZReleaseCapture(this);
                refocusToNotifyTarget();
            }
        } else if (flag == kWinFade) {
            set_fade_state(on ? 0 : 0xff, on ? fadeInMs_ : 0);
        }
    } else if (flag == kWinPrivateBuffer) {
        PrivateBuffer(on && IsVisible());
    } else if (flag == kWinPrivateBufferTransparent) {
        if (GetFlag(kWinPrivateBuffer)) InvalidateSelfAndParents();
    } else if (flag == kWinDesktop) {
        SetBufferToDrawToRecursive();
        SetAreaToDrawToRecursive();
    }
    return flags_;
}

bool cGZWin::child_private_buffers(bool on) {
    ChildList* list = LockChildListRead();
    bool failed = false;
    for (cIGZWin* child : list->items) {
        bool moveAfter = false;
        if (!child->GetFlag(kWinPrivateBuffer)) {
            static_cast<cGZWin*>(child)->child_private_buffers(on);
            moveAfter = on;
        } else if (on) {
            if (!child->IsVisible()) static_cast<cGZWin*>(child)->child_private_buffers(false);
            else failed |= !child->PrivateBuffer(true);
            moveAfter = true;
        } else {
            failed |= !child->PrivateBuffer(false);
        }
        if (moveAfter) child->GZWinMoveTo(child->GetL(), child->GetT());
    }
    UnlockChildListRead(list);
    return !failed;
}

void cGZWin::ShowWindow() {
    if (!GetFlag(kWinVisible)) {
        SetFlag(kWinVisible, true);
        if (GetFlag(kWinFade)) set_fade_state(0, fadeInMs_);
    }
}

void cGZWin::HideWindow() {
    if (GetFlag(kWinVisible)) {
        SetFlag(kWinVisible, false);
        if (GetFlag(kWinFade)) set_fade_state(0xff, 0);
    }
}

bool cGZWin::SetFocusTo(cIGZWin* window) {
    const bool valid = mgr_->IsWindowValid(window);
    if (valid) mgr_->GZSetFocus(window);
    return valid;
}

bool cGZWin::GZOnSetFocus(cIGZWin*) {
    if (!GetFlag(kWinVisible) && mgr_->IsWindowValid(notify_)) mgr_->GZSetFocus(notify_);
    return false;
}

void cGZWin::InvalidateAllParents() {
    for (cIGZWin* p = GetParentWin(); p; p = p->GetParentWin()) {
        p->InvalidateSelf();
        if (p->GetFlag(kWinDesktop)) break;
    }
}

void cGZWin::InvalidateSelfAndParents() {
    InvalidateSelf();
    if (!GetFlag(kWinDesktop)) InvalidateAllParents();
}

// ---- messages ----------------------------------------------------------------------------------------------------

bool cGZWin::DoMessage(cGZMessage& m) {
    const bool wasDispatching = dispatching_;
    dispatching_ = true;
    for (cIGZWinMessageFilter* filter : filters_) {
        if (filter->DoMessage(this, m)) {
            dispatching_ = wasDispatching;
            return true;
        }
    }
    dispatching_ = wasDispatching;
    const int a = static_cast<int>(m.param1), b = static_cast<int>(m.param2);
    const short sa = static_cast<short>(m.param1), sb = static_cast<short>(m.param2);
    const std::uint16_t flags16 = static_cast<std::uint16_t>(m.param3 >> 16);
    switch (m.type) {
        case kGZMsgDeleteChild: return ChildDelete(reinterpret_cast<cIGZWin*>(m.param1));
        case kGZMsgCommand: return GZOnCommand(static_cast<std::uint32_t>(a), m.param2);
        case kGZMsgCharacter: return GZOnCharacter(static_cast<char>(m.param1));
        case kGZMsgKeyDown: {
            const bool handled = GZOnKeyDown(a, b);
            if (accelerator_ && !handled) return AccelerateKeyboardMsg(m);
            return handled;
        }
        case kGZMsgKeyUp: {
            const bool handled = GZOnKeyUp(a, b);
            if (accelerator_ && !handled) return AccelerateKeyboardMsg(m);
            return handled;
        }
        case kGZMsgMouseDownL: return GZOnMouseDownL(sa, sb, flags16);
        case kGZMsgMouseDownR: return GZOnMouseDownR(sa, sb, flags16);
        case kGZMsgMouseUpL: return GZOnMouseUpL(sa, sb, flags16);
        case kGZMsgMouseUpR: return GZOnMouseUpR(sa, sb, flags16);
        case kGZMsgMouseMove: return GZOnMouseMove(sa, sb, flags16);
        case kGZMsgMouseWheel:
            return GZOnMouseWheel(a, b, static_cast<std::uint32_t>(m.param3 & 0xffffffff) >> 16,
                                  static_cast<short>(m.param3 & 0xffffffff));
        case kGZMsgKillFocus: return GZOnKillFocus(reinterpret_cast<cIGZWin*>(m.param1));
        case kGZMsgSetFocus: return GZOnSetFocus(reinterpret_cast<cIGZWin*>(m.param1));
        case kGZMsgCaptureChanged: return GZOnCaptureChanged(reinterpret_cast<cIGZWin*>(m.param1), reinterpret_cast<cIGZWin*>(m.param2));
        case kGZMsgMouseEnter: return GZOnMouseEnter(reinterpret_cast<cIGZWin*>(m.param1));
        case kGZMsgMouseExit: return GZOnMouseExit(reinterpret_cast<cIGZWin*>(m.param1));
        default: return false;
    }
}

bool cGZWin::SendMsg(cIGZWin* target, cGZMessage& message) {
    return mgr_->IsWindowValid(target) ? target->DoMessage(message) : false;
}

void cGZWin::SendMsg(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) {
    cGZMessage m;
    m.type = type; m.param1 = p1; m.param2 = p2; m.param3 = p3;
    SendMsg(target, m);
}

bool cGZWin::PostMsg(cIGZWin* target, cGZMessage& message) {
    mgr_->PostMessageToTarget(target, message);
    return true;
}

void cGZWin::PostMsg(cIGZWin* target, std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) {
    cGZMessage m;
    m.type = type; m.param1 = p1; m.param2 = p2; m.param3 = p3;
    PostMsg(target, m);
}

bool cGZWin::AddMessageFilter(cIGZWinMessageFilter* filter) {
    if (filter) filter->AddRef();
    filters_.push_back(filter);
    return true;
}

bool cGZWin::RemoveMessageFilter(cIGZWinMessageFilter* filter) {
    if (filter) filter->AddRef();
    bool result = false;
    auto it = std::find(filters_.begin(), filters_.end(), filter);
    if (it != filters_.end()) {
        result = true;
        if (!dispatching_) {
            (*it)->Release();
            filters_.erase(it);
        } else {
            pendingRemove_.push_back(*it);
        }
    }
    if (filter) filter->Release();
    return result;
}

void cGZWin::SetKeyboardAccelerator(cIGZWinKeyAccelerator* accelerator) {
    if (accelerator_ == accelerator) return;
    if (accelerator_) accelerator_->Release();
    accelerator_ = accelerator;
    if (accelerator) accelerator->AddRef();
}

// ---- cursor ------------------------------------------------------------------------------------------------------

void cGZWin::UpdateCursor() {
    if (!cursor_) {
        cursor_ = mgr_->GetCursorManager()->LoadCursor(1);
        if (!cursor_) return;
    }
    mgr_->GetCursorManager()->SetCursor(cursor_);
}

bool cGZWin::ProcessCursorMessage(cGZMessage& m) {
    const std::uint32_t type = m.type;
    ChildList* list = LockChildListRead();
    for (cIGZWin* child : list->items) {
        if (child->IsVisible() && !child->GetFlag(kWinIgnoreCursor) &&
            child->IsPointInWindowScreenCoordinates(static_cast<int>(m.param1), static_cast<int>(m.param2)) &&
            (type == kGZMsgMouseMove || child->IsEnabled())) {
            UnlockChildListRead(list);
            return child->ProcessCursorMessage(m);
        }
    }
    UnlockChildListRead(list);
    if (GetFlag(kWinIgnoreCursor) || !IsVisible()) return false;
    int x = static_cast<int>(m.param1), y = static_cast<int>(m.param2);
    if (!IsEnabled()) {
        if (type != kGZMsgMouseMove) return false;
        ScreenToWindowCoordinates(x, y);
    } else {
        if (type - 7u < 3u) {
            PullToFront();
            mgr_->GZSetFocus(this);
        }
        ScreenToWindowCoordinates(x, y);
        m.param1 = static_cast<std::uint32_t>(x);
        m.param2 = static_cast<std::uint32_t>(y);
        if (type == kGZMsgMouseMove) UpdateCursor();
        mgr_->SetCurrentMouseWin(this);
        return DoMessage(m);
    }
    m.param1 = static_cast<std::uint32_t>(x);
    m.param2 = static_cast<std::uint32_t>(y);
    mgr_->SetCurrentMouseWin(this);
    return DoMessage(m);
}

// ---- parameters ----------------------------------------------------------------------------------------------------

bool cGZWin::SetParam(std::uint32_t key, cIGZVariant* value) {
    auto it = params_.find(key);
    if (it != params_.end()) {
        if (it->second) it->second->Release();
        params_.erase(it);
    }
    if (value) {
        value->AddRef();
        params_.emplace(key, value);
    }
    return true;
}

bool cGZWin::GetParam(std::uint32_t key, cIGZVariant** out) const {
    auto it = params_.find(key);
    if (it == params_.end()) return false;
    *out = it->second;
    it->second->AddRef();
    return true;
}

bool cGZWin::EnumParams(EnumParamFn fn, void* context) {
    for (auto& entry : params_)
        if (!fn(this, entry.first, entry.second, context)) return false;
    return true;
}

// ---- colours ----------------------------------------------------------------------------------------------------------

void cGZWin::GetFillColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const {
    r = static_cast<std::uint8_t>(fillColor_ >> 16);
    g = static_cast<std::uint8_t>(fillColor_ >> 8);
    b = static_cast<std::uint8_t>(fillColor_);
}

std::uint32_t cGZWin::GetFillColor() const {
    return WinGraphics()->MakeNativeColor((fillColor_ >> 16) & 0xff, (fillColor_ >> 8) & 0xff, fillColor_ & 0xff);
}

void cGZWin::SetFillColor(std::uint32_t nativeColor) {
    std::uint8_t r, g, b;
    WinGraphics()->NativeToRGB(nativeColor, r, g, b);
    SetFillColor(r, g, b);
}

std::uint32_t cGZWin::MakeFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (drawBuffer_) return drawBuffer_->MakeNativeColor(r, g, b);
    return WinGraphics() ? WinGraphics()->MakeNativeColor(r, g, b) : 0;
}

std::uint32_t cGZWin::ConvertPackedRGBToNative(std::uint32_t rgb) const {
    const std::uint8_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    if (drawBuffer_) return drawBuffer_->MakeNativeColor(r, g, b);
    return WinGraphics() ? WinGraphics()->MakeNativeColor(r, g, b) : 0;
}

std::uint32_t cGZWin::ConvertNativeToPackedRGB(std::uint32_t native) const {
    std::uint8_t r = 0, g = 0, b = 0;
    if (drawBuffer_) drawBuffer_->NativeToRGB(native, r, g, b);
    else if (WinGraphics()) WinGraphics()->NativeToRGB(native, r, g, b);
    return b | (std::uint32_t(g) << 8) | (std::uint32_t(r) << 16) | 0xff000000u;
}

// ---- fade effect ------------------------------------------------------------------------------------------------------------

void cGZWin::set_fade_state(std::uint8_t alpha, int periodMs) {
    if ((flags_ & 0x10020) != 0x10020) return;
    auto store = [this](std::uint32_t key, auto setter) {
        auto* v = new cRZVariant();
        v->AddRef();
        setter(v);
        SetParam(key, v);
        v->Release();
    };
    store(kFadeAlphaKey, [&](cRZVariant* v) { v->SetValUint8(alpha); });
    store(kFadeStartKey, [&](cRZVariant* v) { v->SetValUint32(g_clock ? g_clock() : 0); });
    store(kFadePeriodKey, [&](cRZVariant* v) { v->SetValSint32(periodMs); });
    if (privateBuffer_) privateBuffer_->SetFadeAlpha(alpha);
}

bool cGZWin::fade_in_progress() {
    if ((flags_ & 0x10020) != 0x10020) return false;
    cIGZVariant* v = nullptr;
    if (!GetParam(kFadePeriodKey, &v)) return false;
    const bool active = (v->AsSint32() & 0xff) != 0;   // the original tests only the low byte
    v->Release();
    return active;
}

void cGZWin::recalc_blt_area() {
    bltArea_ = GetArea();
    for (cIGZWin* p = GetParentWin(); p && !p->GetPrivateBuffer(); p = p->GetParentWin()) {
        const int l = p->GetL(), t = p->GetT();
        bltArea_.left += l; bltArea_.top += t; bltArea_.right += l; bltArea_.bottom += t;
    }
}

void cGZWin::SetBufferToDrawTo() {
    drawBuffer_ = static_cast<IWinBuffer*>(GetPrivateBuffer());
    usesOwnBuffer_ = true;
    if (!drawBuffer_) {
        for (cIGZWin* p = GetParentWin(); p; p = p->GetParentWin()) {
            if (void* b = p->GetPrivateBuffer()) {
                drawBuffer_ = static_cast<IWinBuffer*>(b);
                break;
            }
            if (GetFlag(kWinDesktop)) break;
        }
        if (!drawBuffer_ && WinGraphics()) {
            drawBuffer_ = WinGraphics()->PrimaryBuffer();
            usesOwnBuffer_ = false;
        }
    }
    drawContext_ = drawBuffer_ ? drawBuffer_->GetDrawContext() : nullptr;
    SetAreaToDrawToRecursive();
}

void cGZWin::SetBufferToDrawToRecursive() {
    SetBufferToDrawTo();
    ChildList* list = LockChildListRead();
    for (cIGZWin* child : list->items) child->SetBufferToDrawToRecursive();
    UnlockChildListRead(list);
}

void cGZWin::SetAreaToDrawToRecursive() {
    SetAreaToDrawTo();
    ChildList* list = LockChildListRead();
    for (cIGZWin* child : list->items) child->SetAreaToDrawToRecursive();
    UnlockChildListRead(list);
}

void cGZWin::SetAreaToDrawTo() {
    if (!privateBuffer_) {
        cRZRect local = area_;
        drawArea_ = area_;
        for (cIGZWin* p = GetParentWin(); p; p = p->GetParentWin()) {
            if (p->GetPrivateBuffer()) break;
            if (GetFlag(kWinDesktop)) {
                local = absoluteArea_;
                break;
            }
            const int l = p->GetL(), t = p->GetT();
            local.left += l; local.top += t; local.right += l; local.bottom += t;
        }
        drawArea_ = local;
        if (WinGraphics() && WinGraphics()->DrawSystemActive()) {
            // Original quirk: the top edge is cleared before the bottom is rebased, so bottom keeps its value.
            const int left = drawArea_.left;
            drawArea_.left = 0;
            drawArea_.top = 0;
            drawArea_.right -= left;
        }
    } else {
        drawArea_ = {0, 0, area_.right - area_.left, area_.bottom - area_.top};
        recalc_blt_area();
    }
    accumulatedArea_ = drawArea_;
    cIGZWin* p = this;
    while (!p->GetPrivateBuffer()) {
        if (p->GetFlag(kWinDesktop)) {
            const cRZRect& a = p->GetAreaAbsolute();
            accumulatedArea_.left += a.left; accumulatedArea_.top += a.top;
            accumulatedArea_.right += a.left; accumulatedArea_.bottom += a.top;
            break;
        }
        const cRZRect& a = p->GetArea();
        accumulatedArea_.left += a.left; accumulatedArea_.top += a.top;
        accumulatedArea_.right += a.left; accumulatedArea_.bottom += a.top;
        p = p->GetParentWin();
        if (!p) break;
    }
}

bool cGZWin::PrivateBuffer(bool enable) {
    // Rendering pipeline (private buffer creation / fade temp window): not recovered; only the flag is kept.
    if (enable) flags_ |= kWinPrivateBuffer;
    return true;
}

bool cGZWin::Plot() {
    bool ok = PlotComposite() ? PlotPresent() : false;
    if (!usesOwnBuffer_) mgr_->Plot();
    return ok;
}

bool cGZWin::add_delayedplot_windows(cIGZWin*, std::uint32_t, void* child, void* manager) {
    auto* win = static_cast<cIGZWin*>(child);
    if (win->IsVisible()) {
        if (!win->GetFlag(kWinDesktop)) {
            win->EnumChildren(kGZIID_cIGZWin, add_delayedplot_windows, manager);
        } else {
            win->SetFlag(kWinPlotted, true);
            static_cast<cIGZWinMgr*>(manager)->AppendToPlotStrategy(win);
        }
    }
    return true;
}
