#include "gz/PersistBinaryWriter.h"

cGZPersistBinaryWriter::~cGZPersistBinaryWriter() = default;   // @ 0x1004de87e

bool cGZPersistBinaryWriter::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1004de8c6
    if (!out || (iid != 1 && iid != kGZIID_cIGZPersistWriter)) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZPersistBinaryWriter::Release() {   // @ 0x1004de8fc
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

void cGZPersistBinaryWriter::WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) {   // @ 0x1004de918
    cRZAutoRefCount<cIGZPersistSurveyorWriter> surveyor;
    if (IPersistHost* host = PersistHost()) host->GetCOMObject(kGZCLSID_cGZPersistSurveyorWriter, kGZIID_cIGZPersistSurveyorWriter, surveyor.AsOutParam());
    surveyors_.push_back(surveyor);
    cIGZPersistSurveyorWriter* top = surveyors_.back().get();
    top->WriteRoot(resource, stream);

    OutputStream()->SetUint32(top->NumDependentResources());
    for (std::uint32_t i = 0; i < top->NumDependentResources(); ++i) {
        cIGZPersistResource2* dependent = top->DependentResource(static_cast<int>(i));
        cIGZOStream* out = OutputStream();
        const cIGZPersistResource* asResource = static_cast<const cIGZPersistResource2*>(dependent)->AsResource();
        out->SetUint32(asResource->GetKey().group);
        out->SetUint32(asResource->GetKey().instance);
        out->SetUint32(asResource->GetKey().type);
    }
    OutputStream()->SetUint32(top->NumObjectRefs());
    for (std::uint32_t i = 0; i < top->NumObjectRefs(); ++i) {
        cIGZOStream* out = OutputStream();
        const cGZPersistTypeInfo info = top->ObjectRef(static_cast<int>(i))->GetTypeInfo();
        out->SetUint32(info.classId);
    }
    for (std::uint32_t i = 0; i < top->NumObjectRefs(); ++i) top->ObjectRef(static_cast<int>(i))->Write(*this);
    OutputStream()->Flush();
    for (std::uint32_t i = 0; i < top->NumDependentResources(); ++i) top->DependentResource(static_cast<int>(i))->Slot9();
    surveyors_.pop_back();
}

void cGZPersistBinaryWriter::WriteBool(const char*, bool value) { OutputStream()->SetUint8(value); }              // @ 0x1004ded32
void cGZPersistBinaryWriter::WriteSint8(const char*, std::int8_t value) { OutputStream()->SetSint8(value); }      // @ 0x1004ded58
void cGZPersistBinaryWriter::WriteUint8(const char*, std::uint8_t value) { OutputStream()->SetUint8(value); }     // @ 0x1004ded7e
void cGZPersistBinaryWriter::WriteSint16(const char*, std::int16_t value) { OutputStream()->SetSint16(value); }   // @ 0x1004deda4
void cGZPersistBinaryWriter::WriteUint16(const char*, std::uint16_t value) { OutputStream()->SetUint16(value); }  // @ 0x1004dedca
void cGZPersistBinaryWriter::WriteSint32(const char*, std::int32_t value) { OutputStream()->SetSint32(value); }   // @ 0x1004dedf0
void cGZPersistBinaryWriter::WriteUint32(const char*, std::uint32_t value) { OutputStream()->SetUint32(value); }  // @ 0x1004dee16
void cGZPersistBinaryWriter::WriteFloat32(const char*, float value) { OutputStream()->SetFloat32(value); }        // @ 0x1004dee3c
void cGZPersistBinaryWriter::WriteFloat64(const char*, double value) { OutputStream()->SetFloat64(value); }       // @ 0x1004dee68

void cGZPersistBinaryWriter::WriteBoolArray(const char*, const bool* values, std::uint32_t count) {   // @ 0x1004dee94
    for (; count != 0; --count) OutputStream()->SetUint8(*values++);
}
void cGZPersistBinaryWriter::WriteSint8Array(const char*, const std::int8_t* values, std::uint32_t count) {   // @ 0x1004deed8
    OutputStream()->SetVoid(values, count);
}
void cGZPersistBinaryWriter::WriteUint8Array(const char*, const std::uint8_t* values, std::uint32_t count) {   // @ 0x1004def08
    OutputStream()->SetVoid(values, count);
}
void cGZPersistBinaryWriter::WriteSint16Array(const char*, const std::int16_t* values, std::uint32_t count) {   // @ 0x1004def38
    for (; count != 0; --count) OutputStream()->SetSint16(*values++);
}
void cGZPersistBinaryWriter::WriteUint16Array(const char*, const std::uint16_t* values, std::uint32_t count) {   // @ 0x1004def7c
    for (; count != 0; --count) OutputStream()->SetUint16(*values++);
}
void cGZPersistBinaryWriter::WriteSint32Array(const char*, const std::int32_t* values, std::uint32_t count) {   // @ 0x1004defc0
    for (; count != 0; --count) OutputStream()->SetSint32(*values++);
}
void cGZPersistBinaryWriter::WriteUint32Array(const char*, const std::uint32_t* values, std::uint32_t count) {   // @ 0x1004df004
    for (; count != 0; --count) OutputStream()->SetUint32(*values++);
}
void cGZPersistBinaryWriter::WriteFloat32Array(const char*, const float* values, std::uint32_t count) {   // @ 0x1004df048
    for (; count != 0; --count) OutputStream()->SetFloat32(*values++);
}
void cGZPersistBinaryWriter::WriteFloat64Array(const char*, const double* values, std::uint32_t count) {   // @ 0x1004df08e
    for (; count != 0; --count) OutputStream()->SetFloat64(*values++);
}

void cGZPersistBinaryWriter::WriteString(const char*, const cIGZString* value) { OutputStream()->SetGZStr(*value); }   // @ 0x1004df0d4
void cGZPersistBinaryWriter::WriteString(const char*, const char* value) { OutputStream()->SetRZCharStr(value); }      // @ 0x1004df0fc

void cGZPersistBinaryWriter::WriteObject(const char*, const cIGZPersistSerializable2* object) {   // @ 0x1004df124
    cIGZOStream* out = OutputStream();
    if (!object) {
        out->SetUint32(0);
        return;
    }
    out->SetUint32(object->GetTypeInfo().classId);
    object->Write(*this);
}

void cGZPersistBinaryWriter::WriteObjectRef(const char*, const cIGZPersistSerializableReferent* object) {   // @ 0x1004df1c0
    OutputStream()->SetUint8(object != nullptr);
    if (!object) return;
    cIGZPersistResource2* owner = object->GetResource();
    cIGZPersistSurveyorWriter* top = surveyors_.back().get();
    const bool elsewhere = top->Resource() != owner;
    OutputStream()->SetUint8(elsewhere);
    top = surveyors_.back().get();
    const int index = top->Resource() == owner ? top->IndexOfObjectRef(object) : top->IndexOfDependentResource(owner);
    OutputStream()->SetUint32(static_cast<std::uint32_t>(index));
}

void cGZPersistBinaryWriter::WriteObjectRef(const char* name, const cIGZUnknown* object) {   // @ 0x1004df2e8
    cRZAutoRefCount<cIGZPersistSerializableReferent> referent;
    if (object) const_cast<cIGZUnknown*>(object)->QueryInterface(kGZIID_cIGZPersistSerializableReferent, referent.AsOutParam());
    WriteObjectRef(name, referent.get());
}

void cGZPersistBinaryWriter::WriteTypeInfo(const cGZPersistTypeInfo& info) {   // @ 0x1004df368
    OutputStream()->SetGZStr(*info.name);
    OutputStream()->SetUint32(info.classId);
    OutputStream()->SetUint32(info.version);
}

cIGZOStream* cGZPersistBinaryWriter::OutputStream() { return surveyors_.back()->OutputStream(); }   // @ 0x1004df3c4
