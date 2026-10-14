// cIGZPersistDBSerialRecord — typed field access over a database record. Slot layout (3..56)
// recovered from the primary vtable of cGZDBReadRecordRAM (0x100a9b760); the record is the
// object QueryInterface returns for iid 0xc56b8f08.
//
// Byte order: records created with `littleEndian == true` store fields in host order (no swap);
// otherwise multi-byte fields are byte-swapped on every read/write.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZPersistRecord.h"
#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"

class cIGZPersistDBSerialRecord : public cIGZUnknown {
public:
    // [3] the cIGZPersistDBRecord view of the same object (not AddRef'd).
    virtual cIGZPersistDBRecord* AsIGZPersistDBRecord() = 0;
    // [4..27] reads. The array forms read `count` elements.
    virtual bool GetFieldUint8(std::uint8_t& v) = 0;
    virtual bool GetFieldUint8(std::uint8_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldSint8(std::int8_t& v) = 0;
    virtual bool GetFieldSint8(std::int8_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldUint16(std::uint16_t& v) = 0;
    virtual bool GetFieldUint16(std::uint16_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldSint16(std::int16_t& v) = 0;
    virtual bool GetFieldSint16(std::int16_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldUint32(std::uint32_t& v) = 0;
    virtual bool GetFieldUint32(std::uint32_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldSint32(std::int32_t& v) = 0;
    virtual bool GetFieldSint32(std::int32_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldUint64(std::uint64_t& v) = 0;
    virtual bool GetFieldUint64(std::uint64_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldSint64(std::int64_t& v) = 0;
    virtual bool GetFieldSint64(std::int64_t* v, std::uint32_t count) = 0;
    virtual bool GetFieldFloat32(float& v) = 0;
    virtual bool GetFieldFloat32(float* v, std::uint32_t count) = 0;
    virtual bool GetFieldFloat64(double& v) = 0;
    virtual bool GetFieldFloat64(double* v, std::uint32_t count) = 0;
    virtual bool GetFieldString(cIGZString& v) = 0;
    virtual bool GetFieldResKey(cGZPersistResourceKey& v) = 0;
    virtual bool GetFieldVariant(cIGZVariant** v) = 0;
    virtual bool GetFieldVoid(void* destination, std::uint32_t byteCount) = 0;     // [27]
    // [28..51] writes (same order as the reads).
    virtual bool SetFieldUint8(std::uint8_t v) = 0;
    virtual bool SetFieldUint8(const std::uint8_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldSint8(std::int8_t v) = 0;
    virtual bool SetFieldSint8(const std::int8_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldUint16(std::uint16_t v) = 0;
    virtual bool SetFieldUint16(const std::uint16_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldSint16(std::int16_t v) = 0;
    virtual bool SetFieldSint16(const std::int16_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldUint32(std::uint32_t v) = 0;
    virtual bool SetFieldUint32(const std::uint32_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldSint32(std::int32_t v) = 0;
    virtual bool SetFieldSint32(const std::int32_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldUint64(std::uint64_t v) = 0;
    virtual bool SetFieldUint64(const std::uint64_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldSint64(std::int64_t v) = 0;
    virtual bool SetFieldSint64(const std::int64_t* v, std::uint32_t count) = 0;
    virtual bool SetFieldFloat32(float v) = 0;
    virtual bool SetFieldFloat32(const float* v, std::uint32_t count) = 0;
    virtual bool SetFieldFloat64(double v) = 0;
    virtual bool SetFieldFloat64(const double* v, std::uint32_t count) = 0;
    virtual bool SetFieldString(const cIGZString& v) = 0;
    virtual bool SetFieldResKey(const cGZPersistResourceKey& v) = 0;
    virtual bool SetFieldVariant(const cIGZVariant* v) = 0;
    virtual bool SetFieldVoid(const void* source, std::uint32_t byteCount) = 0;   // [51]
    // [52..56]
    virtual std::uint32_t GetSize() = 0;
    virtual bool SetSize(std::uint32_t size) = 0;
    virtual std::uint32_t GetPosition() = 0;
    virtual bool SeekAbsolute(std::uint32_t position) = 0;
    virtual bool SeekRelative(std::int32_t delta) = 0;
};
constexpr std::uint32_t kGZIID_cIGZPersistDBSerialRecord = 0xc56b8f08;

// A record whose every operation fails. Base class for test doubles.
class cIGZPersistDBSerialRecordStub : public cIGZPersistDBSerialRecord {
public:
    cIGZPersistDBRecord* AsIGZPersistDBRecord() override { return nullptr; }
#define STUB_GET(T, N) \
    bool GetField##N(T& v) override { (void)v; return false; } \
    bool GetField##N(T* v, std::uint32_t) override { (void)v; return false; }
#define STUB_SET(T, N) \
    bool SetField##N(T v) override { (void)v; return false; } \
    bool SetField##N(const T* v, std::uint32_t) override { (void)v; return false; }
    STUB_GET(std::uint8_t, Uint8) STUB_GET(std::int8_t, Sint8) STUB_GET(std::uint16_t, Uint16)
    STUB_GET(std::int16_t, Sint16) STUB_GET(std::uint32_t, Uint32) STUB_GET(std::int32_t, Sint32)
    STUB_GET(std::uint64_t, Uint64) STUB_GET(std::int64_t, Sint64) STUB_GET(float, Float32)
    STUB_GET(double, Float64)
    STUB_SET(std::uint8_t, Uint8) STUB_SET(std::int8_t, Sint8) STUB_SET(std::uint16_t, Uint16)
    STUB_SET(std::int16_t, Sint16) STUB_SET(std::uint32_t, Uint32) STUB_SET(std::int32_t, Sint32)
    STUB_SET(std::uint64_t, Uint64) STUB_SET(std::int64_t, Sint64) STUB_SET(float, Float32)
    STUB_SET(double, Float64)
#undef STUB_GET
#undef STUB_SET
    bool GetFieldString(cIGZString&) override { return false; }
    bool GetFieldResKey(cGZPersistResourceKey&) override { return false; }
    bool GetFieldVariant(cIGZVariant**) override { return false; }
    bool GetFieldVoid(void*, std::uint32_t) override { return false; }
    bool SetFieldString(const cIGZString&) override { return false; }
    bool SetFieldResKey(const cGZPersistResourceKey&) override { return false; }
    bool SetFieldVariant(const cIGZVariant*) override { return false; }
    bool SetFieldVoid(const void*, std::uint32_t) override { return false; }
    std::uint32_t GetSize() override { return 0; }
    bool SetSize(std::uint32_t) override { return false; }
    std::uint32_t GetPosition() override { return 0; }
    bool SeekAbsolute(std::uint32_t) override { return false; }
    bool SeekRelative(std::int32_t) override { return false; }
};

