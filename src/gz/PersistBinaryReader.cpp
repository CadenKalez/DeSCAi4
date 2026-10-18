#include "gz/PersistBinaryReader.h"

#include <cstdio>

#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"

namespace {
constexpr std::uint32_t kPersistResourceManagerService = 0x056b906e, kPersistResourceManagerIid = 0x656b8efc;

void Report(const char* format, std::uint32_t group, std::uint32_t instance, std::uint32_t type) {
    char text[160];
    std::snprintf(text, sizeof text, format, group, instance, type);
    if (IPersistHost* host = PersistHost()) host->Diagnostic(text);
}
}  // namespace

cGZPersistBinaryReader::~cGZPersistBinaryReader() = default;   // @ 0x1004dd994

bool cGZPersistBinaryReader::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1004dd9dc
    if (!out || (iid != 1 && iid != kGZIID_cIGZPersistReader)) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZPersistBinaryReader::Release() {   // @ 0x1004dda12
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

void cGZPersistBinaryReader::ReadRoot(cIGZPersistResource2* resource, cIGZIStream* stream) {   // @ 0x1004dda2e
    frames_.push_back(cResourceInfo(stream));
    cRZSysServPtr<cIGZPersistResourceManager, kPersistResourceManagerService, kPersistResourceManagerIid> manager;
    const cGZPersistResourceKey rootKey = resource->AsResource()->GetKey();
    const bool alreadyRegistered = manager ? manager->HasRegisteredResource(rootKey) : true;
    if (!alreadyRegistered) manager->RegisterResource(resource->AsResource()->GetKey(), *resource->AsResource());

    std::uint32_t count = 0;
    InputStream()->GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        cGZPersistResourceKey key;
        InputStream()->GetUint32(key.group);
        InputStream()->GetUint32(key.instance);
        InputStream()->GetUint32(key.type);
        cRZAutoRefCount<cIGZPersistResource2> dependent;
        if (key.type == 0) {
            Report("Null dependent resource with group ID = %xu, instance ID = %xu, type = %xu\n", key.group, key.instance, 0);
        } else if (!manager || !manager->GetResource(key, kGZIID_cIGZPersistResource2Loaded, dependent.AsOutParam(), 0, nullptr)) {
            Report("Cannot load dependent resource with group ID = %xu, instance ID = %xu, type = %xu\n", key.group, key.instance, key.type);
        }
        frames_.back().dependents.push_back(dependent);
    }

    std::uint32_t objectCount = 0;
    InputStream()->GetUint32(objectCount);
    frames_.back().objects.emplace_back(static_cast<cIGZPersistSerializableReferent*>(resource));   // object 0 is the root resource
    std::uint32_t classId = 0;
    InputStream()->GetUint32(classId);
    for (std::uint32_t i = 1; i < objectCount; ++i) {
        InputStream()->GetUint32(classId);
        cRZAutoRefCount<cIGZPersistSerializableReferent> object;
        if (classId != 0) {
            if (IPersistHost* host = PersistHost()) host->GetCOMObject(classId, kGZIID_cIGZPersistSerializableReferent, object.AsOutParam());
        }
        frames_.back().objects.push_back(object);
    }
    for (std::uint32_t i = 0; i < objectCount; ++i) {
        if (cIGZPersistSerializableReferent* object = frames_.back().objects[i].get()) object->Read(*this);
    }
    if (!alreadyRegistered) manager->UnregisterResource(resource->AsResource()->GetKey());
    frames_.pop_back();
}

bool cGZPersistBinaryReader::ReadBool(const char*) { std::uint8_t v = 0; InputStream()->GetUint8(v); return v != 0; }   // @ 0x1004de17c
std::int8_t cGZPersistBinaryReader::ReadSint8(const char*) { std::int8_t v = 0; InputStream()->GetSint8(v); return v; }   // @ 0x1004de1a8
std::uint8_t cGZPersistBinaryReader::ReadUint8(const char*) { std::uint8_t v = 0; InputStream()->GetUint8(v); return v; }   // @ 0x1004de1d2
std::int16_t cGZPersistBinaryReader::ReadSint16(const char*) { std::int16_t v = 0; InputStream()->GetSint16(v); return v; }   // @ 0x1004de1fc
std::uint16_t cGZPersistBinaryReader::ReadUint16(const char*) { std::uint16_t v = 0; InputStream()->GetUint16(v); return v; }   // @ 0x1004de226
std::int32_t cGZPersistBinaryReader::ReadSint32(const char*) { std::int32_t v = 0; InputStream()->GetSint32(v); return v; }   // @ 0x1004de250
std::uint32_t cGZPersistBinaryReader::ReadUint32(const char*) { std::uint32_t v = 0; InputStream()->GetUint32(v); return v; }   // @ 0x1004de278
float cGZPersistBinaryReader::ReadFloat32(const char*) { float v = 0; InputStream()->GetFloat32(v); return v; }   // @ 0x1004de2a0
double cGZPersistBinaryReader::ReadFloat64(const char*) { double v = 0; InputStream()->GetFloat64(v); return v; }   // @ 0x1004de2ca

void cGZPersistBinaryReader::ReadBoolArray(const char*, bool* values, std::uint32_t count) {   // @ 0x1004de2f4
    for (; count != 0; --count) {
        std::uint8_t v = 0;
        InputStream()->GetUint8(v);
        *values++ = v != 0;
    }
}
void cGZPersistBinaryReader::ReadSint8Array(const char*, std::int8_t* values, std::uint32_t count) { InputStream()->GetVoid(values, count); }   // @ 0x1004de34a
void cGZPersistBinaryReader::ReadUint8Array(const char*, std::uint8_t* values, std::uint32_t count) { InputStream()->GetVoid(values, count); }   // @ 0x1004de37a
void cGZPersistBinaryReader::ReadSint16Array(const char*, std::int16_t* values, std::uint32_t count) {   // @ 0x1004de3aa
    for (; count != 0; --count) InputStream()->GetSint16(*values++);
}
void cGZPersistBinaryReader::ReadUint16Array(const char*, std::uint16_t* values, std::uint32_t count) {   // @ 0x1004de3ee
    for (; count != 0; --count) InputStream()->GetUint16(*values++);
}
void cGZPersistBinaryReader::ReadSint32Array(const char*, std::int32_t* values, std::uint32_t count) {   // @ 0x1004de432
    for (; count != 0; --count) InputStream()->GetSint32(*values++);
}
void cGZPersistBinaryReader::ReadUint32Array(const char*, std::uint32_t* values, std::uint32_t count) {   // @ 0x1004de476
    for (; count != 0; --count) InputStream()->GetUint32(*values++);
}
void cGZPersistBinaryReader::ReadFloat32Array(const char*, float* values, std::uint32_t count) {   // @ 0x1004de4ba
    for (; count != 0; --count) InputStream()->GetFloat32(*values++);
}
void cGZPersistBinaryReader::ReadFloat64Array(const char*, double* values, std::uint32_t count) {   // @ 0x1004de4fe
    for (; count != 0; --count) InputStream()->GetFloat64(*values++);
}

void cGZPersistBinaryReader::ReadString(const char*, cIGZString* value) { InputStream()->GetGZStr(*value); }   // @ 0x1004de542
void cGZPersistBinaryReader::SkipBytes(const char*, std::uint32_t count) { InputStream()->Skip(count); }       // @ 0x1004de56a

void cGZPersistBinaryReader::ReadObject(const char*, std::uint32_t iid, void** out) {   // @ 0x1004de590
    std::uint32_t classId = 0;
    InputStream()->GetUint32(classId);
    if (classId == 0) {
        *out = nullptr;
        return;
    }
    cRZAutoRefCount<cIGZPersistSerializable2> object;
    if (IPersistHost* host = PersistHost()) host->GetCOMObject(classId, kGZIID_cIGZPersistSerializable2, object.AsOutParam());
    object->Read(*this);
    object->QueryInterface(iid, out);
}

void cGZPersistBinaryReader::ReadObjectRef(const char*, std::uint32_t iid, void** out) {   // @ 0x1004de64c
    std::uint8_t present = 0, elsewhere = 0;
    std::uint32_t index = 0;
    InputStream()->GetUint8(present);
    if (!present) {
        *out = nullptr;
        return;
    }
    InputStream()->GetUint8(elsewhere);
    InputStream()->GetUint32(index);
    *out = nullptr;
    cIGZUnknown* target = elsewhere ? static_cast<cIGZUnknown*>(frames_.back().dependents[index].get())
                                    : static_cast<cIGZUnknown*>(frames_.back().objects[index].get());
    if (target) target->QueryInterface(iid, out);
}

cGZPersistTypeInfo cGZPersistBinaryReader::ReadTypeInfo() {   // @ 0x1004de740
    cGZPersistTypeInfo info;
    info.name = cRZAutoRefCount<cIGZString>(new cRZString());
    InputStream()->GetGZStr(*info.name);
    InputStream()->GetUint32(info.classId);
    InputStream()->GetUint32(info.version);
    return info;
}

cIGZIStream* cGZPersistBinaryReader::InputStream() { return frames_.back().stream.get(); }   // @ 0x1004de80a
