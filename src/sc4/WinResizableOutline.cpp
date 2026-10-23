#include "sc4/WinResizableOutline.h"

#include <cstdio>
#include <string>

namespace sc4 {

namespace {
IResizableOutlineHost* g_host = nullptr;
// Cursor ids per zone (table @ 0x100a1df10 is indexed by zone ^ 8; the others are loaded as immediates).
constexpr std::uint32_t kCursorDiagonalNWSE = 0x68024b0e, kCursorDiagonalNESW = 0x88024b02, kCursorHorizontal = 0xc8024b17,
                        kCursorVertical = 0xe8024b13, kCursorMove = 0x11;
constexpr int kBorder = 4;
}  // namespace

IResizableOutlineHost* ResizableOutlineHost() { return g_host; }
void SetResizableOutlineHost(IResizableOutlineHost* host) { g_host = host; }

// Quirk kept: the destructor does not release the font (only Shutdown does).
cSC4WinResizableOutline::~cSC4WinResizableOutline() = default;

bool cSC4WinResizableOutline::Init() {
    if (get_init_flag()) return true;
    if (GetFlag(0x10000)) {
        SetFlag(0x20000, true);
        SetFlag(0x40000, true);
    }
    cGZWin::Init();
    if (normalColor_ == -0xff) normalColor_ = static_cast<std::int32_t>(MakeFillColor(0, 0, 0));
    if (activeColor_ == -0xff) activeColor_ = static_cast<std::int32_t>(MakeFillColor(0xff, 0xff, 0xff));
    if (idleColor_ == -0xff) idleColor_ = static_cast<std::int32_t>(MakeFillColor(200, 0, 0));
    if (!font_ && g_host) font_ = g_host->CreateFont("Arial MT", 0, 14, 0, 1);
    SetCursor(moving_ ? GetCursorManager()->LoadCursor(kCursorMove) : nullptr, false);
    return true;
}

bool cSC4WinResizableOutline::Shutdown() {
    if (font_) {
        font_->Release();
        font_ = nullptr;
    }
    return cGZWin::Shutdown();
}

bool cSC4WinResizableOutline::GetResizeTypeAndCursorForPosition(int x, int y, ResizeType& type, std::uint32_t& cursorId) {
    bool nearRight = false, nearBottom = false;
    if (GetW() - kBorder <= x) nearRight = GetW() + kBorder >= x;
    if (GetH() - kBorder <= y) nearBottom = GetH() + kBorder >= y;
    const bool nearLeft = static_cast<std::uint32_t>(x + kBorder) <= 8;
    const bool nearTop = static_cast<std::uint32_t>(y + kBorder) <= 8;
    type = kNone;
    std::uint32_t cursor;
    if (nearLeft) {
        if (nearTop) {
            type = kTopLeft;
            cursor = kCursorDiagonalNWSE;
        } else {
            type = nearBottom ? kBottomLeft : kLeft;
            cursor = type == kBottomLeft ? kCursorDiagonalNESW : kCursorHorizontal;
        }
    } else if (!nearRight) {
        if (nearTop) {
            type = kTop;
            cursor = kCursorVertical;
        } else if (nearBottom) {
            type = kBottom;
            cursor = kCursorVertical;
        } else {
            cursor = kCursorMove;
        }
    } else if (nearTop) {
        type = kTopRight;
        cursor = kCursorDiagonalNESW;
    } else {
        type = nearBottom ? kBottomRight : kRight;
        cursor = type == kBottomRight ? kCursorDiagonalNWSE : kCursorHorizontal;
    }
    cursorId = cursor;
    if (type == kNone && !moving_) return false;
    return resizing_;
}

bool cSC4WinResizableOutline::GZOnMouseDownL(int x, int y, std::uint32_t) {
    ResizeType type;
    std::uint32_t cursor = 0;
    if (GetResizeTypeAndCursorForPosition(x, y, type, cursor)) {
        zone_ = type;
        dragging_ = true;
        GZSetCapture();
        lastX_ = x;
        lastY_ = y;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cSC4WinResizableOutline::GZOnMouseUpL(int, int, std::uint32_t) {
    if (GZAmCapturing()) {
        dragging_ = false;
        zone_ = kNone;
        GZReleaseCapture();
        InvalidateSelfAndParents();
    }
    return true;
}

bool cSC4WinResizableOutline::GZOnMouseMove(int x, int y, std::uint32_t) {
    if (!GZAmCapturing()) {
        ResizeType type;
        std::uint32_t cursor = 0;
        GetResizeTypeAndCursorForPosition(x, y, type, cursor);
        if (cursorId_ != cursor) {
            cursorId_ = cursor;
            SetCursor(cursor == 0 ? nullptr : GetCursorManager()->LoadCursor(cursorId_), false);
        }
        if (dragging_) {
            dragging_ = false;
            InvalidateSelfAndParents();
        }
        lastX_ = x;
        lastY_ = y;
        return true;
    }
    cRZRect r = area_;   // left, top, right, bottom (parent coordinates)
    switch (zone_) {
        case kLeft: r.left = area_.left + x; break;
        case kRight: r.right = area_.left + x; break;
        case kTop: r.top = area_.top + y; break;
        case kBottom: r.bottom = area_.top + y; break;
        case kTopLeft: r.left = area_.left + x; r.top = area_.top + y; break;
        case kTopRight: r.right = area_.left + x; r.top = area_.top + y; break;
        case kBottomLeft: r.left = area_.left + x; r.bottom = area_.top + y; break;
        case kBottomRight: r.right = area_.left + x; r.bottom = area_.top + y; break;
        default:
            if (dragging_) {
                const int px = lastX_, py = lastY_;
                GZWinOffset(x - px, y - py);   // vslot 0x1c8
                x = px;                         // the stored position stays where the drag started
                y = py;
            }
            lastX_ = x;
            lastY_ = y;
            return true;   // zone 0: nothing to resize
    }
    if (r.left > r.right - 8) r.left = r.right - 8;
    if (r.top > r.bottom - 8) r.top = r.bottom - 8;
    if (r.left != area_.left || r.top != area_.top || r.right != area_.right || r.bottom != area_.bottom) {
        SetArea(r);   // vslot 0x1b8
        InvalidateSelfAndParents();
    }
    lastX_ = x;
    lastY_ = y;
    return true;
}

bool cSC4WinResizableOutline::GZPaint() {
    std::int32_t color;
    if (dragging_) color = activeColor_;
    else if (!moving_ && !resizing_) color = idleColor_;
    else color = normalColor_;
    if (drawBuffer_ && g_host && drawBuffer_->Lock(0x8004)) {
        const int l = drawArea_.left, t = drawArea_.top, r = drawArea_.right - 1, b = drawArea_.bottom - 1;
        const auto c = static_cast<std::uint32_t>(color);
        g_host->DrawLine(drawBuffer_, l, t, l, b, c, 1);
        g_host->DrawLine(drawBuffer_, l, t, r, t, c, 1);
        g_host->DrawLine(drawBuffer_, r, t, r, b, c, 1);
        g_host->DrawLine(drawBuffer_, l, b, r, b, c, 1);
        drawBuffer_->Unlock(0x8004);
    }
    if (dragging_ && font_) {
        char text[64];
        std::snprintf(text, sizeof text, "%u x %u", static_cast<unsigned>((drawArea_.right - 2) - drawArea_.left),
                      static_cast<unsigned>((drawArea_.bottom - 2) - drawArea_.top));
        const std::uint32_t saved = font_->GetStyle();
        font_->SetStyle(1);
        font_->DrawText(drawBuffer_, 8, 8, text, static_cast<std::uint32_t>(std::char_traits<char>::length(text)),
                        static_cast<std::uint32_t>(color), 0x8000000, nullptr, 0);
        font_->SetStyle(saved);
    }
    return true;
}

void cSC4WinResizableOutline::SetResizingAndMoving(bool resizing, bool moving) {
    moving_ = moving;
    resizing_ = resizing;
    if (GZAmCapturing()) {
        const bool allowed = zone_ == kNone ? resizing_ : moving_;   // quirk kept: zone 0 tests "resizing", a zone tests "moving"
        if (!allowed) {
            dragging_ = false;
            zone_ = kNone;
            GZReleaseCapture();
            if (cursorId_ != 0) {
                cursorId_ = 0;
                SetCursor(nullptr, false);
            }
            InvalidateSelfAndParents();
        }
    }
}

}  // namespace sc4
