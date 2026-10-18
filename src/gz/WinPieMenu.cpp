#include "gz/WinPieMenu.h"

#include <algorithm>
#include <cmath>

#include "gz/DrawContext.h"
#include "gz/WinBtn.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinStyle.h"

namespace gz {

namespace {
constexpr int kSpokeTable[9] = {1, 2, 2, 4, 4, 8, 8, 8, 8};   // spokes by item count (table at 0x100a23a70)
}  // namespace

cGZWinPieMenu::cGZWinPieMenu(cIGZWinMgr* mgr) : cGZWin(mgr) {}   // 0x1005165f0

cGZWinPieMenu::~cGZWinPieMenu() {   // 0x100516746
    if (ownerDraw_) { ownerDraw_->Release(); ownerDraw_ = nullptr; }
    for (PieMenuItem& item : items_) if (item.button) item.button->Release();
    items_.clear();
    if (background_) background_->Release();
    if (font_) font_->Release();
}

bool cGZWinPieMenu::QueryInterface(std::uint32_t iid, void** out) {   // 0x1005168b2
    if (iid == kGZIID_cIGZWinPieMenu) {
        *out = static_cast<cIGZWinPieMenu*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

bool cGZWinPieMenu::Init() {   // 0x10051693a
    if (!get_init_flag()) {
        if (!font_) {
            if (IWinStyleManager* styles = WinStyleManager()) SetFont(static_cast<cIGZFont*>(styles->GetFontStyle(kPieDefaultFontStyle)));
        }
        if (lowlight_ == 0) lowlight_ = MakeFillColor(0xb9, 0xb9, 0xd0);
        if (highlight_ == 0) highlight_ = MakeFillColor(0, 0xff, 0xff);
        if (selected_ == 0) selected_ = MakeFillColor(0xff, 0xff, 0xff);
        if (GetFillColorRGB() == 0) cGZWin::SetFillColor(MakeFillColor(0x76, 0x76, 0x76));
        if (background_) {
            if (IWinButtonHost* host = WinButtonHost()) {
                cIGZBuffer* image = host->Image(kPieDefaultBackground);
                if (image != background_) {
                    if (image) image->AddRef();
                    cIGZBuffer* old = background_;
                    background_ = image;
                    if (old) old->Release();
                }
            }
        }
    }
    return cGZWin::Init();
}

bool cGZWinPieMenu::Shutdown() {   // 0x100516afa
    if (background_) { cIGZBuffer* b = background_; background_ = nullptr; b->Release(); }
    if (font_) { cIGZFont* f = font_; font_ = nullptr; f->Release(); }
    return cGZWin::Shutdown();
}

void cGZWinPieMenu::SetFont(cIGZFont* font) {   // 0x100516b68
    cIGZFont* old = font_;
    if (old == font) return;
    if (font) font->AddRef();
    font_ = font;
    if (old) old->Release();
}

void cGZWinPieMenu::SetPopupBackgroundBuffer(cIGZBuffer* buffer) {   // 0x100516c22
    cIGZBuffer* old = background_;
    if (old == buffer) return;
    if (buffer) buffer->AddRef();
    background_ = buffer;
    if (old) old->Release();
}

bool cGZWinPieMenu::SetOwnerDraw(cIGZWinPieMenuOwnerDraw* owner) {   // 0x100518f74
    if (ownerDraw_) ownerDraw_->Release();
    ownerDraw_ = owner;
    if (owner) owner->AddRef();
    return true;
}

int cGZWinPieMenu::GetCurItemCenterX() const {   // 0x100516f0e
    if (items_.empty() || selection_ < 0) return 0;
    const PieMenuItem& item = items_[static_cast<std::size_t>(selection_)];
    return item.w / 2 + item.x;
}

int cGZWinPieMenu::GetCurItemCenterY() const {   // 0x100516f7e
    if (items_.empty() || selection_ < 0) return 0;
    const PieMenuItem& item = items_[static_cast<std::size_t>(selection_)];
    return item.h / 2 + item.y;
}

void cGZWinPieMenu::Clear() {   // 0x1005180a6
    for (PieMenuItem& item : items_) if (item.button) item.button->Release();
    items_.clear();
    selection_ = -1;
}

bool cGZWinPieMenu::InsertString(const cIGZString& text, cIGZWinBtn* button) {   // 0x1005181b0
    PieMenuItem item;
    item.text.Copy(text);
    item.button = button;
    if (button) button->AddRef();
    items_.push_back(item);
    return true;
}

void cGZWinPieMenu::EnableItem(int index, bool enabled) {   // 0x100518924
    if (index >= 0 && index < GetItemCount()) items_[static_cast<std::size_t>(index)].enabled = enabled;
}

bool cGZWinPieMenu::Layout() {   // 0x100517048
    Init();
    titleOriginX_ = 0;
    titleOriginY_ = 0;
    int bgW = 0, bgH = 0;
    if (background_) {
        cRZRect bounds;
        background_->GetBufferArea(bounds);
        bgW = bounds.right - bounds.left;
        bgH = bounds.bottom - bounds.top;
    }
    backgroundW_ = bgW;
    backgroundH_ = bgH;
    int left = -(bgW / 2), top = -(bgH / 2);
    titleOriginX_ = left;
    titleOriginY_ = top;
    if (inactiveRadius_ == 0) {
        if (!background_) inactiveRadius_ = 8;
        else inactiveRadius_ = std::max(1, std::max(bgW, bgH) / 2);
    }
    const int spoke = static_cast<int>(spokeRadius_);
    int right = bgW + left + spoke;
    int minY = top - spoke;
    int bottom = bgH + top + spoke;

    // The title box.
    cRZRect box;
    if (!(ownerDraw_ && ownerDraw_->MeasureTitle(this, &title_, box))) {
        font_->TextArea(title_.Data(), title_.Strlen(), box);
        box.left -= 3; box.top -= 3; box.right += 3; box.bottom += 3;
    }
    titleW_ = box.right - box.left;
    titleH_ = box.bottom - box.top;
    titleX_ = -(titleW_ / 2);
    titleY_ = -(titleH_ / 2);
    int minX = left - spoke;
    if (titleX_ <= minX) minX = titleX_;
    if (-minY == titleH_ / 2 || titleY_ < minY) minY = titleY_;
    int maxX = titleW_ - titleW_ / 2;
    if (maxX < right) maxX = right;
    int maxY = titleH_ - titleH_ / 2;
    if (maxY < bottom) maxY = bottom;

    const std::size_t count = items_.size();
    spokes_ = count < 9 ? kSpokeTable[count] : 8;
    for (std::size_t i = 0; i < count; ++i) {
        PieMenuItem& item = items_[i];
        cRZRect size;
        if (item.button) {
            size = cRZRect{0, 0, item.button->AsIGZWin()->GetW(), item.button->AsIGZWin()->GetH()};
        } else if (!(ownerDraw_ && ownerDraw_->MeasureItem(this, static_cast<int>(i), &item, size))) {
            font_->TextArea(item.text.Data(), item.text.Strlen(), size);
            size.left -= 3; size.top -= 3; size.right += 3; size.bottom += 3;
        }
        const int w = size.right - size.left;
        const int h = size.bottom - size.top;
        int x, y;
        float angle;
        const std::uint32_t index = static_cast<std::uint32_t>(i);
        if (index >= static_cast<std::uint32_t>(spokes_)) {
            const std::uint32_t ring = index - static_cast<std::uint32_t>(spokes_);
            if ((ring & 1) == 0) {
                const std::uint32_t reference = ring == 0 ? (firstDirection_ & 7) : index - 2;
                y = items_[reference].y - h;
                angle = 9.424778f;
            } else {
                const std::uint32_t reference = ring == 1 ? ((firstDirection_ + 4) & 7) : index - 2;
                y = items_[reference].y + items_[reference].h;
                angle = 3.1415927f;
            }
            x = -(w / 2);
        } else {
            std::uint32_t slot = index;
            if (spokes_ != 8) {
                if (spokes_ == 4) slot = index * 2;
                else slot = spokes_ == 2 ? index * 4 : 0;
            }
            angle = static_cast<float>(static_cast<int>(2 - ((slot + firstDirection_) & 7))) * 0.7853982f;
            while (angle < 0.0f) angle += 6.2831855f;
            switch ((slot + firstDirection_) & 7) {
                case 1: x = radiusY_; y = -(radiusY_ + h); break;
                case 2: x = radiusX_; y = -(h / 2); break;
                case 3: x = radiusY_; y = radiusY_; break;
                case 4: x = -(w / 2); y = radiusX_; break;
                case 5: x = -(radiusY_ + w); y = radiusY_; break;
                case 6: x = -(radiusX_ + w); y = -(h / 2); break;
                case 7: x = -radiusY_ - w; y = -radiusY_ - h; break;
                default: x = -(w / 2); y = -(radiusX_ + h); break;
            }
        }
        item.x = x;
        item.y = y;
        item.w = w;
        item.h = h;
        item.angle = angle;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x + w);
        maxY = std::max(maxY, y + h);
    }
    centerX_ = 4 - minX;
    centerY_ = 4 - minY;
    windowW_ = (maxX + 8) - minX;
    windowH_ = (maxY + 8) - minY;
    titleOriginX_ += centerX_;
    titleOriginY_ += centerY_;
    titleX_ += centerX_;
    titleY_ += centerY_;
    for (PieMenuItem& item : items_) {
        item.x += centerX_;
        item.y += centerY_;
    }
    InvalidateSelf();
    return true;
}

bool cGZWinPieMenu::ReLayout() {   // 0x1005176c8
    const bool result = Layout();
    popupX_ = downX_ - centerX_;
    popupY_ = downY_ - centerY_;
    SetArea(popupX_, popupY_, windowW_ + popupX_, windowH_ + popupY_);
    return result;
}

int cGZWinPieMenu::CalcItem(int x, int y) {   // 0x100518744
    const int count = GetItemCount();
    if (count == 0) return -1;
    const int dx = (popupX_ + x) - downX_;
    const int dy = (popupY_ + y) - downY_;
    distance_ = std::sqrt(static_cast<float>(dy * dy + dx * dx));
    if (distance_ < 1.0f) return -1;
    const float angle = std::atan2(static_cast<float>(dx), static_cast<float>(-dy));
    direction_ = angle;
    for (int i = 0; i < count; ++i) {
        const PieMenuItem& item = items_[static_cast<std::size_t>(i)];
        if (item.enabled && item.x <= x && x < item.x + item.w && item.y <= y && y < item.y + item.h) return i;
    }
    if (distance_ <= static_cast<float>(inactiveRadius_)) {
        direction_ = 0;
        return -1;
    }
    if (maxRadius_ == 0 || distance_ <= static_cast<float>(static_cast<std::uint32_t>(maxRadius_))) {
        const float spokes = static_cast<float>(spokes_);
        float position = (angle + static_cast<float>(static_cast<int>(firstDirection_)) * -0.7853982f) * (spokes / 6.2831855f) + 0.5f;
        while (spokes <= position) position -= spokes;
        while (position < 0.0f) position += spokes;
        const std::uint32_t index = static_cast<std::uint32_t>(position);
        if (index != 0xffffffffu && static_cast<int>(index) < count)
            return items_[index].enabled ? static_cast<int>(index) : -1;
    }
    return -1;
}

bool cGZWinPieMenu::SetSelection(int index, bool commit) {   // 0x100518528
    if (index != -1) {
        const int count = GetItemCount();
        if (index < 0 || count <= index) {
            const int low = std::max(0, index);
            index = low < count ? low : count - 1;
        }
    }
    if (index != selection_) selection_ = index;
    Send(kPieMsgSelectionChanged, reinterpret_cast<std::uint64_t>(this));
    if (commit) {
        Send(selection_ == -1 ? kPieMsgCancelled : kPieMsgChosen, reinterpret_cast<std::uint64_t>(this));
        if (endMode_ != 0) EndPopup();
    } else {
        InvalidateSelf();
    }
    return true;
}

void cGZWinPieMenu::EndPopup(int selection) {   // 0x100518646
    if (eventLoop_) {
        GetWindowManager()->EndDialog(0xf, this, selection);
        poppedUp_ = false;
        active_ = false;
        return;
    }
    PopupEnd();
}

int cGZWinPieMenu::Popup(int x, int y, int clickMode, int endMode) {   // 0x100518a0a
    PopupBegin(x, y, clickMode, endMode);
    const int result = PopupEventLoop();
    PopupEnd();
    return result;
}

void cGZWinPieMenu::PopupBegin(int x, int y, int clickMode, int endMode) {   // 0x100518a96
    eventLoop_ = 0;
    cIGZWinMgr* mgr = GetWindowManager();
    if (cIGZWin* capture = mgr->GZGetCapture()) mgr->GZReleaseCapture(capture);
    GZSetCapture();
    BeginPopupWindow(x, y, clickMode, endMode);
    SetSelection(selection_, false);
}

int cGZWinPieMenu::PopupEventLoop() {   // 0x100518b34
    eventLoop_ = 1;
    return GetWindowManager()->RunPopup(this);
}

void cGZWinPieMenu::PopupEnd() {   // 0x100518b80
    if (-1 < endMode_) EndPopupWindow();
}

void cGZWinPieMenu::BeginPopupWindow(int x, int y, int clickMode, int endMode) {   // 0x100518bba
    poppedUp_ = true;
    active_ = true;
    downX_ = x;
    downY_ = y;
    clickMode_ = clickMode != 0;
    endMode_ = endMode;
    moved_ = false;
    mouseDown_ = clickMode != 0;
    Layout();
    cGZWin::SetFlag(kWinVisible, true);
    cIGZWin* parent = GetParentWin();
    const int parentW = parent->GetW(), parentH = parent->GetH();
    popupX_ = downX_ - centerX_;
    popupY_ = downY_ - centerY_;
    if (popupX_ < 0) { popupX_ = 0; moved_ = true; }
    else if (parentW <= windowW_ + popupX_) { popupX_ = parentW - windowW_; moved_ = true; }
    if (popupY_ < 0) { popupY_ = 0; moved_ = true; }
    else if (parentH <= windowH_ + popupY_) { popupY_ = parentH - windowH_; moved_ = true; }
    if (moved_) {
        downX_ = centerX_ + popupX_;
        downY_ = centerY_ + popupY_;
    }
    SetArea(popupX_, popupY_, windowW_ + popupX_, windowH_ + popupY_);
    for (PieMenuItem& item : items_) {
        if (!item.button) continue;
        cIGZWin* window = item.button->AsIGZWin();
        const int bx = item.x + popupX_;
        const int by = item.y + popupY_;
        window->SetArea(bx, by, bx + window->GetW(), by + window->GetH());
        window->SetFlag(kWinEnabled, item.enabled);
        parent->ChildAdd(window);
    }
    selection_ = -1;
    int mx = 0, my = 0;
    GetWindowManager()->GetMousePosition(mx, my);
    GZOnMouseMove(mx - popupX_, my - popupY_, 0);
}

void cGZWinPieMenu::EndPopupWindow() {   // 0x100518ea0
    cIGZWin* parent = GetParentWin();
    for (PieMenuItem& item : items_)
        if (item.button) parent->ChildRemove(item.button->AsIGZWin());
    poppedUp_ = false;
}

bool cGZWinPieMenu::GZOnCharacter(char c) {   // 0x10051827e
    if (notify_) SendMsg(notify_, kGZMsgCommand, kPieMsgCharacter, static_cast<std::uint64_t>(static_cast<int>(c)), 0);
    return cGZWin::GZOnCharacter(c);
}

bool cGZWinPieMenu::GZOnKeyDown(std::uint32_t key, std::uint32_t) {   // 0x1005182c4
    if (key == 0x1b) {
        selection_ = -1;
        if (notify_) SendMsg(notify_, kGZMsgCommand, kPieMsgCancelled, 0x1b, 0);
        EndPopup();
        return true;
    }
    if (!notify_) return false;
    SendMsg(notify_, kGZMsgCommand, kPieMsgKeyDown, key, 0);
    return false;
}

bool cGZWinPieMenu::GZOnMouseDownL(int x, int y, std::uint32_t modifiers) {   // 0x10051834e
    mouseDown_ = false;
    if (!clickMode_ || abbreviated_ == 0) {
        GZOnMouseMove(x, y, modifiers);
        clickMode_ = true;
    } else {
        GZOnMouseUpL(x, y, modifiers);
    }
    return true;
}

bool cGZWinPieMenu::GZOnMouseUpL(int x, int y, std::uint32_t modifiers) {   // 0x10051839a
    mouseDown_ = false;
    GZOnMouseMove(x, y, modifiers);
    if (active_) {
        if (clickMode_ || (!moved_ && static_cast<float>(inactiveRadius_) < distance_)) SetSelection(selection_, true);
        else clickMode_ = true;
    }
    moved_ = false;
    return true;
}

bool cGZWinPieMenu::GZOnMouseMove(int x, int y, std::uint32_t) {   // 0x10051842a
    if (!active_) return true;
    if (moved_) {
        int mx = 0, my = 0;
        GetWindowManager()->GetMousePosition(mx, my);
        x = mx - popupX_;
        y = my - popupY_;
    }
    const int old = selection_;
    selection_ = CalcItem(x, y);
    if (selection_ != old) SetSelection(selection_, false);
    if (autoCancel_ != 0) {
        if (static_cast<std::uint32_t>(maxRadius_) != 0 && selection_ == -1) {
            if (static_cast<float>(static_cast<std::uint32_t>(maxRadius_)) < distance_) SetSelection(-1, true);
        }
    }
    if (notify_) SendMsg(notify_, kGZMsgCommand, kPieMsgMouseMoved, reinterpret_cast<std::uint64_t>(this), 0);
    return true;
}

void cGZWinPieMenu::DrawLabelFrame(std::uint32_t color, int x, int y, int w, int h) {   // 0x10051773a
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    const int L = drawArea_.left, T = drawArea_.top;
    context->SetColor(color);
    context->Fill(cRZRect{x + 2 + L, y + 2 + T, x + w - 2 + L, y + h - 2 + T});
    int bottomStart = h - 2, bottomEnd = h - 1, rightStart = w - 2, rightEnd = w - 1;
    for (int i = 0; i >= -1; --i) {
        int inset = i == -1 ? 3 : 0;
        if (i == 0) inset = 1;
        const int left = inset + x + 2;
        const int right = (x - 2 + w) - inset;
        context->Fill(cRZRect{L + left, T + 1 + y + i, L + right, T + 2 + y + i});
        context->Fill(cRZRect{L + left, y + bottomStart + T, L + right, y + bottomEnd + T});
        const int bottom = (y - 2 + h) - inset;
        const int top = inset + y + 2;
        const int column = x + i;
        context->Fill(cRZRect{L + 1 + column, T + top, L + 2 + column, T + bottom});
        context->Fill(cRZRect{x + rightStart + L, top + T, x + rightEnd + L, bottom + T});
        ++bottomStart; ++bottomEnd; ++rightStart; ++rightEnd;
    }
}

void cGZWinPieMenu::DrawFramedLabel(const char* text, int index, int length, cIGZFont* font, std::uint32_t textColor,
                                    std::uint32_t frameColor, int x, int y, int w, int h) {   // 0x1005179de
    DrawLabelFrame(frameColor, x, y, w, h);
    const std::uint32_t oldColor = font->GetFontColor();
    font->SetFontColor(textColor, kFontDefaultColor);
    int offsetX = 3, offsetY = 3;
    if (ownerDraw_) {
        if (index < 0) ownerDraw_->GetTitleTextOffset(this, items_.empty() ? nullptr : &items_[0], offsetX, offsetY);
        else ownerDraw_->GetItemTextOffset(this, index, &items_[static_cast<std::size_t>(index)], offsetX, offsetY);
    }
    font->DrawTextLine(drawBuffer_, x + offsetX + drawArea_.left, y + offsetY + drawArea_.top, text, static_cast<std::uint32_t>(length),
                       kFontDefaultColor, kFontDefaultColor, nullptr, nullptr);
    font->SetFontColor(oldColor, kFontDefaultColor);
}

bool cGZWinPieMenu::GZPaint() {   // 0x100517b20
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    if (background_) {
        cRZRect bounds;
        background_->GetBufferArea(bounds);
        const cRZRect dest{titleOriginX_ + drawArea_.left, titleOriginY_ + drawArea_.top, titleOriginX_ + drawArea_.left + backgroundW_,
                           titleOriginY_ + drawArea_.top + backgroundH_};
        context->Blt(reinterpret_cast<IDrawTargetBuffer*>(background_), bounds, dest);
    }
    const std::uint32_t fill = GetFillColorRGB();
    if (!(ownerDraw_ && ownerDraw_->DrawTitle(this, &title_, titleX_, titleY_, titleW_, titleH_, selected_, GetFillColor()))) {
        if (title_.Strlen() != 0)
            DrawFramedLabel(title_.Data(), -1, static_cast<int>(title_.Strlen()), font_, selected_, fill, titleX_, titleY_, titleW_, titleH_);
    }
    if (ownerDraw_) ownerDraw_->AfterDrawTitle(this, &title_, titleX_, titleY_, titleW_, titleH_, selected_, GetFillColor());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const PieMenuItem& item = items_[i];
        if (item.button || !item.enabled) continue;
        std::uint32_t color = lowlight_;
        if (static_cast<int>(i) == selection_) color = mouseDown_ ? selected_ : highlight_;
        if (!(ownerDraw_ && ownerDraw_->DrawItem(this, static_cast<int>(i), &item.text, item.x, item.y, item.w, item.h, color, GetFillColor())))
            DrawFramedLabel(item.text.Data(), static_cast<int>(i), static_cast<int>(item.text.Strlen()), font_, color, fill, item.x, item.y, item.w, item.h);
        if (ownerDraw_) ownerDraw_->AfterDrawItem(this, static_cast<int>(i), &item.text, item.x, item.y, item.w, item.h, color, GetFillColor());
    }
    for (const PieMenuItem& item : items_)
        if (item.button) item.button->AsIGZWin()->InvalidateSelf();
    return true;
}

}  // namespace gz
