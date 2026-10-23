#include "gz/DrawContextRenderToBuffer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rz/SpanList.h"

#pragma clang fp contract(off)

namespace {

constexpr std::uint32_t kLockTarget = 0x8040;   // lock flags used on the target
constexpr std::uint32_t kLockSource = 0x800;    // lock flags used on a blt source
// nGZDrawSoftRender::SrcFormatId of each colour type - 2 (table at 0x100a27fa0); type 3 has none.
constexpr std::uint32_t kSourceFormat[8] = {6, 0, 2, 3, 1, 0, 5, 4};

// floor(v + 0.5) the way the original computes it: the float product is widened to double before the 0.5 is added.
inline std::int32_t RoundFixed(double v) { return static_cast<std::int32_t>(std::floor(v + 0.5)); }

}  // namespace

cGZDrawContextRenderToBuffer::cGZDrawContextRenderToBuffer() = default;   // 0x100653c8c

// 0x100653cc4: the span renderer, the blt source and the target are let go.
cGZDrawContextRenderToBuffer::~cGZDrawContextRenderToBuffer() {
    spanRender_.reset();
    if (bltSource_ != nullptr) bltSource_->Release();
    if (target_ != nullptr) target_->Release();
}

bool cGZDrawContextRenderToBuffer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZDrawEngine) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZDrawContextRenderToBuffer::AddRef() { return static_cast<std::uint32_t>(++refCount_); }

// 0x100653d60: deletes itself when the count reaches zero.
std::uint32_t cGZDrawContextRenderToBuffer::Release() {
    const std::int32_t left = --refCount_;
    if (left == 0) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(left);
}

// 0x100653dca: locks the buffer and picks the span renderer of its pixel format.
bool cGZDrawContextRenderToBuffer::Init(gz::IDrawTargetBuffer* target) {
    Shutdown();
    if (target_ != nullptr) {
        cIGZBuffer* old = target_;
        target_ = nullptr;
        old->Release();
    }
    void* queried = nullptr;
    if (!target->QueryInterface(kGZIID_cIGZBuffer, &queried)) return false;
    target_ = static_cast<cIGZBuffer*>(queried);
    auto drop = [this]() {
        cIGZBuffer* b = target_;
        target_ = nullptr;
        if (b != nullptr) b->Release();
    };
    if (!target_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockTarget))) {
        drop();
        return false;
    }
    const std::int32_t type = target_->GetColorType().type;   // vslot 0x80
    switch (type) {
        case 2: spanRender_ = std::make_unique<nGZDrawSoftRender::cSpanRender8>(); break;
        case 4:
        case 6: spanRender_ = std::make_unique<nGZDrawSoftRender::cSpanRender1555>(); break;
        case 5: spanRender_ = std::make_unique<nGZDrawSoftRender::cSpanRender565>(); break;
        case 7: spanRender_ = std::make_unique<nGZDrawSoftRender::cSpanRender4444>(); break;
        case 8:
        case 9: spanRender_ = std::make_unique<nGZDrawSoftRender::cSpanRender8888>(); break;
        default:
            drop();   // (the lock is not released: kept)
            return false;
    }
    surface_.width = target_->Width();                                  // vslot 0x58
    surface_.height = target_->Height();                                // vslot 0x60
    surface_.reserved[0] = static_cast<std::uint32_t>(type);
    surface_.pixels = static_cast<std::uint8_t*>(target_->GetColorSurfaceBits());       // vslot 0x120
    surface_.pitch = target_->GetColorSurfaceStride();                  // vslot 0x128
    surface_.reserved[1] = target_->GetBytesPerPixel();                 // vslot 0x98
    canvasWidth_ = static_cast<std::int32_t>(surface_.width);
    canvasHeight_ = static_cast<std::int32_t>(surface_.height);
    surface_.reserved[2] = target_->GetBytesPerPixel();
    tiling_ = false;
    locked_ = true;
    return true;
}

// 0x100653f78
void cGZDrawContextRenderToBuffer::Shutdown() {
    spanRender_.reset();
    if (target_ == nullptr) return;
    if (locked_) target_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockTarget));
    cIGZBuffer* b = target_;
    target_ = nullptr;
    b->Release();
}

void cGZDrawContextRenderToBuffer::GetCanvasArea(cRZRect& out) { target_->GetBufferArea(out); }   // 0x100653fe0 (vslot 0x70)

void cGZDrawContextRenderToBuffer::SetClipRect(const cRZRect& clip) { clip_ = clip; }   // 0x100653ff2

// 0x100654008: keeps both the RGB value and the target's native pixel value of each entry (four entries are used).
void cGZDrawContextRenderToBuffer::SetColorTable(int first, int count, const std::uint32_t* colors) {
    for (int i = 0; i < count; ++i) {
        const int index = first + i;
        const std::uint32_t rgb = colors[i];
        rgbColors_[index] = rgb;
        nativeColors_[index] = target_->ConvertRGBValueToNative(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                                                                static_cast<std::uint8_t>(rgb));   // vslot 0x100
    }
}

void cGZDrawContextRenderToBuffer::SetAlphaBlend(int blend) { alphaBlend_ = blend > 0; }   // 0x100654430
void cGZDrawContextRenderToBuffer::SetBltModulate(bool on) { bltModulate_ = on; }          // 0x10065443c
void cGZDrawContextRenderToBuffer::SetAntialiasing(bool on) { antialiasing_ = on; }        // 0x100654446
void cGZDrawContextRenderToBuffer::SetLineThickness(float) {}                              // 0x100654450
void cGZDrawContextRenderToBuffer::Flush() {}                                              // 0x100654456

// 0x100654068: three matrix rows (start, per-pixel step, per-row step) become 16.16 channel ramps (x 255); the start is
// moved to the origin (x, y) of the gradient and gets the 0.5 rounding bias.
void cGZDrawContextRenderToBuffer::SetGradientTransform(const float (*matrix)[4], float x, float y) {
    if (matrix == nullptr) {
        gradientSet_ = false;
        return;
    }
    const float* m = &matrix[0][0];
    for (int i = 0; i < 4; ++i) {
        const std::int32_t dx = static_cast<std::int32_t>(std::floor(static_cast<double>(m[4 + i] * 16711680.0f) + 0.5));
        const std::int32_t dy = static_cast<std::int32_t>(std::floor(static_cast<double>(m[8 + i] * 16711680.0f) + 0.5));
        const std::int32_t start = static_cast<std::int32_t>(std::floor(static_cast<double>(m[i] * 16711680.0f) + 0.5));
        gradient_.dx[i] = dx;
        gradient_.dy[i] = dy;
        const float shifted = static_cast<float>(start + 0x8000) - (static_cast<float>(dy) * y + static_cast<float>(dx) * x);
        gradient_.base[i] = static_cast<std::int32_t>(shifted);
    }
    gradientSet_ = true;
}

// 0x100654138: installs a blt source drawn at (x, y) with scale (sx, sy); `clip` receives the destination rectangle.
// Returns true even when the source does not expose a cIGZBuffer (kept).
bool cGZDrawContextRenderToBuffer::SetBltSource(cIGZUnknown* source, float x, float y, float sx, float sy, cRZRect& clip) {
    if (bltSource_ != nullptr) {
        cIGZBuffer* old = bltSource_;
        bltSource_ = nullptr;
        old->Release();
    }
    if (source == nullptr) return false;
    void* queried = nullptr;
    if (!source->QueryInterface(kGZIID_cIGZBuffer, &queried)) return true;
    bltSource_ = static_cast<cIGZBuffer*>(queried);
    cRZRect area;
    bltSource_->GetBufferArea(area);   // vslot 0x70
    bltScaleX_ = RoundFixed(static_cast<double>(sx * 65536.0f));
    bltScaleY_ = RoundFixed(static_cast<double>(sy * 65536.0f));
    bltInverseX_ = RoundFixed(static_cast<double>(65536.0f / sx));
    bltInverseY_ = RoundFixed(static_cast<double>(65536.0f / sy));
    cRZRect destination;
    if (bltScaleX_ == 0x10000 && bltScaleY_ == 0x10000) {
        bltOriginX_ = RoundFixed(static_cast<double>(x));
        bltOriginY_ = RoundFixed(static_cast<double>(y));
        bltScaled_ = false;
        destination = {area.left + bltOriginX_, area.top + bltOriginY_, area.right + bltOriginX_, area.bottom + bltOriginY_};
    } else {
        bltOriginX_ = RoundFixed(static_cast<double>(static_cast<float>(bltInverseX_) * x));
        bltOriginY_ = RoundFixed(static_cast<double>(static_cast<float>(bltInverseY_) * y));
        bltScaled_ = true;
        const float corners[4] = {x, y, static_cast<float>(area.right - area.left) * sx + x, static_cast<float>(area.bottom - area.top) * sy + y};
        // round half down: the original adds 1.5 * 2^23 to force round-to-even and corrects the exact .5 cases
        std::int32_t r[4];
        for (int i = 0; i < 4; ++i) r[i] = static_cast<std::int32_t>(std::ceil(corners[i] - 0.5f));
        destination = {r[0], r[1], r[2], r[3]};
    }
    clip = destination;
    bltWidth_ = destination.right - destination.left;
    bltHeight_ = destination.bottom - destination.top;
    return true;
}

void cGZDrawContextRenderToBuffer::ClearBltTiling() { tiling_ = false; }   // 0x100654400

void cGZDrawContextRenderToBuffer::SetBltTiling(const cRZRect& tile) {   // 0x10065440e
    tiling_ = true;
    tile_ = tile;
}

// 0x10065445c / 0x10065448c: the target is unlocked while the context is suspended.
void cGZDrawContextRenderToBuffer::Suspend() {
    if (locked_) {
        target_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockTarget));
        locked_ = false;
    }
}

void cGZDrawContextRenderToBuffer::Resume() {
    if (!locked_) {
        target_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockTarget));
        locked_ = true;
    }
}

// 0x1006544bc: the cached surface description of the locked target.
bool cGZDrawContextRenderToBuffer::LockTarget(gz::cGZBufferSurfaceDesc& desc, bool) {
    desc.pixels = surface_.pixels;
    desc.pitch = surface_.pitch;
    desc.width = surface_.width;
    desc.height = surface_.height;
    desc.format = surface_.reserved[0];
    desc.bytesPerPixel = surface_.reserved[1];
    return true;
}

void cGZDrawContextRenderToBuffer::UnlockTarget() {}   // 0x1006544f0

// 0x1006544f6: with antialiasing on, polygons are filled here; everything else is left to the context (Rasterize) or, when
// antialiasing is on and the primitive is not a polygon, to be tesselated.
gz::eEngineDrawResult cGZDrawContextRenderToBuffer::Draw(gz::eDrawingOp op, const float* points, int count) {
    using gz::eDrawingOp;
    using gz::eEngineDrawResult;
    if (!antialiasing_) return eEngineDrawResult::Rasterize;
    if (op == eDrawingOp::FillPolygon) {
        FillAntialiasedPolygon(points, count);
        return eEngineDrawResult::Done;
    }
    if (op == eDrawingOp::FillQuad) {
        for (; count >= 4; count -= 4, points += 8) FillAntialiasedPolygon(points, 4);
        return eEngineDrawResult::Done;
    }
    if (op == eDrawingOp::FillTri) {
        for (; count >= 3; count -= 3, points += 6) FillAntialiasedPolygon(points, 3);
        return eEngineDrawResult::Done;
    }
    return eEngineDrawResult::Tesselate;
}

// 0x1006545a6: the polygon is scaled by 8 and rasterized into spans; per pixel the covered sub-samples (of 64) become an
// alpha, and the pixel is set or blended into the target through its own accessors.
void cGZDrawContextRenderToBuffer::FillAntialiasedPolygon(const float* points, int count) {
    if (count < 3) return;
    std::vector<float> scaled(static_cast<std::size_t>(count) * 2);
    float maxX = points[0] * 8.0f, maxY = points[1] * 8.0f;
    float minX = maxX, minY = maxY;
    scaled[0] = maxX;
    scaled[1] = points[1] * 8.0f;
    for (int i = 1; i < count; ++i) {
        const float x = points[i * 2] * 8.0f;
        const float y = points[i * 2 + 1] * 8.0f;
        scaled[static_cast<std::size_t>(i) * 2] = x;
        scaled[static_cast<std::size_t>(i) * 2 + 1] = y;
        if (minX <= x) { if (maxX < x) maxX = x; } else minX = x;
        if (minY <= y) { if (maxY < y) maxY = y; } else minY = y;
    }
    cRZSpanList list;
    list.SetPolygonSimple(scaled.data(), count);
    const std::int32_t height = static_cast<std::int32_t>(target_->Height());   // vslot 0x60

    // The pixel box of the polygon, clipped.
    std::int32_t left = static_cast<std::int32_t>(std::ceil(minX - 0.5f)) >> 3;
    std::int32_t top = static_cast<std::int32_t>(std::ceil(minY - 0.5f)) >> 3;
    std::int32_t right = (static_cast<std::int32_t>(std::ceil(maxX - 0.5f)) + 7) >> 3;
    std::int32_t bottom = (static_cast<std::int32_t>(std::ceil(maxY - 0.5f)) + 7) >> 3;
    left = std::max(left, clip_.left);
    right = std::min(right, clip_.right);
    top = std::max(top, clip_.top);
    bottom = std::min(bottom, clip_.bottom);
    if (!(right - left != 0 && left <= right && top < bottom)) return;

    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(right - left));
    const std::int32_t topSub = top << 3, bottomSub = bottom << 3;
    const std::int32_t leftSub = left * 8, rightSub = right << 3;
    std::int32_t currentRow = -1;
    const std::uint32_t rgb = rgbColors_[0];
    const std::int32_t colorR = (rgb >> 16) & 0xff, colorG = (rgb >> 8) & 0xff, colorB = rgb & 0xff;

    auto flush = [&](std::int32_t row) {
        for (std::int32_t i = 0; i < right - left; ++i) {
            const std::uint32_t alpha = static_cast<std::uint32_t>(coverage[static_cast<std::size_t>(i)]) * 0xff >> 6;
            if (alpha != 0) {
                if (alpha == 0xff) {
                    target_->SetPixel(static_cast<std::uint32_t>(left + i), static_cast<std::uint32_t>(row), nativeColors_[0]);   // vslot 0xc0
                } else {
                    const std::uint32_t dst = target_->GetPixel(static_cast<std::uint32_t>(left + i), static_cast<std::uint32_t>(row));   // vslot 0xb8
                    std::uint8_t r, g, b;
                    target_->ConvertNativeValueToRGB(dst, r, g, b);   // vslot 0x108
                    const std::int32_t a = static_cast<std::int32_t>(alpha);
                    const std::uint8_t nr = static_cast<std::uint8_t>((((colorR - r) * a + 0x80) >> 8) + r);
                    const std::uint8_t ng = static_cast<std::uint8_t>((((colorG - g) * a + 0x80) >> 8) + g);
                    const std::uint8_t nb = static_cast<std::uint8_t>((((colorB - b) * a + 0x80) >> 8) + b);
                    target_->SetPixel(static_cast<std::uint32_t>(left + i), static_cast<std::uint32_t>(row),
                                      target_->ConvertRGBValueToNative(nr, ng, nb));   // vslot 0x100
                }
            }
            coverage[static_cast<std::size_t>(i)] = 0;
        }
    };

    const auto& spans = list.Spans();
    for (std::size_t s = 0; s <= spans.size(); ++s) {
        const bool atEnd = s == spans.size();
        const std::int32_t y = atEnd ? -1 : spans[s].y;
        const std::int32_t row = y >> 3;
        std::int32_t nextRow = currentRow;
        if (row != currentRow) {
            nextRow = row;
            if (currentRow >= 0 && currentRow < height) flush(currentRow);
        }
        currentRow = nextRow;
        if (topSub <= y && y < bottomSub) {
            std::int32_t xa = std::max<std::int32_t>(spans[s].x0, leftSub);
            std::int32_t xb = std::min<std::int32_t>(spans[s].x1, rightSub);
            if (xa < xb) {
                std::int32_t ca = xa >> 3;
                const std::int32_t cb = xb >> 3;
                bool done = false;
                if (ca != cb) {
                    coverage[static_cast<std::size_t>(ca - left)] = static_cast<std::uint8_t>(coverage[static_cast<std::size_t>(ca - left)] + (8 - (xa & 7)));
                    for (std::int32_t c = ca + 1; c < cb; ++c) coverage[static_cast<std::size_t>(c - left)] = static_cast<std::uint8_t>(coverage[static_cast<std::size_t>(c - left)] + 8);
                    if ((xb & 7) == 0) done = true;
                    xa = cb * 8;
                    ca = cb;
                }
                if (!done) coverage[static_cast<std::size_t>(ca - left)] = static_cast<std::uint8_t>(coverage[static_cast<std::size_t>(ca - left)] + (xb - xa));
            }
        }
    }
}

// 0x100654b2a: fills clipped spans (rows packed as (y << 16) | x pairs) with the current colour, gradient or blt source.
void cGZDrawContextRenderToBuffer::DrawPreclippedSpanList(const std::uint32_t* spans, int count) {
    using namespace nGZDrawSoftRender;
    if (bltSource_ != nullptr) {
        if (!bltSource_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockSource))) return;
        cGZBufferSurfaceDesc source{};
        source.width = bltSource_->Width();
        source.height = bltSource_->Height();
        const std::int32_t type = bltSource_->GetColorType().type;
        source.pixels = static_cast<std::uint8_t*>(bltSource_->GetColorSurfaceBits());
        source.pitch = bltSource_->GetColorSurfaceStride();
        const std::uint32_t bytesPerPixel = bltSource_->GetBytesPerPixel();
        const std::uint32_t index = static_cast<std::uint32_t>(type) - 2;
        if (index < 8 && ((0xfdu >> index) & 1)) {
            source.reserved[0] = static_cast<std::uint32_t>(type);
            cBltInfo info{};
            info.srcFormatId = kSourceFormat[index];
            std::uint32_t key = 0;
            bool keyed = bltSource_->GetTransparentColor(key);   // vslot 0xe0
            if (keyed) {
                switch (info.srcFormatId) {
                    case 0: key &= 0xfff; break;
                    case 1:
                    case 2: key &= 0x7fff; break;
                    case 3: key &= 0xffff; break;
                    case 4:
                    case 5: key &= 0xffffff; break;
                    case 6: keyed = false; break;
                }
            }
            info.key = key;
            info.keyed = keyed;
            info.colour = rgbColors_[0];
            std::uint32_t method = alphaBlend_ ? (bltModulate_ ? 3u : 1u) : (bltModulate_ ? 2u : 0u);
            if (!tiling_) {
                if (!bltScaled_) {
                    source.pixels = source.pixels - static_cast<std::int64_t>(bltOriginY_) * static_cast<std::int64_t>(source.pitch) -
                                    static_cast<std::int64_t>(bltOriginX_) * bytesPerPixel;
                } else {
                    info.stepX = bltInverseX_;
                    info.startX = (info.stepX >> 1) - bltOriginX_;
                    info.stepY = bltInverseY_;
                    info.startY = (info.stepY >> 1) - bltOriginY_;
                    method |= 4;
                }
            } else {
                source.width = static_cast<std::uint32_t>(tile_.right - tile_.left);
                source.height = static_cast<std::uint32_t>(tile_.bottom - tile_.top);
                source.pixels = source.pixels + static_cast<std::uint64_t>(bytesPerPixel) * static_cast<std::uint32_t>(tile_.left) +
                                static_cast<std::uint64_t>(source.pitch) * static_cast<std::int64_t>(tile_.top);
                info.startX = static_cast<std::int32_t>(((0x7fffffffu % source.width ^ 0x7fffffffu) - static_cast<std::uint32_t>(bltOriginX_)) % source.width);
                info.startY = static_cast<std::int32_t>(((0x7fffffffu % source.height ^ 0x7fffffffu) - static_cast<std::uint32_t>(bltOriginY_)) % source.height);
                method |= 8;
            }
            spanRender_->RenderBlt(spans, static_cast<std::uint32_t>(count), surface_, source, info, method);
        }
        bltSource_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockSource));
        return;
    }
    if (!gradientSet_) {
        if (!alphaBlend_) spanRender_->RenderSolid(spans, static_cast<std::uint32_t>(count), surface_, nativeColors_[0]);
        else spanRender_->RenderAlpha(spans, static_cast<std::uint32_t>(count), surface_, nativeColors_[0], rgbColors_[0]);
    } else if (!alphaBlend_) {
        spanRender_->RenderGradient(spans, static_cast<std::uint32_t>(count), surface_, gradient_);
    } else {
        spanRender_->RenderAlphaGradient(spans, static_cast<std::uint32_t>(count), surface_, gradient_);
    }
}
