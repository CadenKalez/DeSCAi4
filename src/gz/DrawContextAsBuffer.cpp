#include "gz/DrawContextAsBuffer.h"

namespace gz {

namespace {
// bits per pixel for colour types 2..9 (the table at 0x100a27ef0, high dwords)
constexpr std::uint32_t kBitsPerColorType[8] = {8, 16, 16, 16, 16, 16, 32, 32};
// colour key masks per surface format 4..9 (the table at 0x100a27f30)
constexpr std::uint32_t kKeyMasks[6] = {0x7fff, 0xffff, 0x7fff, 0xfff, 0xffffff, 0xffffff};
constexpr int kFormatInitial = 1;
}  // namespace

cGZDrawContextAsBuffer::cGZDrawContextAsBuffer() {   // @ 0x100652e32
    surface_.format = kFormatInitial;
}

cGZDrawContextAsBuffer::~cGZDrawContextAsBuffer() {   // @ 0x100652eac
    if (buffer_) buffer_->Release();
    if (context_) context_->Release();
}

bool cGZDrawContextAsBuffer::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100652fee
    if (iid == kGZIID_cIGZBufferDrawTarget) {
        *out = static_cast<cIGZBufferDrawTarget*>(this);
    } else if (iid == kGZIID_cIGZBuffer || iid == 1) {
        *out = static_cast<cIGZBuffer*>(this);
    } else {
        return buffer_ ? buffer_->QueryInterface(iid, out) : false;   // anything else is the real buffer's business
    }
    AddRef();
    return true;
}

cIGZDrawContext* cGZDrawContextAsBuffer::GetDrawContext() {   // @ 0x100653040
    if (!context_->IsTargetAttached()) {
        context_->SetTarget(buffer_);
        context_->Suspend();
    }
    return context_;
}

cIGZDrawContext* cGZDrawContextAsBuffer::LockContext() {   // @ 0x100653092
    Lock(static_cast<eLockFlags>(kLockContextFlags));
    return context_;
}

void cGZDrawContextAsBuffer::UnlockContext() { Unlock(static_cast<eLockFlags>(kLockContextFlags)); }   // @ 0x1006530d6

void cGZDrawContextAsBuffer::Init(cIGZDrawContext* context, cIGZBuffer* buffer) {   // @ 0x100653102
    if (context_ != context) {
        cIGZDrawContext* old = context_;
        if (context) context->AddRef();
        context_ = context;
        if (old) old->Release();
    }
    if (buffer_ != buffer) {
        cIGZBuffer* old = buffer_;
        if (buffer) buffer->AddRef();
        buffer_ = buffer;
        if (old) old->Release();
    }
    area_ = *buffer->GetBufferArea();
    colorType_ = buffer->GetColorType().type;
    surface_.bytesPerPixel = buffer->GetBytesPerPixel();
    surface_.width = static_cast<std::uint32_t>(area_.right - area_.left);
    surface_.height = static_cast<std::uint32_t>(area_.bottom - area_.top);
    context_->SetTarget(nullptr);
}

void cGZDrawContextAsBuffer::ResetSize() {   // @ 0x1006531da
    area_ = *buffer_->GetBufferArea();
    context_->SetTarget(nullptr);
    context_->SetTarget(buffer_);
    context_->Suspend();
}

std::uint32_t cGZDrawContextAsBuffer::NativeToARGB32(std::uint32_t p) const {   // @ 0x10065323e
    std::uint32_t u, t;
    switch (colorType_) {
        case kBufferColorXRGB1555:
        case kBufferColorARGB1555:
            u = ((p & 0x3e0) << 6 | (p & 0x7c00) << 9) + (p & 0x1f) * 8;
            return ((u >> 5 & 0x70707) - 0x1000000) + u;
        case kBufferColorRGB565:
            t = (p & 0xf800) * 0x100 + (p & 0x1f) * 8;
            u = (p >> 1 & 0x300) | (p & 0x7e0) << 5 | t;
            return ((t >> 5 & 0x70007) - 0x1000000) + u;
        case kBufferColorARGB4444:
            u = (p & 0xf) | (p & 0xf0) << 4 | (p & 0xf00) << 8;
            return ((u << 4) - 0x1000000) + u;
        case kBufferColorXRGB8888:
        case kBufferColorARGB8888:
            return p | 0xff000000;
        default:
            return 0;
    }
}

void cGZDrawContextAsBuffer::SetBlitSource(cIGZBuffer* source) {   // @ 0x100653314 (SetBltShader)
    cIGZBufferShade* shade = nullptr;
    const bool hasShade = source->QueryInterface(kGZIID_cIGZBufferShade, reinterpret_cast<void**>(&shade));
    if (!hasShade) {
        context_->SetColor(0xffffffffu);
    } else {
        std::uint8_t r = 0, g = 0, b = 0, a = 0;
        if (shade->GetShadeColor(r, g, b, a)) {
            context_->SetColor(std::uint32_t(b) | std::uint32_t(g) << 8 | std::uint32_t(r) << 16 | std::uint32_t(a) << 24);
        } else {
            context_->SetColor(0xffffffffu);
        }
    }
    if (shade) shade->Release();
}

bool cGZDrawContextAsBuffer::Lock(eLockFlags flags) {   // @ 0x10065340a
    if (lockCount_ == 0) {
        suspendOnUnlock_ = context_->IsTargetAttached() ? context_->IsSuspended() : true;
        context_->SetTarget(buffer_);
        if (context_->IsSuspended()) context_->Resume();
    }
    context_->Lock(surface_, static_cast<std::int16_t>(flags) >= 0);
    ++lockCount_;
    return true;
}

bool cGZDrawContextAsBuffer::Unlock(eLockFlags) {   // @ 0x100653496
    if (--lockCount_ == 0 && suspendOnUnlock_) context_->Suspend();
    return true;
}

std::uint32_t cGZDrawContextAsBuffer::Width() const {   // @ 0x1006534c4
    const cRZRect* area = GetBufferArea();
    return static_cast<std::uint32_t>(area->right - area->left);
}

std::uint32_t cGZDrawContextAsBuffer::Height() const {   // @ 0x1006534d8
    const cRZRect* area = GetBufferArea();
    return static_cast<std::uint32_t>(area->bottom - area->top);
}

cGZBufferColorType cGZDrawContextAsBuffer::GetColorType() const {   // @ 0x100653514
    const std::uint32_t index = static_cast<std::uint32_t>(colorType_) - 2;
    return cGZBufferColorType{colorType_, index < 8 ? kBitsPerColorType[index] : 0};
}

bool cGZDrawContextAsBuffer::Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) {   // @ 0x10065357a
    context_->PushTransform();
    if (clip) context_->Clip(*clip);
    context_->SetColor(0, NativeToARGB32(color));
    context_->Fill(rect);
    context_->PopTransform();
    context_->Flush();
    return true;
}

bool cGZDrawContextAsBuffer::Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) {   // @ 0x10065360c
    return Fill(color, cRZRect{l, t, r, b}, clip);
}

bool cGZDrawContextAsBuffer::Fill(std::uint32_t color, const cRZRect* clip) {   // @ 0x100653642
    context_->PushTransform();
    if (clip) context_->Clip(*clip);
    context_->SetColor(0, NativeToARGB32(color));
    context_->Clear();
    context_->PopTransform();
    context_->Flush();
    return true;
}

std::uint32_t cGZDrawContextAsBuffer::GetPixel(std::uint32_t x, std::uint32_t y) const {   // @ 0x1006536c4
    cGZBufferSurfaceDesc desc;
    if (!context_->Lock(desc, true)) return 0;
    const std::uint8_t* row = desc.pixels + static_cast<std::int64_t>(static_cast<std::int32_t>(y)) * desc.pitch;
    switch (desc.bytesPerPixel) {
        case 1: return row[x];
        case 2: return *reinterpret_cast<const std::uint16_t*>(row + std::uint64_t(x) * 2);
        case 3: return std::uint32_t(row[x * 3]) | std::uint32_t(row[x * 3 + 1]) << 8 | std::uint32_t(row[x * 3 + 2]) << 16;
        case 4: return *reinterpret_cast<const std::uint32_t*>(row + std::uint64_t(x) * 4);
        default: return 0;
    }
}

void cGZDrawContextAsBuffer::SetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) {   // @ 0x100653790
    context_->SetColor(0, NativeToARGB32(color));
    context_->Point(static_cast<int>(x), static_cast<int>(y));
    context_->Flush();
}

void cGZDrawContextAsBuffer::SetTransparency(std::uint32_t color) {   // @ 0x1006537f6
    hasKey_ = true;
    buffer_->SetTransparency(color);
}

bool cGZDrawContextAsBuffer::IsPixelTransparent(std::uint32_t x, std::uint32_t y) {   // @ 0x10065381a
    const cRZPoint origin = context_->GetOrigin();
    std::uint32_t key;
    if (!buffer_->GetTransparentColor(key)) return false;
    const std::uint32_t px = x + static_cast<std::uint32_t>(origin.x);
    const std::uint32_t py = y + static_cast<std::uint32_t>(origin.y);
    if (px >= surface_.width || py >= surface_.height) return false;
    const std::uint32_t pixel = GetPixel(px, py);
    const std::uint32_t index = surface_.format - 4;
    const std::uint32_t mask = index < 6 ? kKeyMasks[index] : 0xffffffffu;
    return (mask & (pixel ^ key)) == 0;
}

void cGZDrawContextAsBuffer::RemoveTransparency() {   // @ 0x1006538c8
    hasKey_ = false;
    buffer_->RemoveTransparency();
}

bool cGZDrawContextAsBuffer::DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) {   // @ 0x1006538de
    context_->PushTransform();
    context_->SetColor(0, NativeToARGB32(static_cast<std::uint32_t>(color)));
    context_->SetLineThickness(static_cast<float>(thickness));
    if (thickness < 2) {
        context_->Line(x0, y0, x1, y1);
    } else {
        float points[4] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1)};
        const float half = static_cast<float>(thickness - 1) * 0.5f;
        const int dx = x1 - x0, dy = y1 - y0;
        if ((dx > 0 ? dx : -dx) < (dy > 0 ? dy : -dy)) {   // steep: shift sideways
            points[0] += half;
            points[2] += half;
        } else {
            points[1] += half;
            points[3] += half;
        }
        context_->Draw(eDrawingOp::Line, points, 2);
    }
    context_->PopTransform();
    context_->Flush();
    return true;
}

bool cGZDrawContextAsBuffer::Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) {   // @ 0x100653a56
    context_->PushTransform();
    if (clip) context_->Clip(*clip);
    SetBlitSource(source);
    // The source rectangle is positioned at the given top-left but takes the destination's size.
    const cRZRect from{sourceRect.left, sourceRect.top, sourceRect.left + (destination.right - destination.left),
                       sourceRect.top + (destination.bottom - destination.top)};
    context_->Blt(source, from, destination);
    context_->PopTransform();
    context_->Flush();
    return true;
}

std::uint32_t cGZDrawContextAsBuffer::ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) {   // @ 0x100653b10
    std::uint32_t u;
    switch (colorType_) {
        case kBufferColorXRGB1555:
        case kBufferColorARGB1555:
            u = std::uint32_t(r & 0xf8) * 0x80 + 0xf000 + std::uint32_t(g & 0xf8) * 4;
            return b >> 3 | u;
        case kBufferColorRGB565:
            u = std::uint32_t(r & 0xf8) * 0x100 + 0xf000 + std::uint32_t(g & 0xfc) * 8;
            return b >> 3 | u;
        case kBufferColorARGB4444:
            return (b >> 4) + 0xf000 + (std::uint32_t(g & 0xf0) | std::uint32_t(r & 0xf0) << 4);
        case kBufferColorXRGB8888:
        case kBufferColorARGB8888:
            return std::uint32_t(b) | std::uint32_t(g) << 8 | std::uint32_t(r) << 16 | 0xff000000u;
        default:
            return 0;
    }
}

bool cGZDrawContextAsBuffer::ConvertNativeValueToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {   // @ 0x100653bc4
    const std::uint32_t argb = NativeToARGB32(native);
    r = static_cast<std::uint8_t>(argb >> 16);
    g = static_cast<std::uint8_t>(argb >> 8);
    b = static_cast<std::uint8_t>(argb);
    return true;
}

}  // namespace gz
