#include "gz/WinBMP.h"

namespace gz {

cGZWinBMP::~cGZWinBMP() {
    if (bmp_) bmp_->Release();
}

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
    const cRZRect window{0, 0, GetW(), GetH()};
    SetSourceBufferAreaFromWindowArea(window);
    return cGZWin::Init();
}

// The image area starts as (0,0,w,h) of the window area and is never larger than the buffer.
void cGZWinBMP::SetSourceBufferAreaFromWindowArea(const cRZRect& area) {
    imageArea_.left = 0;
    imageArea_.top = 0;
    imageArea_.right = area.right - area.left;
    imageArea_.bottom = area.bottom - area.top;
    if (bmp_) {
        if (static_cast<int>(bmp_->Width()) < area.right - area.left) imageArea_.right = static_cast<int>(bmp_->Width());
        if (static_cast<int>(bmp_->Height()) < imageArea_.bottom) imageArea_.bottom = static_cast<int>(bmp_->Height());
    }
}

bool cGZWinBMP::Shutdown() {
    if (bmp_) {
        bmp_->Release();
        bmp_ = nullptr;
    }
    return cGZWin::Shutdown();
}

bool cGZWinBMP::GZPaint() {
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    if (!(flags_ & kWinBMPNoFill)) {
        context->SetColor(GetFillColorRGB());
        context->Fill(drawArea_);
    }
    if (!bmp_) return true;
    cRZRect source;
    if (!(flags_ & kWinBMPUseImageArea) && !(flags_ & kWinBMPImageAreaOffset)) source = *bmp_->GetBufferArea();
    else source = imageArea_;
    if (!(flags_ & kWinBMPNineSlice)) {
        const int left = drawArea_.left, top = drawArea_.top;
        cRZRect destination{left, top, (source.right - source.left) + left, (source.bottom - source.top) + top};
        if (flags_ & kWinBMPImageAreaOffset) {
            destination.left = left + source.left;
            destination.top = top + source.top;
            destination.right = source.right + left;
            destination.bottom = source.bottom + top;
        }
        context->Blt(bmp_, source, destination);
    } else {
        source.right /= 3;
        source.bottom /= 3;
        RZDrawBltEdge(context, bmp_, source, drawArea_, true);
    }
    return true;
}

void cGZWinBMP::SetBMP(cIGZBuffer* buffer) {
    if (bmp_ == buffer) return;
    if (buffer) buffer->AddRef();
    if (bmp_) bmp_->Release();
    bmp_ = buffer;
    SetSourceBufferAreaFromWindowArea(GetArea());
    InvalidateSelfAndParents();
}

bool cGZWinBMP::GetBMP(std::uint32_t iid, void** out) const {
    return bmp_ ? bmp_->QueryInterface(iid, out) : false;
}

void cGZWinBMP::SetAlphaBlending(bool enable, float alpha) {
    alphaBlend_ = enable;
    alpha_ = alpha;
    InvalidateSelfAndParents();
}

void cGZWinBMP::SetImageArea(const cRZRect& area) {
    imageArea_ = area;
    if (!bmp_) return;
    if (imageArea_.left < 0) imageArea_.left = 0;
    if (imageArea_.top < 0) imageArea_.top = 0;
    if (static_cast<int>(bmp_->Width()) < imageArea_.right) imageArea_.right = static_cast<int>(bmp_->Width());
    if (static_cast<int>(bmp_->Height()) < imageArea_.bottom) imageArea_.bottom = static_cast<int>(bmp_->Height());
}

// Key / mouse-move / mouse-up / mouse-down messages are forwarded to the parent (with the window's offset added
// to the mouse position) when flag 1 is set; paint-type messages go to cGZWin.
bool cGZWinBMP::DoMessage(cGZMessage& message) {
    const std::uint32_t type = message.type;
    if (type < 0xe) {
        if ((0x2480u >> (type & 0x1f)) & 1) {
            cGZWin::DoMessage(message);
        } else if ((0x60u >> (type & 0x1f)) & 1) {
            return cGZWin::DoMessage(message);
        }
    }
    if (GetWinBMPFlag(kWinBMPPassMessages) && parent_) {
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

bool cGZWinBMP::IsPointInWindowScreenCoordinates(int x, int y) const {
    if (GetWinBMPFlag(kWinBMPMouseTransparent)) return false;
    return cGZWin::IsPointInWindowScreenCoordinates(x, y);
}

bool cGZWinBMP::GZOnMouseMove(int x, int y, std::uint32_t) {
    SendMsg(notify_, kGZMsgBMPMouseMove, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

bool cGZWinBMP::GZOnMouseDownL(int x, int y, std::uint32_t) {
    SendMsg(notify_, kGZMsgBMPMouseDownL, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

bool cGZWinBMP::GZOnMouseUpL(int x, int y, std::uint32_t) {
    SendMsg(notify_, kGZMsgBMPMouseUpL, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), GetID());
    return true;
}

}  // namespace gz
