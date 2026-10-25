#include "gz/DrawContextRenderToHardware.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "gz/DrawContextRenderToBuffer.h"   // kGZIID_cIGZDrawEngine

#pragma clang fp contract(off)

namespace {

// 12582912.0f = 1.5 * 2^23 (bit pattern 0x4b400000): adding it turns a small float into an integer-valued float, and
// the low mantissa bits of the result are the rounded integer. The original subtracts 0x4b400000 from the *bit pattern*,
// which is numerically the same as subtracting 12582912 from the integer value.
constexpr std::int32_t kRoundMagicValue = 12582912;

// What UpdateTransform installs for an empty clip: only z = 0.5 and w = 1 survive, x and y collapse to the origin.
constexpr float kNullOrthoMat[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1.0f};

// floor(v + 0.5) with the product widened to double first (the original's fixed-point rounding helper).
inline std::int32_t RoundFixed(double v) { return static_cast<std::int32_t>(std::floor(v + 0.5)); }

// 0x1006552c4: rounds one scaled edge of a blit destination to nearest, with exact .5 ties toward -infinity. The caller
// passes `extent * scale - 0.5f`, exactly as the original computes it before the magic-number add.
inline std::int32_t RoundScaledExtent(float x) {
    const float shifted = (x + 0.5f) + 12582912.0f;
    if (!(shifted >= 0.0f && shifted < 16777216.0f)) return 0;   // outside the trick's range (and NaN)
    const std::int32_t high = static_cast<std::int32_t>(shifted) - kRoundMagicValue;
    const std::int32_t low = high - 1;
    return x == static_cast<float>(low) ? low : high;
}

// The anonymous-namespace cRectFloat32::Intersect at 0x100656a82: clamp the float rectangle to `rect` and report
// whether the result is non-empty (strict comparisons).
bool IntersectImageRect(const float source[4], const cRZRect& rect, float out[4]) {
    out[0] = source[0];
    out[1] = source[1];
    out[2] = source[2];
    out[3] = source[3];
    if (static_cast<float>(rect.left) > out[0]) out[0] = static_cast<float>(rect.left);
    if (static_cast<float>(rect.top) > out[1]) out[1] = static_cast<float>(rect.top);
    if (out[2] > static_cast<float>(rect.right)) out[2] = static_cast<float>(rect.right);
    if (out[3] > static_cast<float>(rect.bottom)) out[3] = static_cast<float>(rect.bottom);
    return out[0] < out[2] && out[1] < out[3];
}

inline cRZRect FragmentRect(const cGZDrawImageFragment& f) { return {f.left, f.top, f.right, f.bottom}; }

}  // namespace

// The port keeps the original object layout exactly and appends the seam pointer: 0xf0 + 8 bytes.
static_assert(sizeof(cGZDrawContextRenderToHardware) == 0xf8);

cGZDrawContextRenderToHardware::cGZDrawContextRenderToHardware() = default;   // 0x100654eb0

// 0x100654f2c: the two image lists and the vertex batch are freed, then the blt source and the target are released. The
// hardware backend is not shut down here (Shutdown does that).
cGZDrawContextRenderToHardware::~cGZDrawContextRenderToHardware() {
    imageFragments_.clear();
    imageTextures_.clear();
    vertices_.clear();
    if (bltSource_ != nullptr) {
        cIGZBuffer* source = bltSource_;
        bltSource_ = nullptr;
        source->Release();
    }
    if (target_ != nullptr) {
        cIGZBuffer* target = target_;
        target_ = nullptr;
        target->Release();
    }
}

// 0x100655016: only cIGZUnknown and the draw-engine iid are answered (counted).
bool cGZDrawContextRenderToHardware::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZDrawEngine) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZDrawContextRenderToHardware::AddRef() { return static_cast<std::uint32_t>(++refCount_); }

// 0x100654fec: deletes itself when the count reaches zero.
std::uint32_t cGZDrawContextRenderToHardware::Release() {
    const std::int32_t left = --refCount_;
    if (left == 0) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(left);
}

// 0x100655038: the target is queried for cIGZBuffer, its size is cached, and the hardware is initialised. Any failure
// detaches (and releases) the target again and leaves the engine unusable.
bool cGZDrawContextRenderToHardware::Init(gz::IDrawTargetBuffer* target) {
    if (target_ != nullptr) {
        cIGZBuffer* old = target_;
        target_ = nullptr;
        old->Release();
    }
    if (target == nullptr) return false;
    void* queried = nullptr;
    if (!target->QueryInterface(kGZIID_cIGZBuffer, &queried)) return false;
    target_ = static_cast<cIGZBuffer*>(queried);
    width_ = static_cast<std::int32_t>(target_->Width());      // vslot 0x58
    height_ = static_cast<std::int32_t>(target_->Height());    // vslot 0x60
    if (hardware_ != nullptr && hardware_->Init()) {
        dirty_ = true;
        return true;
    }
    cIGZBuffer* failed = target_;
    target_ = nullptr;
    failed->Release();
    return false;
}

// 0x1006550ce: a tail call into the backend; the target stays attached.
void cGZDrawContextRenderToHardware::Shutdown() {
    if (hardware_ != nullptr) hardware_->Shutdown();
}

// 0x1006550dc: forwards straight to the target buffer (cIGZBuffer vslot 0x70, the GetBufferArea(cRZRect&) overload).
void cGZDrawContextRenderToHardware::GetCanvasArea(cRZRect& out) { target_->GetBufferArea(out); }

// 0x1006550ee: store the clip, intersect it with the target's area and mark the transform dirty. The four straddle
// tests reject a rectangle entirely outside the area, but an inverted one (right < left) that still straddles it is
// accepted and stored inverted: the clamped result is not re-tested for emptiness.
void cGZDrawContextRenderToHardware::SetClipRect(const cRZRect& clip) {
    clip_ = clip;
    cRZRect area;
    target_->GetBufferArea(area);
    if (area.left < clip.right && clip.left < area.right && area.top < clip.bottom && clip.top < area.bottom) {
        clip_.left = std::max(area.left, clip.left);
        clip_.top = std::max(area.top, clip.top);
        clip_.right = std::min(clip.right, area.right);
        clip_.bottom = std::min(clip.bottom, area.bottom);
    } else {
        clip_ = cRZRect{};
    }
    transformDirty_ = true;
}

// 0x1006551a4: ARGB -> ABGR with integer masks; unlike the software engine the target is not asked for its native value.
void cGZDrawContextRenderToHardware::SetColorTable(int first, int count, const std::uint32_t* colors) {
    for (int i = 0; i < count; ++i) {
        const std::uint32_t rgb = colors[i];
        nativeColors_[first + i] = ((rgb & 0xffffff) >> 16) | ((rgb << 8 & 0xff0000) >> 8) | ((rgb << 8 & 0xff00) << 8) |
                                  (rgb & 0xff000000);
    }
}

// 0x100655574: two levels of blend (any / more than one), plus the state dirty flag.
void cGZDrawContextRenderToHardware::SetAlphaBlend(int blend) {
    alphaBlend_ = blend > 0;
    alphaBlendStrong_ = blend > 1;
    dirty_ = true;
}

void cGZDrawContextRenderToHardware::SetBltModulate(bool on) {   // 0x10065558c
    bltModulate_ = on;
    dirty_ = true;
}

void cGZDrawContextRenderToHardware::SetAntialiasing(bool on) { antialiasing_ = on; }   // 0x10065559a (write-only here)
void cGZDrawContextRenderToHardware::SetLineThickness(float) {}                          // 0x1006555a4 (ignored)
void cGZDrawContextRenderToHardware::ClearBltTiling() { tiling_ = false; }               // 0x100655472

void cGZDrawContextRenderToHardware::SetBltTiling(const cRZRect& tile) {   // 0x10065547c
    tiling_ = true;
    tile_ = tile;
}

// 0x100655496: one gradient ramp per B/G/R/A channel. `x` / `y` are the gradient origin (the base value is moved there),
// the steps are 24.8 fixed point (m * 255 * 65536) and the base carries a 0x800000 rounding bias and an extra *256.
void cGZDrawContextRenderToHardware::SetGradientTransform(const float (*matrix)[4], float x, float y) {
    gradientSet_ = matrix != nullptr;
    if (matrix == nullptr) return;
    const float* m = &matrix[0][0];
    for (int i = 0; i < 4; ++i) {
        const std::int32_t dx = RoundFixed(static_cast<double>(m[4 + i] * 16711680.0f));
        const std::int32_t dy = RoundFixed(static_cast<double>(m[8 + i] * 16711680.0f));
        gradientDx_[i] = dx;
        gradientDy_[i] = dy;
        const float shifted = m[i] * 16711680.0f - (static_cast<float>(dy) * y + static_cast<float>(dx) * x);
        const std::int32_t base = RoundFixed(static_cast<double>(shifted));
        gradientBase_[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(base) * 0x100u + 0x800000u);
    }
}

// 0x1006551ce: installs a blit source, tells the backend about it and fills in the destination rectangle. The rectangle
// is the target area scaled by (sx, sy) — rounded with the original's magic sequence — shifted by the truncated origin.
bool cGZDrawContextRenderToHardware::SetBltSource(cIGZUnknown* source, float x, float y, float sx, float sy,
                                                 cRZRect& clip) {
    dirty_ = true;
    if (source != nullptr) {
        if (bltSource_ != nullptr) {
            cIGZBuffer* old = bltSource_;
            bltSource_ = nullptr;
            old->Release();
        }
        void* queried = nullptr;
        if (source->QueryInterface(kGZIID_cIGZBuffer, &queried) &&
            (hardware_ != nullptr && hardware_->SetImage(static_cast<cIGZBuffer*>(queried)))) {
            bltSource_ = static_cast<cIGZBuffer*>(queried);
            bltX_ = x;
            bltY_ = y;
            bltScaleX_ = sx;
            bltScaleY_ = sy;
            bltScaled_ = std::fabs(sx - 1.0f) + std::fabs(sy - 1.0f) > 1e-05f;
            cRZRect area;
            target_->GetBufferArea(area);
            cRZRect destination;
            if (!bltScaled_) {
                destination = area;
            } else {
                destination.left = RoundScaledExtent(static_cast<float>(area.left) * sx - 0.5f);
                destination.top = RoundScaledExtent(static_cast<float>(area.top) * sy - 0.5f);
                destination.right = RoundScaledExtent(static_cast<float>(area.right) * sx - 0.5f);
                destination.bottom = RoundScaledExtent(static_cast<float>(area.bottom) * sy - 0.5f);
            }
            const int originX = static_cast<int>(x);
            const int originY = static_cast<int>(y);
            destination.left += originX;
            destination.right += originX;
            destination.top += originY;
            destination.bottom += originY;
            clip = destination;
            return true;
        }
        if (queried != nullptr) static_cast<cIGZBuffer*>(queried)->Release();
    }
    if (hardware_ != nullptr) hardware_->SetImage(nullptr);
    if (bltSource_ != nullptr) {
        cIGZBuffer* old = bltSource_;
        bltSource_ = nullptr;
        old->Release();
    }
    return false;
}

// 0x1006555aa: both flags are raised, so the next Draw pushes the state and rebuilds the transform.
void cGZDrawContextRenderToHardware::Suspend() {
    dirty_ = true;
    transformDirty_ = true;
    if (hardware_ != nullptr) hardware_->Suspend();
}

// 0x1006555b6: only filled rectangles and filled triangles are handled here; everything else is left to the context's
// software tesselator (the original answers 1 = "tesselate").
gz::eEngineDrawResult cGZDrawContextRenderToHardware::Draw(gz::eDrawingOp op, const float* points, int count) {
    using gz::eDrawingOp;
    using gz::eEngineDrawResult;
    if (op == eDrawingOp::FillRect) {
        if (dirty_) {
            if (hardware_ != nullptr) hardware_->UpdateState();
            dirty_ = false;
        }
        if (transformDirty_) UpdateTransform();
        DrawRects(points, count);
        return eEngineDrawResult::Done;
    }
    if (op != eDrawingOp::FillTri) return eEngineDrawResult::Tesselate;
    if (dirty_) {
        if (hardware_ != nullptr) hardware_->UpdateState();
        dirty_ = false;
    }
    if (transformDirty_) UpdateTransform();
    DrawTriangles(points, count);
    return eEngineDrawResult::Done;
}

// 0x10065564c: an orthographic projection of the clip rectangle onto [-1,1]^2, y down (translation in the fourth row). An empty
// clip installs the degenerate matrix instead.
void cGZDrawContextRenderToHardware::UpdateTransform() {
    transformDirty_ = false;
    if (clip_.left < clip_.right && clip_.top < clip_.bottom) {
        if (hardware_ != nullptr) hardware_->SetViewport(clip_);
        // The literal pool of 0x10065564c: scale +2/w and -2/h (y is flipped: the clip's top row is +1), then the translation
        // -1 - left * scaleX and 1 - top * scaleY; z = 0.5, w = 1 in the last row. (An earlier reading took the constants
        // as 1, 1, 1 and shipped a matrix that put every vertex outside the clip volume.)
        const float scaleX = 2.0f / static_cast<float>(clip_.right - clip_.left);
        const float scaleY = -2.0f / static_cast<float>(clip_.bottom - clip_.top);
        const float matrix[16] = {scaleX, 0.0f, 0.0f, 0.0f,
                                  0.0f, scaleY, 0.0f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f,
                                  -1.0f - static_cast<float>(clip_.left) * scaleX,
                                  1.0f - static_cast<float>(clip_.top) * scaleY, 0.5f, 1.0f};
        if (hardware_ != nullptr) hardware_->SetTransform(matrix);
    } else {
        if (hardware_ != nullptr) hardware_->SetTransform(kNullOrthoMat);
    }
}

// 0x100655782: fills the vertex batch from the point list. Without a blt source the batch goes straight to the backend as
// a triangle list. With one, the batch's bounding box (rounded half down, clipped) becomes a source-space rectangle, which
// is drawn once or tiled over the tile rectangle; each fragment then draws the batch with its own clip and u/v.
void cGZDrawContextRenderToHardware::DrawTriangles(const float* points, int count) {
    vertices_.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float px = points[i * 2];
        const float py = points[i * 2 + 1];
        cGZDrawVertex& vertex = vertices_[static_cast<std::size_t>(i)];
        vertex.x = px;
        vertex.y = py;
        vertex.z = 0.5f;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
        std::uint32_t color = bltSource_ != nullptr ? 0xffffffffu : nativeColors_[0];
        if (gradientSet_) {
            const int gradientX = RoundFixed(static_cast<double>(px * 256.0f));
            const int gradientY = RoundFixed(static_cast<double>(py * 256.0f));
            color = ComputeGradientColor(gradientX, gradientY);
        }
        vertex.color = color;
    }
    if (bltSource_ == nullptr) {
        if (hardware_ != nullptr) hardware_->DrawTriangleList(vertices_.data(), static_cast<int>(vertices_.size()));
        return;
    }
    // The source's buffer area is queried and thrown away (kept: the original does this on every textured triangle list).
    cRZRect sourceArea;
    bltSource_->GetBufferArea(sourceArea);
    (void)sourceArea;

    const cRZRect savedClip = clip_;
    float minX = vertices_[0].x;
    float maxX = minX;
    float minY = vertices_[0].y;
    float maxY = minY;
    for (int i = 1; i < count; ++i) {
        const cGZDrawVertex& vertex = vertices_[static_cast<std::size_t>(i)];
        if (minX <= vertex.x) {
            if (maxX < vertex.x) maxX = vertex.x;
        } else {
            minX = vertex.x;
        }
        if (minY <= vertex.y) {
            if (maxY < vertex.y) maxY = vertex.y;
        } else {
            minY = vertex.y;
        }
    }
    cRZRect bounds{static_cast<int>(std::ceil(minX - 0.5f)), static_cast<int>(std::ceil(minY - 0.5f)),
                   static_cast<int>(std::ceil(maxX - 0.5f)), static_cast<int>(std::ceil(maxY - 0.5f))};
    if (clip_.left < bounds.right && bounds.left < clip_.right && clip_.top < bounds.bottom &&
        bounds.top < clip_.bottom) {
        bounds.left = std::max(bounds.left, clip_.left);
        bounds.top = std::max(bounds.top, clip_.top);
        bounds.right = std::min(bounds.right, clip_.right);
        bounds.bottom = std::min(bounds.bottom, clip_.bottom);
    } else {
        bounds = cRZRect{};
    }
    // Source-space rectangle: (destination - origin) / scale, truncated toward zero.
    cRZRect textureRect{static_cast<int>((static_cast<float>(bounds.left) - bltX_) / bltScaleX_),
                        static_cast<int>((static_cast<float>(bounds.top) - bltY_) / bltScaleY_),
                        static_cast<int>((static_cast<float>(bounds.right) - bltX_) / bltScaleX_),
                        static_cast<int>((static_cast<float>(bounds.bottom) - bltY_) / bltScaleY_)};
    if (!tiling_) {
        DrawTriangleBlt(bltX_, bltY_, textureRect);
    } else {
        const float tileHeight = static_cast<float>(tile_.bottom - tile_.top);
        const float inverseTileHeight = 1.0f / tileHeight;
        const int startRow = static_cast<int>(std::floor(static_cast<float>(textureRect.top) * inverseTileHeight));
        const int endRow = static_cast<int>(std::ceil(static_cast<float>(textureRect.bottom) * inverseTileHeight));
        if (startRow < endRow) {
            const float tileWidth = static_cast<float>(tile_.right - tile_.left);
            const float inverseTileWidth = 1.0f / tileWidth;
            const float xStride = bltScaleX_ * tileWidth;
            const float yStride = bltScaleY_ * tileHeight;
            const int startColumn = static_cast<int>(std::floor(static_cast<float>(textureRect.left) * inverseTileWidth));
            const int endColumn = static_cast<int>(std::ceil(static_cast<float>(textureRect.right) * inverseTileWidth));
            for (int row = startRow; row < endRow; ++row) {
                const float rowOffset = static_cast<float>(row) * yStride;
                for (int column = startColumn; column < endColumn; ++column) {
                    DrawTriangleBlt(static_cast<float>(column) * xStride + bltX_, bltY_ + rowOffset, tile_);
                }
            }
        }
    }
    SetClipRect(savedClip);
}

// 0x100656afe: packs the 24.8 B/G/R/A ramps into an ABGR colour. All arithmetic wraps like the original's 32-bit adds.
std::uint32_t cGZDrawContextRenderToHardware::ComputeGradientColor(int x, int y) {
    const auto ramp = [](const std::int32_t* base, const std::int32_t* dx, const std::int32_t* dy, std::int32_t channel,
                         std::uint32_t xValue, std::uint32_t yValue) {
        return static_cast<std::uint32_t>(dy[channel]) * yValue + static_cast<std::uint32_t>(dx[channel]) * xValue +
               static_cast<std::uint32_t>(base[channel]);
    };
    const std::uint32_t xValue = static_cast<std::uint32_t>(x);
    const std::uint32_t yValue = static_cast<std::uint32_t>(y);
    const std::uint32_t blue = ramp(gradientBase_, gradientDx_, gradientDy_, 0, xValue, yValue) >> 24;
    const std::uint32_t green = (ramp(gradientBase_, gradientDx_, gradientDy_, 1, xValue, yValue) >> 16) & 0xff00u;
    const std::uint32_t red = (ramp(gradientBase_, gradientDx_, gradientDy_, 2, xValue, yValue) >> 8) & 0xff0000u;
    const std::uint32_t alpha = ramp(gradientBase_, gradientDx_, gradientDy_, 3, xValue, yValue) & 0xff000000u;
    return blue | green | red | alpha;
}

// 0x100655bb6: fills the vertex batch with one quad per rectangle (2 points / 4 floats each). Without a blt source the
// quads are coloured with native colour 0 and sent as one quad list. With one, every texture of the backend's image list
// is drawn: one quad per source-space rectangle (or per tile of it when the tile rectangle does not match the fragment).
void cGZDrawContextRenderToHardware::DrawRects(const float* points, int count) {
    const std::uint32_t color = nativeColors_[0];
    const int rectCount = count >> 1;
    const auto pushQuad = [this, color](float x1, float y1, float u1, float v1, float x2, float y2, float u2,
                                        float v2) {
        vertices_.push_back(cGZDrawVertex{x1, y1, 0.5f, color, u1, v1});
        vertices_.push_back(cGZDrawVertex{x1, y2, 0.5f, color, u1, v2});
        vertices_.push_back(cGZDrawVertex{x2, y2, 0.5f, color, u2, v2});
        vertices_.push_back(cGZDrawVertex{x2, y1, 0.5f, color, u2, v1});
    };
    if (bltSource_ == nullptr) {
        vertices_.clear();
        for (int i = 0; i < rectCount; ++i, points += 4) {
            pushQuad(points[0], points[1], 0.0f, 0.0f, points[2], points[3], 0.0f, 0.0f);
        }
        if (hardware_ != nullptr) hardware_->DrawQuadList(vertices_.data(), static_cast<int>(vertices_.size()));
        return;
    }
    // The destination rectangles in source space: (point - origin) / scale, component-wise.
    const float inverseScaleX = 1.0f / bltScaleX_;
    const float inverseScaleY = 1.0f / bltScaleY_;
    const bool validRects = rectCount != 0;
    std::vector<float> sourceRects;
    if (validRects) {
        sourceRects.resize(static_cast<std::size_t>(rectCount) * 4);
        for (int i = 0; i < rectCount; ++i) {
            const float* p = points + i * 4;
            float* source = &sourceRects[static_cast<std::size_t>(i) * 4];
            source[0] = (p[0] - bltX_) * inverseScaleX;
            source[1] = (p[1] - bltY_) * inverseScaleY;
            source[2] = (p[2] - bltX_) * inverseScaleX;
            source[3] = (p[3] - bltY_) * inverseScaleY;
        }
    }
    imageTextures_.clear();
    imageFragments_.clear();
    if (hardware_ != nullptr) hardware_->GetImageFragments(imageTextures_, imageFragments_);
    const cGZDrawImageFragment* fragment = imageFragments_.data();
    for (const cGZDrawImageTexture& texture : imageTextures_) {
        if (hardware_ != nullptr) hardware_->SetTexture(texture.id, bltScaled_, false);
        const float inverseTextureWidth = 1.0f / static_cast<float>(texture.width);
        const float inverseTextureHeight = 1.0f / static_cast<float>(texture.height);
        vertices_.clear();
        for (std::uint32_t f = 0; f < texture.fragmentCount; ++f, ++fragment) {
            const cRZRect fragmentRect = FragmentRect(*fragment);
            if (!tiling_) {
                if (!validRects) continue;
                for (int i = 0; i < rectCount; ++i) {
                    float intersection[4];
                    if (!IntersectImageRect(&sourceRects[static_cast<std::size_t>(i) * 4], fragmentRect, intersection)) {
                        continue;
                    }
                    const float u1 = (static_cast<float>(fragment->u0) +
                                      (intersection[0] - static_cast<float>(fragment->left))) * inverseTextureWidth;
                    const float v1 = (static_cast<float>(fragment->v0) +
                                      (intersection[1] - static_cast<float>(fragment->top))) * inverseTextureHeight;
                    const float u2 = (static_cast<float>(fragment->u0) +
                                      (intersection[2] - static_cast<float>(fragment->left))) * inverseTextureWidth;
                    const float v2 = (static_cast<float>(fragment->v0) +
                                      (intersection[3] - static_cast<float>(fragment->top))) * inverseTextureHeight;
                    pushQuad(intersection[0] * bltScaleX_ + bltX_, intersection[1] * bltScaleY_ + bltY_, u1, v1,
                             intersection[2] * bltScaleX_ + bltX_, intersection[3] * bltScaleY_ + bltY_, u2, v2);
                }
            } else if (fragment->left == tile_.left && fragment->top == tile_.top && fragment->right == tile_.right &&
                       fragment->bottom == tile_.bottom) {
                if (hardware_ != nullptr) hardware_->SetTexture(texture.id, bltScaled_, true);
                if (!validRects) continue;
                for (int i = 0; i < rectCount; ++i) {
                    const float* p = points + i * 4;
                    const float* source = &sourceRects[static_cast<std::size_t>(i) * 4];
                    pushQuad(p[0], p[1], source[0] * inverseTextureWidth, source[1] * inverseTextureHeight, p[2], p[3],
                             source[2] * inverseTextureWidth, source[3] * inverseTextureHeight);
                }
            } else if (fragmentRect.left < tile_.right && tile_.left < fragmentRect.right &&
                       fragmentRect.top < tile_.bottom && tile_.top < fragmentRect.bottom && rectCount > 0) {
                const float tileWidth = static_cast<float>(tile_.right - tile_.left);
                const float tileHeight = static_cast<float>(tile_.bottom - tile_.top);
                for (int i = 0; i < rectCount; ++i) {
                    const float* source = &sourceRects[static_cast<std::size_t>(i) * 4];
                    const int startRow = static_cast<int>(std::floor(source[1] / tileHeight));
                    const int endRow = static_cast<int>(std::ceil(source[3] / tileHeight));
                    const int startColumn = static_cast<int>(std::floor(source[0] / tileWidth));
                    const int endColumn = static_cast<int>(std::ceil(source[2] / tileWidth));
                    for (int row = startRow; row < endRow; ++row) {
                        const int offsetY = tile_.top - (tile_.bottom - tile_.top) * row;
                        for (int column = startColumn; column < endColumn; ++column) {
                            const int offsetX = tile_.left - (tile_.right - tile_.left) * column;
                            const float shifted[4] = {source[0] + static_cast<float>(offsetX),
                                                      source[1] + static_cast<float>(offsetY),
                                                      source[2] + static_cast<float>(offsetX),
                                                      source[3] + static_cast<float>(offsetY)};
                            float clipped[4];
                            (void)IntersectImageRect(shifted, tile_, clipped);
                            float intersection[4];
                            if (!IntersectImageRect(clipped, fragmentRect, intersection)) continue;
                            const float xOffset =
                                static_cast<float>(column) * (tile_.right - tile_.left) * bltScaleX_;
                            const float yOffset = static_cast<float>(row) * (tile_.bottom - tile_.top) * bltScaleY_;
                            const float x1 = (intersection[0] - static_cast<float>(tile_.left)) * bltScaleX_ + bltX_ + xOffset;
                            const float y1 = (intersection[1] - static_cast<float>(tile_.top)) * bltScaleY_ + bltY_ + yOffset;
                            const float x2 = (intersection[2] - static_cast<float>(tile_.left)) * bltScaleX_ + bltX_ + xOffset;
                            const float y2 = (intersection[3] - static_cast<float>(tile_.top)) * bltScaleY_ + bltY_ + yOffset;
                            const float u1 = (static_cast<float>(fragment->u0) +
                                              (intersection[0] - static_cast<float>(fragment->left))) * inverseTextureWidth;
                            const float v1 = (static_cast<float>(fragment->v0) +
                                              (intersection[1] - static_cast<float>(fragment->top))) * inverseTextureHeight;
                            const float u2 = (static_cast<float>(fragment->u0) +
                                              (intersection[2] - static_cast<float>(fragment->left))) * inverseTextureWidth;
                            const float v2 = (static_cast<float>(fragment->v0) +
                                              (intersection[3] - static_cast<float>(fragment->top))) * inverseTextureHeight;
                            pushQuad(x1, y1, u1, v1, x2, y2, u2, v2);
                        }
                    }
                }
            }
        }
        if (hardware_ != nullptr) hardware_->DrawQuadList(vertices_.data(), static_cast<int>(vertices_.size()));
    }
}

// 0x100656bbc: draws the current vertex batch once per image fragment that overlaps `rect`. The fragment's u/v rectangle
// becomes a clip rectangle (scaled and shifted), the batch's u/v are re-normalised by 1 / (scale * texture size), and the
// backend draws the batch as a triangle list.
void cGZDrawContextRenderToHardware::DrawTriangleBlt(float x, float y, const cRZRect& rect) {
    const int vertexCount = static_cast<int>(vertices_.size());
    imageTextures_.clear();
    imageFragments_.clear();
    if (hardware_ != nullptr) hardware_->GetImageFragments(imageTextures_, imageFragments_);
    if (imageTextures_.empty()) return;
    const int originX = static_cast<int>(x);
    const int originY = static_cast<int>(y);
    const cGZDrawImageFragment* fragment = imageFragments_.data();
    for (const cGZDrawImageTexture& texture : imageTextures_) {
        const float textureWidth = static_cast<float>(texture.width);
        const float textureHeightScaled = static_cast<float>(texture.height) * bltScaleY_;
        if (hardware_ != nullptr) hardware_->SetTexture(texture.id, bltScaled_, false);
        if (texture.fragmentCount == 0) continue;
        const float inverseWidth = 1.0f / (bltScaleX_ * textureWidth);
        const float inverseHeight = 1.0f / textureHeightScaled;
        for (std::uint32_t f = 0; f < texture.fragmentCount; ++f, ++fragment) {
            if (!(fragment->left < rect.right && rect.left < fragment->right && fragment->top < rect.bottom &&
                  rect.top < fragment->bottom)) {
                continue;
            }
            for (cGZDrawVertex& vertex : vertices_) {
                vertex.u = (static_cast<float>(fragment->u0) +
                            ((vertex.x - x) - static_cast<float>(fragment->left))) * inverseWidth;
                vertex.v = (static_cast<float>(fragment->v0) +
                            ((vertex.y - y) - static_cast<float>(fragment->top))) * inverseHeight;
            }
            cRZRect subRect;
            subRect.left = static_cast<int>(bltScaleX_ * static_cast<float>(fragment->u0)) + originX;
            subRect.top = static_cast<int>(bltScaleY_ * static_cast<float>(fragment->v0)) + originY;
            subRect.right = static_cast<int>(bltScaleX_ * static_cast<float>(fragment->u1)) + originX;
            subRect.bottom = static_cast<int>(bltScaleY_ * static_cast<float>(fragment->v1)) + originY;
            SetClipRect(subRect);
            UpdateTransform();
            if (hardware_ != nullptr) hardware_->DrawTriangleList(vertices_.data(), vertexCount);
        }
    }
}

void cGZDrawContextRenderToHardware::Flush() {}                                                   // 0x1007b4f4c (nothing to do)
void cGZDrawContextRenderToHardware::Resume() {                                                   // 0x100656e7c (nothing to do)
    if (hardware_ != nullptr) hardware_->Resume();                                                // the derived class's Resume
}
bool cGZDrawContextRenderToHardware::LockTarget(gz::cGZBufferSurfaceDesc&, bool) { return false; }   // 0x100656e82
void cGZDrawContextRenderToHardware::UnlockTarget() {}                                            // 0x100656e8a (nothing to do)

void cGZDrawContextRenderToHardware::DrawPreclippedSpanList(const std::uint32_t*, int) {}   // 0x100656a7c (nothing to do)
