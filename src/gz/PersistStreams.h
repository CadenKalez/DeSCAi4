// The object-graph persistence layer: writers and readers that save / load a resource together with the objects it owns and
// the resources it depends on. The interfaces here are reconstructed from how cGZPersistBinaryWriter / Reader,
// cGZPersistASCIIWriter and cGZPersistSurveyorWriter call them (the binary has no other implementers):
//   cIGZPersistSerializable2 (iid 0x5b0aaa74): [3] Read(reader), [4] GetTypeInfo, [5] Write(writer)
//   cIGZPersistSerializableReferent (iid 0xbb11a53e): a serializable object that lives in a resource ([6] GetResource)
//   cIGZPersistResource2: a referent that is itself a resource: [7] / [11] the cIGZPersistResource view (it knows the key),
//     [10] the index of an object inside it; [8], [9] are not modelled ([9] is called once on every dependent resource
//     after a save).
//   cIGZPersistWriter (iid 0x1b0aab0e) / cIGZPersistReader (iid 0xfb0c4344): named scalar / array / string / object
//   accessors; cIGZPersistSurveyorWriter (iid 0x9b194c66, class 0xbb194c78) is the writer that only collects which objects
//   and dependent resources a save will touch.
// On disk a save is: the dependent-resource keys (group, instance, type each), the object count and the type id of every
// object, then the objects' own data in order.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZString.h"
#include "rz/cRZAutoRefCount.h"

constexpr std::uint32_t kGZIID_cIGZPersistWriter = 0x1b0aab0e, kGZIID_cIGZPersistSurveyorWriter = 0x9b194c66,
                        kGZIID_cIGZPersistReader = 0xfb0c4344, kGZIID_cIGZPersistSerializable2 = 0x5b0aaa74,
                        kGZIID_cIGZPersistSerializableReferent = 0xbb11a53e, kGZIID_cIGZPersistResource2Loaded = 0x9b11bf70;
constexpr std::uint32_t kGZCLSID_cGZPersistSurveyorWriter = 0xbb194c78;

// The type name, class id and version of a serializable object (the name is a counted reference).
struct cGZPersistTypeInfo {
    cRZAutoRefCount<cIGZString> name;   // +0
    std::uint32_t classId = 0;          // +8
    std::uint32_t version = 0;          // +0xc
};

class cIGZPersistReader;
class cIGZPersistWriter;

class cIGZPersistSerializable2 : public cIGZUnknown {
public:
    virtual void Read(cIGZPersistReader& reader) = 0;               // [3]
    virtual cGZPersistTypeInfo GetTypeInfo() const = 0;             // [4]
    virtual void Write(cIGZPersistWriter& writer) const = 0;        // [5]
};

class cIGZPersistResource2;

class cIGZPersistSerializableReferent : public cIGZPersistSerializable2 {
public:
    virtual cIGZPersistResource2* GetResource() const = 0;          // [6]
};

class cIGZPersistResource2 : public cIGZPersistSerializableReferent {
public:
    virtual cIGZPersistResource* AsResource() = 0;                                          // [7]
    virtual void Slot8() {}                                                                 // [8] (not modelled)
    virtual void Slot9() {}                                                                 // [9] called on dependent resources after a save
    virtual int IndexOfObject(const cIGZPersistSerializableReferent* object) const = 0;     // [10]
    virtual const cIGZPersistResource* AsResource() const = 0;                              // [11]
};

class cIGZPersistWriter : public cIGZUnknown {
public:
    virtual void WriteRoot(const cIGZPersistResource2* resource, cIGZOStream* stream) = 0;                    // [3]
    virtual void WriteBool(const char* name, bool value) = 0;                                                 // [4]
    virtual void WriteSint8(const char* name, std::int8_t value) = 0;                                         // [5]
    virtual void WriteUint8(const char* name, std::uint8_t value) = 0;                                        // [6]
    virtual void WriteSint16(const char* name, std::int16_t value) = 0;                                       // [7]
    virtual void WriteUint16(const char* name, std::uint16_t value) = 0;                                      // [8]
    virtual void WriteSint32(const char* name, std::int32_t value) = 0;                                       // [9]
    virtual void WriteUint32(const char* name, std::uint32_t value) = 0;                                      // [10]
    virtual void WriteFloat32(const char* name, float value) = 0;                                             // [11]
    virtual void WriteFloat64(const char* name, double value) = 0;                                            // [12]
    virtual void WriteBoolArray(const char* name, const bool* values, std::uint32_t count) = 0;               // [13]
    virtual void WriteSint8Array(const char* name, const std::int8_t* values, std::uint32_t count) = 0;       // [14]
    virtual void WriteUint8Array(const char* name, const std::uint8_t* values, std::uint32_t count) = 0;      // [15]
    virtual void WriteSint16Array(const char* name, const std::int16_t* values, std::uint32_t count) = 0;     // [16]
    virtual void WriteUint16Array(const char* name, const std::uint16_t* values, std::uint32_t count) = 0;    // [17]
    virtual void WriteSint32Array(const char* name, const std::int32_t* values, std::uint32_t count) = 0;     // [18]
    virtual void WriteUint32Array(const char* name, const std::uint32_t* values, std::uint32_t count) = 0;    // [19]
    virtual void WriteFloat32Array(const char* name, const float* values, std::uint32_t count) = 0;           // [20]
    virtual void WriteFloat64Array(const char* name, const double* values, std::uint32_t count) = 0;          // [21]
    virtual void WriteString(const char* name, const cIGZString* value) = 0;                                  // [22]
    virtual void WriteString(const char* name, const char* value) = 0;                                        // [23]
    virtual void WriteObject(const char* name, const cIGZPersistSerializable2* object) = 0;                   // [24]
    virtual void WriteObjectRef(const char* name, const cIGZPersistSerializableReferent* object) = 0;         // [25]
    virtual void WriteObjectRef(const char* name, const cIGZUnknown* object) = 0;                             // [26]
    virtual void WriteTypeInfo(const cGZPersistTypeInfo& info) = 0;                                           // [27]
    virtual cIGZOStream* OutputStream() = 0;                                                                  // [28]
};

class cIGZPersistSurveyorWriter : public cIGZPersistWriter {
public:
    virtual void Clear() = 0;                                                                                 // [29]
    virtual cIGZPersistResource2* Resource() const = 0;                                                       // [30]
    virtual std::uint32_t NumObjectRefs() const = 0;                                                          // [31]
    virtual cIGZPersistSerializableReferent* ObjectRef(int index) const = 0;                                  // [32]
    virtual int IndexOfObjectRef(const cIGZPersistSerializableReferent* object) const = 0;                    // [33]
    virtual std::uint32_t NumDependentResources() const = 0;                                                  // [34]
    virtual cIGZPersistResource2* DependentResource(int index) const = 0;                                     // [35]
    virtual int IndexOfDependentResource(const cIGZPersistResource2* resource) const = 0;                     // [36]
};

class cIGZPersistReader : public cIGZUnknown {
public:
    virtual void ReadRoot(cIGZPersistResource2* resource, cIGZIStream* stream) = 0;                           // [3]
    virtual bool ReadBool(const char* name) = 0;                                                              // [4]
    virtual std::int8_t ReadSint8(const char* name) = 0;                                                      // [5]
    virtual std::uint8_t ReadUint8(const char* name) = 0;                                                     // [6]
    virtual std::int16_t ReadSint16(const char* name) = 0;                                                    // [7]
    virtual std::uint16_t ReadUint16(const char* name) = 0;                                                   // [8]
    virtual std::int32_t ReadSint32(const char* name) = 0;                                                    // [9]
    virtual std::uint32_t ReadUint32(const char* name) = 0;                                                   // [10]
    virtual float ReadFloat32(const char* name) = 0;                                                          // [11]
    virtual double ReadFloat64(const char* name) = 0;                                                         // [12]
    virtual void ReadBoolArray(const char* name, bool* values, std::uint32_t count) = 0;                      // [13]
    virtual void ReadSint8Array(const char* name, std::int8_t* values, std::uint32_t count) = 0;              // [14]
    virtual void ReadUint8Array(const char* name, std::uint8_t* values, std::uint32_t count) = 0;             // [15]
    virtual void ReadSint16Array(const char* name, std::int16_t* values, std::uint32_t count) = 0;            // [16]
    virtual void ReadUint16Array(const char* name, std::uint16_t* values, std::uint32_t count) = 0;           // [17]
    virtual void ReadSint32Array(const char* name, std::int32_t* values, std::uint32_t count) = 0;            // [18]
    virtual void ReadUint32Array(const char* name, std::uint32_t* values, std::uint32_t count) = 0;           // [19]
    virtual void ReadFloat32Array(const char* name, float* values, std::uint32_t count) = 0;                  // [20]
    virtual void ReadFloat64Array(const char* name, double* values, std::uint32_t count) = 0;                 // [21]
    virtual void ReadString(const char* name, cIGZString* value) = 0;                                         // [22]
    virtual void SkipBytes(const char* name, std::uint32_t count) = 0;                                        // [23]
    virtual void ReadObject(const char* name, std::uint32_t iid, void** out) = 0;                             // [24]
    virtual void ReadObjectRef(const char* name, std::uint32_t iid, void** out) = 0;                          // [25]
    virtual cGZPersistTypeInfo ReadTypeInfo() = 0;                                                            // [26]
    virtual cIGZIStream* InputStream() = 0;                                                                   // [27]
};

// What the persistence layer needs from the rest of the game: the COM class factory (GZCOM()->GetCOMObject, creates
// surveyors and the objects named by a type id) and a sink for the diagnostics the reader formats for a missing dependent
// resource (the original builds the text and drops it).
class IPersistHost {
public:
    virtual ~IPersistHost() = default;
    virtual bool GetCOMObject(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
    virtual void Diagnostic(const char* /*text*/) {}
};
IPersistHost* PersistHost();
void SetPersistHost(IPersistHost* host);
