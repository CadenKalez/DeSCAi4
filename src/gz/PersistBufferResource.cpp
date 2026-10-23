#include "gz/PersistBufferResource.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "gz/RGBColorConverter.h"

namespace {

constexpr std::uint32_t kLockBmp = 0x8040;   // lock flags used while a bitmap is translated
constexpr std::uint32_t kLockTga = 0x8080;   // ... and while a Targa is converted

template <class T>
T Read(const std::uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// Bytes of a bitmap row, padded to a multiple of 4. Deviation: the original record loader computes (bits >> 3) * width = 0
// for 4 bit bitmaps (an empty pixel buffer that translate_4_to_16 then reads past); the file loader and this code use half a
// byte per pixel.
std::uint32_t BmpRowBytes(std::uint32_t width, std::uint32_t bitCount) {
    std::uint32_t bytes = bitCount == 4 ? ((width & 1) + width) / 2 : width * (bitCount >> 3);
    return ((bytes - 1) | 3) + 1;
}

// BMP RLE8: encoded runs, absolute runs, end of line, delta; stops at end of bitmap or when `out` is full. The original
// writes without bounds checks; the writes here are clipped to the buffer.
void DecodeRle8(const std::uint8_t* in, std::uint32_t inSize, std::uint8_t* out, std::uint32_t outSize, std::uint32_t rowBytes,
                std::uint32_t height, bool trackOverflow) {
    std::memset(out, 0, outSize);
    std::uint32_t i = 0, o = 0, x = 0, y = 0;
    bool overflow = false;
    while (outSize != 0) {
        if (i + 1 >= inSize) break;
        const std::uint32_t count = in[i], value = in[i + 1];
        if (count == 0) {
            if (value == 0) {   // end of line
                ++y;
                if (trackOverflow && y > height) overflow = true;
                o = y * rowBytes;
                x = 0;
            } else if (value == 2) {   // delta: right by in[i+2], down by in[i+3]
                if (i + 3 >= inSize) break;
                y += in[i + 3];
                x += in[i + 2];
                o = y * rowBytes + x;
            } else if (value == 1) {   // end of bitmap
                break;
            } else {   // absolute run of `value` literal bytes, padded to an even count (x is not advanced: kept)
                for (std::uint32_t k = 0; k < value; ++k)
                    if (o + k < outSize && i + 2 + k < inSize) out[o + k] = in[i + 2 + k];
                o += value;
                i += (value & 1) + value;
            }
        } else {
            for (std::uint32_t k = 0; k < count; ++k)
                if (o + k < outSize) out[o + k] = static_cast<std::uint8_t>(value);
            o += count;
            x += count;
        }
        if (o >= outSize) break;
        i += 2;
        if (overflow) break;
    }
}

}  // namespace

std::int32_t cGZPersistBufferResource::BitmapInfo::width() const { return Read<std::int32_t>(bytes + 4); }
std::int32_t cGZPersistBufferResource::BitmapInfo::height() const { return Read<std::int32_t>(bytes + 8); }
std::uint16_t cGZPersistBufferResource::BitmapInfo::bitCount() const { return Read<std::uint16_t>(bytes + 14); }
std::uint32_t cGZPersistBufferResource::BitmapInfo::compression() const { return Read<std::uint32_t>(bytes + 16); }
std::uint32_t cGZPersistBufferResource::BitmapInfo::sizeImage() const { return Read<std::uint32_t>(bytes + 20); }
std::uint32_t cGZPersistBufferResource::BitmapInfo::colorsUsed() const { return Read<std::uint32_t>(bytes + 32); }

// ---------------------------------------------------------------------------------------------------------------
// life cycle
// ---------------------------------------------------------------------------------------------------------------
// 0x100612d40: the buffer is an aggregatable one when that class exists (this resource becomes its outer object), else a
// plain buffer.
cGZPersistBufferResource::cGZPersistBufferResource(IPersistBufferHost* host, cGZBufferColorType colorType)
    : host_(host), colorType_(colorType) {
    buffer_ = host_->CreateAggregatableBuffer();
    if (buffer_ == nullptr) {
        buffer_ = host_->CreateBuffer();
    } else {
        void* aggregate = nullptr;
        buffer_->QueryInterface(kGZIID_cIGZBufferAggregate, &aggregate);
        aggregate_ = static_cast<cIGZBufferAggregate*>(aggregate);
        if (aggregate_ != nullptr) aggregate_->SetOuter(static_cast<cIGZUnknown*>(static_cast<cIGZPersistBufferResource*>(this)));
    }
}

// 0x100612e24
cGZPersistBufferResource::~cGZPersistBufferResource() {
    if (aggregate_ != nullptr) {
        aggregate_->SetOuter(nullptr);
        aggregate_->Release();
        aggregate_ = nullptr;
    }
    if (buffer_ != nullptr) {
        buffer_->Release();
        buffer_ = nullptr;
    }
}

// 0x100612ef6: the secondary interface is this object's; anything else goes to the base class and then to the buffer
// (through its aggregation interface when it has one).
bool cGZPersistBufferResource::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistBufferResource) {
        *out = static_cast<cIGZPersistBufferResource*>(this);
        AddRef();
        return true;
    }
    if (cGZPersistResource::QueryInterface(iid, out)) return true;
    if (aggregate_ != nullptr) return aggregate_->QueryInnerInterface(iid, out);
    if (buffer_ != nullptr) return buffer_->QueryInterface(iid, out);
    return false;
}

bool cGZPersistBufferResource::GetBuffer(std::uint32_t iid, void** out) {   // 0x1006147ae
    return buffer_ != nullptr ? buffer_->QueryInterface(iid, out) : false;
}

cIGZBuffer* cGZPersistBufferResource::GetBuffer() const {   // 0x100611fe8
    if (buffer_ == nullptr) return nullptr;
    buffer_->AddRef();
    return buffer_;
}

void cGZPersistBufferResource::SetBuffer(cIGZBuffer* buffer) {   // 0x10061480a
    if (buffer != nullptr) buffer->AddRef();
    if (buffer_ != nullptr) buffer_->Release();
    buffer_ = buffer;
}

bool cGZPersistBufferResource::DebugClassTag(cIGZString& out) {   // 0x100612fbc
    out.AssignFromCString("cSC3Buffer : ");
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// pixel translation
// ---------------------------------------------------------------------------------------------------------------
// 0x100613066: 16 palette entries; two pixels per source byte, high nibble first. Writes a pixel past the row for odd widths.
bool cGZPersistBufferResource::translate_4_to_16(const BitmapInfo& info, const std::uint8_t* pixels) {
    std::uint32_t palette[16];
    for (int i = 0; i < 16; ++i)
        palette[i] = buffer_->ConvertRGBValueToNative(info.palette()[i * 4 + 2], info.palette()[i * 4 + 1], info.palette()[i * 4]);
    const std::uint32_t width = static_cast<std::uint32_t>(info.width()), height = static_cast<std::uint32_t>(info.height());
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    const std::int32_t halfRow = static_cast<std::int32_t>((((width & 1) + width)) / 2);
    std::int32_t row = static_cast<std::int32_t>((height - 1) * (stride >> 1));
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<std::uint16_t*>(bits) + row;
        if (static_cast<std::int32_t>(width) > 0) {
            for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); x += 2) {
                const std::uint8_t b = *pixels++;
                dst[x] = static_cast<std::uint16_t>(palette[b >> 4]);
                dst[x + 1] = static_cast<std::uint16_t>(palette[b & 0xf]);
            }
        }
        pixels += ((halfRow - 1U) | 3) + (1 - halfRow);
        row -= static_cast<std::int32_t>(stride >> 1);
    }
    return true;
}

// 0x1006131a6 / 0x10061335a: palette entries are converted on first use.
bool cGZPersistBufferResource::translate_8_to_32(const BitmapInfo& info, const std::uint8_t* pixels) {
    std::uint32_t cache[256];
    std::memset(cache, 0xff, sizeof(cache));
    const std::uint32_t width = static_cast<std::uint32_t>(info.width()), height = static_cast<std::uint32_t>(info.height());
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::int32_t row = static_cast<std::int32_t>((height - 1) * (stride >> 2));
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<std::uint32_t*>(bits) + row;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t index = pixels[x];
            if (static_cast<std::int32_t>(cache[index]) < 0)
                cache[index] = buffer_->ConvertRGBValueToNative(info.palette()[index * 4 + 2], info.palette()[index * 4 + 1], info.palette()[index * 4]);
            dst[x] = cache[index];
        }
        pixels += ((width - 1) | 3) + 1;
        row -= static_cast<std::int32_t>(stride >> 2);
    }
    return true;
}

bool cGZPersistBufferResource::translate_8_to_16(const BitmapInfo& info, const std::uint8_t* pixels) {
    std::uint32_t cache[256];
    std::memset(cache, 0xff, sizeof(cache));
    const std::uint32_t width = static_cast<std::uint32_t>(info.width()), height = static_cast<std::uint32_t>(info.height());
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::int32_t row = static_cast<std::int32_t>((height - 1) * (stride >> 1));
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<std::uint16_t*>(bits) + row;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t index = pixels[x];
            if (static_cast<std::int32_t>(cache[index]) < 0)
                cache[index] = buffer_->ConvertRGBValueToNative(info.palette()[index * 4 + 2], info.palette()[index * 4 + 1], info.palette()[index * 4]);
            dst[x] = static_cast<std::uint16_t>(cache[index]);
        }
        pixels += ((width - 1) | 3) + 1;
        row -= static_cast<std::int32_t>(stride >> 1);
    }
    return true;
}

// 0x10061350e: raw index bytes.
bool cGZPersistBufferResource::translate_8_to_8(const BitmapInfo& info, const std::uint8_t* pixels) {
    const std::uint32_t width = static_cast<std::uint32_t>(info.width()), height = static_cast<std::uint32_t>(info.height());
    const std::int32_t stride = static_cast<std::int32_t>(buffer_->GetColorSurfaceStride());
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::int32_t row = static_cast<std::int32_t>(height - 1) * stride;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(bits + row, pixels, width);
        pixels += ((width - 1) | 3) + 1;
        row -= stride;
    }
    return true;
}

// 0x1006135b4
bool cGZPersistBufferResource::translate_24_to_16(const BitmapInfo& info, const std::uint8_t* pixels) {
    const std::uint32_t width = static_cast<std::uint32_t>(info.width()), height = static_cast<std::uint32_t>(info.height());
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::int32_t row = static_cast<std::int32_t>((height - 1) * (stride >> 1));
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<std::uint16_t*>(bits) + row;
        const std::uint8_t* p = pixels;
        for (std::uint32_t x = 0; x < width; ++x, p += 3) dst[x] = static_cast<std::uint16_t>(buffer_->ConvertRGBValueToNative(p[2], p[1], p[0]));
        pixels += static_cast<std::size_t>(width) * 3 + (width & 3);
        row -= static_cast<std::int32_t>(stride >> 1);
    }
    return true;
}

// 0x100613bae: BGR -> RGB bytes.
bool cGZPersistBufferResource::memcpy_24_to_24(const BitmapInfo& info, const std::uint8_t* pixels) {
    const std::uint32_t width = static_cast<std::uint32_t>(info.width());
    const std::int32_t height = info.height();
    const std::int32_t stride = static_cast<std::int32_t>(buffer_->GetColorSurfaceStride());
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::uint32_t row = static_cast<std::uint32_t>(height - 1) * static_cast<std::uint32_t>(stride) + 2;
    for (std::int32_t y = 0; y < height; ++y) {
        std::uint32_t o = row;
        const std::uint8_t* p = pixels;
        for (std::uint32_t x = 0; x < width; ++x, o += 3, p += 3) {
            bits[o - 2] = p[2];
            bits[o - 1] = p[1];
            bits[o] = p[0];
        }
        pixels += static_cast<std::size_t>(width) * 3 + static_cast<std::uint32_t>(static_cast<std::int32_t>(width) % 4);
        row -= static_cast<std::uint32_t>(stride);
    }
    return true;
}

// 0x100613c8a: BGR -> 0xffRRGGBB.
bool cGZPersistBufferResource::translate_24_to_32(const BitmapInfo& info, const std::uint8_t* pixels) {
    auto* dst = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    const std::int32_t width = info.width();
    std::int32_t y = info.height();
    const std::int32_t stride = static_cast<std::int32_t>(buffer_->GetColorSurfaceStride());
    const std::uint32_t srcRow = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;
    while (y != 0) {
        const std::int32_t sourceRow = y - 1;
        const std::uint8_t* p = pixels + static_cast<std::size_t>(sourceRow) * srcRow;
        std::uint8_t* d = dst;
        for (std::int32_t x = 0; x < width; ++x, p += 3, d += 4) {
            const std::uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | 0xff000000u;
            std::memcpy(d, &v, 4);
        }
        dst += stride;
        --y;
    }
    return true;
}

// 0x1006136a6: Floyd-Steinberg error diffusion to ARGB4444, scanning rows in alternating directions. The error cells pack a
// remainder per channel (blue at bit 4 .. 7, green at 12, red at 20 once divided by 16); the cell at the start of every
// row is cleared (kept).
bool cGZPersistBufferResource::translate_24_to_ARGB4_dither(const BitmapInfo& info, const std::uint8_t* pixels) {
    const std::int32_t width = info.width(), height = info.height();
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    errors_.assign(static_cast<std::size_t>(width) * 2 + 4, 0);
    if (height == 0) return true;
    const std::uint32_t sourceRow = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;
    const std::uint8_t* source = pixels + static_cast<std::size_t>(height - 1) * sourceRow + 2;   // red of the first pixel of the bottom row
    std::uint32_t* cur = errors_.data() + 1;
    std::uint32_t* next = errors_.data() + 1 + (width + 2);
    int direction = 1, start = 0, bound = width;
    std::uint8_t* destination = bits;
    for (std::int32_t row = 0; row != height; ++row) {
        const int newBound = start - direction;
        cur[start] = 0;
        next[start - direction] = 0;
        next[start] = 0;
        int counter = start - bound;
        if (counter != 0) {
            int x = start;
            const std::uint8_t* p = source + static_cast<std::ptrdiff_t>(start) * 3;
            do {
                const std::uint32_t e = cur[x];
                std::uint32_t b = (e >> 4 & 0xf) + p[-2];
                std::uint32_t g = (e >> 12 & 0xf) + p[-1];
                std::uint32_t r = (e >> 20 & 0xf) + p[0];
                if (b > 0xfe) b = 0xff;
                if (g > 0xfe) g = 0xff;
                if (r > 0xfe) r = 0xff;
                const std::uint16_t out = static_cast<std::uint16_t>((b >> 4) | (g & 0xf0) | ((r & 0xf0) << 4) | 0xf000);
                std::memcpy(destination + static_cast<std::ptrdiff_t>(x) * 2, &out, 2);
                const std::uint32_t error = (b & 0xf) | ((g & 0xf) << 8) | ((r & 0xf) << 16);
                cur[x + direction] += error * 7;
                next[x + direction] = error;
                next[x] += error * 5;
                next[x - direction] += error * 3;
                x += direction;
                p += direction * 3;
                counter += direction;
            } while (counter != 0);
        }
        start = bound - direction;
        direction = -direction;
        bound = newBound;
        destination += stride & ~1u;
        source -= sourceRow;
        std::swap(cur, next);
    }
    return true;
}

// 0x100613924: the same diffusion for 32 bit source pixels (blue, green, red, alpha), `pitch` bytes per source row.
bool cGZPersistBufferResource::translate_ARGB8_to_ARGB4_dither(int width, int height, const std::uint8_t* pixels, int pitch) {
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    errors_.assign(static_cast<std::size_t>(width) * 2 + 4, 0);
    if (height <= 0) return true;
    const std::uint8_t* source = pixels + 1;
    std::uint32_t* cur = errors_.data() + 1;
    std::uint32_t* next = errors_.data() + 1 + (width + 2);
    int direction = 1, start = 0, bound = width;
    std::uint8_t* destination = bits;
    for (int row = 0; row != height; ++row) {
        const int newBound = start - direction;
        cur[start] = 0;
        next[start - direction] = 0;
        next[start] = 0;
        if (start != bound) {
            int x = start;
            const std::uint8_t* p = source + static_cast<std::ptrdiff_t>(start) * 4;
            do {
                const std::uint32_t e = cur[x];
                std::uint32_t b = (e >> 4 & 0xf) + p[-1];
                std::uint32_t g = (e >> 12 & 0xf) + p[0];
                std::uint32_t r = (e >> 20 & 0xf) + p[1];
                std::uint32_t a = (e >> 28) + p[2];
                if (b > 0xfe) b = 0xff;
                if (g > 0xfe) g = 0xff;
                if (r > 0xfe) r = 0xff;
                if (a > 0xfe) a = 0xff;
                const std::uint16_t out = static_cast<std::uint16_t>((b >> 4) | (g & 0xf0) | ((r & 0xf0) << 4) | ((a & 0xf0) << 8));
                std::memcpy(destination + static_cast<std::ptrdiff_t>(x) * 2, &out, 2);
                const std::uint32_t error = (b & 0xf) | ((g & 0xf) << 8) | ((r & 0xf) << 16) | ((a & 0xf) << 24);
                cur[x + direction] += error * 7;
                next[x + direction] = error;
                next[x] += error * 5;
                next[x - direction] += error * 3;
                x += direction;
                p += direction * 4;
            } while (x != bound);
        }
        start = bound - direction;
        direction = -direction;
        bound = newBound;
        destination += stride & ~1u;
        source += pitch;
        std::swap(cur, next);
    }
    return true;
}

// 0x100613d50: picks the translator from the bitmap's depth and the buffer's.
bool cGZPersistBufferResource::bmp_translate(const BitmapInfo& info, const std::uint8_t* pixels) {
    const std::uint32_t bits = buffer_->GetBitsPerPixel();
    const std::uint32_t key = bits << 30 | (bits - 4) >> 2;   // 7: 32 bit, 5: 24 bit, 3: 16 bit, 1: 8 bit surfaces
    switch (info.bitCount()) {
        case 24:
            if (key == 7) translate_24_to_32(info, pixels);
            else if (key == 5) memcpy_24_to_24(info, pixels);
            else if (key == 3) {
                if (colorType_.type == 1 && buffer_->GetColorType().type == kBufferColorARGB4444) translate_24_to_ARGB4_dither(info, pixels);
                else translate_24_to_16(info, pixels);
            } else return false;
            return true;
        case 8:
            if (key == 7) translate_8_to_32(info, pixels);
            else if (key == 3) translate_8_to_16(info, pixels);
            else if (key == 1) translate_8_to_8(info, pixels);
            else return false;
            return true;
        default:
            if (info.bitCount() != 4 || bits != 0x10) return false;
            translate_4_to_16(info, pixels);
            return true;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------------------------------------------
// 0x100611f90: a bitmap first; when that fails the record is rewound and read as a Targa.
bool cGZPersistBufferResource::LoadFromRecord(cIGZPersistDBSerialRecord* record, bool) {
    if (LoadBMPFromRecord(record)) return true;
    if (!record->SeekAbsolute(0)) return false;
    return LoadTGAFromRecord(record);
}

// 0x100613e64: BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40) + palette, then the pixels right behind them (the
// offset in the file header is only range checked).
bool cGZPersistBufferResource::LoadBMPFromRecord(cIGZPersistDBSerialRecord* record) {
    std::uint8_t header[14];
    BitmapInfo info;
    if (!record->GetFieldVoid(header, 14) || Read<std::uint16_t>(header) != 0x4d42 || !record->GetFieldVoid(info.bytes, 0x28)) return false;
    const std::uint32_t fileSize = Read<std::uint32_t>(header + 2);
    const std::uint32_t offsetBits = Read<std::uint32_t>(header + 10);
    const std::uint32_t compression = info.compression();
    const std::uint16_t bitCount = info.bitCount();
    if (compression > 1) return false;
    if (bitCount > 0x18 || ((0x1000110ul >> bitCount) & 1) == 0) return false;   // 4, 8 or 24 bits
    const std::uint32_t width = static_cast<std::uint32_t>(info.width());
    const std::uint32_t height = static_cast<std::uint32_t>(info.height());
    const std::uint32_t rowBytes = BmpRowBytes(width, bitCount);
    const std::uint32_t total = height * rowBytes;
    std::vector<std::uint8_t> pixels(total);
    if (info.colorsUsed() != 0 || compression == 1 || bitCount == 8) {
        const std::uint32_t paletteBytes = info.colorsUsed() != 0 ? info.colorsUsed() * 4 : 0x400;
        record->GetFieldVoid(info.bytes + 0x28, std::min<std::uint32_t>(paletteBytes, 0x400));
    }
    if (static_cast<std::uint64_t>(offsetBits) + 0xe < 0x36) return false;
    if (compression != 1) {
        if (!record->GetFieldVoid(pixels.data(), total)) return false;
    } else {
        std::uint32_t encoded = fileSize - offsetBits;
        if (info.sizeImage() != 0) encoded = info.sizeImage();
        if (fileSize <= encoded) return false;
        std::vector<std::uint8_t> packed(encoded);
        record->GetFieldVoid(packed.data(), encoded);
        DecodeRle8(packed.data(), encoded, pixels.data(), total, rowBytes, height, true);
    }
    if (!buffer_->Init(width, height, colorType_) || !buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBmp))) return false;
    const bool ok = bmp_translate(info, pixels.data());
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBmp));
    return ok;
}

// 0x10061216c: the same for a .bmp file (read with stdio): the pixels are found through the file header's offset.
bool cGZPersistBufferResource::LoadFromFile(const cIGZString& path, bool, cGZBufferColorType colorType, bool) {
    if (path.Strlen() == 0) return false;
    std::FILE* file = std::fopen(path.ToChar(), "rb");
    if (file == nullptr) return false;
    std::uint8_t header[14];
    if (std::fread(header, 14, 1, file) != 1 || Read<std::uint16_t>(header) != 0x4d42) {
        std::fclose(file);
        return false;
    }
    BitmapInfo info;
    std::fread(info.bytes, sizeof(info.bytes), 1, file);
    const std::uint32_t offsetBits = Read<std::uint32_t>(header + 10);
    const std::uint16_t bitCount = info.bitCount();
    const std::uint32_t compression = info.compression();
    if (compression >= 2 || bitCount >= 0x19 || ((0x1000110ul >> bitCount) & 1) == 0) {
        std::fclose(file);
        return false;
    }
    const std::uint32_t width = static_cast<std::uint32_t>(info.width());
    const std::uint32_t rowBytes = BmpRowBytes(width, bitCount);
    const std::uint32_t total = static_cast<std::uint32_t>(info.height()) * rowBytes;
    std::vector<std::uint8_t> pixels(total);
    bool ok = false;
    if (compression == 1) {
        std::vector<std::uint8_t> packed(static_cast<std::size_t>(total) * 2);
        if (std::fseek(file, static_cast<long>(offsetBits), SEEK_SET) == 0) {
            const std::size_t got = std::fread(packed.data(), 1, packed.size(), file);
            if (got != 0 && total != 0) DecodeRle8(packed.data(), static_cast<std::uint32_t>(got), pixels.data(), total, rowBytes, 0, false);
            ok = true;   // (a short read still goes on to translate the zeroed pixels: kept)
        }
    } else if (std::fseek(file, static_cast<long>(offsetBits), SEEK_SET) == 0) {
        ok = std::fread(pixels.data(), 1, total, file) == total;
    }
    std::fclose(file);
    if (!ok) return false;
    if (!buffer_->Init(static_cast<std::uint32_t>(info.width()), static_cast<std::uint32_t>(info.height()), colorType) ||
        !buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBmp)))
        return false;
    const bool translated = bmp_translate(info, pixels.data());
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBmp));
    return translated;
}

// 0x100614238: uncompressed (type 2) and RLE (type 10) true colour images; the colour map and image id are skipped.
bool cGZPersistBufferResource::LoadTGAFromRecord(cIGZPersistDBSerialRecord* record) {
    if (!record->SeekAbsolute(0)) return false;
    std::uint8_t idLength, colorMapType, imageType, colorMapDepth, pixelDepth, descriptor;
    std::uint16_t colorMapOrigin, colorMapLength, xOrigin, yOrigin, width, height;
    if (!record->GetFieldUint8(idLength) || !record->GetFieldUint8(colorMapType) || !record->GetFieldUint8(imageType) ||
        !record->GetFieldUint16(colorMapOrigin) || !record->GetFieldUint16(colorMapLength) || !record->GetFieldUint8(colorMapDepth) ||
        !record->GetFieldUint16(xOrigin) || !record->GetFieldUint16(yOrigin) || !record->GetFieldUint16(width) ||
        !record->GetFieldUint16(height) || !record->GetFieldUint8(pixelDepth) || !record->GetFieldUint8(descriptor) ||
        !record->SeekRelative(static_cast<std::int32_t>(colorMapLength * ((colorMapDepth + 7) >> 3) + idLength)))
        return false;
    if ((imageType & 0xf7) != 2) return false;
    const std::uint32_t bytesPerPixel = (pixelDepth + 7u) >> 3;
    const std::uint32_t total = static_cast<std::uint32_t>(height) * width * bytesPerPixel;
    std::vector<std::uint8_t> pixels(total);
    if ((imageType & 8) == 0) {
        if (!record->GetFieldVoid(pixels.data(), total)) return false;
    } else {
        std::vector<std::uint8_t> packed(record->GetSize() - record->GetPosition());
        if (!record->GetFieldVoid(packed.data(), static_cast<std::uint32_t>(packed.size()))) return false;
        // packets: header byte, then one pixel (repeated hdr & 0x7f more times for a run, or hdr more raw pixels)
        const std::uint8_t* in = packed.data();
        const std::uint8_t* inEnd = in + packed.size();
        std::uint8_t* out = pixels.data();
        std::int64_t remaining = total;
        while (remaining > 0 && in < inEnd) {
            const std::uint8_t packet = *in++;
            const std::uint32_t extra = bytesPerPixel * (packet & 0x7f);
            if (static_cast<std::int64_t>(inEnd - in) < bytesPerPixel) break;
            std::memcpy(out, in, bytesPerPixel);
            in += bytesPerPixel;
            out += bytesPerPixel;
            const bool run = (packet & 0x80) != 0;
            const std::uint32_t room = static_cast<std::uint32_t>(std::max<std::int64_t>(0, remaining - bytesPerPixel));
            const std::uint32_t copy = std::min(extra, room);
            if (extra != 0) {
                if (run) {
                    for (std::uint32_t k = 0; k < copy; ++k) out[k] = out[static_cast<std::ptrdiff_t>(k) - bytesPerPixel];
                } else {
                    const std::uint32_t avail = std::min<std::uint32_t>(copy, static_cast<std::uint32_t>(inEnd - in));
                    std::memcpy(out, in, avail);
                }
                out += copy;
                if (!run) in += extra;
            }
            remaining -= static_cast<std::int64_t>(bytesPerPixel) + extra;
        }
    }
    if (!buffer_->Init(width, height, colorType_) || !buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockTga))) return false;
    void* destination = buffer_->GetColorSurfaceBits();
    const int destinationPitch = static_cast<int>(buffer_->GetColorSurfaceStride());
    int rowBytes = static_cast<int>(bytesPerPixel * width);
    const std::uint8_t* source = pixels.data();
    int sourcePitch = rowBytes;
    if ((descriptor & 0x20) == 0) {   // bottom-up image
        source = pixels.data() + static_cast<std::size_t>(height - 1) * rowBytes;
        sourcePitch = -rowBytes;
    }
    using Format = cRZRGBColorConverter::Format;
    const Format target(buffer_->GetColorType());
    if (pixelDepth == 32) {
        if (colorType_.type == 1 && buffer_->GetColorType().type == kBufferColorARGB4444) {
            translate_ARGB8_to_ARGB4_dither(width, height, source, sourcePitch);
        } else {
            // (the alpha channel of a 32 bit Targa is not read: the converter fills an opaque alpha)
            cRZRGBColorConverter(target, Format(0x20, 0, 0xff0000, 0xff00, 0xff)).Convert(destination, destinationPitch, source, sourcePitch, width, height);
        }
    } else if (pixelDepth == 24) {
        cRZRGBColorConverter(target, Format(0x18, 0, 0xff0000, 0xff00, 0xff)).Convert(destination, destinationPitch, source, sourcePitch, width, height);
    } else if (pixelDepth == 16) {
        cRZRGBColorConverter(target, Format(0x10, (descriptor & 0xf) != 0 ? 0x8000u : 0u, 0x7c00, 0x3e0, 0x1f))
            .Convert(destination, destinationPitch, source, sourcePitch, width, height);
    }
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockTga));
    return true;
}
