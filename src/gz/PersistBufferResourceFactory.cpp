#include "gz/PersistBufferResourceFactory.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "gz/RefPack.h"

namespace {
constexpr std::uint32_t kLockRead = 0x8040, kLockWrite = 0x8080, kLockBlit = 0x8010;
constexpr std::uint32_t kBitsOfColorType[8] = {8, 16, 16, 16, 16, 16, 32, 32};   // table at 0x100a27070, colour types 2..9

cGZBufferColorType ColorTypeOf(std::int32_t type) {
    const std::uint32_t index = static_cast<std::uint32_t>(type) - 2;
    return cGZBufferColorType{type, index < 8 ? kBitsOfColorType[index] : 0};
}

// Bytes per pixel the record loader assumes for a colour type (0 = unsupported).
std::uint32_t BytesOfColorType(std::uint32_t type) {
    if (type - 3 < 5) return 2;
    if (type - 8 < 2) return 4;
    if (type == 2) return 1;
    return 0;
}
}  // namespace

cGZPersistBufferResourceFactory::cGZPersistBufferResourceFactory(IPersistBufferHost* host) : host_(host) {}   // 0x100611c9c
cGZPersistBufferResourceFactory::~cGZPersistBufferResourceFactory() = default;

bool cGZPersistBufferResourceFactory::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistBufferResourceFactory) {
        *out = &secondary_;
        AddRef();
        return true;
    }
    return cGZPersistResourceFactory::QueryInterface(iid, out);
}

bool cGZPersistBufferResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {   // 0x100611d46
    if (type != kGZResourceTypeBufferResource) return false;
    return CreateInstance(type, iid, out, static_cast<std::uint32_t>(colorType_.type), nullptr);
}

// 0x100611d6c: a new resource asking for the given colour type; the caller gets the requested interface (and the only
// reference).
bool cGZPersistBufferResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t flags, cIGZUnknown*) {
    if (type != kGZResourceTypeBufferResource) return false;
    const std::uint32_t index = (flags - 2) & 0xffff;
    cGZBufferColorType colorType{static_cast<std::int32_t>(flags & 0xffff), index < 8 ? kBitsOfColorType[index] : 0};
    auto* resource = new cGZPersistBufferResource(host_, colorType);
    return resource->QueryInterface(iid, out);
}

// 0x100611e16: reads the record into a scratch resource and blits the result into the buffer the target resource holds.
bool cGZPersistBufferResourceFactory::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != kGZResourceTypeBufferResource) return false;
    auto* target = dynamic_cast<cGZPersistBufferResource*>(&resource);
    if (target == nullptr) return false;
    cIGZBuffer* destination = static_cast<cIGZPersistBufferResource*>(target)->GetBuffer();
    if (destination == nullptr) return false;
    auto* scratch = new cGZPersistBufferResource(host_, target->RequestedColorType());
    scratch->AddRef();
    bool result = false;
    cIGZBuffer* loaded = nullptr;
    if (scratch->LoadFromRecord(&record)) loaded = static_cast<cIGZPersistBufferResource*>(scratch)->GetBuffer();
    if (loaded != nullptr) {
        if (destination->Init(loaded->Width(), loaded->Height(), loaded->GetColorType())) {
            if (destination->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBlit))) {
                cRZRect source;
                loaded->GetBufferArea(source);
                cRZRect area;
                destination->GetBufferArea(area);
                destination->Blt(loaded, source, area, nullptr);
                destination->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBlit));
            }
        }
        result = true;   // kept: reported even when the copy did not happen
        loaded->Release();
    }
    destination->Release();
    scratch->Release();
    return result;
}

bool cGZPersistBufferResourceFactory::SerialWrite(cIGZPersistResource&, cIGZPersistDBSerialRecord&) { return false; }   // 0x10061201a

// 0x100612022: a bitmap file; the resource that loaded it is handed out through the buffer interface.
bool cGZPersistBufferResourceFactory::LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, cGZBufferColorType colorType, bool) {
    auto* resource = new cGZPersistBufferResource(host_, cGZBufferColorType{1, 0});
    resource->AddRef();
    bool ok = false;
    if (resource->LoadFromFile(path, false, colorType, false))
        ok = resource->QueryInterface(kGZIID_cIGZBuffer, reinterpret_cast<void**>(out));   // forwarded to the buffer (counted)
    resource->Release();   // (the original never releases the resource)
    return ok;
}

// 0x1006124e0
bool cGZPersistBufferResourceFactory::LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool) {
    if (out == nullptr || record == nullptr) return false;
    std::uint32_t version = 0, dataSize = 0, width = 0, height = 0;
    std::int32_t colorType = 0;
    cIGZBuffer* buffer = nullptr;
    bool success = record->GetFieldUint32(version);
    if (!success) return false;
    if (version == 1 || version == 2) {
        bool created = false;
        std::uint32_t bytesPerPixel = 0;
        bool header = record->GetFieldUint32(dataSize) && record->GetFieldUint32(width) && record->GetFieldUint32(height) &&
                      record->GetFieldSint32(colorType);
        if (header) {
            bytesPerPixel = BytesOfColorType(static_cast<std::uint32_t>(colorType));
            if (bytesPerPixel != 0) {
                buffer = host_->CreateGraphicSystemBuffer();   // RZ::GS() vslot 0x20
                if (buffer != nullptr) created = buffer->Init(width, height, ColorTypeOf(colorType));
            }
        }
        bool locked = created && buffer != nullptr && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
        if (!locked) {
            if (buffer != nullptr) buffer->Release();
            return false;
        }
        const std::uint32_t stride = buffer->GetColorSurfaceStride();
        const std::uint32_t rowBytes = width * bytesPerPixel;
        const std::uint32_t imageSize = height * rowBytes;
        auto* surface = static_cast<std::uint8_t*>(buffer->GetColorSurfaceBits());
        bool ok;
        if (version == 2) {
            std::vector<std::uint8_t> packed(dataSize);
            ok = record->GetFieldVoid(packed.data(), dataSize);
            if (ok) {
                std::vector<std::uint8_t> rows;
                std::uint8_t* target = surface;
                if (stride != rowBytes) {
                    rows.resize(imageSize);
                    target = rows.data();
                }
                std::ptrdiff_t consumed = static_cast<std::ptrdiff_t>(dataSize);
                // (guarded: a header announcing more than the image holds would overrun the target)
                const std::uint32_t decoded = (dataSize >= 5 && sizeref(packed.data() + 4) <= imageSize) ? decoderef(packed.data() + 4, &consumed, target) : 0;
                ok = decoded == imageSize && decoded != 0;
                if (ok && stride != rowBytes) {
                    for (std::uint32_t y = 0; y < height; ++y) std::memcpy(surface + static_cast<std::size_t>(y) * stride, rows.data() + static_cast<std::size_t>(y) * rowBytes, rowBytes);
                }
            }
        } else {
            std::vector<std::uint8_t> rows;
            std::uint8_t* target = surface;
            if (stride != rowBytes) {
                rows.resize(std::max<std::size_t>(dataSize, static_cast<std::size_t>(height) * stride));
                target = rows.data();
            }
            ok = record->GetFieldVoid(target, dataSize);
            if (stride != rowBytes && ok && height != 0) {
                // Kept: the original steps the source by the surface stride and the destination by the row size (swapped).
                const std::uint8_t* from = rows.data();
                std::uint8_t* to = surface;
                for (std::uint32_t y = 0; y < height; ++y, from += stride, to += rowBytes) std::memcpy(to, from, rowBytes);
            }
        }
        if (!ok) {
            buffer->Release();
            return false;
        }
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
        success = host_->ConvertBufferFromFormatToFormat(buffer, colorType);
    } else if (version == kGZResourceTypeBufferResource) {
        // legacy: width, height, then one 32 bit pixel each, drawn through SetPixel into a 16 bit display buffer
        if (!record->GetFieldUint32(width) || !record->GetFieldUint32(height)) return false;
        buffer = host_->CreateGraphicSystemBuffer();
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
        bool ok = buffer != nullptr && record->GetFieldUint32(pixels.data(), width * height);
        if (ok) {
            success = buffer->Init(width, height, cGZBufferColorType{3, 1});
            if (success && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite))) {
                for (std::uint32_t y = 0; y < height; ++y)
                    for (std::uint32_t x = 0; x < width; ++x) buffer->SetPixel(x, y, pixels[static_cast<std::size_t>(y) * width + x]);
                buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockWrite));
            }
        } else {
            success = false;
        }
    }
    // Kept: an unknown version reports success with a null buffer.
    if (success) {
        *out = buffer;
        return true;
    }
    if (buffer != nullptr) buffer->Release();
    return false;
}

// 0x100612a20: version 2 (RefPack, preceded by its length) when `compress`, else version 1 (raw rows).
bool cGZPersistBufferResourceFactory::SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record, bool compress) {
    if (buffer == nullptr || record == nullptr) return false;
    if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockRead))) return false;
    const std::uint32_t width = buffer->Width(), height = buffer->Height(), bytesPerPixel = buffer->GetBytesPerPixel();
    const std::uint32_t stride = buffer->GetColorSurfaceStride();
    const auto* bits = static_cast<const std::uint8_t*>(buffer->GetColorSurfaceBits());
    const cGZBufferColorType colorType = buffer->GetColorType();
    const std::uint32_t rowBytes = width * bytesPerPixel;
    const std::uint32_t imageSize = height * rowBytes;
    std::vector<std::uint8_t> rows;
    const std::uint8_t* data = bits;
    if (stride != rowBytes) {
        rows.resize(imageSize);
        for (std::uint32_t y = 0; y < height; ++y) std::memcpy(rows.data() + static_cast<std::size_t>(y) * rowBytes, bits + static_cast<std::size_t>(y) * stride, rowBytes);
        data = rows.data();
    }
    std::vector<std::uint8_t> packed;
    const std::uint8_t* payload = data;
    std::uint32_t payloadSize = imageSize;
    std::uint32_t version = 1;
    if (compress) {
        packed.resize(imageSize * 15 / 10 + 0x400 + 4);
        const int quick = 1;
        const std::int32_t length = static_cast<std::int32_t>(encoderef(data, imageSize, packed.data() + 4, &quick)) + 4;
        std::memcpy(packed.data(), &length, 4);
        payload = packed.data();
        payloadSize = static_cast<std::uint32_t>(length);
        version = 2;
    }
    bool ok = record->SetFieldUint32(version) && record->SetFieldUint32(payloadSize) && record->SetFieldUint32(width) &&
              record->SetFieldUint32(height) && record->SetFieldSint32(colorType.type) && record->SetFieldVoid(payload, payloadSize);
    buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockRead));
    return ok;
}
