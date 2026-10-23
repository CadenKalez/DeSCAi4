#include "gz/WinFlatRect.h"

namespace gz {

static IWinColorManager* g_colorManager = nullptr;
IWinColorManager* WinColorManager() { return g_colorManager; }
void SetWinColorManager(IWinColorManager* manager) { g_colorManager = manager; }

namespace {
std::uint32_t Pack(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
}
}  // namespace

bool cGZWinFlatRect::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinFlatRect) {
        *out = static_cast<cIGZWinFlatRect*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

// Mouse messages (types 7..14) are re-addressed into the parent's coordinates and handed to the parent.
bool cGZWinFlatRect::DoMessage(cGZMessage& message) {
    if (!parent_) return false;
    if (message.type - 7u < 8u) {
        message.param1 += static_cast<std::int64_t>(GetL());
        message.param2 += static_cast<std::int64_t>(GetT());
    }
    return parent_->DoMessage(message);
}

bool cGZWinFlatRect::GZOnKeyDown(std::uint32_t key, std::uint32_t flags) {
    cGZMessage message{kGZMsgKeyDown, key, flags, 0};
    return notify_->DoMessage(message);
}

bool cGZWinFlatRect::GZOnKeyUp(std::uint32_t key, std::uint32_t flags) {
    cGZMessage message{kGZMsgKeyUp, key, flags, 0};
    return notify_->DoMessage(message);
}

bool cGZWinFlatRect::GZPaint() {
    cIGZDrawContext* context = Context();
    if (!(outlineFlags_ & kOutlineNoFill)) {
        context->SetColor(GetFillColorRGB());
        context->Fill(drawArea_);
    }
    if ((outlineFlags_ & 0xbf) != 0) {
        const int l = drawArea_.left, t = drawArea_.top, r = drawArea_.right - 1, b = drawArea_.bottom - 1;
        if (outlineFlags_ & kOutlineLeft) { context->SetColor(edgeColors_[0]); context->Line(l, t, l, b); }
        if (outlineFlags_ & kOutlineTop) { context->SetColor(edgeColors_[1]); context->Line(l, t, r, t); }
        if (outlineFlags_ & kOutlineRight) { context->SetColor(edgeColors_[2]); context->Line(r, t, r, b); }
        if (outlineFlags_ & kOutlineBottom) { context->SetColor(edgeColors_[3]); context->Line(l, b, r, b); }
    }
    return true;
}

// Raised / sunken pick the bevel colours from the window colour manager (black / white without one).
bool cGZWinFlatRect::SetOutlineFlags(int flags) {
    outlineFlags_ = static_cast<std::uint8_t>(flags);
    if ((flags & (kFlatRectRaised | kFlatRectSunken)) == 0) return true;
    std::uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0xff, g2 = 0xff, b2 = 0xff;
    if (IWinColorManager* colors = WinColorManager()) {
        colors->GetColor(kWinColorShadow, r1, g1, b1);
        colors->GetColor(kWinColorHighlight, r2, g2, b2);
    }
    if (outlineFlags_ & kFlatRectRaised) {
        SetOutlineColor(0, r2, g2, b2);
        SetOutlineColor(1, r2, g2, b2);
        SetOutlineColor(2, r1, g1, b1);
        SetOutlineColor(3, r1, g1, b1);
    } else if (outlineFlags_ & kFlatRectSunken) {
        SetOutlineColor(0, r1, g1, b1);
        SetOutlineColor(1, r1, g1, b1);
        SetOutlineColor(2, r2, g2, b2);
        SetOutlineColor(3, r2, g2, b2);
    }
    outlineFlags_ |= 0xf;
    return true;
}

void cGZWinFlatRect::SetOutlineColor(int edge, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (edge >= 0 && edge < 4) edgeColors_[edge] = Pack(r, g, b);
}

void cGZWinFlatRect::GetOutlineColor(int edge, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const {
    if (edge < 0 || edge > 3) return;
    const std::uint32_t c = edgeColors_[edge];
    r = (c >> 16) & 0xff;
    g = (c >> 8) & 0xff;
    b = c & 0xff;
}

void cGZWinFlatRect::SetOutlineColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    edgeColors_[0] = edgeColors_[1] = edgeColors_[2] = edgeColors_[3] = Pack(r, g, b);
}

void cGZWinFlatRect::SetOutlineColor(std::uint32_t nativeColor) {
    std::uint8_t r, g, b;
    WinGraphics()->NativeToRGB(nativeColor, r, g, b);
    SetOutlineColor(r, g, b);
}

void cGZWinFlatRect::SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) {
    IWinGraphics* graphics = WinGraphics();
    if (!graphics) return;
    const std::uint32_t native[4] = {left, top, right, bottom};
    for (int i = 0; i < 4; ++i) {
        std::uint8_t r, g, b;
        graphics->NativeToRGB(native[i], r, g, b);
        edgeColors_[i] = Pack(r, g, b);
    }
}

void cGZWinFlatRect::GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) {
    IWinGraphics* graphics = WinGraphics();
    if (!graphics) return;
    std::uint32_t* out[4] = {&left, &top, &right, &bottom};
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t c = edgeColors_[i];
        *out[i] = graphics->MakeNativeColor((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
    }
}

// Size modes follow the parent's area: 3 width, 4 height, 5 left and width; 1, 2 and 6 keep the area.
bool cGZWinFlatRect::AutoSize(const cRZRect& parentArea) {
    if (sizeMode_ == 0) return true;
    cRZRect area;
    GetArea(area);
    switch (sizeMode_) {
        case 1: case 2: case 6:
            break;
        case 4:
            area.bottom = parentArea.bottom;
            break;
        case 5:
            area.left = parentArea.left;
            area.right = parentArea.right;
            break;
        case 3:
            area.right = parentArea.right;
            break;
        default:
            return false;
    }
    if (area.bottom < area.top) area.bottom = area.top;
    if (area.right < area.left) area.right = area.left;
    SetArea(area.left, area.top, area.right, area.bottom);
    return true;
}

}  // namespace gz
