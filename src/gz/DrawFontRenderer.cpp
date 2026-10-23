#include "gz/DrawFontRenderer.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <vector>

namespace {
// Per-type channel widths (tables at 0x100a28060 / 0x100a28080 / 0x100a280a0, indexed by colour type - 4): alpha bits,
// red / blue bits, green bits for XRGB1555, RGB565, ARGB1555, ARGB4444, XRGB8888, ARGB8888.
constexpr int kAlphaBits[6] = {0, 0, 1, 4, 0, 8};
constexpr int kEdgeBits[6] = {5, 5, 5, 4, 8, 8};
constexpr int kGreenBits[6] = {5, 6, 5, 4, 8, 8};
}  // namespace

// 0x1006572ce: the inner buffer renderer is created straight away.
cGZDrawFontRenderer::cGZDrawFontRenderer(IDrawFontRendererHost* host) : host_(host) {
    bufferRenderer_ = host_->CreateBufferFontRenderer();
}

// 0x1006573fc: releases the context, the inner renderer and the atlas.
cGZDrawFontRenderer::~cGZDrawFontRenderer() {
    if (context_ != nullptr) context_->Release();
    if (bufferRenderer_ != nullptr) bufferRenderer_->Release();
    if (atlas_ != nullptr) atlas_->Release();
}

bool cGZDrawFontRenderer::QueryInterface(std::uint32_t iid, void** out) {   // 0x1006574d4
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZFontRenderer) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZDrawFontRenderer::AddRef() { return ++refCount_; }   // 0x1006574f6

std::uint32_t cGZDrawFontRenderer::Release() {   // 0x100657504
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

bool cGZDrawFontRenderer::Init(cIGZFont*) { return true; }   // 0x10065751e

// 0x100657526: forgets the font's cached glyph positions.
bool cGZDrawFontRenderer::Shutdown(cIGZFont* font) {
    auto it = fontMaps_.find(font);
    if (it != fontMaps_.end()) {
        if (currentMap_ == &it->second) currentMap_ = nullptr;   // (the original leaves the pointer dangling)
        fontMaps_.erase(it);
    }
    return true;
}

bool cGZDrawFontRenderer::PrepChar(std::uint32_t, const std::uint8_t*, const cRZRect*, std::uint32_t) { return true; }   // 0x1006585a0

void cGZDrawFontRenderer::SetGlyphCacheDimensionsHint(std::uint32_t width, std::uint32_t height) {   // 0x1006585a8
    atlasWidth_ = width;
    atlasHeight_ = height;
    CreateTexturePages();
}

std::size_t cGZDrawFontRenderer::CachedGlyphCount(cIGZFont* font) const {
    auto it = fontMaps_.find(font);
    return it == fontMaps_.end() ? 0 : it->second.size();
}

// 0x10065754a
bool cGZDrawFontRenderer::DrawBegin(cIGZFont* font, void* surface, eRenderingMode mode) {
    direct_ = true;
    originX_ = originY_ = 0;
    cIGZBufferDrawTarget* target = nullptr;
    auto* unknown = static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(surface));
    void* queried = nullptr;
    if (unknown->QueryInterface(kGZIID_cIGZBufferDrawTarget, &queried)) target = static_cast<cIGZBufferDrawTarget*>(queried);
    if (target != nullptr) {
        gz::cIGZDrawContext* context = target->LockContext();   // +0x28
        gz::cGZBufferSurfaceDesc desc{};
        if (!context->Lock(desc, true)) {   // vslot 0x170
            // Not lockable as pixels: draw through the context.
            context->Unlock();                                                         // vslot 0x178
            if (context_ != context) {
                context->AddRef();
                gz::cIGZDrawContext* old = context_;
                context_ = context;
                if (old != nullptr) old->Release();
            }
            direct_ = false;
        } else {
            const cRZPoint origin = context->GetOrigin();   // vslot 0x70
            originX_ = origin.x;
            originY_ = origin.y;
            target->UnlockContext();                         // +0x30
        }
    }
    bool result;
    if (!direct_) {
        if (atlas_ == nullptr) CreateTexturePages();
        currentMap_ = &fontMaps_[font];
        result = true;
    } else {
        result = bufferRenderer_->DrawBegin(font, surface, mode);
    }
    if (target != nullptr) target->Release();
    return result;
}

// 0x10065791e: (re)creates the 256x256 ARGB4444 atlas and the coverage -> texel table.
void cGZDrawFontRenderer::CreateTexturePages() {
    if (atlas_ != nullptr) {
        atlas_->Release();
        atlas_ = nullptr;
    }
    atlas_ = host_->CreateBuffer();
    if (atlas_ != nullptr) {
        atlas_->Init(atlasWidth_, atlasHeight_, cGZBufferColorType{kBufferColorARGB4444, 16});   // the original passes {7, 1}: this cGZBuffer wants the bit count
        const int type = atlas_->GetColorType().type;
        int alphaBits = 8, edgeBits = 8, greenBits = 8;
        if (static_cast<unsigned>(type - 4) < 6) {
            alphaBits = kAlphaBits[type - 4];
            edgeBits = kEdgeBits[type - 4];
            greenBits = kGreenBits[type - 4];
        }
        const int alphaShift = greenBits + 2 * edgeBits;
        // Every colour channel full: the texel is white, its alpha field carries the coverage.
        const std::uint32_t white = static_cast<std::uint32_t>((((1ll << greenBits) - 1) << edgeBits) - 1 + (1ll << edgeBits) +
                                                               (((1ll << edgeBits) - 1) << (greenBits + edgeBits)));
        const std::uint32_t topMask = static_cast<std::uint32_t>(0x100 - (0x100u >> alphaBits));
        const std::uint32_t scale = alphaBits == 0 ? 0 : static_cast<std::uint32_t>(0xffffffffull / ((1ull << alphaBits) - 1));
        const std::uint32_t fieldMask = static_cast<std::uint32_t>(((1ll << alphaBits) - 1) << alphaShift);
        const int shift = (32 - alphaBits) - alphaShift;
        for (std::uint32_t i = 0; i < 256; ++i) {
            const std::uint32_t alpha = ((i & topMask) * scale) >> shift & fieldMask;
            coverageTable_[i] = alpha + white;
        }
    }
    penX_ = penY_ = 0;
    rowHeight_ = 0;
    fontMaps_.clear();
    currentMap_ = nullptr;
}

// 0x100657b0c
bool cGZDrawFontRenderer::DrawEnd(cIGZFont* font, void* surface, eRenderingMode mode) {
    if (direct_) return bufferRenderer_->DrawEnd(font, surface, mode);
    currentMap_ = nullptr;
    void* queried = nullptr;
    static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(surface))->QueryInterface(kGZIID_cIGZBufferDrawTarget, &queried);
    auto* target = static_cast<cIGZBufferDrawTarget*>(queried);
    target->UnlockContext();
    target->Release();
    return true;
}

// 0x100658052
const cGZDrawFontRenderer::cCachedGlyphImgInfo* cGZDrawFontRenderer::CreateCachedGlyphImg(cIGZFont* font, const sGlyphInfo& glyph,
                                                                                         bool flushIfFull) {
    const int width = static_cast<int>(glyph.width);
    const int height = static_cast<int>(glyph.height);
    const int atlasW = static_cast<int>(atlas_->Width());
    const int atlasH = static_cast<int>(atlas_->Height());
    std::uint32_t row;
    if (atlasW < static_cast<int>(penX_) + width || atlasH < static_cast<int>(penY_) + height) {
        // Start a new row.
        penX_ = 0;
        penY_ += rowHeight_;
        row = static_cast<std::uint32_t>(height);
        if (atlasH < static_cast<int>(penY_) + height) {
            if (!flushIfFull) return nullptr;
            // Atlas full: forget everything and start over.
            penX_ = penY_ = 0;
            rowHeight_ = 0;
            fontMaps_.clear();
        }
    } else {
        row = std::max<std::uint32_t>(rowHeight_, static_cast<std::uint32_t>(height));
    }
    rowHeight_ = row;
    GlyphMap& map = fontMaps_[font];
    auto found = map.find(glyph.character);
    if (found == map.end()) found = map.emplace(glyph.character, cCachedGlyphImgInfo{static_cast<std::int32_t>(penX_), static_cast<std::int32_t>(penY_)}).first;
    currentMap_ = &fontMaps_[font];
    cCachedGlyphImgInfo* info = &found->second;

    if (atlas_->Lock(static_cast<cIGZBuffer::eLockFlags>(0x8040))) {
        std::uint8_t* bits = static_cast<std::uint8_t*>(atlas_->GetColorSurfaceBits());
        const std::uint32_t stride = atlas_->GetColorSurfaceStride();
        const std::uint32_t bytesPerPixel = atlas_->GetBytesPerPixel();
        const std::uint32_t bpp = atlas_->GetBitsPerPixel();
        const std::uint8_t* source = glyph.pixels;
        const int skip = static_cast<int>(glyph.pitch) - width;
        std::uint8_t* dst = bits + static_cast<std::size_t>(penX_) * bytesPerPixel + static_cast<std::size_t>(penY_) * stride;
        if (bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32) {
            for (int y = 0; y < height; ++y) {
                std::uint8_t* out = dst;
                for (int x = 0; x < width; ++x) {
                    const std::uint32_t texel = coverageTable_[*source++];
                    switch (bpp) {
                        case 8: *out = static_cast<std::uint8_t>(texel); out += 1; break;
                        case 16: { const std::uint16_t v = static_cast<std::uint16_t>(texel); std::memcpy(out, &v, 2); out += 2; break; }
                        case 24: out[0] = static_cast<std::uint8_t>(texel); out[1] = static_cast<std::uint8_t>(texel >> 8);
                                 out[2] = static_cast<std::uint8_t>(texel >> 16); out += 3; break;
                        default: std::memcpy(out, &texel, 4); out += 4; break;
                    }
                }
                source += skip;
                dst += stride;
            }
        }
        penX_ += static_cast<std::uint32_t>(width);
        atlas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(0x8040));
    }
    return info;
}

// 0x100657b92
bool cGZDrawFontRenderer::DrawChars(cIGZFont* font, void* surface, sGlyphInfo* glyphs, std::uint32_t count, eRenderingMode mode,
                                    const std::uint32_t* colors, const cRZRect* clip) {
    if (direct_) {
        // Pixel targets: shift the glyphs (and the clip) by the context origin and let the inner renderer draw.
        for (std::uint32_t i = 0; i < count; ++i) {
            glyphs[i].left += originX_;
            glyphs[i].top += originY_;
            glyphs[i].right += originX_;
            glyphs[i].bottom += originY_;
        }
        cRZRect shifted;
        const cRZRect* innerClip = nullptr;
        if (clip != nullptr) {
            shifted = {clip->left + originX_, clip->top + originY_, clip->right + originX_, clip->bottom + originY_};
            innerClip = &shifted;
        }
        return bufferRenderer_->DrawChars(font, surface, glyphs, count, mode, colors, innerClip);
    }

    std::uint8_t r, g, b;
    host_->NativeColorToRGB(colors[0], r, g, b);   // RZ::GS() vslot 0x120
    context_->SetColor(0xff000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b);   // vslot 0xa0
    context_->PushTransform();                                                  // vslot 0x18
    if (clip != nullptr) context_->Clip(*clip);                                 // vslot 0x28
    context_->PushState();                                                      // vslot 0x50
    context_->SetAlphaBlend(2);                                                 // vslot 0xb0
    context_->SetBltModulate(true);                                             // vslot 0xb8

    std::list<const sGlyphInfo*> pending;
    for (std::uint32_t i = 0; i < count; ++i) pending.push_back(&glyphs[i]);
    bool allowCreate = false;   // second pass: also render glyphs that are not in the atlas yet
    bool retry = false;         // third pass: the atlas may be flushed to make room
    while (!pending.empty()) {
        const sGlyphInfo* batch[64];
        const cCachedGlyphImgInfo* infos[64];
        int placed = 0;
        auto it = pending.begin();
        while (it != pending.end()) {
            const cCachedGlyphImgInfo* info = nullptr;
            auto found = currentMap_->find((*it)->character);
            if (found != currentMap_->end()) info = &found->second;
            else if (allowCreate) info = CreateCachedGlyphImg(font, **it, retry);
            if (info != nullptr) {
                batch[placed] = *it;
                infos[placed] = info;
                ++placed;
                it = pending.erase(it);
                retry = false;
            } else {
                ++it;
            }
            if (placed > 0x3f) break;
        }
        if (placed == 0) {
            if (!allowCreate) allowCreate = true;
            else retry = true;
        } else {
            for (int i = 0; i < placed; ++i) {
                const sGlyphInfo& g2 = *batch[i];
                const cRZRect source{infos[i]->x, infos[i]->y, g2.right + infos[i]->x - g2.left, g2.bottom + infos[i]->y - g2.top};
                const cRZRect dest{g2.left, g2.top, g2.right, g2.bottom};
                context_->Blt(atlas_, source, dest);   // vslot 0x130
            }
            allowCreate = false;
            retry = false;
        }
    }
    context_->SetAlphaBlend(0);
    context_->SetBltModulate(false);
    context_->PopState();       // vslot 0x58
    context_->PopTransform();   // vslot 0x48
    return true;
}
