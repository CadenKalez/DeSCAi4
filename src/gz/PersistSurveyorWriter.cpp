#include "gz/PersistSurveyorWriter.h"

#include "rz/cRZOStream.h"

cGZPersistSurveyorWriter::~cGZPersistSurveyorWriter() = default;   // @ 0x1004e3206

bool cGZPersistSurveyorWriter::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1004e32e6
    if (!out || (iid != kGZIID_cIGZPersistWriter && iid != 1 && iid != kGZIID_cIGZPersistSurveyorWriter)) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZPersistSurveyorWriter::Release() {   // @ 0x1004e3324
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

void cGZPersistSurveyorWriter::WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) {   // @ 0x1004e3340
    Clear();
    resource_ = const_cast<cIGZPersistResource2*>(resource);
    AddObjectRef(resource);
    // The list grows while it is walked: writing an object may reference further ones.
    for (std::size_t i = 0; i < objects_.size(); ++i) objects_[i]->Write(*this);
    stream_ = stream;
}

void cGZPersistSurveyorWriter::AddObjectRef(const cIGZPersistSerializableReferent* object) {   // @ 0x1004e340c
    if (objectIndex_.find(object) != objectIndex_.end()) return;
    objects_.emplace_back(const_cast<cIGZPersistSerializableReferent*>(object));
    objectIndex_[object] = static_cast<int>(objects_.size()) - 1;
}

void cGZPersistSurveyorWriter::AddDepResource(cIGZPersistResource2* resource) {   // @ 0x1004e35ce
    if (depIndex_.find(resource) != depIndex_.end()) return;
    deps_.emplace_back(resource);
    depIndex_[resource] = static_cast<int>(deps_.size()) - 1;
}

void cGZPersistSurveyorWriter::WriteObject(const char*, const cIGZPersistSerializable2* object) {   // @ 0x1004e356c
    if (object) object->Write(*this);
}

void cGZPersistSurveyorWriter::WriteObjectRef(const char*, const cIGZPersistSerializableReferent* object) {   // @ 0x1004e358a
    if (!object) return;
    cIGZPersistResource2* owner = object->GetResource();
    if (owner != resource_.get()) {
        AddDepResource(owner);
        return;
    }
    AddObjectRef(object);
}

void cGZPersistSurveyorWriter::WriteObjectRef(const char* name, const cIGZUnknown* object) {   // @ 0x1004e36ba
    cRZAutoRefCount<cIGZPersistSerializableReferent> referent;
    if (object) const_cast<cIGZUnknown*>(object)->QueryInterface(kGZIID_cIGZPersistSerializableReferent, referent.AsOutParam());
    WriteObjectRef(name, referent.get());
}

cIGZOStream* cGZPersistSurveyorWriter::OutputStream() {   // @ 0x1004e3740
    if (stream_) return stream_.get();
    static cRZAutoRefCount<cRZNullOStream> nullStream(new cRZNullOStream());
    return nullStream.get();
}

void cGZPersistSurveyorWriter::Clear() {   // @ 0x1004e37fc
    resource_ = nullptr;
    objects_.clear();
    objectIndex_.clear();
    deps_.clear();
    depIndex_.clear();
}

int cGZPersistSurveyorWriter::IndexOfObjectRef(const cIGZPersistSerializableReferent* object) const {   // @ 0x1004e38fa
    auto it = objectIndex_.find(object);
    return it == objectIndex_.end() ? -1 : it->second;
}

int cGZPersistSurveyorWriter::IndexOfDependentResource(const cIGZPersistResource2* resource) const {   // @ 0x1004e3a32
    auto it = depIndex_.find(resource);
    return it == depIndex_.end() ? -1 : it->second;
}
