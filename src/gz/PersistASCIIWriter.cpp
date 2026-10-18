#include "gz/PersistASCIIWriter.h"

#include <cstdio>
#include <string>

cGZPersistASCIIWriter::~cGZPersistASCIIWriter() = default;   // @ 0x1004df42c

bool cGZPersistASCIIWriter::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1004df474
    if (!out || (iid != 1 && iid != kGZIID_cIGZPersistWriter)) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZPersistASCIIWriter::Release() {   // @ 0x1004df4aa
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

namespace {
std::string Hex(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", value);
    return buffer;
}
}  // namespace

void cGZPersistASCIIWriter::WriteKey(const cGZPersistResourceKey& key) {   // @ 0x1004dfa9e
    Text("G:");
    Text(Hex(key.group).c_str());
    Text(" I:");
    Text(Hex(key.instance).c_str());
    Text(" T:");
    Text(Hex(key.type).c_str());
}

void cGZPersistASCIIWriter::WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) {   // @ 0x1004df4c6
    cRZAutoRefCount<cIGZPersistSurveyorWriter> surveyor;
    if (IPersistHost* host = PersistHost()) host->GetCOMObject(kGZCLSID_cGZPersistSurveyorWriter, kGZIID_cIGZPersistSurveyorWriter, surveyor.AsOutParam());
    surveyors_.push_back(surveyor);
    cIGZPersistSurveyorWriter* top = surveyors_.back().get();
    top->WriteRoot(resource, stream);

    Text("Resource ");
    {
        const cGZPersistTypeInfo info = resource->GetTypeInfo();
        cIGZOStream* out = OutputStream();
        out->SetGZStr(*info.name);
        Text(" ");
    }
    WriteKey(resource->AsResource()->GetKey());
    Text("\n");
    for (std::uint32_t i = 0; i < top->NumDependentResources(); ++i) {
        cIGZPersistResource2* dependent = top->DependentResource(static_cast<int>(i));
        cIGZOStream* out = OutputStream();
        Text("   Dep. Resource #");
        out->SetUint32(i);
        Text(" ");
        {
            const cGZPersistTypeInfo info = dependent->GetTypeInfo();
            out->SetGZStr(*info.name);
            Text(" ");
        }
        WriteKey(static_cast<const cIGZPersistResource2*>(dependent)->AsResource()->GetKey());
        Text("\n");
    }
    for (std::uint32_t i = 0; i < top->NumObjectRefs(); ++i) {
        cIGZPersistSerializableReferent* object = top->ObjectRef(static_cast<int>(i));
        cIGZOStream* out = OutputStream();
        Text("   Object #");
        out->SetUint32(i);
        Text(" ");
        {
            const cGZPersistTypeInfo info = object->GetTypeInfo();
            out->SetGZStr(*info.name);
            Text("\n");
        }
    }
    Text("\n");
    for (std::uint32_t i = 0; i < top->NumObjectRefs(); ++i) {
        cIGZPersistSerializableReferent* object = top->ObjectRef(static_cast<int>(i));
        cIGZOStream* out = OutputStream();
        Text("   Object #");
        out->SetUint32(i);
        Text(" ");
        {
            const cGZPersistTypeInfo info = object->GetTypeInfo();
            out->SetGZStr(*info.name);
            Text("\n");
        }
        Text("   {\n");
        object->Write(*this);
        Text("   }\n\n");
    }
    Text("\n");
    OutputStream()->Flush();
    for (std::uint32_t i = 0; i < top->NumDependentResources(); ++i) top->DependentResource(static_cast<int>(i))->Slot9();
    surveyors_.pop_back();
}

void cGZPersistASCIIWriter::WriteBool(const char* name, bool value) {   // @ 0x1004dfbfe
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      bool ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetRZCharStr(value ? "true" : "false");
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint8(const char* name, std::int8_t value) {   // @ 0x1004dfc82
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint8 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetSint8(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint8(const char* name, std::uint8_t value) {   // @ 0x1004dfcf4
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint8 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetUint8(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint16(const char* name, std::int16_t value) {   // @ 0x1004dfd66
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint16 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetSint16(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint16(const char* name, std::uint16_t value) {   // @ 0x1004dfdd8
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint16 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetUint16(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint32(const char* name, std::int32_t value) {   // @ 0x1004dfe4a
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetSint32(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint32(const char* name, std::uint32_t value) {   // @ 0x1004dfebc
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetUint32(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteFloat32(const char* name, float value) {   // @ 0x1004dff2e
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Float32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetFloat32(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteFloat64(const char* name, double value) {   // @ 0x1004dffa2
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Float64 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetFloat64(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteBoolArray(const char* name, const bool* values, std::uint32_t count) {   // @ 0x1004e0016
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      bool ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetRZCharStr(values[i] ? "true" : "false");
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint8Array(const char* name, const std::int8_t* values, std::uint32_t count) {   // @ 0x1004e0102
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint8 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetSint8(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint8Array(const char* name, const std::uint8_t* values, std::uint32_t count) {   // @ 0x1004e01de
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint8 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetUint8(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint16Array(const char* name, const std::int16_t* values, std::uint32_t count) {   // @ 0x1004e02ba
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint16 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetSint16(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint16Array(const char* name, const std::uint16_t* values, std::uint32_t count) {   // @ 0x1004e0396
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint16 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetUint16(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteSint32Array(const char* name, const std::int32_t* values, std::uint32_t count) {   // @ 0x1004e0472
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Sint32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetSint32(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteUint32Array(const char* name, const std::uint32_t* values, std::uint32_t count) {   // @ 0x1004e054e
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Uint32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetUint32(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteFloat32Array(const char* name, const float* values, std::uint32_t count) {   // @ 0x1004e062a
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Float32 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetFloat32(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteFloat64Array(const char* name, const double* values, std::uint32_t count) {   // @ 0x1004e0708
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      Float64 ");
    out->SetRZCharStr(name);
    out->SetRZCharStr("[");
    out->SetUint32(count);
    out->SetRZCharStr("] = ");
    out = OutputStream();
    for (std::uint32_t i = 0; i < count; ++i) {
        out->SetFloat64(values[i]);
        out->SetRZCharStr(", ");
        out = OutputStream();
    }
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteString(const char* name, const cIGZString* value) {   // @ 0x1004e07e6
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      RZString ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetGZStr(*value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteString(const char* name, const char* value) {   // @ 0x1004e0858
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      RZChar* ");
    out->SetRZCharStr(name);
    out->SetRZCharStr(" = ");
    out->SetRZCharStr(value);
    out->SetRZCharStr("\n");
}

void cGZPersistASCIIWriter::WriteObject(const char*, const cIGZPersistSerializable2* object) {   // @ 0x1004e08ca
    Text("      Object GZCLSID: ");
    if (!object) {
        Text(Hex(0).c_str());
    } else {
        {
            const cGZPersistTypeInfo info = object->GetTypeInfo();
            Text(Hex(info.classId).c_str());
        }
        Text("\n      {\n");
        object->Write(*this);
        Text("      }\n");
    }
    Text("\n");
}

void cGZPersistASCIIWriter::WriteObjectRef(const char* name, const cIGZPersistSerializableReferent* object) {   // @ 0x1004e0a60
    Text("      cIGZPersistSerializableReferent* ");
    Text(name);
    Text(" = ");
    if (!object) {
        Text("0");
    } else {
        cIGZPersistResource2* owner = object->GetResource();
        cIGZPersistSurveyorWriter* top = surveyors_.back().get();
        const bool local = top->Resource() == owner;
        cIGZOStream* out = OutputStream();
        if (local) {
            out->SetRZCharStr("Object #");
            out->SetUint32(static_cast<std::uint32_t>(surveyors_.back()->IndexOfObjectRef(object)));
            out->SetRZCharStr(" ");
            {
                const cGZPersistTypeInfo info = object->GetTypeInfo();
                out->SetGZStr(*info.name);
            }
            Text(" GZCLSID: ");
            const cGZPersistTypeInfo info = object->GetTypeInfo();
            const std::string id = Hex(info.classId);
            Text(id.c_str());
        } else {
            out->SetRZCharStr("Object ID ");
            out->SetUint32(static_cast<std::uint32_t>(static_cast<const cIGZPersistResource2*>(owner)->IndexOfObject(object)));
            out->SetRZCharStr(" ");
            {
                const cGZPersistTypeInfo info = object->GetTypeInfo();
                out->SetGZStr(*info.name);
                out->SetRZCharStr(" in dep. resource #");
                out->SetUint32(static_cast<std::uint32_t>(surveyors_.back()->IndexOfDependentResource(owner)));
                out->SetRZCharStr(" ");
            }
            WriteKey(static_cast<const cIGZPersistResource2*>(owner)->AsResource()->GetKey());
        }
    }
    Text("\n");
}

void cGZPersistASCIIWriter::WriteObjectRef(const char* name, const cIGZUnknown* object) {   // @ 0x1004e0dba
    cRZAutoRefCount<cIGZPersistSerializableReferent> referent;
    if (object) const_cast<cIGZUnknown*>(object)->QueryInterface(kGZIID_cIGZPersistSerializableReferent, referent.AsOutParam());
    WriteObjectRef(name, referent.get());
}

void cGZPersistASCIIWriter::WriteTypeInfo(const cGZPersistTypeInfo& info) {   // @ 0x1004e0e3a
    cIGZOStream* out = OutputStream();
    out->SetRZCharStr("      TypeName: ");
    out->SetGZStr(*info.name);
    out->SetRZCharStr(" GZCLSID: ");
    const std::string id = Hex(info.classId);
    Text(id.c_str());
    out = OutputStream();
    out->SetRZCharStr(" Version: ");
    out->SetUint32(info.version);
    out->SetRZCharStr("\n");
}

cIGZOStream* cGZPersistASCIIWriter::OutputStream() { return surveyors_.back()->OutputStream(); }   // @ 0x1004e0f38
