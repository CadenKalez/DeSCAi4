#include "gz/RZDraw.h"

#include <algorithm>

namespace {
std::uint32_t Clamp8(int v) { return static_cast<std::uint32_t>(std::clamp(v, 0, 255)); }
}  // namespace

// ---------------------------------------------------------------------------------- tiled / stretched

bool RZDrawBltTiled(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& s, const cRZRect& d, int offsetX,
                    int offsetY, const cRZRect* clip) {
    int destLeft = d.left, destTop = d.top, destRight = d.right, destBottom = d.bottom;
    const int tileW = s.right - s.left;
    const int tileH = s.bottom - s.top;
    if (tileW == 1 && tileH == 1) {                       // one pixel: a plain fill
        const cRZRect* bounds = source->GetBufferArea();
        if (s.left < bounds->left || s.top < bounds->top || bounds->right <= s.left || bounds->bottom <= s.top)
            return false;
        if (source->IsPixelTransparent(static_cast<std::uint32_t>(s.left), static_cast<std::uint32_t>(s.top)))
            return true;
        std::uint8_t r = 0, g = 0, b = 0;
        source->ConvertNativeValueToRGB(source->GetPixel(static_cast<std::uint32_t>(s.left),
                                                         static_cast<std::uint32_t>(s.top)), r, g, b);
        return dest->Fill(dest->ConvertRGBValueToNative(r, g, b), d, clip);
    }
    void* target = nullptr;
    if (dest->QueryInterface(kGZIID_cIGZBufferDrawTarget, &target) && target) {
        auto* hardware = static_cast<cIGZBufferDrawTarget*>(target);
        gz::cIGZDrawContext* context = hardware->GetDrawContext();
        hardware->SetBlitSource(source);
        context->PushTransform();
        if (clip) context->Clip(*clip);
        context->BltTiled(source, s, d, offsetX, offsetY);
        context->PopTransform();
        context->Flush();
        hardware->Release();
        return true;
    }
    int phaseX = offsetX % tileW;
    if (phaseX > 0) phaseX -= tileW;
    int phaseY = offsetY % tileH;
    if (phaseY > 0) phaseY -= tileH;
    if (destLeft < 0) {
        phaseX = -(-(phaseX + destLeft) % tileW);
        destLeft = 0;
    }
    if (destTop < 0) {
        phaseY = -(-(phaseY + destTop) % tileH);
        destTop = 0;
    }
    const cRZRect* bounds = dest->GetBufferArea();
    destRight = std::min(destRight, bounds->right);
    destBottom = std::min(destBottom, bounds->bottom);
    int firstRow = destTop;
    if (clip) {
        if (clip->left - destLeft != 0 && destLeft <= clip->left) {
            phaseX = clip->left - ((clip->left - destLeft) - phaseX) % tileW;
            destLeft = clip->left;
        }
        if (clip->top - destTop != 0 && destTop <= clip->top) {
            phaseY = clip->top - ((clip->top - destTop) - phaseY) % tileH;
            firstRow = clip->top;
        }
        destRight = std::min(destRight, clip->right);
        destBottom = std::min(destBottom, clip->bottom);
    }
    if (destLeft < destRight && firstRow < destBottom) {
        const int rightSlack = ((destRight + s.left) - destLeft - phaseX) - s.right;   // dr - dl - phase - W
        int y0 = phaseY + firstRow;
        do {
            const int y1 = y0 + tileH;
            int accumulated = 0;
            int x0 = phaseX + destLeft;
            int x1;
            do {
                x1 = x0 + tileW;
                cRZRect src = s;
                cRZRect dst{x0, y0, x1, y1};
                if (x0 < destLeft) {
                    src.left += -phaseX + accumulated;
                    dst.left = destLeft;
                }
                if (destRight < x1) {
                    src.right += rightSlack + accumulated;
                    dst.right = destRight;
                }
                if (y0 < firstRow) {
                    src.top += firstRow - y0;
                    dst.top = firstRow;
                }
                if (destBottom < y1) {
                    src.bottom += destBottom - y1;
                    dst.bottom = destBottom;
                }
                if (clip) {
                    int cut = dst.right - clip->right;
                    if (clip->right <= dst.right && cut != 0) {
                        src.right -= cut;
                        dst.right = clip->right;
                    }
                    cut = clip->left - dst.left;
                    if (dst.left <= clip->left && cut != 0) {
                        src.left += cut;
                        dst.left = clip->left;
                    }
                    cut = dst.bottom - clip->bottom;
                    if (clip->bottom <= dst.bottom && cut != 0) {
                        src.bottom -= cut;
                        dst.bottom = clip->bottom;
                    }
                    cut = clip->top - dst.top;
                    if (dst.top <= clip->top && cut != 0) {
                        src.top += cut;
                        dst.top = clip->top;
                    }
                }
                dest->Blt(source, src, dst, nullptr);
                accumulated -= tileW;
                x0 = x1;
            } while (x1 < destRight);
            y0 = y1;
        } while (y0 < destBottom);
    }
    return true;
}

bool RZDrawBltStretch(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& s, const cRZRect& d, const cRZRect* clip) {
    void* target = nullptr;
    if (dest->QueryInterface(kGZIID_cIGZBufferDrawTarget, &target) && target) {
        auto* hardware = static_cast<cIGZBufferDrawTarget*>(target);
        gz::cIGZDrawContext* context = hardware->GetDrawContext();
        hardware->SetBlitSource(source);
        context->PushTransform();
        if (clip) context->Clip(*clip);
        context->Blt(source, s, d);
        context->PopTransform();
        context->Flush();
        hardware->Release();
        return true;
    }
    if (!(s.left < s.right && s.top < s.bottom && d.left < d.right && d.top < d.bottom)) return false;
    const cRZRect* sourceBounds = source->GetBufferArea();
    if (!(sourceBounds->left <= s.left && sourceBounds->top <= s.top && s.right <= sourceBounds->right &&
          s.bottom <= sourceBounds->bottom))
        return false;
    const std::uint32_t xStep = static_cast<std::uint32_t>((s.right - s.left) * 0x10000) /
                                static_cast<std::uint32_t>(d.right - d.left);
    const std::uint32_t yStep = static_cast<std::uint32_t>((s.bottom - s.top) * 0x10000) /
                                static_cast<std::uint32_t>(d.bottom - d.top);
    // visible part of the destination: its overlap with the buffer, then with the clip rectangle
    const cRZRect* bounds = dest->GetBufferArea();
    cRZRect visible{0, 0, 0, 0};
    if (d.left < bounds->right && bounds->left < d.right && d.top < bounds->bottom && bounds->top < d.bottom) {
        visible.left = std::max(bounds->left, d.left);
        visible.top = std::max(bounds->top, d.top);
        visible.right = std::min(bounds->right, d.right);
        visible.bottom = std::min(bounds->bottom, d.bottom);
    }
    if (clip) {
        if (clip->left < visible.right && visible.left < clip->right && clip->top < visible.bottom &&
            visible.top < clip->bottom) {
            visible.left = std::max(visible.left, clip->left);
            visible.top = std::max(visible.top, clip->top);
            visible.right = std::min(visible.right, clip->right);
            visible.bottom = std::min(visible.bottom, clip->bottom);
        } else {
            return true;
        }
    }
    const std::uint32_t width = static_cast<std::uint32_t>(visible.right - visible.left);
    const std::uint32_t height = static_cast<std::uint32_t>(visible.bottom - visible.top);
    if (visible.left < visible.right && visible.top < visible.bottom &&
        source->Lock(static_cast<cIGZBuffer::eLockFlags>(0x800))) {
        std::uint32_t rowSampleY = (yStep >> 1) + static_cast<std::uint32_t>(s.top) * 0x10000 +
                                   static_cast<std::uint32_t>(visible.top - d.top) * yStep;
        const std::uint32_t firstSampleX = (xStep >> 1) + static_cast<std::uint32_t>(s.left) * 0x10000 +
                                           static_cast<std::uint32_t>(visible.left - d.left) * xStep;
        for (std::uint32_t row = 0; row < height; ++row) {
            std::uint32_t sampleX = firstSampleX;
            for (std::uint32_t col = 0; col < width; ++col) {
                std::uint8_t r = 0, g = 0, b = 0, a = 0;
                source->ConvertNativeValueToRGBA(source->GetPixel(sampleX >> 16, rowSampleY >> 16), r, g, b, a);
                dest->SetPixel(static_cast<std::uint32_t>(visible.left) + col,
                               static_cast<std::uint32_t>(visible.top) + row, dest->ConvertRGBAValueToNative(r, g, b, a));
                sampleX += xStep;
            }
            rowSampleY += yStep;
        }
        source->Unlock(static_cast<cIGZBuffer::eLockFlags>(0x800));
    }
    return true;
}

// ----------------------------------------------------------------------------------------- nine-slice

bool RZDrawBltEdge(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& s, const cRZRect& d, bool fillCenter) {
    const int w = s.right - s.left, h = s.bottom - s.top;
    cRZRect a = s;                                   // the tile being drawn
    cRZRect r{d.left, d.top, 0, d.top + h};          // the destination
    dest->Blt(source, a, r, nullptr);                  // top-left corner
    a.left += w; a.right += w;
    r.left = d.left + w;
    r.right = d.right - w;
    if (r.left < r.right) RZDrawBltTiled(dest, source, a, r, 0, 0, nullptr);   // top edge
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right;
    dest->Blt(source, a, r, nullptr);                  // top-right corner
    // second row: left edge, centre, right edge
    a.left = s.left; a.right = s.left + w;
    a.top = s.top + h; a.bottom = s.top + 2 * h;
    r.left = d.left; r.right = d.left + w;
    r.top = d.top + h; r.bottom = d.bottom - h;
    if (r.top < r.bottom) RZDrawBltTiled(dest, source, a, r, 0, 0, nullptr);
    a.left += w; a.right += w;
    const int innerRight = d.right - w;
    if (r.right < innerRight && fillCenter && r.top < r.bottom) {
        r.left = r.right;
        r.right = innerRight;
        RZDrawBltTiled(dest, source, a, r, 0, 0, nullptr);
    }
    a.left += w; a.right += w;
    r.left = innerRight;
    if (r.top < r.bottom) {
        r.right = d.right;
        RZDrawBltTiled(dest, source, a, r, 0, 0, nullptr);
    }
    // bottom row
    a.left = s.left; a.right = s.left + w;
    a.top = s.top + 2 * h; a.bottom = s.top + 3 * h;
    r.left = d.left; r.right = d.left + w;
    r.top = d.bottom - h; r.bottom = d.bottom;
    dest->Blt(source, a, r, nullptr);                  // bottom-left corner
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right - w;
    if (r.left < r.right) RZDrawBltTiled(dest, source, a, r, 0, 0, nullptr);   // bottom edge
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right;
    dest->Blt(source, a, r, nullptr);                  // bottom-right corner
    return true;
}

void RZDrawBltEdge(gz::cIGZDrawContext* context, cIGZBuffer* source, const cRZRect& s, const cRZRect& d,
                   bool fillCenter) {
    const int w = s.right - s.left, h = s.bottom - s.top;
    context->PushState();
    std::uint8_t r8 = 0, g8 = 0, b8 = 0, a8 = 0;
    void* shade = nullptr;
    std::uint32_t color = 0xffffffffu;
    if (source->QueryInterface(kGZIID_cIGZBufferShade, &shade) && shade) {
        auto* view = static_cast<cIGZBufferShade*>(shade);
        if (view->GetShadeColor(r8, g8, b8, a8))
            color = std::uint32_t(b8) | (std::uint32_t(g8) << 8) | (std::uint32_t(r8) << 16) | (std::uint32_t(a8) << 24);
        view->Release();
    }
    context->SetColor(color);
    cRZRect a = s;
    cRZRect r{d.left, d.top, d.left + w, d.top + h};
    context->Blt(source, a, r);                                              // top-left corner
    a.left += w; a.right += w;
    r.left = d.left + w;
    r.right = d.right - w;
    if (r.left < r.right) context->BltTiled(source, a, r, 0, 0);             // top edge
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right;
    context->Blt(source, a, r);                                              // top-right corner
    a.left = s.left; a.right = s.left + w;
    a.top = s.top + h; a.bottom = s.top + 2 * h;
    r.left = d.left; r.right = d.left + w;
    r.top = d.top + h; r.bottom = d.bottom - h;
    if (r.top < r.bottom) context->BltTiled(source, a, r, 0, 0);             // left edge
    a.left += w; a.right += w;
    const int innerRight = d.right - w;
    if (r.right < innerRight && fillCenter && r.top < r.bottom) {
        r.left = r.right;
        r.right = innerRight;
        context->BltTiled(source, a, r, 0, 0);                               // centre
    }
    a.left += w; a.right += w;
    r.left = innerRight;
    if (r.top < r.bottom) {
        r.right = d.right;
        context->BltTiled(source, a, r, 0, 0);                               // right edge
    }
    a.left = s.left; a.right = s.left + w;
    a.top = s.top + 2 * h; a.bottom = s.top + 3 * h;
    r.left = d.left; r.right = d.left + w;
    r.top = d.bottom - h; r.bottom = d.bottom;
    context->Blt(source, a, r);                                              // bottom-left corner
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right - w;
    if (r.left < r.right) context->BltTiled(source, a, r, 0, 0);             // bottom edge
    a.left += w; a.right += w;
    r.left = r.right;
    r.right = d.right;
    context->Blt(source, a, r);                                              // bottom-right corner
    context->PopState();
}

// ------------------------------------------------------------------------------------- rectangles

void RZDraw3DRectangleOutline(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t topLeft,
                              std::uint32_t bottomRight) {
    context->SetColor(topLeft);
    context->Fill(cRZRect{rect.left, rect.top, rect.right, rect.top + 1});
    context->Fill(cRZRect{rect.left, rect.top, rect.left + 1, rect.bottom});
    context->SetColor(bottomRight);
    context->Fill(cRZRect{rect.left + 1, rect.bottom - 1, rect.right, rect.bottom});
    context->Fill(cRZRect{rect.right - 1, rect.top + 1, rect.right, rect.bottom});
}

void RZDrawRectangleOutline(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t color,
                            std::uint32_t thickness) {
    const int t = static_cast<int>(thickness);
    context->PushState();
    context->SetLineThickness(static_cast<float>(thickness));
    context->SetColor(color);
    context->Fill(cRZRect{rect.left, rect.top, t - 1 + rect.right, rect.top + t});
    context->Fill(cRZRect{rect.left, rect.top + t, rect.left + t, rect.bottom + (1 - t)});
    context->Fill(cRZRect{t - 1 + rect.right, rect.top + t, rect.right + 1, (1 - t) + rect.bottom});
    context->Fill(cRZRect{rect.left, rect.bottom + 1 - t, rect.right + 1, rect.bottom + 1});
    context->PopState();
}

// Fills the rectangle with (r,g,b) and bevels it: the light edges are r/g/b + 0x38 (raised) or - 0x38 (sunken),
// the other two edges the opposite, all channels saturated.
void RZDrawRaisedRect(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint8_t r, std::uint8_t g,
                      std::uint8_t b, bool raised, bool) {
    const int delta = raised ? 0x38 : -0x38;
    const std::uint32_t dark = (Clamp8(b - delta)) | (Clamp8(g - delta) << 8) | (Clamp8(r - delta) << 16) | 0xff000000u;
    const std::uint32_t light = (Clamp8(b + delta)) | (Clamp8(g + delta) << 8) | (Clamp8(r + delta) << 16) | 0xff000000u;
    context->SetColor(std::uint32_t(b) | (std::uint32_t(g) << 8) | (std::uint32_t(r) << 16) | 0xff000000u);
    context->Fill(rect);
    RZDraw3DRectangleOutline(context, rect, light, dark);
}

void RZDrawRaisedRect(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t rgb, bool raised, bool) {
    RZDrawRaisedRect(context, rect, static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                     static_cast<std::uint8_t>(rgb), raised, false);
}
