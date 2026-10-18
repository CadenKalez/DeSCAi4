// cGZPersistBinaryReader (32 functions + the cResourceInfo constructor, 0x1004dd962..0x1004e1146): reads what the binary
// writer wrote. ReadRoot registers the resource with the resource manager (service 0x056b906e) unless it already is, loads the
// dependent resources named by the keys at the front of the stream (a key with type 0 is a null dependent), creates one object
// per class id through the COM factory (the root resource is object 0), lets every object read itself, and unregisters the
// resource again if it registered it. A frame (cResourceInfo: the dependent resources, the objects and the input stream)
// stays on a stack while the objects read, so object references resolve against the innermost load.
// Layout: +8 refcount, +0x10 the frame stack.
// Quirks kept: an object reference whose "other resource" flag is set resolves to the dependent resource itself, not to an
// object inside it; failures to load a dependent resource are only formatted into a message, and the slot stays null.
#pragma once
#include <deque>
#include <vector>

#include "gz/PersistStreams.h"
#include "gz/cIGZPersistResourceManager.h"

class cGZPersistBinaryReader : public cIGZPersistReader {
public:
    struct cResourceInfo {   // @ 0x1004e1146
        cResourceInfo() = default;
        explicit cResourceInfo(cIGZIStream* in) : stream(in) {}
        std::vector<cRZAutoRefCount<cIGZPersistResource2>> dependents;               // +0
        std::vector<cRZAutoRefCount<cIGZPersistSerializableReferent>> objects;       // +0x18
        cRZAutoRefCount<cIGZIStream> stream;                                         // +0x30
    };

    cGZPersistBinaryReader() = default;
    virtual ~cGZPersistBinaryReader();

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1004dd9dc
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1004dda04
    std::uint32_t Release() override;                              // @ 0x1004dda12

    void ReadRoot(cIGZPersistResource2* resource, cIGZIStream* stream) override;   // @ 0x1004dda2e
    bool ReadBool(const char* name) override;
    std::int8_t ReadSint8(const char* name) override;
    std::uint8_t ReadUint8(const char* name) override;
    std::int16_t ReadSint16(const char* name) override;
    std::uint16_t ReadUint16(const char* name) override;
    std::int32_t ReadSint32(const char* name) override;
    std::uint32_t ReadUint32(const char* name) override;
    float ReadFloat32(const char* name) override;
    double ReadFloat64(const char* name) override;
    void ReadBoolArray(const char* name, bool* values, std::uint32_t count) override;
    void ReadSint8Array(const char* name, std::int8_t* values, std::uint32_t count) override;
    void ReadUint8Array(const char* name, std::uint8_t* values, std::uint32_t count) override;
    void ReadSint16Array(const char* name, std::int16_t* values, std::uint32_t count) override;
    void ReadUint16Array(const char* name, std::uint16_t* values, std::uint32_t count) override;
    void ReadSint32Array(const char* name, std::int32_t* values, std::uint32_t count) override;
    void ReadUint32Array(const char* name, std::uint32_t* values, std::uint32_t count) override;
    void ReadFloat32Array(const char* name, float* values, std::uint32_t count) override;
    void ReadFloat64Array(const char* name, double* values, std::uint32_t count) override;
    void ReadString(const char* name, cIGZString* value) override;
    void SkipBytes(const char* name, std::uint32_t count) override;
    void ReadObject(const char* name, std::uint32_t iid, void** out) override;        // @ 0x1004de590
    void ReadObjectRef(const char* name, std::uint32_t iid, void** out) override;     // @ 0x1004de64c
    cGZPersistTypeInfo ReadTypeInfo() override;                                       // @ 0x1004de740
    cIGZIStream* InputStream() override;                                              // @ 0x1004de80a

private:
    std::uint32_t refCount_ = 0;                  // +8
    std::deque<cResourceInfo> frames_;            // +0x10
};
