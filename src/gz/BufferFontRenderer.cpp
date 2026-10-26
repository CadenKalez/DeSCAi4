#include "gz/BufferFontRenderer.h"

#include <algorithm>

#include "gz/Buffer.h"

bool cGZBufferFontRenderer::alphaInitialized_ = false;
std::uint8_t cGZBufferFontRenderer::blendTable_[32][32][32] = {};

// 0x1002ba254
bool cGZBufferFontRenderer::PrepAlpha() {
    for (int foreground = 0; foreground < 32; ++foreground) {
        for (int background = 0; background < 32; ++background) {
            for (int step = 0; step < 32; ++step) {
                // The original does this in float (four lanes at a time); every operand is a multiple of 1/32 below 32 so
                // the float result is exact and the truncation equals the integer formula.
                const int value = ((32 - step) * background + step * foreground) / 32;
                blendTable_[foreground][background][step] = static_cast<std::uint8_t>(value);
            }
        }
    }
    return true;
}

// 0x1002ba3ea: the first renderer built prepares the shared blend table.
cGZBufferFontRenderer::cGZBufferFontRenderer() {
    if (!alphaInitialized_) {
        PrepAlpha();
        alphaInitialized_ = true;
    }
}

bool cGZBufferFontRenderer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZFontRenderer || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZBufferFontRenderer::AddRef() { return ++refCount_; }

// 0x1002ba426: deletes the object when the count is 1 or less (a renderer nobody counted is deleted by its first Release).
std::uint32_t cGZBufferFontRenderer::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZBufferFontRenderer::Init(cIGZFont*) { return true; }
bool cGZBufferFontRenderer::Shutdown(cIGZFont*) { return true; }

bool cGZBufferFontRenderer::DrawBegin(cIGZFont*, void* surface, eRenderingMode) {
    return static_cast<cIGZBuffer*>(surface)->Lock(static_cast<cIGZBuffer::eLockFlags>(0x8080));
}

bool cGZBufferFontRenderer::DrawEnd(cIGZFont*, void* surface, eRenderingMode) {
    return static_cast<cIGZBuffer*>(surface)->Unlock(static_cast<cIGZBuffer::eLockFlags>(0x8080));
}

// 0x1002baadc: every glyph with a code of 0x20 or more is drawn from the rectangle (0, 0, width, height) of its bitmap.
bool cGZBufferFontRenderer::DrawChars(cIGZFont* font, void* surface, sGlyphInfo* glyphs, std::uint32_t count,
                                      eRenderingMode mode, const std::uint32_t* colors, const cRZRect* clip) {
    for (; count != 0; --count, ++glyphs) {
        cRZRect source;
        source.left = 0;
        source.top = 0;
        source.right = static_cast<std::int32_t>(glyphs->width);
        source.bottom = static_cast<std::int32_t>(glyphs->height);
        if (glyphs->character >= 0x20) {
            const cRZRect dest{glyphs->left, glyphs->top, glyphs->right, glyphs->bottom};
            DrawChar(font, surface, &dest, glyphs->pixels, &source, glyphs->pitch, mode, colors, clip);
        }
    }
    return true;
}

namespace {

// Blends one coverage byte into a 5-bit-per-channel value through the table row of the foreground level.
inline unsigned Blend(const std::uint8_t* table, unsigned foreground, unsigned background, unsigned coverage) {
    return table[foreground * 1024 + background * 32 + (coverage >> 3)];
}

}  // namespace

// 0x1002ba4a0
bool cGZBufferFontRenderer::DrawChar(cIGZFont*, void* surfaceVoid, const cRZRect* dest, const std::uint8_t* pixels,
                                     const cRZRect* source, std::uint32_t pitch, eRenderingMode mode,
                                     const std::uint32_t* colors, const cRZRect* clip) {
    auto* surface = static_cast<cIGZBuffer*>(surfaceVoid);
    const int bufferWidth = static_cast<int>(surface->Width());
    const int bufferHeight = static_cast<int>(surface->Height());
    int sourceX = source->left;
    int sourceY = source->top;
    int left = dest->left, top = dest->top, right = dest->right, bottom = dest->bottom;
    const int bitsPerPixel = static_cast<int>(surface->GetBitsPerPixel());
    cGZBufferColorType colorType;
    colorType.type = 3;
    colorType.bitsPerPixel = 1;
    if (mode == kAlpha) surface->GetColorType(colorType);   // vslot 0x88

    // Intersect with the extra clip, moving the bitmap origin by the amount cut off the left / top.
    int clippedLeft = left, clippedTop = top;
    if (clip != nullptr) {
        if (left <= clip->left) clippedLeft = clip->left;
        sourceX += (clip->left > left) ? clip->left - left : 0;
        if (top <= clip->top) clippedTop = clip->top;
        sourceY += (clip->top > top) ? clip->top - top : 0;
        right = std::min(right, clip->right);
        bottom = std::min(bottom, clip->bottom);
    }
    // ... and with the surface.
    const int x0 = std::max(clippedLeft, 0);
    const int y0 = std::max(clippedTop, 0);
    right = std::min(right, bufferWidth);
    bottom = std::min(bottom, bufferHeight);
    const int height = bottom - y0;
    const int width = right - x0;
    if (height == 0 || y0 > bottom || width == 0 || x0 > right) return true;

    sourceX -= std::min(clippedLeft, 0);
    sourceY -= std::min(clippedTop, 0);
    const std::uint8_t* sourceRow = pixels + sourceX + static_cast<std::ptrdiff_t>(sourceY) * pitch;

    if (bitsPerPixel == 0x20 || bitsPerPixel == 0x18) {
        // 24 bit surfaces are written as 32 bit pixels (kept: both cases share one path, 0x1002ba92f).
        // Port: the text colours reach here as 0x00RRGGBB; the shipped 32-bit screen had no alpha, but the port's window
        // buffers do (they are blended), so the coverage blend treats the colour as opaque.
        const std::uint32_t foreground = colors[0] | 0xff000000u;
        const std::uint32_t stridePixels = surface->GetColorSurfaceStride() >> 2;
        auto* bits = static_cast<std::uint32_t*>(surface->GetColorSurfaceBits());
        if (static_cast<std::uint64_t>(height) * stridePixels == 0) return true;
        std::uint32_t* row = bits + static_cast<std::size_t>(y0) * stridePixels + x0;
        for (int y = 0; y < height; ++y, row += stridePixels, sourceRow += pitch) {
            const std::uint8_t* coverage = sourceRow;
            for (int x = 0; x < width; ++x, ++coverage) {
                std::uint32_t& dst = row[x];
                if (mode == kPalette) {
                    if (*coverage != 0) dst = colors[*coverage];
                } else if (mode == kMono) {
                    if (*coverage & 0x80) dst = foreground;
                } else {
                    const std::uint32_t a = *coverage;
                    const std::uint32_t inverse = 0x100 - a;
                    dst = ((a * (foreground & 0xff00ff) + 0x800080 + (dst & 0xff00ff) * inverse) >> 8 & 0xff00ff) |
                          (((foreground >> 8 & 0xff00ff) * a + 0x800080 + (dst >> 8 & 0xff00ff) * inverse) & 0xff00ff00);
                }
            }
        }
    } else if (bitsPerPixel == 0x10) {
        const std::uint32_t foreground = colors[0];
        const std::uint32_t stridePixels = surface->GetColorSurfaceStride() >> 1;
        auto* bits = static_cast<std::uint16_t*>(surface->GetColorSurfaceBits());
        if (static_cast<std::uint64_t>(height) * stridePixels == 0) return true;
        std::uint16_t* row = bits + static_cast<std::size_t>(y0) * stridePixels + x0;
        const std::uint8_t* table = BlendTable();
        for (int y = 0; y < height; ++y, row += stridePixels, sourceRow += pitch) {
            const std::uint8_t* coverage = sourceRow;
            for (int x = 0; x < width; ++x, ++coverage) {
                std::uint16_t& dst = row[x];
                if (mode == kPalette) {
                    if (*coverage != 0) dst = static_cast<std::uint16_t>(colors[*coverage]);
                } else if (mode == kMono) {
                    if (*coverage & 0x80) dst = static_cast<std::uint16_t>(foreground);
                } else if (*coverage != 0) {
                    // The 4-way jump table at 0x1002baacc: formats 4 and 6 share the 555 path, 5 is 565, 7 is 4444;
                    // anything else leaves the pixel alone.
                    const unsigned p = dst;
                    const unsigned c = *coverage;
                    switch (colorType.type) {
                        case kBufferColorXRGB1555:
                        case kBufferColorARGB1555:   // quirk: the alpha bit of the result is left clear
                            dst = static_cast<std::uint16_t>(
                                (Blend(table, foreground >> 10 & 0x1f, p >> 10 & 0x1f, c) << 10) |
                                (Blend(table, foreground >> 5 & 0x1f, p >> 5 & 0x1f, c) << 5) |
                                Blend(table, foreground & 0x1f, p & 0x1f, c));
                            break;
                        case kBufferColorRGB565:   // green keeps its top five bits only, written at bit 6
                            dst = static_cast<std::uint16_t>(
                                (Blend(table, foreground >> 11 & 0x1f, p >> 11 & 0x1f, c) << 11) |
                                (Blend(table, foreground >> 6 & 0x1f, p >> 6 & 0x1f, c) << 6) |
                                Blend(table, foreground & 0x1f, p & 0x1f, c));
                            break;
                        case kBufferColorARGB4444:   // 4-bit levels index the 5-bit table directly; alpha forced opaque
                            dst = static_cast<std::uint16_t>(
                                (Blend(table, foreground >> 8 & 0xf, p >> 8 & 0xf, c) << 8) |
                                (Blend(table, foreground >> 4 & 0xf, p >> 4 & 0xf, c) << 4) |
                                Blend(table, foreground & 0xf, p & 0xf, c) | 0xf000);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
    return true;
}
