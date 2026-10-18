// cGZPersistBinaryWriter (33 functions + thunks at 0x1004de84c..0x1004df3c4): writes a resource and its objects to a binary
// stream. WriteRoot first surveys the graph (a surveyor from the COM factory sits on a stack for the duration of the save, so
// nested saves work), then writes: the dependent resources' keys (group, instance, type), the object count, every object's
// class id, every object's data, and flushes. Scalars go straight to the current output stream (arrays element by element,
// except the byte arrays, which are copied raw); an object reference is a "present" flag, a "lives in another resource"
// flag and an index (into the surveyor's objects, or into its dependent resources).
// Layout: +8 refcount, +0x10 the surveyor stack.
#pragma once
#include <deque>

#include "gz/PersistStreams.h"

class cGZPersistBinaryWriter : public cIGZPersistWriter {
public:
    cGZPersistBinaryWriter() = default;
    virtual ~cGZPersistBinaryWriter();

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1004de8c6
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1004de8ee
    std::uint32_t Release() override;                              // @ 0x1004de8fc

    void WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) override;   // @ 0x1004de918
    void WriteBool(const char* name, bool value) override;
    void WriteSint8(const char* name, std::int8_t value) override;
    void WriteUint8(const char* name, std::uint8_t value) override;
    void WriteSint16(const char* name, std::int16_t value) override;
    void WriteUint16(const char* name, std::uint16_t value) override;
    void WriteSint32(const char* name, std::int32_t value) override;
    void WriteUint32(const char* name, std::uint32_t value) override;
    void WriteFloat32(const char* name, float value) override;
    void WriteFloat64(const char* name, double value) override;
    void WriteBoolArray(const char* name, const bool* values, std::uint32_t count) override;
    void WriteSint8Array(const char* name, const std::int8_t* values, std::uint32_t count) override;
    void WriteUint8Array(const char* name, const std::uint8_t* values, std::uint32_t count) override;
    void WriteSint16Array(const char* name, const std::int16_t* values, std::uint32_t count) override;
    void WriteUint16Array(const char* name, const std::uint16_t* values, std::uint32_t count) override;
    void WriteSint32Array(const char* name, const std::int32_t* values, std::uint32_t count) override;
    void WriteUint32Array(const char* name, const std::uint32_t* values, std::uint32_t count) override;
    void WriteFloat32Array(const char* name, const float* values, std::uint32_t count) override;
    void WriteFloat64Array(const char* name, const double* values, std::uint32_t count) override;
    void WriteString(const char* name, const cIGZString* value) override;
    void WriteString(const char* name, const char* value) override;
    void WriteObject(const char* name, const cIGZPersistSerializable2* object) override;           // @ 0x1004df124
    void WriteObjectRef(const char* name, const cIGZPersistSerializableReferent* object) override;   // @ 0x1004df1c0
    void WriteObjectRef(const char* name, const cIGZUnknown* object) override;                     // @ 0x1004df2e8
    void WriteTypeInfo(const cGZPersistTypeInfo& info) override;                                   // @ 0x1004df368
    cIGZOStream* OutputStream() override;                                                          // @ 0x1004df3c4

private:
    std::uint32_t refCount_ = 0;                                                  // +8
    std::deque<cRZAutoRefCount<cIGZPersistSurveyorWriter>> surveyors_;            // +0x10
};
