#include "gz/BufferResource.h"

#include <cstring>
#include <vector>

#include "gz/GZCOMDirector.h"
#include "gz/RefPack.h"

namespace {
constexpr std::uint32_t kBufferClass = 0xc470d325;
constexpr std::uint32_t kLockRead = 0x8040, kLockWrite = 0x8080;
constexpr std::uint32_t kBitsOfColorType[8] = {8, 16, 16, 16, 16, 16, 32, 32};   // DAT_100a27170, colour types 2..9
BufferFormatConverter g_converter;
BufferMaker g_bufferMaker;

std::uint64_t PackedColorType(std::uint32_t type) {
    std::uint32_t bits = type - 2 < 8 ? kBitsOfColorType[type - 2] : 0;
    return (static_cast<std::uint64_t>(bits) << 32) | type;
}
cGZBufferColorType ColorTypeOf(std::uint32_t type) {
    return cGZBufferColorType{static_cast<std::int32_t>(type), type - 2 < 8 ? kBitsOfColorType[type - 2] : 0};
}
// A BITMAPINFOHEADER (0x28 bytes) followed by the palette.
struct BitmapInfo {
    std::int32_t width, height;
    std::uint16_t bitCount;
    std::uint32_t compression, paletteCount;
    const std::uint8_t* palette;
};
BitmapInfo ParseInfo(const std::uint8_t* info) {
    BitmapInfo r{};
    std::memcpy(&r.width, info + 4, 4);
    std::memcpy(&r.height, info + 8, 4);
    std::memcpy(&r.bitCount, info + 14, 2);
    std::memcpy(&r.compression, info + 16, 4);
    std::memcpy(&r.paletteCount, info + 32, 4);
    r.palette = info + 0x28;
    return r;
}
}  // namespace

void SetBufferFormatConverter(BufferFormatConverter converter) { g_converter = std::move(converter); }
void SetBufferMaker(BufferMaker maker) { g_bufferMaker = std::move(maker); }

cGZBufferResource::cGZBufferResource() {
    if (cIGZCOM* com = GZCOM()) com->CreateInstance(kBufferClass, kGZIID_cIGZBuffer, reinterpret_cast<void**>(&buffer_));
    if (!buffer_ && g_bufferMaker) buffer_ = g_bufferMaker();
}

cGZBufferResource::~cGZBufferResource() {
    if (buffer_) buffer_->Release();
    buffer_ = nullptr;
}

bool cGZBufferResource::QueryInterface(std::uint32_t iid, void** out) {
    if (buffer_ && buffer_->QueryInterface(iid, out)) return true;
    if (iid != kGZIID_cIGZBufferResource) return cGZPersistResource::QueryInterface(iid, out);
    if (out) {
        *out = static_cast<cIGZBufferResource*>(this);
        AddRef();
    }
    return out != nullptr;
}

bool cGZBufferResource::SetBuffer(cIGZBuffer* buffer) {
    if (buffer) buffer->AddRef();
    if (buffer_) buffer_->Release();
    buffer_ = buffer;
    return true;
}

bool cGZBufferResource::GetBuffer(cIGZBuffer** out) const {
    return buffer_ ? buffer_->QueryInterface(kGZIID_cIGZBuffer, reinterpret_cast<void**>(out)) : false;
}

bool cGZBufferResource::GetBufferAs(std::uint32_t iid, void** out) const {
    return buffer_ ? buffer_->QueryInterface(iid, out) : false;
}

// ---- saving ---------------------------------------------------------------------------------------

bool cGZBufferResource::SaveToSerialRecord(cIGZPersistDBSerialRecord* record, std::uint32_t format) const {
    if (!record || !buffer_) return false;
    if (!buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockRead))) return false;
    bool result = false;
    if (format == kGZRecordTypeBitmap24) result = save_bitmap_24(record);
    else if (format == 2) result = save_v2(record);
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
    return result;
}

bool cGZBufferResource::save_v2(cIGZPersistDBSerialRecord* record) const {
    const std::uint32_t width = buffer_->Width(), height = buffer_->Height(), bytesPerPixel = buffer_->GetBytesPerPixel();
    const std::uint32_t rowBytes = width * bytesPerPixel, total = height * rowBytes;
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    const auto* bits = static_cast<const std::uint8_t*>(buffer_->GetColorSurfaceBits());
    const cGZBufferColorType colorType = buffer_->GetColorType();
    std::vector<std::uint8_t> packedRows;
    const std::uint8_t* source = bits;
    if (stride != rowBytes) {
        packedRows.resize(total);
        for (std::uint32_t y = 0; y < height; ++y) std::memcpy(packedRows.data() + y * rowBytes, bits + static_cast<std::size_t>(y) * stride, rowBytes);
        source = packedRows.data();
    }
    std::vector<std::uint8_t> out(total * 15 / 10 + 0x400 + 4);
    const int quick = 1;
    std::int32_t length = static_cast<std::int32_t>(encoderef(source, total, out.data() + 4, &quick)) + 4;
    std::memcpy(out.data(), &length, 4);
    if (length == 0 || !record->SetFieldUint32(2u) || !record->SetFieldUint32(static_cast<std::uint32_t>(length)) ||
        !record->SetFieldUint32(width) || !record->SetFieldUint32(height) || !record->SetFieldSint32(colorType.type))
        return false;
    return record->SetFieldVoid(out.data(), static_cast<std::uint32_t>(length));
}

// 16-bit buffers only: converted pixel by pixel to a bottom-up 24-bit bitmap.
bool cGZBufferResource::save_bitmap_24(cIGZPersistDBSerialRecord* record) const {
    if (buffer_->GetBitsPerPixel() != 0x10) return false;
    const std::uint32_t width = buffer_->Width(), height = buffer_->Height();
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    const std::uint32_t rowSize = ((width * 3 - 1) | 3) + 1;
    const std::uint32_t dataSize = rowSize * height;
    std::vector<std::uint8_t> data(dataSize);
    const auto* bits = static_cast<const std::uint8_t*>(buffer_->GetColorSurfaceBits());
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(bits + static_cast<std::size_t>(y) * (stride & ~1u));
        std::uint8_t* out = data.data() + static_cast<std::size_t>(height - 1 - y) * rowSize;
        for (std::uint32_t x = 0; x < width; ++x, out += 3) {
            std::uint8_t r, g, b;
            buffer_->ConvertNativeValueToRGB(row[x], r, g, b);
            out[0] = b; out[1] = g; out[2] = r;
        }
    }
    std::uint8_t fileHeader[14] = {'B', 'M'};
    const std::uint32_t fileSize = dataSize + 0x36, offset = 0x36;
    std::memcpy(fileHeader + 2, &fileSize, 4);
    std::memcpy(fileHeader + 10, &offset, 4);
    std::uint8_t info[0x28] = {};
    const std::uint32_t headerSize = 0x28;
    const std::uint16_t planes = 1, bitCount = 0x18;
    std::memcpy(info, &headerSize, 4);
    std::memcpy(info + 4, &width, 4);
    std::memcpy(info + 8, &height, 4);
    std::memcpy(info + 12, &planes, 2);
    std::memcpy(info + 14, &bitCount, 2);
    std::memcpy(info + 20, &dataSize, 4);
    record->SetFieldVoid(fileHeader, 14);
    record->SetFieldVoid(info, 0x28);
    record->SetFieldVoid(data.data(), dataSize);
    return true;
}

// ---- loading --------------------------------------------------------------------------------------

bool cGZBufferResource::LoadFromSerialRecord(cIGZPersistDBSerialRecord* record, const cGZBufferColorType& colorType) {
    if (!record || !buffer_) return false;
    std::uint32_t first[2] = {};
    if (!record->GetFieldVoid(first, 8)) return false;
    if (static_cast<std::uint16_t>(first[0]) == 0x4d42) {
        std::uint8_t rest[6];
        record->GetFieldVoid(rest, 6);       // the remainder of the 14-byte file header
        return load_bitmap(record, colorType);
    }
    Header header;
    header.version = first[0];
    header.size = first[1];
    if (first[0] == 1 || first[0] == 2) {
        if (!record->GetFieldUint32(header.width) || !record->GetFieldUint32(header.height) || !record->GetFieldSint32(header.colorType))
            return false;
        return first[0] == 1 ? load_v1(record, header) : load_v2(record, header);
    }
    if (first[0] == kGZBufferResourceV0) {
        std::uint32_t height = 0;
        if (!record->GetFieldUint32(height)) return false;
        return load_v0(record, first[1], height, colorType);
    }
    return false;
}

bool cGZBufferResource::load_bitmap(cIGZPersistDBSerialRecord* record, const cGZBufferColorType& colorType) {
    std::vector<std::uint8_t> info(0x28 + 0x400);
    if (!record->GetFieldVoid(info.data(), 0x28)) return false;
    const BitmapInfo bmp = ParseInfo(info.data());
    if (bmp.compression != 0) return false;
    if (bmp.bitCount > 0x18 || ((0x1000110ull >> bmp.bitCount) & 1) == 0) return false;   // 4, 8 or 24 bits
    buffer_->Shutdown();
    if (!buffer_->Init(static_cast<std::uint32_t>(bmp.width), static_cast<std::uint32_t>(bmp.height), colorType)) return false;
    if (!buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite))) return false;
    const std::uint32_t rowBytes = ((((bmp.bitCount * static_cast<std::uint32_t>(bmp.width) - 1) | 7) + 1) >> 3);
    const std::uint32_t size = (((rowBytes - 1) | 3) + 1) * static_cast<std::uint32_t>(bmp.height);
    std::vector<std::uint8_t> pixels(size);
    bool result = false;
    if (bmp.bitCount == 0x18) {
        result = record->GetFieldVoid(pixels.data(), size) && translate_bmp(info.data(), pixels.data(), size);
    } else {
        std::uint32_t paletteBytes = bmp.bitCount == 8 ? 0x400 : 0x40;
        if (bmp.paletteCount != 0) paletteBytes = bmp.paletteCount * 4;
        if (paletteBytes <= 0x400 && record->GetFieldVoid(info.data() + 0x28, paletteBytes) && record->GetFieldVoid(pixels.data(), size))
            result = translate_bmp(info.data(), pixels.data(), size);
    }
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite));
    return result;
}

bool cGZBufferResource::load_v0(cIGZPersistDBSerialRecord* record, std::uint32_t width, std::uint32_t height, const cGZBufferColorType& colorType) {
    buffer_->Shutdown();
    if (!buffer_->Init(width, height, colorType) || !buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite))) return false;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    bool ok = record->GetFieldUint32(pixels.data(), width * height);
    if (ok) {
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x) buffer_->SetPixel(x, y, pixels[y * width + x]);
    }
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite));
    return ok;
}

bool cGZBufferResource::load_v1(cIGZPersistDBSerialRecord* record, const Header& header) {
    const std::uint64_t packed = PackedColorType(static_cast<std::uint32_t>(header.colorType));
    buffer_->Shutdown();
    if (!buffer_->Init(header.width, header.height, ColorTypeOf(static_cast<std::uint32_t>(header.colorType)))) return false;
    if (!buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockRead))) return false;
    const std::uint32_t bytesPerPixel = static_cast<std::uint32_t>(packed >> 32) >> 3;
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    const std::uint32_t rowBytes = header.width * bytesPerPixel;
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    std::vector<std::uint8_t> staging;
    std::uint8_t* target = bits;
    if (stride != rowBytes) {
        staging.resize(header.size);
        target = staging.data();
    }
    bool ok = record->GetFieldVoid(target, header.size);
    if (stride != rowBytes && ok) {
        // the original advances the destination by the row size and the source by the surface stride
        const std::uint8_t* source = staging.data();
        std::uint8_t* destination = bits;
        for (std::uint32_t y = 0; y < header.height; ++y) {
            std::memcpy(destination, source, rowBytes);
            source += stride;
            destination += rowBytes;
        }
    }
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
    return ok && g_converter && g_converter(buffer_, static_cast<std::uint32_t>(header.colorType));
}

bool cGZBufferResource::load_v2(cIGZPersistDBSerialRecord* record, const Header& header) {
    buffer_->Shutdown();
    if (!buffer_->Init(header.width, header.height, ColorTypeOf(static_cast<std::uint32_t>(header.colorType)))) return false;
    if (!buffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockRead))) return false;
    const std::uint64_t packed = PackedColorType(static_cast<std::uint32_t>(header.colorType));
    const std::uint32_t bytesPerPixel = static_cast<std::uint32_t>(packed >> 32) >> 3;
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    const std::uint32_t rowBytes = header.width * bytesPerPixel;
    const std::uint32_t total = header.height * rowBytes;
    std::vector<std::uint8_t> blob(header.size);
    bool ok = record->GetFieldVoid(blob.data(), header.size);
    if (ok) {
        auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
        std::vector<std::uint8_t> staging;
        std::uint8_t* target = bits;
        if (stride != rowBytes) {
            staging.resize(total);
            target = staging.data();
        }
        std::ptrdiff_t consumed = header.size;
        std::uint32_t decoded = blob.size() >= 4 ? decoderef(blob.data() + 4, &consumed, target) : 0;
        ok = decoded == total && decoded != 0;
        if (ok && stride != rowBytes) {
            for (std::uint32_t y = 0; y < header.height; ++y) std::memcpy(bits + static_cast<std::size_t>(y) * stride, staging.data() + static_cast<std::size_t>(y) * rowBytes, rowBytes);
        }
    }
    buffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
    return ok && g_converter && g_converter(buffer_, static_cast<std::uint32_t>(header.colorType));
}

// ---- bitmap translation -----------------------------------------------------------------------------

bool cGZBufferResource::translate_bmp(const std::uint8_t* info, const std::uint8_t* pixels, std::size_t pixelBytes) {
    const BitmapInfo bmp = ParseInfo(info);
    const std::uint32_t bits = buffer_->GetBitsPerPixel();
    if (bmp.bitCount == 0x18) {
        if (bits == 0x20) translate_24_to_32(info, pixels, pixelBytes);
        else if (bits == 0x18) translate_24_to_24(info);
        else if (bits == 0x10) translate_24_to_16(info, pixels);
        else return false;
    } else if (bmp.bitCount == 8) {
        if (bits == 0x10) translate_8_to_16(info, pixels);
        else if (bits == 8) translate_8_to_8(info, pixels);
        else return false;
    } else if (bmp.bitCount == 4 && bits == 0x10) {
        translate_4_to_16(info, pixels);
    } else {
        return false;
    }
    return true;
}

// Two pixels per byte, high nibble first; the palette (16 entries of blue, green, red, pad) is converted up front.
void cGZBufferResource::translate_4_to_16(const std::uint8_t* info, const std::uint8_t* pixels) {
    const BitmapInfo bmp = ParseInfo(info);
    std::uint16_t palette[16];
    for (int i = 0; i < 16; ++i) palette[i] = static_cast<std::uint16_t>(buffer_->ConvertRGBValueToNative(bmp.palette[i * 4 + 2], bmp.palette[i * 4 + 1], bmp.palette[i * 4]));
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    const std::uint32_t rowBytes = (((bmp.width * 4 - 1) | 7) + 1) >> 3;
    std::int32_t y = bmp.height;
    auto* row = reinterpret_cast<std::uint16_t*>(bits) + static_cast<std::ptrdiff_t>((y - 1)) * (stride >> 1);
    for (; y != 0; --y) {
        for (std::int32_t x = 0; x < bmp.width; x += 2) {
            const std::uint8_t b = *pixels++;
            row[x] = palette[b >> 4];
            row[x + 1] = palette[b & 0xf];
        }
        pixels += (((rowBytes - 1) | 3) + 1) - ((bmp.width - 1) / 2 + 1);
        row -= stride >> 1;
    }
}

void cGZBufferResource::translate_8_to_8(const std::uint8_t* info, const std::uint8_t* pixels) {
    const BitmapInfo bmp = ParseInfo(info);
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* row = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits()) + static_cast<std::size_t>(bmp.height - 1) * stride;
    for (std::int32_t y = bmp.height; y != 0; --y) {
        if (bmp.width > 0) {
            std::memcpy(row, pixels, static_cast<std::size_t>(bmp.width));
            pixels += bmp.width;
        }
        pixels += ((static_cast<std::uint32_t>(bmp.width) - 1) | 3) + 1 - bmp.width;
        row -= stride;
    }
}

void cGZBufferResource::translate_8_to_16(const std::uint8_t* info, const std::uint8_t* pixels) {
    const BitmapInfo bmp = ParseInfo(info);
    std::uint16_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = static_cast<std::uint16_t>(buffer_->ConvertRGBValueToNative(bmp.palette[i * 4 + 2], bmp.palette[i * 4 + 1], bmp.palette[i * 4]));
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    auto* row = reinterpret_cast<std::uint16_t*>(bits) + static_cast<std::ptrdiff_t>(bmp.height - 1) * (stride >> 1);
    for (std::int32_t y = bmp.height; y != 0; --y) {
        for (std::int32_t x = 0; x < bmp.width; ++x) row[x] = palette[pixels[x]];
        if (bmp.width > 0) pixels += bmp.width;
        pixels += ((static_cast<std::uint32_t>(bmp.width) - 1) | 3) + 1 - bmp.width;
        row -= stride >> 1;
    }
}

void cGZBufferResource::translate_24_to_16(const std::uint8_t* info, const std::uint8_t* pixels) {
    const BitmapInfo bmp = ParseInfo(info);
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    auto* row = reinterpret_cast<std::uint16_t*>(bits) + static_cast<std::ptrdiff_t>(bmp.height - 1) * (stride >> 1);
    for (std::int32_t y = bmp.height; y != 0; --y) {
        for (std::int32_t x = 0; x < bmp.width; ++x, pixels += 3)
            row[x] = static_cast<std::uint16_t>(buffer_->ConvertRGBValueToNative(pixels[2], pixels[1], pixels[0]));
        pixels += (((static_cast<std::uint32_t>(bmp.width) * 3 - 1) | 3) + 1) - bmp.width * 3;
        row -= stride >> 1;
    }
}

// The original ignores the source and fills the buffer with 0xff.
void cGZBufferResource::translate_24_to_24(const std::uint8_t* info) {
    const BitmapInfo bmp = ParseInfo(info);
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    for (std::int32_t y = 0; y < bmp.height; ++y) {
        std::uint8_t* row = bits + static_cast<std::size_t>(bmp.height - 1 - y) * stride;
        std::memset(row, 0xff, static_cast<std::size_t>(bmp.width) * 3);
    }
}

// Source and destination share one byte offset (row * stride / 4), counting rows from the height down to 1; words
// that would lie outside the bitmap data read as zero.
void cGZBufferResource::translate_24_to_32(const std::uint8_t* info, const std::uint8_t* pixels, std::size_t pixelBytes) {
    const BitmapInfo bmp = ParseInfo(info);
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    for (std::int64_t row = bmp.height; row != 0; --row) {
        if (bmp.width == 0) continue;
        const std::size_t offset = static_cast<std::size_t>(row) * (stride >> 2);
        for (std::int32_t x = 0; x < bmp.width; ++x) {
            std::uint32_t value = 0;
            if (offset + x * 4 + 4 <= pixelBytes) std::memcpy(&value, pixels + offset + x * 4, 4);
            value |= 0xff000000u;
            std::memcpy(bits + offset + x * 4, &value, 4);
        }
    }
}
