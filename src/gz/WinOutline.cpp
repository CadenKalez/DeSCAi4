#include "gz/WinOutline.h"

#include <algorithm>
#include <limits>

#include "gz/DrawContext.h"
#include "gz/WinCtrlFactory.h"

namespace gz {

namespace {
constexpr std::uint32_t kDarkColorId = 0xa8963fad, kLightColorId = 0x08963faf, kFillColorId = 0x68963f6f;
void DefaultRGB(std::uint32_t id, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    IWinCtrlFactory* factory = WinCtrlFactory();
    IWinGraphics* graphics = WinGraphics();
    const std::uint32_t native = factory ? factory->DefaultColor(static_cast<int>(id)) : 0;
    if (graphics) graphics->NativeToRGB(native, r, g, b);
    else { r = g = b = 0; }
}
}  // namespace

cGZWinOutline::cGZWinOutline(cIGZWinMgr* mgr) : cGZWin(mgr) {   // 0x1005150e0
    std::uint8_t r, g, b;
    DefaultRGB(kDarkColorId, r, g, b);
    SetColor(1, r, g, b);
    DefaultRGB(kLightColorId, r, g, b);
    SetColor(2, r, g, b);
    DefaultRGB(kFillColorId, r, g, b);
    SetColor(0, r, g, b);
}

cGZWin* cGZWinOutline::CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinOutline(mgr); }   // 0x1004dc8e6

bool cGZWinOutline::QueryInterface(std::uint32_t iid, void** out) {   // 0x100515378
    if (iid == kGZIID_cIGZWinOutline) {
        *out = static_cast<cIGZWinOutline*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

void cGZWinOutline::SetColor(int which, std::uint8_t r, std::uint8_t g, std::uint8_t b) {   // 0x100515e8c
    const std::uint32_t packed = b | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(r) << 16) | 0xff000000u;
    if (which == 2) lightColor_ = packed;
    else if (which == 1) darkColor_ = packed;
    else if (which == 0) cGZWin::SetFillColor(r, g, b);
}

void cGZWinOutline::GetColor(int which, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {   // 0x100515ef4
    std::uint32_t packed;
    if (which == 2) packed = lightColor_;
    else if (which == 1) packed = darkColor_;
    else if (which == 0) { cGZWin::GetFillColor(r, g, b); return; }
    else return;
    r = static_cast<std::uint8_t>(packed >> 16);
    g = static_cast<std::uint8_t>(packed >> 8);
    b = static_cast<std::uint8_t>(packed);
}

void cGZWinOutline::SetDarkOutlineColor(std::uint32_t native) {   // 0x100515f72
    std::uint8_t r = 0, g = 0, b = 0;
    if (IWinGraphics* graphics = WinGraphics()) graphics->NativeToRGB(native, r, g, b);
    SetColor(1, r, g, b);
}

void cGZWinOutline::SetLightOutlineColor(std::uint32_t native) {   // 0x100515fdc
    std::uint8_t r = 0, g = 0, b = 0;
    if (IWinGraphics* graphics = WinGraphics()) graphics->NativeToRGB(native, r, g, b);
    SetColor(2, r, g, b);
}

std::uint32_t cGZWinOutline::GetDarkOutlineColor() {   // 0x100516046
    IWinGraphics* graphics = WinGraphics();
    return graphics->MakeNativeColor(static_cast<std::uint8_t>(darkColor_ >> 16), static_cast<std::uint8_t>(darkColor_ >> 8), static_cast<std::uint8_t>(darkColor_));
}

std::uint32_t cGZWinOutline::GetLightOutlineColor() {   // 0x10051607c
    IWinGraphics* graphics = WinGraphics();
    return graphics->MakeNativeColor(static_cast<std::uint8_t>(lightColor_ >> 16), static_cast<std::uint8_t>(lightColor_ >> 8), static_cast<std::uint8_t>(lightColor_));
}

bool cGZWinOutline::SetShapePoly(int count, const cRZPoint* points, bool resize, bool resizeToBounds) {   // 0x1005153f0
    count_ = count;
    points_.assign(static_cast<std::size_t>(std::max(count, 0)), cRZPoint{});
    flags_outline_ |= kOutlinePolygon;
    bounds_ = cRZRect{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
    int start = 0;
    for (int i = 0; i < count; ++i) {
        if (points[i].y < points[start].y || (points[i].y == points[start].y && points[i].x < points[start].x)) start = i;
        bounds_.left = std::min(bounds_.left, points[i].x);
        bounds_.top = std::min(bounds_.top, points[i].y);
        bounds_.right = std::max(bounds_.right, points[i].x);
        bounds_.bottom = std::max(bounds_.bottom, points[i].y);
    }
    // Rotate so the top-most, left-most vertex is first.
    for (int i = 0; i < count; ++i) points_[static_cast<std::size_t>(i)] = points[(start + i) % count];
    if (resize) ResizeToFitVertexBounds(resizeToBounds);
    scanlines_.clear();
    scanlinesValid_ = false;
    BuildScanlines();
    InvalidateSelf();
    return true;
}

void cGZWinOutline::BuildScanlines() {   // 0x100515626
    if (scanlinesValid_) return;
    const int height = drawArea_.bottom - drawArea_.top;
    if (height == 0) return;
    scanlines_.resize(static_cast<std::size_t>(std::max(height, 0)));
    const int n = count_;
    for (int row = 0; row < height; ++row) {
        std::list<int>& crossings = scanlines_[static_cast<std::size_t>(row)];
        for (int i = 0; i < n; ++i) {
            const int j = i + 1 < n ? i + 1 : 0;
            const cRZPoint& a = points_[static_cast<std::size_t>(i)];
            const cRZPoint& b = points_[static_cast<std::size_t>(j)];
            const std::uint32_t y0 = static_cast<std::uint32_t>(a.y), y1 = static_cast<std::uint32_t>(b.y);
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            if (y == y0 && y == y1) {
                const int before = points_[static_cast<std::size_t>(i == 0 ? n - 1 : i - 1)].y;
                const int after = points_[static_cast<std::size_t>((i + 2) % n)].y;
                if ((before < row && after < row) || (row < before && row < after))
                    crossings.push_back(a.x < b.x ? a.x : b.x);
            } else if (y != y1) {
                if (y == y0) {
                    crossings.push_back(a.x);
                } else {
                    if ((static_cast<int>(y0) < row && static_cast<int>(y1) < row) || (row < static_cast<int>(y0) && row < static_cast<int>(y1))) continue;
                    crossings.push_back((row - a.y) * (b.x - a.x) / (b.y - a.y) + a.x);
                }
            }
        }
        crossings.sort();
    }
    scanlinesValid_ = true;
}

void cGZWinOutline::ResizeToFitVertexBounds(bool keepLarger) {   // 0x10051587a
    int width = bounds_.right;
    int height;
    if (keepLarger) {
        width = std::max(width, 0);
        height = bounds_.bottom;
    } else {
        if (width < GetW()) width = GetW();
        width = std::max(width, 0);
        height = bounds_.bottom;
        if (height < GetH()) height = GetH();
    }
    SetSize(width, std::max(height, 0));
}

bool cGZWinOutline::GetShapePoly(cRZPoint* out) const {   // 0x10051595e
    if (!(flags_outline_ & kOutlinePolygon)) {
        out[0] = cRZPoint{drawArea_.left, drawArea_.top};
        out[1] = cRZPoint{drawArea_.left, drawArea_.bottom};
        out[2] = cRZPoint{drawArea_.right, drawArea_.bottom};
        out[3] = cRZPoint{drawArea_.right, drawArea_.top};
    } else {
        for (int i = 0; i < count_; ++i) out[i] = points_[static_cast<std::size_t>(i)];
    }
    return true;
}

bool cGZWinOutline::DoMessage(cGZMessage& message) {   // 0x1005159ca
    if (parent_ == nullptr) return false;
    if (message.type - 7u < 8u) {   // mouse messages arrive in this window's coordinates
        message.param1 = static_cast<std::uint32_t>(GetL() + static_cast<int>(message.param1));
        message.param2 = static_cast<std::uint32_t>(GetT() + static_cast<int>(message.param2));
    }
    return parent_->DoMessage(message);
}

void cGZWinOutline::SetArea(int l, int t, int r, int b) {   // 0x100515db0
    cGZWin::SetArea(l, t, r, b);
    scanlines_.clear();
    scanlinesValid_ = false;
    BuildScanlines();
}

bool cGZWinOutline::GZPaint() {   // 0x100515a54
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    cRZRect rect = drawArea_;
    if (rect.right == rect.left || rect.bottom == rect.top) return true;
    const std::uint32_t f = flags_outline_;
    auto edgeColors = [&](std::uint32_t& topLeft, std::uint32_t& bottomRight) {
        if (!(f & kOutlineSunken)) {
            if (!(f & kOutlineRaised)) { topLeft = bottomRight = darkColor_; }
            else { bottomRight = darkColor_; topLeft = lightColor_; }
        } else {
            topLeft = darkColor_;
            bottomRight = lightColor_;
        }
    };
    if (!(f & kOutlinePolygon)) {
        if (f & kOutlineFilled) {
            context->SetColor(GetFillColorRGB());
            context->Fill(rect);
        }
        std::uint32_t topLeft, bottomRight;
        edgeColors(topLeft, bottomRight);
        rect.right -= 1;
        rect.bottom -= 1;
        context->SetColor(topLeft);
        context->Line(rect.left, rect.top, rect.right, rect.top);
        context->Line(rect.left, rect.top, rect.left, rect.bottom);
        if (bottomRight != topLeft) context->SetColor(bottomRight);
        context->Line(rect.right, rect.top, rect.right, rect.bottom);
        context->Line(rect.left, rect.bottom, rect.right, rect.bottom);
        return true;
    }
    const int x0 = rect.left, y0 = rect.top;
    const int height = rect.bottom - rect.top;
    if ((f & kOutlineFilled) && count_ > 2 && scanlinesValid_ && height != 0 && y0 <= rect.bottom) {
        for (int row = 0; row < height; ++row) {
            context->SetColor(GetFillColorRGB());
            const std::list<int>& crossings = scanlines_[static_cast<std::size_t>(row)];
            for (auto it = crossings.begin(); it != crossings.end();) {
                const int a = *it;
                if (++it == crossings.end()) break;
                const int b = *it;
                ++it;
                if (a != b) context->Line(a + x0, y0 + row, b + x0, y0 + row);
            }
        }
    }
    if (f & 7) {
        for (int i = 0; i < count_; ++i) {
            const cRZPoint& p0 = points_[static_cast<std::size_t>(i)];
            const cRZPoint& p1 = points_[static_cast<std::size_t>(i + 1 < count_ ? i + 1 : 0)];
            std::uint32_t first, second;
            edgeColors(first, second);
            std::uint32_t color = first;
            if (p1.y <= p0.y && (p0.x <= p1.x || p1.y != p0.y)) color = second;
            context->SetColor(color);
            context->Line(p0.x + x0, p0.y + y0, p1.x + x0, p1.y + y0);
        }
    }
    return true;
}

}  // namespace gz
