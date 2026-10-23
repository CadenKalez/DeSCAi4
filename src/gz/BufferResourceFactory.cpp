#include "gz/BufferResourceFactory.h"

#include "gz/cGZDBSDFRecord.h"

cGZBufferResourceFactory::cGZBufferResourceFactory() { writeVersions_.push_back(2); }   // 0x1006162fa

cGZBufferResourceFactory::~cGZBufferResourceFactory() = default;   // 0x100616b62

bool cGZBufferResourceFactory::QueryInterface(std::uint32_t iid, void** out) {   // 0x100616398
    if (iid == kGZIID_cIGZBufferResourceFactory) {
        *out = &secondary_;
        AddRef();
        return true;
    }
    return cGZPersistResourceFactory::QueryInterface(iid, out);
}

// 0x100616410
bool cGZBufferResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    if (type != kGZBufferResourceV0) return false;
    auto* resource = new cGZBufferResource();
    resource->AddRef();
    const bool ok = resource->QueryInterface(iid, out);
    resource->Release();
    return ok;
}

// 0x100616486: reads with the factory's colour type.
bool cGZBufferResourceFactory::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != kGZBufferResourceV0) return false;
    cIGZBufferResource* bufferResource = nullptr;
    if (!resource.QueryInterface(kGZIID_cIGZBufferResource, reinterpret_cast<void**>(&bufferResource))) return false;
    const bool ok = bufferResource->LoadFromSerialRecord(&record, colorType_);
    bufferResource->Release();
    return ok;
}

// 0x1006164f8: writes with the current write version.
bool cGZBufferResourceFactory::SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != kGZBufferResourceV0) return false;
    cIGZBufferResource* bufferResource = nullptr;
    if (!resource.QueryInterface(kGZIID_cIGZBufferResource, reinterpret_cast<void**>(&bufferResource))) return false;
    const bool ok = bufferResource->SaveToSerialRecord(&record, GetWriteVersion());
    bufferResource->Release();
    return ok;
}

// 0x100616586: the file is opened read-only as a plain-file record; the colour type is the caller's.
bool cGZBufferResourceFactory::LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, const cGZBufferColorType& colorType, bool) {
    cGZDBSDFRecord record(nullptr);
    record.AddRef();
    if (!record.Init(cGZPersistResourceKey{}, path, cIGZPersistDBSDFRecord::kModeRead)) return false;
    auto* resource = new cGZBufferResource();
    resource->AddRef();
    bool ok = false;
    if (resource->LoadFromSerialRecord(static_cast<cIGZPersistDBSerialRecord*>(&record), colorType)) {
        cIGZBuffer* buffer = nullptr;
        if (resource->GetBuffer(&buffer) && buffer != nullptr) {
            ok = buffer->QueryInterface(kGZIID_cIGZBuffer, reinterpret_cast<void**>(out));
            buffer->Release();   // GetBuffer hands out a counted reference
        }
    }
    static_cast<cIGZPersistDBRecord&>(record).Close();
    resource->Release();
    return ok;
}

// 0x1006166a0: a new file record (mode 3) receives the buffer at the current write version.
bool cGZBufferResourceFactory::SaveToFile(const cIGZString& path, cIGZBuffer* buffer) {
    if (buffer == nullptr) return false;
    cGZDBSDFRecord record(nullptr);
    record.AddRef();
    if (!record.Init(cGZPersistResourceKey{}, path, cIGZPersistDBSDFRecord::kModeCreate)) return false;
    auto* resource = new cGZBufferResource();
    resource->AddRef();
    resource->SetBuffer(buffer);
    const bool ok = resource->SaveToSerialRecord(static_cast<cIGZPersistDBSerialRecord*>(&record), GetWriteVersion());
    static_cast<cIGZPersistDBRecord&>(record).Close();
    resource->Release();
    return ok;
}

// 0x1006167ce / 0x1006167de: the Windows resource loaders do nothing (the second one builds and drops a bitmap record).
bool cGZBufferResourceFactory::LoadFromResourceId(int, cIGZBuffer**, bool, const cGZBufferColorType&, bool) { return false; }
bool cGZBufferResourceFactory::LoadFromResourceId(void*, std::uint32_t, cIGZBuffer**) { return false; }

// 0x1006168b6
bool cGZBufferResourceFactory::LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool) {
    auto* resource = new cGZBufferResource();
    resource->AddRef();
    bool ok = resource->LoadFromSerialRecord(record, colorType_);
    if (ok) {
        cIGZBuffer* buffer = nullptr;
        if (resource->GetBuffer(&buffer) && buffer != nullptr) {
            buffer->QueryInterface(kGZIID_cIGZBuffer, reinterpret_cast<void**>(out));
            buffer->Release();
        }
    }
    resource->Release();
    return ok;
}

// 0x100616958
bool cGZBufferResourceFactory::SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record) {
    if (buffer == nullptr || record == nullptr) return false;
    auto* resource = new cGZBufferResource();
    resource->AddRef();
    resource->SetBuffer(buffer);
    const bool ok = resource->SaveToSerialRecord(record, GetWriteVersion());
    resource->Release();
    return ok;
}

void cGZBufferResourceFactory::SetColorType(const cGZBufferColorType& colorType) { colorType_ = colorType; }   // 0x100616a0c

bool cGZBufferResourceFactory::PushWriteVersion(std::uint32_t version) {   // 0x100616a28
    writeVersions_.push_back(version);
    return true;
}

// 0x100616a6c: the base version stays.
bool cGZBufferResourceFactory::PopWriteVersion() {
    const bool popped = writeVersions_.size() > 1;
    if (popped) writeVersions_.pop_back();
    return popped;
}

std::uint32_t cGZBufferResourceFactory::GetWriteVersion() const {   // 0x100616aa8
    return writeVersions_.empty() ? 2 : writeVersions_.back();
}
