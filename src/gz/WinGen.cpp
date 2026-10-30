#include "gz/WinGen.h"

#include <algorithm>

#include "gz/DrawContext.h"
#include "gz/RZDraw.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinStyle.h"

namespace gz {

namespace {
IWinGenHost* g_host = nullptr;
constexpr std::uint32_t kBackgroundImageId = 0xe2b14588, kTitleFontId = 0xe2b14587, kTitleImageId = 0xe2b14589,
                        kDefaultFontStyle = 0x68963c4c, kTitleFillColorId = 0xbb5b2ec6, kTitleOutlineColorId = 0xbb5b2ec7,
                        kEllipsisStringId = 0x5377be31;
}  // namespace

IWinGenHost* WinGenHost() { return g_host; }
void SetWinGenHost(IWinGenHost* host) { g_host = host; }

// ======================================== cGZWinGenTitleBar ==============================================

cGZWinGenTitleBar::cGZWinGenTitleBar(cIGZWinMgr* mgr) : cGZWinFlatRect(mgr) {   // 0x10024c880
    displayCaption_ = new cRZString();
    displayCaption_->AddRef();
}

cGZWinGenTitleBar::~cGZWinGenTitleBar() {   // 0x10024c976
    cGZWinGenTitleBar::Shutdown();
    if (displayCaption_) displayCaption_->Release();
}

bool cGZWinGenTitleBar::QueryInterface(std::uint32_t iid, void** out) {   // 0x10024ca34
    if (iid == kGZIID_cIGZWinGenTitleBar) {
        *out = static_cast<cIGZWinGenTitleBar*>(this);
        AddRef();
        return true;
    }
    return cGZWinFlatRect::QueryInterface(iid, out);
}

bool cGZWinGenTitleBar::Init() {   // 0x10024cb0c
    if (get_init_flag()) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (!factory) return false;
    cIGZFont* font = static_cast<cIGZFont*>(factory->Font(kTitleFontId));
    if (!font) font = static_cast<cIGZFont*>(factory->Font(kDefaultFontStyle));
    SetTitleFont(font);
    // The original builds a plain 64x64 bitmap when the colour manager has no title image; that path is not recovered.
    cIGZBuffer* image = factory->Image(kTitleImageId);
    if (!image) return false;
    cRZRect bounds;
    image->GetBufferArea(bounds);
    const int width = bounds.right - bounds.left;
    const int rowHeight = (bounds.bottom - bounds.top) >> 2;
    cRZRect area{0, 0, width, rowHeight};
    auto shift = [&]() { area.top += rowHeight; area.bottom += rowHeight; };
    if (closeButton_) { closeButton_->Release(); closeButton_ = nullptr; }
    if (!create_and_add_sys_btn(kWinGenCloseId, image, area, closeButton_)) return false;
    closeButton_->AsIGZWin()->SetFlag(kWinVisible, (barFlags_ & 1) != 0);
    closeButton_->AsIGZWin()->SetFlag(kWinEnabled, (barFlags_ & 0x100) == 0);
    shift();
    if (zoomButton_) { zoomButton_->Release(); zoomButton_ = nullptr; }
    if (!create_and_add_sys_btn(kWinGenZoomId, image, area, zoomButton_)) return false;
    zoomButton_->AsIGZWin()->SetFlag(kWinVisible, (barFlags_ & 4) >> 2);
    zoomButton_->AsIGZWin()->SetFlag(kWinEnabled, (barFlags_ & 0x400) == 0);
    shift();
    if (minimizeButton_) { minimizeButton_->Release(); minimizeButton_ = nullptr; }
    if (!create_and_add_sys_btn(kWinGenMinimizeId, image, area, minimizeButton_)) return false;
    minimizeButton_->AsIGZWin()->SetFlag(kWinVisible, (barFlags_ & 2) >> 1);
    minimizeButton_->AsIGZWin()->SetFlag(kWinEnabled, (barFlags_ & 0x200) == 0);
    cGZWin::Init();
    return true;
}

bool cGZWinGenTitleBar::SetTitleFont(cIGZFont* font) {   // 0x10024cf02
    if (font_ != font) {
        if (font) font->AddRef();
        if (font_) font_->Release();
        font_ = font;
        InvalidateSelf();
    }
    return true;
}

bool cGZWinGenTitleBar::create_and_add_sys_btn(std::uint32_t id, cIGZBuffer* image, const cRZRect& area, cIGZWinBtn*& out) {   // 0x10024cf56
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (!factory || !image) return false;
    out = factory->CreateButton(2, id, "", image, &area, 0);
    if (!out) return false;
    cIGZWin* window = out->AsIGZWin();
    if (!ChildAdd(window)) {
        out->Release();
        out = nullptr;
        return false;
    }
    window->SetNotificationTarget(parent_);
    out->SetBtnFlag(4, false);
    return true;
}

bool cGZWinGenTitleBar::Shutdown() {   // 0x10024d0c6
    if (get_init_flag()) {
        if (closeButton_) { closeButton_->Release(); closeButton_ = nullptr; }
        if (minimizeButton_) { minimizeButton_->Release(); minimizeButton_ = nullptr; }
        if (zoomButton_) { zoomButton_->Release(); zoomButton_ = nullptr; }
        if (displayCaption_) { cRZString* caption = displayCaption_; displayCaption_ = nullptr; caption->Release(); }
        HideWindow();
        if (font_) {
            font_->Release();
            font_ = nullptr;
            InvalidateSelf();
        }
        cGZWin::Shutdown();
    }
    return true;
}

void cGZWinGenTitleBar::SetCaption(const cIGZString& caption) {   // 0x10024d196
    cGZWin::SetCaption(caption);
    calc_display_caption();
}

bool cGZWinGenTitleBar::calc_display_caption() {   // 0x10024d1b2
    displayCaption_->Copy(GetCaption());
    if (!font_) {
        displayCaption_->Erase(0, 0xffffffffu);
        return false;
    }
    const std::uint32_t length = displayCaption_->Strlen();
    if (length < 2) return true;
    const int room = rightLimit_ - titleX_;
    if (room != 0 && titleX_ <= rightLimit_) {
        const std::uint32_t fit = font_->CalculateCharsToFitInWidth(displayCaption_->Data(), length, static_cast<std::uint32_t>(room), nullptr);
        if (fit != 0) {
            if (length <= fit) return true;
            cRZString ellipsis("...");
            if (IWinStyleManager* styles = WinStyleManager()) (void)styles, (void)kEllipsisStringId;
            const int ellipsisWidth = font_->GetStringWidth(ellipsis.Data(), ellipsis.Strlen());
            if (room - ellipsisWidth == 0 || room < ellipsisWidth) {
                displayCaption_->Erase(0, 0xffffffffu);
            } else {
                const std::uint32_t keep = font_->CalculateCharsToFitInWidth(displayCaption_->Data(), length, static_cast<std::uint32_t>(room - ellipsisWidth), nullptr);
                displayCaption_->Erase(keep, 0xffffffffu);
                displayCaption_->RTrim();
                displayCaption_->AppendChars(ellipsis.Data(), ellipsis.Strlen());
            }
            return true;
        }
    }
    displayCaption_->Erase(0, 0xffffffffu);
    return true;
}

void cGZWinGenTitleBar::SetArea(int l, int t, int r, int b) {   // 0x10024d44c
    const bool sameWidth = (r - l) == GetW();
    const int oldHeight = GetH();
    cGZWin::SetArea(l, t, r, b);
    if (sameWidth && (b - t) == oldHeight) return;
    place_controls();
}

bool cGZWinGenTitleBar::place_controls() {   // 0x10024d4de
    int x = GetW() - 4;
    for (cIGZWinBtn* button : {closeButton_, zoomButton_, minimizeButton_}) {
        if (!button) continue;
        cIGZWin* window = button->AsIGZWin();
        if (!window->GetFlag(kWinVisible)) continue;
        window->GZWinMoveTo(x - window->GetW(), (GetH() - window->GetH()) / 2);
        window->SetFlag(kWinFlag800, true);
        ChildAdd(window);
        window->SetFlag(kWinFlag800, false);
        x = window->GetL();
    }
    const int old = rightLimit_;
    rightLimit_ = x;
    if (0 < x && x != old) calc_display_caption();
    return true;
}

bool cGZWinGenTitleBar::GZPaint() {   // 0x10024d78e
    cGZWinFlatRect::GZPaint();
    if (font_) {
        const std::uint32_t color = (barFlags_ & 0x1000) ? textColor_ : kFontDefaultColor;
        font_->DrawTextLine(drawBuffer_, titleX_ + drawArea_.left, titleY_ + drawArea_.top, displayCaption_->Data(), displayCaption_->Strlen(),
                            color, kFontDefaultColor, nullptr, nullptr);
    }
    return true;
}

bool cGZWinGenTitleBar::AutoSize(const cRZRect& parentArea) {   // 0x10024d82a
    if (!font_) return false;
    int minW = 0, minH = 0;
    GetMinSize(minW, minH);
    cGZWinFlatRect::AutoSize(parentArea);
    if (GetH() < minH) SetH(minH);
    titleX_ = 4;
    const int spare = GetH() - font_->GetLineHeight();
    titleY_ = spare / 2;
    return true;
}

bool cGZWinGenTitleBar::GetMinSize(int& width, int& height) {   // 0x10024d8d8
    width = 0x80;
    height = 8;
    if (font_) {
        const int line = font_->GetLineHeight();
        if (height < line + 8) height = line + 8;
    }
    if (closeButton_) {
        const int buttonHeight = closeButton_->AsIGZWin()->GetH();
        if (height < buttonHeight + 2) height = buttonHeight + 2;
    }
    return true;
}

bool cGZWinGenTitleBar::GetDisplayCaption(cIGZString** out) const {   // 0x10024d958
    if (!displayCaption_) return false;
    *out = displayCaption_;
    displayCaption_->AddRef();
    return true;
}

bool cGZWinGenTitleBar::GetTitleFont(std::uint32_t iid, void** out) const {   // 0x10024d9c4
    return font_ && font_->QueryInterface(iid, out);
}

bool cGZWinGenTitleBar::SetTitleBarFlag(int flag, bool on) {   // 0x10024d9fc
    cIGZWinBtn* button = nullptr;
    if (flag < 0x100) {
        button = flag == 1 ? closeButton_ : flag == 2 ? minimizeButton_ : flag == 4 ? zoomButton_ : nullptr;
        if (flag == 1 || flag == 2 || flag == 4) {
            if (button) {
                button->AsIGZWin()->SetFlag(kWinVisible, on);
                place_controls();
            }
        }
    } else {
        button = flag == 0x100 ? closeButton_ : flag == 0x200 ? minimizeButton_ : flag == 0x400 ? zoomButton_ : nullptr;
        if ((flag == 0x100 || flag == 0x200 || flag == 0x400) && button) button->AsIGZWin()->SetFlag(kWinEnabled, !on);
    }
    barFlags_ = on ? (barFlags_ | static_cast<std::uint32_t>(flag)) : (barFlags_ & ~static_cast<std::uint32_t>(flag));
    return true;
}

// ============================================ cGZWinGen ==================================================

cGZWinGen::cGZWinGen(cIGZWinMgr* mgr) : cGZWin(mgr) {}   // 0x10024dbae

cGZWinGen::~cGZWinGen() {   // 0x10024dcea
    cGZWinGen::Shutdown();
    for (auto& entry : objects_) if (entry.second) entry.second->Release();
    objects_.clear();
    if (cursor_gen_) cursor_gen_->Release();
}

bool cGZWinGen::QueryInterface(std::uint32_t iid, void** out) {   // 0x10024ddc0
    if (iid == kGZIID_cIGZWinGen) {
        *out = static_cast<cIGZWinGen*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

bool cGZWinGen::Dispatch(std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) {
    if (!proc_) return false;
    cGZMessage message;
    message.type = type;
    message.param1 = p1;
    message.param2 = p2;
    message.param3 = p3;
    if (proc_->DoWinProcMessage(this, message)) return true;
    return proc_ && proc_->DoWinMsg(this, type, p1, p2, p3);
}

bool cGZWinGen::Init() {   // 0x10024de48
    if (get_init_flag()) return true;
    cGZWin::Init();
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (factory && !bg_) {
        if (cIGZBuffer* image = factory->Image(kBackgroundImageId)) SetBackgroundBitmap(kBGTiled, image);
    }
    if (!GetWinGenFlag(kGenDialog)) create_title_bar();
    EnumChildren(kGZIID_cIGZWinFlatRect, &enum_flat_rects, nullptr);
    calc_user_area(GetW(), GetH(), userArea_);
    if (IWinGenHost* host = WinGenHost()) host->AddLayoutListener(this);
    SetFocus();
    if (!proc_ || Dispatch(kWinProcCreated)) {
        on_layout();
        return true;
    }
    return false;
}

bool cGZWinGen::Shutdown() {   // 0x10024e74a
    if (!get_init_flag()) return true;
    AddRef();
    HideWindow();
    if (IWinGenHost* host = WinGenHost()) host->RemoveLayoutListener(this);
    if (titleBar_) {
        ChildRemove(titleBar_);
        titleBar_->Shutdown();
        titleBar_->Release();
        titleBar_ = nullptr;
    }
    if (bg_) { bg_->Release(); bg_ = nullptr; }
    if (proc_) Dispatch(kWinProcDestroyed);
    SetWinProc(nullptr);
    for (auto& entry : objects_) if (entry.second) entry.second->Release();
    objects_.clear();
    const bool result = cGZWin::Shutdown();
    Release();
    return result;
}

bool cGZWinGen::calc_user_area(int width, int height, cRZRect& out) {   // 0x10024e500
    out.left = gutterX_;
    if (genFlags_ & kGenSideBar) out.left = gutterX_ + 8 + gutterX_;
    out.right = width - gutterX_;
    int top = gutterY_;
    if (titleBar_) top += titleBar_->GetH();
    out.top = top;
    out.bottom = std::max(height - gutterY_, top);
    return true;
}

bool cGZWinGen::calc_win_area(const cRZRect& user, cRZRect& out) {   // 0x10024eb76
    out = user;
    out.left = user.left - gutterX_;
    if (genFlags_ & kGenSideBar) out.left = (user.left - gutterX_ - 8) - gutterX_;
    out.right = user.right + gutterX_;
    const int barHeight = titleBar_ ? titleBar_->GetH() + gutterY_ * 2 : gutterY_;
    out.top = user.top - barHeight;
    out.bottom = user.bottom + gutterY_;
    return true;
}

void cGZWinGen::ApplySizeLimits(cRZRect& user, const cRZRect& limits) {
    // limits = {min width, min height, max width, max height}
    const int width = user.right - user.left, height = user.bottom - user.top;
    if (width < limits.left) user.right = limits.left + user.left;
    else if (limits.right < width) user.right = limits.right + user.left;
    if (height < limits.top) user.bottom = limits.top + user.top;
    else if (limits.bottom < height) user.bottom = limits.bottom + user.top;
}

bool cGZWinGen::on_layout() {   // 0x10024e598
    if (!proc_) return false;
    cRZRect limits{0, 0, 0x7fffffff, 0x7fffffff};
    cRZRect user = userArea_;
    if (Dispatch(kWinProcSizeLimits, reinterpret_cast<std::uint64_t>(&limits), reinterpret_cast<std::uint64_t>(&user))) {
        ApplySizeLimits(user, limits);
        cRZRect window;
        calc_win_area(user, window);
        if (window.right - window.left != GetW() || window.bottom - window.top != GetH()) {
            SetSize(window.right - window.left, window.bottom - window.top);
            return true;
        }
    }
    Dispatch(kWinProcLaidOut, reinterpret_cast<std::uint64_t>(&userArea_), reinterpret_cast<std::uint64_t>(&GetArea()));
    return true;
}

bool cGZWinGen::enum_flat_rects(cIGZWin* parent, std::uint32_t, void* child, void*) {   // 0x10025062e
    auto* rect = static_cast<cIGZWinFlatRect*>(child);
    rect->AutoSize(cRZRect{0, 0, parent->GetW(), parent->GetH()});
    return true;
}

void cGZWinGen::SetArea(int l, int t, int r, int b) {   // 0x10024e902
    int width = std::max(r - l, minW_);
    int height = std::max(b - t, minH_);
    if (width == GetW() && height == GetH()) {
        cGZWin::SetArea(l, t, l + width, t + height);
    } else {
        if (proc_) {
            cRZRect user;
            calc_user_area(width, height, user);
            cRZRect limits{0, 0, 0x7fffffff, 0x7fffffff};
            if (Dispatch(kWinProcSizeLimits, reinterpret_cast<std::uint64_t>(&limits), reinterpret_cast<std::uint64_t>(&user))) {
                ApplySizeLimits(user, limits);
                cRZRect window;
                calc_win_area(user, window);
                width = window.right - window.left;
                height = window.bottom - window.top;
            }
        }
        cGZWin::SetArea(l, t, l + width, t + height);
        EnumChildren(kGZIID_cIGZWinFlatRect, &enum_flat_rects, nullptr);
        calc_user_area(width, height, userArea_);
    }
    if (proc_) Dispatch(kWinProcLaidOut, reinterpret_cast<std::uint64_t>(&userArea_), reinterpret_cast<std::uint64_t>(&GetArea()));
}

void cGZWinGen::SetCaption(const cIGZString& caption) {   // 0x10024ec0c
    cGZWin::SetCaption(caption);
    if (titleBar_) titleBar_->SetCaption(caption);
}

bool cGZWinGen::IsPointInWindowScreenCoordinates(int x, int y) const {   // 0x10024ec42
    int wx = x, wy = y;
    ScreenToWindowCoordinates(wx, wy);
    return IsPointInWindowWindowCoordinates(wx, wy);
}

bool cGZWinGen::IsPointInWindowWindowCoordinates(int x, int y) const {   // 0x10024ec8a
    if (proc_) {
        bool inside = false;
        auto* self = const_cast<cGZWinGen*>(this);
        if (self->Dispatch(kWinProcHitTest, (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 16) | (static_cast<std::uint32_t>(y) & 0xffff),
                           reinterpret_cast<std::uint64_t>(&inside)))
            return inside;
    }
    return cGZWin::IsPointInWindowWindowCoordinates(x, y);
}

bool cGZWinGen::GZPaint() {   // 0x10024ed30
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    if (static_cast<std::int16_t>(genFlags_) < 0) return true;
    bool handled = false;
    if (proc_) handled = Dispatch(kWinProcPaintBefore, reinterpret_cast<std::uint64_t>(&drawArea_));
    if (!handled) {
        bool outline = true;
        if (!bg_) {
            if (genFlags_ & kGenFlatFill) {
                if (!(genFlags_ & kGenNoBackground)) {
                    context->SetColor(GetFillColorRGB());
                    context->Fill(drawArea_);
                }
                outline = false;
            } else if (!(genFlags_ & kGenNoBackground)) {
                RZDrawRaisedRect(context, drawArea_, GetFillColorRGB(), true, true);
                outline = false;
            }
        } else {
            if (!(genFlags_ & kGenNoBackground)) {
                cRZRect bounds;
                bg_->GetBufferArea(bounds);
                if (bgType_ == kBGEdges) {
                    const cRZRect tile{0, 0, (bounds.right - bounds.left) / 3, (bounds.bottom - bounds.top) / 3};
                    RZDrawBltEdge(context, bg_, tile, drawArea_, true);
                } else if (bgType_ == kBGStretched) {
                    context->Blt(reinterpret_cast<IDrawTargetBuffer*>(bg_), bounds, drawArea_);
                } else if (bgType_ == kBGTiled) {
                    context->BltTiled(reinterpret_cast<IDrawTargetBuffer*>(bg_), bounds, drawArea_, 0, 0);
                }
            }
            if (genFlags_ & kGenFlatFill) outline = false;
        }
        if (outline) RZDraw3DRectangleOutline(context, drawArea_, 0xffffffff, 0xff000000);
    }
    if (proc_) {
        Dispatch(kWinProcPaintAfter, reinterpret_cast<std::uint64_t>(&drawArea_));
        Dispatch(kWinProcPaintDone, reinterpret_cast<std::uint64_t>(&drawArea_));
    }
    return true;
}

bool cGZWinGen::DoMessage(cGZMessage& message) {   // 0x10024f01c
    if (message.type == kWinProcLayoutRequest) {
        if (message.param1 == GetID()) {
            on_layout();
            return true;
        }
        return false;
    }
    if (message.type - 7u < 8u) {   // mouse messages
        if (proc_) {
            AddRef();
            if (!proc_->DoWinProcMessage(this, message) && proc_) proc_->DoWinMsg(this, message.type, message.param1, message.param2, message.param3);
            Release();   // only the reference taken above: without a proc the original skips both (0x10024f05b -> 0x10024f115)
        }
        return cGZWin::DoMessage(message);
    }
    if (proc_) {
        AddRef();
        const bool handled = proc_->DoWinProcMessage(this, message) || (proc_ && proc_->DoWinMsg(this, message.type, message.param1, message.param2, message.param3));
        if (handled) {
            Release();
            return true;
        }
        Release();
    }
    return cGZWin::DoMessage(message);
}

bool cGZWinGen::kill_self(int code) {   // 0x10024f1da
    if (GetWinGenFlag(kGenDialog)) {
        GetWindowManager()->EndDialog(0xf, this, code);
    } else {
        PostMsg(parent_, kGZMsgDeleteChild, reinterpret_cast<std::uint64_t>(static_cast<cIGZWin*>(this)), 0, 0);
    }
    return true;
}

bool cGZWinGen::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // 0x10024f14e
    if ((genFlags_ & kGenEscapeCloses) && (key == 0x1b || (key == 0x73 && (modifiers & 4)))) {
        kill_self(0x42b7c353);
        return true;
    }
    if (!GetWinGenFlag(kGenDialog)) {
        SendMsg(notify_, kGZMsgKeyDown, key, modifiers, 0);
        return true;
    }
    return false;
}

bool cGZWinGen::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // 0x10024f260
    if (GetWinGenFlag(kGenDialog)) return false;
    SendMsg(notify_, kGZMsgKeyUp, key, modifiers, 0);
    return true;
}

int cGZWinGen::calc_size_mode(int x, int y) {   // 0x10024f314
    if (userArea_.left <= x && userArea_.top <= y && x <= userArea_.right && y <= userArea_.bottom) return 0;
    int mode = 0;
    if (genFlags_ & kGenResizable) {
        if (x < gutterX_) {
            mode = 6;
            if (y >= gutterY_) {
                const int limit = GetH() - gutterY_;
                mode = limit - y >= 0 ? 2 : 8;
            }
        } else if (x <= GetW() - gutterX_) {
            mode = 4;
            if (gutterY_ <= y) mode = (GetH() - gutterY_ < y) ? 5 : 0;
        } else {
            mode = 7;
            if (y >= gutterY_) {
                const int limit = GetH() - gutterY_;
                mode = limit - y >= 0 ? 3 : 9;
            }
        }
    }
    int result = mode;
    if (genFlags_ & kGenMovable) {
        if (!titleBar_) {
            result = 1;
        } else {
            const cRZRect& bar = titleBar_->GetArea();
            // Inside the title bar's rows the mouse moves the window; below it the edge mode applies.
            if (bar.left <= x && bar.top <= y && x <= bar.right) result = bar.bottom < y ? mode : 1;
        }
    }
    return result;
}

bool cGZWinGen::GZOnMouseDownL(int x, int y, std::uint32_t) {   // 0x10024f2be
    sizeMode_ = calc_size_mode(x, y);
    if (sizeMode_ != 0) {
        GZSetCapture();
        if (!GZAmCapturing()) {
            sizeMode_ = 0;
        } else {
            mouseDownX_ = x;
            mouseDownY_ = y;
        }
    }
    return true;
}

bool cGZWinGen::GZOnMouseUpL(int, int, std::uint32_t) {   // 0x10024f47c
    const bool dragging = sizeMode_ != 0;
    if (dragging) {
        sizeMode_ = 0;
        GZReleaseCapture();
    }
    return dragging;
}

bool cGZWinGen::GZOnMouseMove(int x, int y, std::uint32_t) {   // 0x10024f4a0
    if (sizeMode_ == 0) {
        const int mode = calc_size_mode(x, y);
        if ((genFlags_ & 3) != 3 || cursorMode_ == mode) return true;
        cIGZCursorManager* cursors = GetCursorManager();
        if (!cursors) return true;
        cursorMode_ = mode;
        cIGZCursor* cursor = nullptr;
        std::uint32_t id = 1;
        switch (mode) {
            case 1: id = 1; break;
            case 2: case 3: id = 10; break;
            case 4: case 5: id = 8; break;
            case 6: case 9: id = 9; break;
            case 7: case 8: id = 7; break;
            default: cursor = cursor_gen_; break;
        }
        if (!cursor) {
            cursor = cursors->LoadCursor(id);
            if (!cursor) cursor = cursors->LoadCursor(0x11);
            if (!cursor) return true;
        }
        cGZWin::SetCursor(cursor, true);
        return true;
    }
    const int dx = x - mouseDownX_, dy = y - mouseDownY_;
    cRZRect area = GetArea();
    switch (sizeMode_) {
        case 1: area.left += dx; area.right += dx; area.top += dy; area.bottom += dy; break;
        case 2: area.left += dx; break;
        case 3: area.right += dx; mouseDownX_ += dx; break;
        case 4: area.top += dy; break;
        case 5: area.bottom += dy; mouseDownY_ += dy; break;
        case 6: area.left += dx; area.top += dy; break;
        case 7: area.right += dx; area.top += dy; mouseDownX_ += dx; break;
        case 8: area.left += dx; area.bottom += dy; mouseDownY_ += dy; break;
        case 9: area.right += dx; area.bottom += dy; mouseDownX_ += dx; mouseDownY_ += dy; break;
        default: return true;
    }
    cGZWin::SetArea(area);
    if (sizeMode_ == 1) InvalidateAllParents();
    else InvalidateSelfAndParents();
    return true;
}

bool cGZWinGen::GZOnCommand(std::uint32_t sender, std::uint64_t id) {   // 0x10024f6c8
    if (sender == kWinCommandSender && (id == kWinGenCloseCommand || id == kWinGenCloseId)) {
        kill_self(static_cast<int>(id));
        return true;
    }
    return false;
}

void cGZWinGen::SetCursor(cIGZCursor* cursor, bool updateNow) {   // 0x1002505c0
    if (cursor_gen_ != cursor) {
        if (cursor) cursor->AddRef();
        cIGZCursor* old = cursor_gen_;
        cursor_gen_ = cursor;
        if (old) old->Release();
    }
    if (cursorMode_ != 0) return;
    cGZWin::SetCursor(cursor, updateNow);
}

bool cGZWinGen::SetWinProc(cIGZWinProc* proc) {   // 0x10024fb76
    if (proc_ != proc) {
        if (proc_) {
            Dispatch(kWinProcShutdown);
            proc_->Release();
        }
        proc_ = proc;
        if (proc) {
            proc->AddRef();
            Dispatch(kWinProcInit);
            Dispatch(kWinProcLayoutRequest);
        }
    }
    return true;
}

bool cGZWinGen::SetBackgroundBitmap(BGBltType type, cIGZBuffer* image) {   // 0x10024fd26
    if (bg_ != image || bgType_ != type) {
        if (image) image->AddRef();
        if (bg_) bg_->Release();
        bg_ = image;
        bgType_ = type;
    }
    return true;
}

bool cGZWinGen::SetBackgroundBitmap(cIGZBuffer* image) {   // 0x10024fd9e
    if (bg_ != image) {
        if (image) image->AddRef();
        if (bg_) bg_->Release();
        bg_ = image;
    }
    return true;
}

bool cGZWinGen::GetBackgroundBitmap(cIGZBuffer** out) const {   // 0x10024fe52
    if (out) {
        *out = bg_;
        bg_->AddRef();
    }
    return true;
}

void cGZWinGen::SetUserData(std::uint32_t id, cIGZUnknown* object) {   // 0x10024ff8c
    RemoveUserData(id);
    if (object) {
        object->AddRef();
        objects_[id] = object;
    }
}

bool cGZWinGen::GetUserData(std::uint32_t id, std::uint32_t iid, void** out) {   // 0x10025010e
    auto it = objects_.find(id);
    if (it == objects_.end() || !it->second) return false;
    return it->second->QueryInterface(iid, out);
}

bool cGZWinGen::create_title_bar() {   // 0x10024e066
    if (titleBar_) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    std::uint32_t fill = 0, outline = 0;
    if (factory) {
        fill = factory->DefaultColor(static_cast<int>(kTitleFillColorId));
        outline = factory->DefaultColor(static_cast<int>(kTitleOutlineColorId));
    }
    titleBar_ = new cGZWinGenTitleBar(GetWindowManager());
    titleBar_->AddRef();
    titleBar_->SetID(kWinGenTitleBarId);
    titleBar_->GZWinMoveTo(gutterX_, gutterY_);
    titleBar_->SetFlag(kWinFlag800, false);
    for (std::uint32_t flag : {kGenCloseButton, kGenMinimizeButton, kGenZoomButton, kGenCloseDisabled, kGenZoomDisabled, kGenMinimizeDisabled})
        SetWinGenFlag(static_cast<int>(flag), GetWinGenFlag(static_cast<int>(flag)));
    titleBar_->SetOutlineFlags(0xf);
    titleBar_->SetSizeMode(5);
    titleBar_->SetFillColor(fill);
    titleBar_->SetOutlineColor(outline);
    if (!ChildAdd(titleBar_)) {
        titleBar_->Release();
        titleBar_ = nullptr;
        return false;
    }
    SetCaption(GetCaption());
    titleBar_->AutoSize(cRZRect{0, 0, GetW(), GetH()});
    int barW = 0, barH = 0;
    titleBar_->GetMinSize(barW, barH);
    minW_ = barW + gutterX_ * 2;
    minH_ = barH + gutterY_ * 2;
    if (GetW() < minW_ || GetH() < minH_) SetSize(std::max(minW_, GetW()), std::max(minH_, GetH()));
    if (cIGZWin* side = GetChildWindowFromID(kWinGenSideBarId)) {
        side->SetFlag(kWinFlag800, true);
        ChildToFront(side);
        side->SetFlag(kWinFlag800, false);
        side->SetArea(gutterX_, titleBar_->GetB() - 1, gutterX_ + 8, GetH() - gutterY_);
    }
    return true;
}

bool cGZWinGen::destroy_title_bar() {   // 0x10024f898
    if (titleBar_) {
        ChildRemove(titleBar_);
        titleBar_->Release();
        titleBar_ = nullptr;
        minW_ = gutterX_ * 2;
        minH_ = gutterY_ * 2;
        if (cIGZWin* side = GetChildWindowFromID(kWinGenSideBarId))
            side->SetArea(gutterX_, gutterY_, gutterX_ + 8, GetH() - gutterY_);
    }
    return true;
}

bool cGZWinGen::create_side_bar() {   // 0x10024f96c
    if (ChildExists(kWinGenSideBarId)) return true;
    const int top = titleBar_ ? titleBar_->GetB() - 1 : gutterY_;
    const cRZRect area{gutterX_, top, gutterX_ + 8, GetH() - gutterY_};
    if (!AddColorBar(static_cast<int>(kWinGenSideBarId), 4, 0xd, area)) return false;
    if (cIGZWin* side = GetChildWindowFromID(kWinGenSideBarId)) {
        side->SetFlag(kWinFlag800, true);
        ChildToFront(side);
        side->SetFlag(kWinFlag800, false);
        if (titleBar_) {
            titleBar_->SetFlag(kWinFlag800, true);
            ChildToFront(titleBar_);
            titleBar_->SetFlag(kWinFlag800, false);
        }
    }
    return true;
}

bool cGZWinGen::SetWinGenFlag(int flag, bool on) {   // 0x10024f71c
    genFlags_ = on ? (genFlags_ | static_cast<std::uint32_t>(flag)) : (genFlags_ & ~static_cast<std::uint32_t>(flag));
    if (!titleBar_) {
        if (flag != static_cast<int>(kGenSideBar) && flag != static_cast<int>(kGenDialog)) return true;
    } else if (flag != static_cast<int>(kGenSideBar) && flag != static_cast<int>(kGenDialog)) {
        int barFlag;
        switch (flag) {
            case 0x10: barFlag = 1; break;
            case 0x20: barFlag = 4; break;
            case 0x40: barFlag = 2; break;
            case 0x100: barFlag = 0x100; break;
            case 0x200: barFlag = 0x400; break;
            case 0x400: barFlag = 0x200; break;
            default: return true;
        }
        titleBar_->SetTitleBarFlag(barFlag, on);
        return true;
    }
    if (flag == static_cast<int>(kGenSideBar)) {
        if (on) create_side_bar();
        else if (cIGZWin* side = GetChildWindowFromID(kWinGenSideBarId)) ChildDelete(side);
    } else {
        if (on) destroy_title_bar();
        else create_title_bar();
    }
    calc_user_area(GetW(), GetH(), userArea_);
    on_layout();
    return true;
}

bool cGZWinGen::AddColorBar(int id, int sizeMode, int outlineFlags, const cRZRect& area) {   // 0x1002502b2
    if (ChildExists(static_cast<std::uint32_t>(id))) return false;
    IWinCtrlFactory* factory = WinCtrlFactory();
    void* raw = nullptr;
    // A class that cannot be created is reported as success.
    if (!factory || !factory->CreateCtrl(0xc2afa76e, kGZIID_cIGZWinFlatRect, &raw)) return true;
    auto* rect = static_cast<cIGZWinFlatRect*>(raw);
    cIGZWin* window = rect->AsIGZWin();
    window->SetID(static_cast<std::uint32_t>(id));
    window->SetArea(area.left, area.top, area.right, area.bottom);
    const std::uint32_t fill = factory->DefaultColor(static_cast<int>(kTitleFillColorId));
    const std::uint32_t outline = factory->DefaultColor(static_cast<int>(kTitleOutlineColorId));
    window->SetFlag(kWinFlag800, false);
    window->SetFillColor(fill);
    rect->SetOutlineColor(outline);
    rect->SetSizeMode(sizeMode);
    rect->SetOutlineFlags(outlineFlags);
    if (!ChildAdd(window)) {
        GetWindowManager()->DestroyWindow(window);
        rect->Release();
        return false;
    }
    rect->Release();
    return true;
}

void cGZWinGen::AddDividerH(int id, int y) {   // 0x1002504b0
    int left = (genFlags_ & kGenSideBar) ? gutterX_ + 8 : gutterX_;
    cRZRect area{left - 1, y, GetW() - gutterX_, y + 8};
    AddColorBar(id, 3, 0xe, area);
}

bool cGZWinGen::RemoveColorBar(int id) {   // 0x10025054a
    cIGZWin* bar = GetChildWindowFromID(static_cast<std::uint32_t>(id));
    if (bar) ChildDelete(bar);
    return bar != nullptr;
}

}  // namespace gz
