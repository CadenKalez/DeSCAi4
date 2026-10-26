#include "sc4/WinCatalogView.h"

namespace sc4 {

namespace {
ICatalogViewHost* g_host = nullptr;
void Sound(std::uint32_t id) {
    if (g_host) g_host->PlaySound(id);
}
}  // namespace

ICatalogViewHost* CatalogViewHost() { return g_host; }
void SetCatalogViewHost(ICatalogViewHost* host) { g_host = host; }

cSC4WinCatalogView::cSC4WinCatalogView(cIGZWinMgr* mgr) : cGZWin(mgr) {}

cSC4WinCatalogView::~cSC4WinCatalogView() {
    cSC4WinCatalogView::Shutdown();
    if (unknown1a0_) unknown1a0_->Release();
    if (callout_) callout_->Release();
    if (font_) font_->Release();
    // flashTimer_ and the item vector go with the members (the vector is not cleared of references here, as in the original)
}

bool cSC4WinCatalogView::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4WinCatalogView) {
        *out = static_cast<cISC4WinCatalogView*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

bool cSC4WinCatalogView::Init() {
    if (!get_init_flag()) {
        SetFlag(0x10000, false);
        SetFlag(4, true);
        cGZWin::Init();
        if (!font_) SetTextFontStyle(kCatalogDefaultFontStyle);
        SetScrollPosition(0);
    }
    return true;
}

bool cSC4WinCatalogView::Shutdown() {
    if (!get_init_flag()) return true;
    if (flashTimer_.IsRunning()) flashTimer_.Stop();
    Clear();
    if (gz::IWinFont* font = font_) {
        font_ = nullptr;
        font->Release();
    }
    if (callout_) {
        ChildDelete(callout_->AsIGZWin());
        if (cISC4WinCalloutBox* box = callout_) {
            callout_ = nullptr;
            box->Release();
        }
    }
    if (cIGZUnknown* u = unknown1a0_) {
        unknown1a0_ = nullptr;
        u->Release();
    }
    return cGZWin::Shutdown();
}

void cSC4WinCatalogView::SetCalloutBox(cISC4WinCalloutBox* box) {
    cISC4WinCalloutBox* old = nullptr;
    if (callout_) {
        ChildDelete(callout_->AsIGZWin());
        old = callout_;
    }
    cISC4WinCalloutBox* current = old;
    if (old != box) {
        if (box) box->AddRef();
        callout_ = box;
        current = box;
        if (old) {
            old->Release();
            current = callout_;
        }
    }
    if (current) ChildAdd(current->AsIGZWin());
}

void cSC4WinCatalogView::Clear() {
    for (cISC4CatalogItem* item : items_) item->Release();
    items_.clear();
    count_ = 0;
}

void cSC4WinCatalogView::AddItem(cISC4CatalogItem* item) {
    item->AddRef();
    items_.push_back(item);
    ++count_;
}

cISC4CatalogItem* cSC4WinCatalogView::GetItemByID(int id) {
    for (cISC4CatalogItem* item : items_) {
        if (static_cast<int>(item->GetItemID()) == id) return item;
    }
    return nullptr;
}

bool cSC4WinCatalogView::SetEnabledAllItemsExceptItemWithID(std::uint32_t id, bool enabled) {
    bool found = false;
    for (cISC4CatalogItem* item : items_) {
        if (item->GetItemID() == id) {
            found = true;
        } else {
            item->SetEnabled(enabled);
        }
    }
    return found;
}

bool cSC4WinCatalogView::GetItemPositionByID(std::uint32_t id, int& x, int& y) {
    int index = 0;
    for (cISC4CatalogItem* item : items_) {
        if (item->GetItemID() == id) {
            x = static_cast<int>(itemWidth_) + 0x10;
            y = static_cast<int>(itemHeight_) / 2 + static_cast<int>(itemSpacing_ + itemHeight_) * (index - scroll_);
            WindowToScreenCoordinates(x, y);   // vslot 0x1e0
            return true;
        }
        ++index;
    }
    return false;
}

bool cSC4WinCatalogView::FlashItemByID(std::uint32_t id, std::uint32_t count, std::uint32_t period) {
    if (id == 0xffffffffu) {
        if (flashTimer_.IsRunning()) flashTimer_.Stop();
        flashIndex_ = 0;
        flashCount_ = static_cast<int>(count);
        return false;
    }
    int index = 0;
    for (cISC4CatalogItem* item : items_) {
        if (item->GetItemID() == id) {
            if (flashTimer_.IsRunning()) flashTimer_.Stop();
            flashIndex_ = index;
            flashCount_ = static_cast<int>(count);
            flashTimer_.SetCallbackFunction(&FlashTimerCallback, this);
            flashTimer_.StartPeriodic(period, nullptr);
            return true;
        }
        ++index;
    }
    return false;
}

void cSC4WinCatalogView::FlashTimerCallback(void* view) {
    auto* self = static_cast<cSC4WinCatalogView*>(view);
    cGZWin* window = self;
    window->PostMsg(window, kCatalogMsgFlashTick, 0, 0, 0);
}

cISC4CatalogItem* cSC4WinCatalogView::GetHighlightedItem() {
    return (highlighted_ < 0 || count_ <= highlighted_) ? nullptr : items_[highlighted_];
}

cISC4CatalogItem* cSC4WinCatalogView::GetSelectedItem() {
    return (selected_ < 0 || count_ <= selected_) ? nullptr : items_[selected_];
}

std::uint32_t cSC4WinCatalogView::GetSelectedItemID() {
    return (selected_ < 0 || selected_ >= count_) ? 0 : items_[selected_]->GetItemID();
}

void cSC4WinCatalogView::SetSelectedItemByID(std::uint32_t id) {
    int found = -1;
    for (int i = 0; i < count_; ++i) {
        if (items_[i]->GetItemID() == id) {
            found = i;
            break;
        }
    }
    selected_ = found;
}

void cSC4WinCatalogView::SetItemSize(std::uint32_t width, std::uint32_t height, std::uint32_t spacing) {
    itemWidth_ = width;
    itemHeight_ = height;
    itemSpacing_ = spacing;
}

cRZPoint cSC4WinCatalogView::CalcPreferredSize(std::uint32_t itemCount) {
    cRZPoint size;
    size.x = static_cast<std::int32_t>(itemWidth_);
    size.y = itemCount == 0 ? 0 : static_cast<std::int32_t>((itemHeight_ + itemSpacing_) * itemCount - itemSpacing_);
    return size;
}

int cSC4WinCatalogView::VisibleRows() const {
    return ((area_.bottom - area_.top) + static_cast<int>(itemSpacing_)) / static_cast<int>(itemSpacing_ + itemHeight_);
}

void cSC4WinCatalogView::SetScrollPosition(int position) {
    const int visible = VisibleRows();
    const int limit = count_ - visible;
    const int clampedHigh = position < limit ? position : limit;
    const int clamped = clampedHigh >= 0 ? clampedHigh : 0;
    if (scroll_ != clamped) {
        scroll_ = clamped;
        InvalidateSelfAndParents();
    }
    if (cIGZWin* parent = GetParentWin()) {
        void* target = nullptr;
        if (parent->QueryInterface(kCatalogViewScrollTargetIID, &target)) {
            auto* buttons = static_cast<ICatalogScrollTarget*>(target);
            buttons->SetScrollButtonsEnabled(clampedHigh > 0, clamped < count_ - visible);
            buttons->Release();
        }
    }
}

void cSC4WinCatalogView::ChangeScrollPos(int delta) {
    const int before = scroll_;
    SetScrollPosition(delta + before);
    if (before != scroll_) Sound(kCatalogSoundScroll);
}

bool cSC4WinCatalogView::DoMessage(cGZMessage& message) {
    if (message.type == kCatalogMsgFlashTick) {
        if (flashCount_ == 0) {
            if (flashTimer_.IsRunning()) flashTimer_.Stop();
        } else {
            --flashCount_;
        }
        return true;
    }
    if (message.type == kCatalogMsgScroll) {
        ChangeScrollPos(static_cast<int>(message.param1));
        return true;
    }
    return cGZWin::DoMessage(message);
}

void cSC4WinCatalogView::SetTextFontStyle(std::uint32_t style) {
    gz::IWinFontSource* source = gz::WinFontSource();
    gz::IWinFont* font = source ? source->Font(style) : nullptr;
    gz::IWinFont* old = font_;
    if (old != font) {
        if (font) font->AddRef();
        font_ = font;
        if (old) old->Release();
    }
}

void cSC4WinCatalogView::SetHighlightedItem(int index) {
    if (highlighted_ == index) return;
    highlighted_ = index;
    InvalidateSelfAndParents();
    if (!callout_) return;
    cIGZWin* calloutWindow = callout_->AsIGZWin();
    if (highlighted_ < 0) {
        calloutWindow->HideWindow();
        return;
    }
    cISC4CatalogItem* item = items_[highlighted_];
    cRZRect constraint = callout_->GetConstraintRect();
    cRZString title(item->GetName());
    if (item->GetCost() != 0 && font_) {
        title.AppendChars(" - ", 3);
        const std::string money = g_host ? g_host->MakeMoneyString(item->GetCost()) : std::string();
        title.AppendChars(money.data(), static_cast<std::uint32_t>(money.size()));
    }
    cRZPoint at;
    at.x = static_cast<int>(itemWidth_) + 0x10;
    at.y = static_cast<int>(itemHeight_) / 2 + static_cast<int>(itemSpacing_ + itemHeight_) * (highlighted_ - scroll_);
    WindowToScreenCoordinates(at.x, at.y);
    constraint.left = at.x;
    callout_->SetTitle(title);
    callout_->SetText(item->GetDescription());
    callout_->SetPointOfInterest(at);
    callout_->SetAvoidanceRect(cRZRect{0, 0, 0, 0});
    callout_->SetConstraintRect(constraint);
    calloutWindow->ShowWindow();
}

bool cSC4WinCatalogView::MakeItemVisible(std::uint32_t id) {
    const int visible = VisibleRows();
    int index = 0;
    for (cISC4CatalogItem* item : items_) {
        if (item->GetItemID() == id) {
            const auto position = static_cast<std::uint32_t>(index);
            if (position < static_cast<std::uint32_t>(scroll_)) {
                SetScrollPosition(index);
            } else if (position >= static_cast<std::uint32_t>(scroll_ + visible)) {
                SetScrollPosition(1 - visible + index);
            }
            return true;
        }
        ++index;
    }
    return false;
}

bool cSC4WinCatalogView::GZOnMouseMove(int, int y, std::uint32_t) {
    int row = (y - static_cast<int>(itemSpacing_) / 2) / static_cast<int>(itemHeight_ + itemSpacing_);
    if (row < 0 || (row += scroll_, count_ <= row)) row = -1;
    SetHighlightedItem(row);
    return true;
}

bool cSC4WinCatalogView::GZOnMouseExit(cIGZWin*) {
    SetHighlightedItem(-1);
    return true;
}

bool cSC4WinCatalogView::GZOnMouseUpL(int, int, std::uint32_t) {
    if (highlighted_ < 0 || !items_[highlighted_]->GetEnabled()) {
        Sound(kCatalogSoundDisabled);
        return true;
    }
    cISC4CatalogItem* item = items_[highlighted_];
    PostMsg(notify_, kGZMsgCommand, kCatalogMsgItemChosen, item->GetItemType(), item->GetItemID());
    selected_ = highlighted_;
    Sound(kCatalogSoundScroll);
    return true;
}

bool cSC4WinCatalogView::GZOnMouseWheel(int x, int y, std::uint32_t modifiers, int delta) {
    // delta * -0x1b4e81b5 >> 32, then >> 5 rounding towards zero: about -delta / 300 rows
    const int high = static_cast<int>((static_cast<std::int64_t>(delta) * -0x1b4e81b5LL) >> 32);
    ChangeScrollPos((high >> 5) - (high >> 31));
    return GZOnMouseMove(x, y, modifiers);
}

bool cSC4WinCatalogView::GZPaint() {
    int y = drawArea_.top;
    const int rows = VisibleRows();
    for (int i = 0; i < rows; ++i) {
        const int index = scroll_ + i;
        if (count_ <= index) return true;
        cISC4CatalogItem* item = items_[index];
        if (cIGZUnknown* icon = item->GetIcon()) {
            int sourceLeft;
            const int width = static_cast<int>(itemWidth_);
            if (!item->GetEnabled()) {
                sourceLeft = 0;
            } else if (index == highlighted_ || (index == flashIndex_ && (flashCount_ & 1))) {
                sourceLeft = width * 3;
            } else {
                sourceLeft = width << (index == selected_ ? 1 : 0);
            }
            const cRZRect source{sourceLeft, 0, sourceLeft + width, static_cast<int>(itemHeight_)};
            const cRZRect dest{drawArea_.left, y, drawArea_.left + width, y + static_cast<int>(itemHeight_)};
            if (drawBuffer_ && drawBuffer_->Lock(0x8010)) {
                ICatalogViewHost* host = CatalogViewHost();
                if (!host || !host->BlitIcon(drawBuffer_, icon, source, dest))
                    drawBuffer_->Blt(static_cast<IWinBuffer*>(icon), source, dest);   // vslot 0xf8; the icon is a buffer
                drawBuffer_->Unlock(0x8010);
            }
            y += static_cast<int>(itemHeight_ + itemSpacing_);
        }
    }
    return true;
}

}  // namespace sc4
