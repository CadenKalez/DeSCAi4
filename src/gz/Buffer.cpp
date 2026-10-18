#include "gz/Buffer.h"

#include <algorithm>
#include <cstring>

using nGZDrawSoftRender::cBlitter;
using nGZDrawSoftRender::cBltInfo;
using nGZDrawSoftRender::cGZBufferSurfaceDesc;
using nGZDrawSoftRender::RGBFormat;

namespace {
IBufferHost* g_host = nullptr;

// colour type 4..9: bytes per pixel and the mask of the significant colour bits (DAT_100a207a0 / DAT_100a207c0)
constexpr std::uint32_t kBytesByType[6] = {2, 2, 2, 2, 4, 4};
constexpr std::uint32_t kMaskByType[6] = {0x7fff, 0xffff, 0x7fff, 0x0fff, 0xffffff, 0xffffff};

std::uint32_t BytesForType(std::int32_t type) {
    const std::uint32_t index = static_cast<std::uint32_t>(type) - 4;
    return index < 6 ? kBytesByType[index] : 0;
}
std::uint32_t MaskForType(std::int32_t type) {
    const std::uint32_t index = static_cast<std::uint32_t>(type) - 4;
    return index < 6 ? kMaskByType[index] : 0xffffffffu;
}

// gTexConvVector: [dst][src][keyed]; colour types 5..9 select rows 1..5, everything else row 0 (XRGB1555).
using XRGB1555 = RGBFormat<0u, 31744u, 992u, 31u, 2, 5>;
using RGB565 = RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>;
using ARGB1555 = RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>;
using ARGB4444 = RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>;
using XRGB8888 = RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>;
using ARGB8888 = RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>;

template <class Dst, class Src>
void FillPair(PixelBlitterFn (&cell)[2]) {
    cell[0] = &cBlitter<Dst, Src, false>::BltRect;
    cell[1] = &cBlitter<Dst, Src, true>::BltRect;
}
template <class Dst>
void FillRow(PixelBlitterFn (&row)[6][2]) {
    FillPair<Dst, XRGB1555>(row[0]);
    FillPair<Dst, RGB565>(row[1]);
    FillPair<Dst, ARGB1555>(row[2]);
    FillPair<Dst, ARGB4444>(row[3]);
    FillPair<Dst, XRGB8888>(row[4]);
    FillPair<Dst, ARGB8888>(row[5]);
}
struct BlitterTable {
    PixelBlitterFn table[6][6][2];
    BlitterTable() {
        FillRow<XRGB1555>(table[0]);
        FillRow<RGB565>(table[1]);
        FillRow<ARGB1555>(table[2]);
        FillRow<ARGB4444>(table[3]);
        FillRow<XRGB8888>(table[4]);
        FillRow<ARGB8888>(table[5]);
    }
};
std::size_t TableIndex(const cGZBufferColorType& type) {
    const std::uint32_t index = static_cast<std::uint32_t>(type.type) - 5;
    return index < 5 ? index + 1 : 0;
}
const BlitterTable& Blitters() {
    static const BlitterTable table;
    return table;
}
}  // namespace

IBufferHost* BufferHost() { return g_host; }
void SetBufferHost(IBufferHost* host) { g_host = host; }

PixelBlitterFn GetPixelBlitter(const cGZBufferColorType& dst, const cGZBufferColorType& src) {
    return Blitters().table[TableIndex(dst)][TableIndex(src)][0];
}
PixelBlitterFn GetPixelBlitterColorKey(const cGZBufferColorType& dst, const cGZBufferColorType& src) {
    return Blitters().table[TableIndex(dst)][TableIndex(src)][1];
}

cGZBuffer::~cGZBuffer() {
    bits_.reset();
    ready_ = false;
    if (hardwareCache_) hardwareCache_->Release();
}

bool cGZBuffer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZBufferShade) {
        *out = static_cast<cIGZBufferShade*>(this);
    } else if (iid == 1 || iid == kGZIID_cIGZBuffer) {
        *out = static_cast<cIGZBuffer*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZBuffer::Release() {
    if (refCount_ == 1) {
        refCount_ = 0;
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cGZBuffer::Init(std::uint32_t width, std::uint32_t height, cGZBufferColorType type) {
    if (ready_) return false;
    if (width == 0 || height == 0) return false;
    area_.left = 0;
    area_.top = 0;
    area_.right = static_cast<std::int32_t>(width);
    area_.bottom = static_cast<std::int32_t>(height);
    colorType_ = type;
    if (CreateSurfaces()) {
        ready_ = true;
        return true;
    }
    return false;
}

bool cGZBuffer::Uninitialize() {
    DestroySurfaces();
    ready_ = false;
    return true;
}

bool cGZBuffer::Shutdown() {
    DestroySurfaces();
    ready_ = false;
    return true;
}

bool cGZBuffer::CreateSurfaces() {
    const bool ok = InitializeSurfaces();
    if (!ok) bits_.reset();
    return ok;
}

// Chooses the display pixel format for "display" colour types, then allocates a 4-byte aligned surface.
bool cGZBuffer::InitializeSurfaces() {
    DestroySurfaces();
    DeterminePixelFormat();
    const std::uint32_t bytes = colorType_.bitsPerPixel >> 3;
    stride_ = (static_cast<std::uint32_t>(area_.right) * bytes + 3) & ~3u;
    const std::size_t size = static_cast<std::size_t>(static_cast<std::int64_t>(area_.bottom) * stride_);
    bits_.reset(new std::uint8_t[size]);
    bytesPerPixel_ = static_cast<std::uint8_t>(bytes);
    ++changeCounter_;
    return true;
}

bool cGZBuffer::DestroySurfaces() {
    bits_.reset();
    return true;
}

void cGZBuffer::DeterminePixelFormat() {
    if ((static_cast<std::uint32_t>(colorType_.type) | 2) != 3) return;
    IBufferHost* host = g_host;
    if (!host || !host->HasGraphicsSystem()) {
        colorType_.type = kBufferColorRGB565;
        colorType_.bitsPerPixel = 0x10;
        return;
    }
    std::uint32_t bitCount = 0;
    if (!host->QueryTextureBitCount(bitCount)) {
        colorType_ = host->DisplayColorType();
        return;
    }
    const std::uint32_t bits = bitCount & 0x3f;
    colorType_.bitsPerPixel = bits < 0x11 ? 0x10 : 0x20;
    colorType_.type = bits > 0x10 ? kBufferColorARGB8888 : kBufferColorARGB4444;
}

std::uint32_t cGZBuffer::GetMemoryUsage() const {
    if (!bits_) return 0;
    return (colorType_.bitsPerPixel >> 3) * static_cast<std::uint32_t>(area_.bottom) *
           ((static_cast<std::uint32_t>(area_.right) - 1 | 3) + 1);
}

bool cGZBuffer::Lock(eLockFlags flags) {
    if (!bits_) return false;
    if (static_cast<std::int16_t>(flags) < 0) ++changeCounter_;
    lockFlags_ |= flags;
    ++lockCount_;
    return true;
}

bool cGZBuffer::Unlock(eLockFlags flags) {
    lockFlags_ &= ~static_cast<std::uint32_t>(flags);
    --lockCount_;
    return true;
}

void cGZBuffer::SetTransparency(std::uint32_t color) {
    transparentColor_ = color;
    flags_ |= 2;
    if (hardwareCache_) hardwareCache_->Invalidate();
}

void cGZBuffer::RemoveTransparency() {
    flags_ &= ~2u;
    if (hardwareCache_) hardwareCache_->Invalidate();
}

bool cGZBuffer::GetTransparentColor(std::uint32_t& color) const {
    if (!(flags_ & 2)) return false;
    color = transparentColor_;
    return true;
}

bool cGZBuffer::IsPixelTransparent(std::uint32_t x, std::uint32_t y) {
    if (!(flags_ & 2) || x >= static_cast<std::uint32_t>(area_.right) || y >= static_cast<std::uint32_t>(area_.bottom))
        return false;
    const std::uint32_t mask = MaskForType(colorType_.type);
    return ((GetPixel(x, y) ^ transparentColor_) & mask) == 0;
}

bool cGZBuffer::SetShadeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    shadeColor_ = (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) | r;
    return true;
}

bool cGZBuffer::GetShadeColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) const {
    if (shadeColor_ == 0xffffffffu) return false;
    r = static_cast<std::uint8_t>(shadeColor_);
    g = static_cast<std::uint8_t>(shadeColor_ >> 8);
    b = static_cast<std::uint8_t>(shadeColor_ >> 16);
    a = static_cast<std::uint8_t>(shadeColor_ >> 24);
    return true;
}

bool cGZBuffer::SetHardwareCache(cIGZBufferHardwareCache* cache) {
    if (hardwareCache_) {
        cIGZBufferHardwareCache* old = hardwareCache_;
        hardwareCache_ = nullptr;
        old->Release();
    }
    if (!cache->Attach(this)) return false;
    if (hardwareCache_ != cache) {
        cache->AddRef();
        hardwareCache_ = cache;
    }
    return true;
}

std::uint32_t cGZBuffer::GetPixel(std::uint32_t x, std::uint32_t y) const {
    const std::uint8_t* p = bits_.get() + std::uint64_t(x * bytesPerPixel_ + y * stride_);
    switch (bytesPerPixel_) {
        case 2: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case 3: return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16);
        case 4: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        default: return *p;
    }
}

void cGZBuffer::SetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) {
    std::uint8_t* p = bits_.get() + std::uint64_t(x * bytesPerPixel_ + y * stride_);
    switch (bytesPerPixel_) {
        case 2: { std::uint16_t v = static_cast<std::uint16_t>(color); std::memcpy(p, &v, 2); return; }
        case 3:
            p[2] = static_cast<std::uint8_t>(color >> 16);
            p[1] = static_cast<std::uint8_t>(color >> 8);
            p[0] = static_cast<std::uint8_t>(color);        // the original falls through to the byte store
            return;
        case 4: std::memcpy(p, &color, 4); return;
        default: *p = static_cast<std::uint8_t>(color);
    }
}

// ---------------------------------------------------------------------------------- colour conversion

std::uint32_t cGZBuffer::ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    switch (colorType_.type) {
        case 4:
        case 6: return (b >> 3) + 0x8000 + std::uint32_t(r & 0xf8) * 0x80 + std::uint32_t(g & 0xf8) * 4;
        case 5: return std::uint32_t(b >> 3) | (std::uint32_t(r & 0xf8) * 0x100 + std::uint32_t(g & 0xfc) * 8);
        case 7: return (b >> 4) + 0xf000 + (std::uint32_t(g & 0xf0) | (std::uint32_t(r & 0xf0) << 4));
        case 8:
        case 9: return std::uint32_t(b) | (std::uint32_t(g) << 8) | (std::uint32_t(r) << 16) | 0xff000000u;
        default: return 0;
    }
}

std::uint32_t cGZBuffer::ConvertRGBAValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::uint32_t high, low;
    switch (colorType_.type) {
        case 4:
        case 6:
            high = std::uint32_t(a & 0x80) << 8;
            low = std::uint32_t(r & 0xf8) * 0x80 + std::uint32_t(g & 0xf8) * 4 | std::uint32_t(b >> 3);
            return low | high;
        case 5: return std::uint32_t(b >> 3) | (std::uint32_t(r & 0xf8) * 0x100 + std::uint32_t(g & 0xfc) * 8);
        case 7:
            high = std::uint32_t(a & 0xf0) << 8;
            low = std::uint32_t(g & 0xf0) | (std::uint32_t(r & 0xf0) << 4);
            return (std::uint32_t(b >> 4) | low) | high;
        case 8:
        case 9:
            high = std::uint32_t(a) << 24;
            low = (std::uint32_t(g) << 8) | (std::uint32_t(r) << 16);
            return (std::uint32_t(b) | low) | high;
        default: return 0;
    }
}

bool cGZBuffer::ConvertNativeValueToRGB(std::uint32_t v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    switch (colorType_.type) {
        case 4:
        case 6:
            r = static_cast<std::uint8_t>((v & 0x7c00) * 0x21 >> 12);
            g = static_cast<std::uint8_t>((v & 0x3e0) * 0x21 >> 7);
            b = static_cast<std::uint8_t>((v & 0x1f) * 0x21 >> 2);
            return true;
        case 5:
            r = static_cast<std::uint8_t>((v & 0xf800) * 0x21 >> 13);
            g = static_cast<std::uint8_t>((v & 0x7e0) * 0x41 >> 9);
            b = static_cast<std::uint8_t>((v & 0x1f) * 0x21 >> 2);
            return true;
        case 7:
            r = static_cast<std::uint8_t>((v & 0xf00) * 0x11 >> 8);
            g = static_cast<std::uint8_t>((v & 0xf0) * 0x11 >> 4);
            b = static_cast<std::uint8_t>((v & 0xf) * 0x11);
            return true;
        case 8:
        case 9:
            r = static_cast<std::uint8_t>(v >> 16);
            g = static_cast<std::uint8_t>(v >> 8);
            b = static_cast<std::uint8_t>(v);
            return true;
        default: return false;
    }
}

bool cGZBuffer::ConvertNativeValueToRGBA(std::uint32_t v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b,
                                         std::uint8_t& a) {
    switch (colorType_.type) {
        case 4:
        case 6:
            a = static_cast<std::uint8_t>((v & 0x8000) * 0x21 >> 13);
            r = static_cast<std::uint8_t>((v & 0x7c00) * 0x21 >> 12);
            g = static_cast<std::uint8_t>((v & 0x3e0) * 0x21 >> 7);
            b = static_cast<std::uint8_t>((v & 0x1f) * 0x21 >> 2);
            return true;
        case 5:
            a = 0xff;
            r = static_cast<std::uint8_t>((v & 0xf800) * 0x21 >> 13);
            g = static_cast<std::uint8_t>((v & 0x7e0) * 0x41 >> 9);
            b = static_cast<std::uint8_t>((v & 0x1f) * 0x21 >> 2);
            return true;
        case 7:
            a = (v & 0xf000) == 0xf000 ? 0xff : static_cast<std::uint8_t>((v & 0xf000) >> 8);
            r = static_cast<std::uint8_t>((((v & 0xf00) << 4) >> 8) | ((v & 0xf00) >> 8));
            g = static_cast<std::uint8_t>((((v & 0xf0) << 4) | (v & 0xf0)) >> 4);
            b = static_cast<std::uint8_t>((v & 0xf) | ((v & 0xf) << 4));
            return true;
        case 8:
        case 9:
            a = static_cast<std::uint8_t>(v >> 24);
            r = static_cast<std::uint8_t>(v >> 16);
            g = static_cast<std::uint8_t>(v >> 8);
            b = static_cast<std::uint8_t>(v);
            return true;
        default: return false;
    }
}

// ------------------------------------------------------------------------------------------ clipping

// Clips (`clipped`, a rectangle in `buffer`) to the buffer, then to `clip`; every edge cut is mirrored on
// `adjusted` (the matching rectangle in the other buffer). False when nothing is left.
bool cGZBuffer::GenericBufferClip(cIGZBuffer* buffer, cRZRect& adjusted, cRZRect& clipped, const cRZRect* clip) {
    const cRZRect* area = buffer->GetBufferArea();
    int excess = clipped.right - area->right;
    if (excess != 0 && clipped.right >= area->right) {
        clipped.right -= excess;
        adjusted.right -= excess;
        if (clipped.right <= clipped.left) return false;
    }
    if (clipped.left < 0) {
        adjusted.left -= clipped.left;
        clipped.left = 0;
        if (clipped.right <= clipped.left) return false;
    }
    excess = clipped.bottom - area->bottom;
    if (excess != 0 && clipped.bottom >= area->bottom) {
        clipped.bottom -= excess;
        adjusted.bottom -= excess;
        if (clipped.bottom <= clipped.top) return false;
    }
    if (clipped.top < 0) {
        adjusted.top -= clipped.top;
        clipped.top = 0;
        if (clipped.bottom <= clipped.top) return false;
    }
    if (clip) {
        excess = clipped.right - clip->right;
        if (excess != 0 && clip->right <= clipped.right) {
            clipped.right = clip->right;
            adjusted.right -= excess;
        }
        excess = clip->left - clipped.left;
        if (excess != 0 && clipped.left <= clip->left) {
            clipped.left = clip->left;
            adjusted.left += excess;
        }
        excess = clipped.bottom - clip->bottom;
        if (excess != 0 && clip->bottom <= clipped.bottom) {
            clipped.bottom = clip->bottom;
            adjusted.bottom -= excess;
        }
        excess = clip->top - clipped.top;
        if (excess != 0 && clipped.top <= clip->top) {
            clipped.top = clip->top;
            adjusted.top += excess;
        }
    }
    if (clipped.right <= clipped.left) return false;
    return clipped.top < clipped.bottom;
}

namespace {
std::uint8_t OutCode(int x, int y, int minX, int minY, int maxX, int maxY) {
    std::uint8_t code = (x < minX) ? 4 : 0;
    if (x > maxX) code = static_cast<std::uint8_t>(code + 8);
    code |= (y < minY) ? 1 : 0;
    if (y > maxY) code |= 2;
    return code;
}
}  // namespace

bool cGZBuffer::ClipRect(int* x0, int* y0, int* x1, int* y1, int minX, int minY, int maxX, int maxY) {
    std::uint8_t outA = OutCode(*x0, *y0, minX, minY, maxX, maxY);
    std::uint8_t outB = OutCode(*x1, *y1, minX, minY, maxX, maxY);
    while (outA != 0 || outB != 0) {
        if (outA & outB) return false;
        const std::uint8_t code = outA != 0 ? outA : outB;
        int x = 1, y = maxY;
        if (code & 1) {
            y = minY;
            x = ((minY - *y0) * (*x1 - *x0)) / (*y1 - *y0) + *x0;
        } else if (code & 2) {
            y = maxY;
            x = ((maxY - *y0) * (*x1 - *x0)) / (*y1 - *y0) + *x0;
        } else if (code & 8) {
            x = maxX;
            y = ((maxX - *x0) * (*y1 - *y0)) / (*x1 - *x0) + *y0;
        } else if (code & 4) {
            x = minX;
            y = ((minX - *x0) * (*y1 - *y0)) / (*x1 - *x0) + *y0;
        }
        if (code == outA) {
            *x0 = x;
            *y0 = y;
            outA = OutCode(*x0, *y0, minX, minY, maxX, maxY);
        } else {
            *x1 = x;
            *y1 = y;
            outB = OutCode(*x1, *y1, minX, minY, maxX, maxY);
        }
    }
    return true;
}

// -------------------------------------------------------------------------------------------- drawing

bool cGZBuffer::Fill(std::uint32_t color, const cRZRect* clip) {
    return Fill(color, *GetBufferArea(), clip);
}

bool cGZBuffer::Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) {
    cRZRect rect;
    rect.left = l;
    rect.top = t;
    rect.right = r;
    rect.bottom = b;
    return Fill(color, rect, clip);
}

bool cGZBuffer::Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) {
    cRZRect adjusted;
    adjusted.right = rect.right - rect.left;
    adjusted.bottom = rect.bottom - rect.top;
    cRZRect clipped = rect;
    if (!GenericBufferClip(this, adjusted, clipped, clip)) return false;
    const std::uint32_t bits = colorType_.bitsPerPixel;
    std::uint8_t* base = bits_.get();
    const std::uint32_t stride = GetColorSurfaceStride();
    if (bits == 8) {
        for (int y = clipped.top; y < clipped.bottom; ++y)
            std::memset(base + std::uint64_t(y) * stride + clipped.left, static_cast<int>(color & 0xff),
                        static_cast<std::size_t>(clipped.right - clipped.left));
    } else if (bits == 0x20) {
        for (int y = clipped.top; y < clipped.bottom; ++y) {
            auto* row = reinterpret_cast<std::uint32_t*>(base + std::uint64_t(y) * (stride >> 2) * 4) + clipped.left;
            std::fill(row, row + (clipped.right - clipped.left), color);
        }
    } else if (bits == 0x10) {
        for (int y = clipped.top; y < clipped.bottom; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + std::uint64_t(y) * (stride >> 1) * 2) + clipped.left;
            std::fill(row, row + (clipped.right - clipped.left), static_cast<std::uint16_t>(color));
        }
    }
    ++changeCounter_;
    return true;
}

bool cGZBuffer::Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) {
    cRZRect src = sourceRect;
    cRZRect dst;
    dst.left = destination.left;
    dst.top = destination.top;
    dst.right = destination.left + (sourceRect.right - sourceRect.left);
    dst.bottom = destination.top + (sourceRect.bottom - sourceRect.top);
    if (!GenericBufferClip(this, src, dst, clip)) return false;
    return BltNoClip(source, src, dst);
}

// Copies (`sourceRect` of `source`) to (`destRect` of this) through the pixel blitter for the two colour types;
// a source with a colour key uses the keyed blitter. Overlapping self-blits run bottom-up.
bool cGZBuffer::BltNoClip(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) {
    ++changeCounter_;
    const cGZBufferColorType sourceType = source->GetColorType();
    const std::uint32_t sourceStride = source->GetColorSurfaceStride();
    const std::uint32_t destStride = GetColorSurfaceStride();
    const std::uint32_t width = static_cast<std::uint32_t>(destRect.right - destRect.left);
    const std::uint32_t height = static_cast<std::uint32_t>(destRect.bottom - destRect.top);
    const std::uint32_t sourceBytes = BytesForType(sourceType.type);
    const std::uint32_t destBytes = BytesForType(colorType_.type);
    PixelBlitterFn plain = GetPixelBlitter(colorType_, sourceType);
    PixelBlitterFn keyed = GetPixelBlitterColorKey(colorType_, sourceType);
    std::uint8_t* destBits = static_cast<std::uint8_t*>(GetColorSurfaceBits());
    std::uint8_t* sourceBits = static_cast<std::uint8_t*>(source->GetColorSurfaceBits());
    if (!destBits || !sourceBits) return true;
    std::uint8_t* destPtr = destBits + std::uint64_t(destRect.left) * destBytes + std::uint64_t(destRect.top) * destStride;
    std::uint8_t* sourcePtr =
        sourceBits + std::uint64_t(sourceRect.left) * sourceBytes + std::uint64_t(sourceRect.top) * sourceStride;
    std::int64_t destStep = destStride, sourceStep = sourceStride;
    if (static_cast<cIGZBuffer*>(this) == source && sourceRect.top < destRect.bottom && destRect.top < sourceRect.bottom) {
        destPtr += std::int64_t(destRect.bottom - 1 - destRect.top) * destStride;
        sourcePtr += std::int64_t(sourceRect.bottom - 1 - sourceRect.top) * sourceStride;
        destStep = -destStep;
        sourceStep = -sourceStep;
    }
    cGZBufferSurfaceDesc dst{destPtr, destStep, width, height,
                             {static_cast<std::uint32_t>(colorType_.type), destBytes, 0, 0}};
    cGZBufferSurfaceDesc src{sourcePtr, sourceStep, width, height,
                             {static_cast<std::uint32_t>(sourceType.type), sourceBytes, 0, 0}};
    cBltInfo info{};
    std::uint32_t key = 0;
    if (source->GetTransparentColor(key)) {
        info.key = key & MaskForType(sourceType.type);
        info.keyed = 1;
        keyed(width, height, dst, src, info);
    } else {
        plain(width, height, dst, src, info);
    }
    return true;
}

// Bresenham line. Horizontal / vertical lines become fills; a diagonal line writes single pixels of the
// surface's depth, or (thickness > 1) thickness x thickness boxes of 16-bit pixels in the direction of travel.
bool cGZBuffer::DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) {
    std::uint8_t* bits = static_cast<std::uint8_t*>(GetColorSurfaceBits());
    const std::uint32_t stride = GetColorSurfaceStride();
    const int width = area_.right, height = area_.bottom;
    const int maxX = width - static_cast<int>(thickness), maxY = height - static_cast<int>(thickness);
    if (!ClipRect(&x0, &y0, &x1, &y1, 0, 0, maxX, maxY)) return true;
    const int dy = y1 - y0;
    if (dy == 0) {
        const int left = x1 < x0 ? x1 : x0;
        const int right = (x1 < x0 ? x0 : x1) + 1;
        Fill(static_cast<std::uint32_t>(color), left, y0, right, y0 + static_cast<int>(thickness), nullptr);
        return true;
    }
    const int dx = x1 - x0;
    if (dx == 0) {
        const int top = y1 < y0 ? y1 : y0;
        const int bottom = (y1 < y0 ? y0 : y1) + 1;
        Fill(static_cast<std::uint32_t>(color), x0, top, x0 + static_cast<int>(thickness), bottom, nullptr);
        return true;
    }
    if (!bits) return true;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const bool xMajor = ady <= adx;
    const int major = xMajor ? adx : ady, minor = xMajor ? ady : adx;
    int error = 2 * minor - major;
    const int incLevel = minor * 2;                   // error < 0: step along the major axis only
    const int incDiagonal = (minor - major) * 2;      // error >= 0: step diagonally
    const int sx = x1 < x0 ? -1 : 1, sy = y0 <= y1 ? 1 : -1;
    const int levelX = xMajor ? sx : 0, levelY = xMajor ? 0 : sy;
    int count = major + 1;
    int x = x0, y = y0;
    if (thickness != 1) {
        const int th = static_cast<int>(thickness);
        const std::int64_t halfStride = stride >> 1;
        auto* shorts = reinterpret_cast<std::uint16_t*>(bits);
        const std::uint16_t value = static_cast<std::uint16_t>(color);
        const std::int64_t total = std::int64_t(height) * halfStride;
        for (; count > 0; --count) {
            // columns of the box: it extends in the direction of travel and is cut at the surface edges
            int start, cols;
            if (x1 < x0) {
                start = std::max(x - th + 1, 0);
                cols = x >= th - 1 ? th : x + 1;
            } else {
                start = std::min(x, width - 1);
                cols = x <= width - th ? th : width - x;
            }
            std::int64_t row = std::int64_t(y) * halfStride + start;
            for (int r = 0; r < th; ++r, row += sy * halfStride) {
                if (row < 0 || row >= total) continue;
                for (int c = 0; c < cols; ++c) shorts[row + c] = value;
            }
            if (error < 0) {
                error += incLevel;
                x += levelX;
                y += levelY;
            } else {
                error += incDiagonal;
                x += sx;
                y += sy;
            }
        }
        return true;
    }
    const std::uint32_t depth = colorType_.bitsPerPixel;
    if (depth != 8 && depth != 16 && depth != 24 && depth != 32) return true;
    for (; count > 0; --count) {
        std::uint8_t* row = bits + std::int64_t(y) * stride;
        if (depth == 16) {
            const std::uint16_t v = static_cast<std::uint16_t>(color);
            std::memcpy(row + std::int64_t(x) * 2, &v, 2);
        } else if (depth == 24) {
            row[std::int64_t(x) * 3] = static_cast<std::uint8_t>(color);
            row[std::int64_t(x) * 3 + 1] = static_cast<std::uint8_t>(color >> 8);
            row[std::int64_t(x) * 3 + 2] = static_cast<std::uint8_t>(color >> 16);
        } else if (depth == 32) {
            std::memcpy(row + std::int64_t(x) * 4, &color, 4);
        } else {
            row[x] = static_cast<std::uint8_t>(color);
        }
        if (error < 0) {
            error += incLevel;
            x += levelX;
            y += levelY;
        } else {
            error += incDiagonal;
            x += sx;
            y += sy;
        }
    }
    return true;
}
