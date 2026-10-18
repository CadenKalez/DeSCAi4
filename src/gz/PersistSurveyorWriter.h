// cGZPersistSurveyorWriter (43 functions + thunks at 0x1004e31ac..0x1004e3a32, class id 0xbb194c78): the writer that writes
// nothing. A save first runs the objects through it so the real writer knows which objects (the root resource and everything
// reachable through object references inside it) and which dependent resources (referenced objects living in other
// resources) take part, and under which index each is stored. Layout: +8 refcount, +0x10 output stream, +0x18 root resource,
// +0x20 objects (a reference each) with +0x38 object -> index, +0x50 dependent resources with +0x68 resource -> index.
// Without a stream it hands out a shared null stream.
#pragma once
#include <map>
#include <vector>

#include "gz/PersistStreams.h"

class cGZPersistSurveyorWriter : public cIGZPersistSurveyorWriter {
public:
    cGZPersistSurveyorWriter() = default;
    virtual ~cGZPersistSurveyorWriter();

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1004e32e6
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1004e3316
    std::uint32_t Release() override;                              // @ 0x1004e3324

    void WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) override;   // @ 0x1004e3340
    void WriteBool(const char*, bool) override {}
    void WriteSint8(const char*, std::int8_t) override {}
    void WriteUint8(const char*, std::uint8_t) override {}
    void WriteSint16(const char*, std::int16_t) override {}
    void WriteUint16(const char*, std::uint16_t) override {}
    void WriteSint32(const char*, std::int32_t) override {}
    void WriteUint32(const char*, std::uint32_t) override {}
    void WriteFloat32(const char*, float) override {}
    void WriteFloat64(const char*, double) override {}
    void WriteBoolArray(const char*, const bool*, std::uint32_t) override {}
    void WriteSint8Array(const char*, const std::int8_t*, std::uint32_t) override {}
    void WriteUint8Array(const char*, const std::uint8_t*, std::uint32_t) override {}
    void WriteSint16Array(const char*, const std::int16_t*, std::uint32_t) override {}
    void WriteUint16Array(const char*, const std::uint16_t*, std::uint32_t) override {}
    void WriteSint32Array(const char*, const std::int32_t*, std::uint32_t) override {}
    void WriteUint32Array(const char*, const std::uint32_t*, std::uint32_t) override {}
    void WriteFloat32Array(const char*, const float*, std::uint32_t) override {}
    void WriteFloat64Array(const char*, const double*, std::uint32_t) override {}
    void WriteString(const char*, const cIGZString*) override {}
    void WriteString(const char*, const char*) override {}
    void WriteObject(const char* name, const cIGZPersistSerializable2* object) override;           // @ 0x1004e356c
    void WriteObjectRef(const char* name, const cIGZPersistSerializableReferent* object) override;   // @ 0x1004e358a
    void WriteObjectRef(const char* name, const cIGZUnknown* object) override;                     // @ 0x1004e36ba
    void WriteTypeInfo(const cGZPersistTypeInfo&) override {}
    cIGZOStream* OutputStream() override;                                                          // @ 0x1004e3740

    void Clear() override;                                                                         // @ 0x1004e37fc
    cIGZPersistResource2* Resource() const override { return resource_; }
    std::uint32_t NumObjectRefs() const override { return static_cast<std::uint32_t>(objects_.size()); }
    cIGZPersistSerializableReferent* ObjectRef(int index) const override { return objects_[static_cast<std::size_t>(index)].get(); }
    int IndexOfObjectRef(const cIGZPersistSerializableReferent* object) const override;
    std::uint32_t NumDependentResources() const override { return static_cast<std::uint32_t>(deps_.size()); }
    cIGZPersistResource2* DependentResource(int index) const override { return deps_[static_cast<std::size_t>(index)].get(); }
    int IndexOfDependentResource(const cIGZPersistResource2* resource) const override;

    void AddObjectRef(const cIGZPersistSerializableReferent* object);   // @ 0x1004e340c
    void AddDepResource(cIGZPersistResource2* resource);                // @ 0x1004e35ce

private:
    std::uint32_t refCount_ = 0;                                                  // +8
    cRZAutoRefCount<cIGZOStream> stream_;                                         // +0x10
    cRZAutoRefCount<cIGZPersistResource2> resource_;                              // +0x18
    std::vector<cRZAutoRefCount<cIGZPersistSerializableReferent>> objects_;       // +0x20
    std::map<const cIGZPersistSerializableReferent*, int> objectIndex_;           // +0x38
    std::vector<cRZAutoRefCount<cIGZPersistResource2>> deps_;                     // +0x50
    std::map<const cIGZPersistResource2*, int> depIndex_;                         // +0x68
};
