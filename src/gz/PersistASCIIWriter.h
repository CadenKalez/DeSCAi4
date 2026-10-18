// cGZPersistASCIIWriter (34 functions + thunks at 0x1004df3fa..0x1004e0f38): the human-readable twin of the binary writer. It
// surveys the graph the same way, then prints a report through the output stream (a text stream that renders the numbers the
// Set* calls receive): "Resource <type> G:0x.. I:0x.. T:0x..", the dependent resources and the objects, then each object in
// braces, every value as "      <type> <name> = <value>". Layout: +8 refcount, +0x10 the surveyor stack.
#pragma once
#include <deque>

#include "gz/PersistStreams.h"

class cGZPersistASCIIWriter : public cIGZPersistWriter {
public:
    cGZPersistASCIIWriter() = default;
    virtual ~cGZPersistASCIIWriter();

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1004df474
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1004df49c
    std::uint32_t Release() override;                              // @ 0x1004df4aa

    void WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) override;   // @ 0x1004df4c6
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
    void WriteObject(const char* name, const cIGZPersistSerializable2* object) override;           // @ 0x1004e08ca
    void WriteObjectRef(const char* name, const cIGZPersistSerializableReferent* object) override;   // @ 0x1004e0a60
    void WriteObjectRef(const char* name, const cIGZUnknown* object) override;                     // @ 0x1004e0dba
    void WriteTypeInfo(const cGZPersistTypeInfo& info) override;                                   // @ 0x1004e0e3a
    cIGZOStream* OutputStream() override;                                                          // @ 0x1004e0f38

    void WriteKey(const cGZPersistResourceKey& key);                                               // @ 0x1004dfa9e

private:
    void Text(const char* text) { OutputStream()->SetRZCharStr(text); }

    std::uint32_t refCount_ = 0;                                                  // +8
    std::deque<cRZAutoRefCount<cIGZPersistSurveyorWriter>> surveyors_;            // +0x10
};
