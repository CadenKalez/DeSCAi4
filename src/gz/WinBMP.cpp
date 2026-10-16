#include "gz/WinBMP.h"

namespace gz {

static IWinEdgeBlit* g_edgeBlit = nullptr;
IWinEdgeBlit* WinEdgeBlit() { return g_edgeBlit; }
void SetWinEdgeBlit(IWinEdgeBlit* blit) { g_edgeBlit = blit; }

bool cGZWinBMP::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinBMP) {
        *out = static_cast<cIGZWinBMP*>(this);
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

bool cGZWinBMP::Init() {
    if (get_init_flag()) return true;
    SetSourceBufferAreaFromWindowArea({0, 0, GetW(), GetH()});
    return cGZWin::Init();
}

// The image area starts at the origin with the window's size, limited to the bitmap's size.
void cGZWinBMP::SetSourceBufferAreaFromWindowArea(const cRZRect& area) {
    imageArea_.left = imageArea_.top = 0;
    imageArea_.right = area.right - area.left;
    imageArea_.bottom = area.bottom - area.top;
    ClampImageSize();
}

void cGZWinBMP::ClampImageSize() {
    if (!bitmap_) return;
    if (bitmap_->Width() < imageArea_.right) imageArea_.right = bitmap_->Width();
    if (bitmap_->Height() < imageArea_.bottom) imageArea_.bottom = bitmap_->Height();
}

bool cGZWinBMP::Shutdown() {
    if (bitmap_) {
        bitmap_->Release();
        bitmap_ = nullptr;
    }
    return cGZWin::Shutdown();
}

void cGZWinBMP::SetBMP(IWinBitmap* bitmap) {
    if (bitmap_ == bitmap) return;
    if (bitmap) bitmap->AddRef();
    if (bitmap_) bitmap_->Release();
    bitmap_ = bitmap;
    SetSourceBufferAreaFromWindowArea(GetArea());
    InvalidateSelfAndParents();
}

void cGZWinBMP::SetAlphaBlending(bool on, float amount) {
    alphaBlending_ = on;
    alphaAmount_ = amount;
    InvalidateSelfAndParents();
}

void cGZWinBMP::SetImageArea(const cRZRect& area) {
    imageArea_ = area;
    if (!bitmap_) return;
    if (imageArea_.left < 0) imageArea_.left = 0;
    if (imageArea_.top < 0) imageArea_.top = 0;
    ClampImageSize();
}

bool cGZWinBMP::GZPaint() {
    cIGZDrawContext* context = Context();
    if (!(bmpFlags_ & kWinBMPNoFill)) {
        context->SetColor(GetFillColorRGB());
        context->Fill(drawArea_);
    }
    if (!bitmap_) return true;
    cRZRect source;
    if (!GetWinBMPFlag(kWinBMPUseImageArea) && !GetWinBMPFlag(kWinBMPAtOffset)) {
        source = *bitmap_->GetBufferArea();
    } else {
        source = imageArea_;
    }
    if (!GetWinBMPFlag(kWinBMPEdge)) {
        cRZRect dest{drawArea_.left, drawArea_.top, source.right + drawArea_.left - source.left,
                     source.bottom + drawArea_.top - source.top};
        if (GetWinBMPFlag(kWinBMPAtOffset)) {
            dest = {drawArea_.left + source.left, drawArea_.top + source.top, source.right + drawArea_.left,
                    source.bottom + drawArea_.top};
        }
        context->Blt(bitmap_, source, dest);
    } else {
        source.right /= 3;
        source.bottom /= 3;
        if (IWinEdgeBlit* blit = WinEdgeBlit()) blit->DrawBltEdge(context, bitmap_, source, drawArea_);
    }
    return true;
}

// Key / paint-like messages are handled by the base; with the pass-through flag everything else goes to the parent.
bool cGZWinBMP::DoMessage(cGZMessage& message) {
    const std::uint32_t type = message.type;
    if (type < 0xe) {
        if ((0x2480u >> type) & 1) {
            cGZWin::DoMessage(message);
        } else if ((0x60u >> type) & 1) {
            return cGZWin::DoMessage(message);
        }
    }
    if (GetWinBMPFlag(kWinBMPPassThrough) && parent_) {
        if (type - 7u < 8u) {
            message.param1 += static_cast<std::int64_t>(GetL());
            message.param2 += static_cast<std::int64_t>(GetT());
        }
        return parent_->DoMessage(message);
    }
    return false;
}

bool cGZWinBMP::GZOnKeyDown(std::uint32_t key, std::uint32_t flags) {
    SendMsg(notify_, kGZMsgKeyDown, key, flags, 0);
    return true;
}

bool cGZWinBMP::GZOnKeyUp(std::uint32_t key, std::uint32_t flags) {
    SendMsg(notify_, kGZMsgKeyUp, key, flags, 0);
    return true;
}

bool cGZWinBMP::GZOnMouseMove(int x, int y, std::uint32_t) {
    SendMsg(notify_, kWinMsgMouseMove, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

bool cGZWinBMP::GZOnMouseDownL(int x, int y, std::uint32_t) {
    SendMsg(notify_, kWinMsgMouseDownL, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

bool cGZWinBMP::GZOnMouseUpL(int x, int y, std::uint32_t) {
    SendMsg(notify_, kWinMsgMouseUpL, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

bool cGZWinBMP::IsPointInWindowScreenCoordinates(int x, int y) const {
    if (GetWinBMPFlag(kWinBMPNoHit)) return false;
    return cGZWin::IsPointInWindowScreenCoordinates(x, y);
}

}  // namespace gz
