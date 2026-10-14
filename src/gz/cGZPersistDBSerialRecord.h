// cGZPersistDBSerialRecord / cGZPersistDBRecord — base of every database record object.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100aa0ed8, 72 + 11 functions).
//
// The original is a multiple-inheritance object (record interface, DB-record interface, ostream
// view, istream view). Here the record implements cIGZPersistDBSerialRecord and cIGZPersistDBRecord
// directly and hands out small stream/audio adapter objects for the other interface ids.
//
//  * `littleEndian` records read/write host order; others byte-swap every multi-byte field.
//  * Reference counts are 16-bit in the original (AddRef returns the new count).
//  * GetFieldVariant/SetFieldVariant need cRZVariant (not recovered yet): they fail.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZPersistDBSegment.h"
#include "gz/cIGZPersistDBSerialRecord.h"

class cGZPersistDBSerialRecord : public cIGZPersistDBSerialRecord, public cIGZPersistDBRecord {
public:
    enum RecordKind : std::int32_t { kKindNone = 0, kKindPackedRead = 1, kKindRAMRead = 2, kKindWrite = 3 };

    // `segment` is AddRef'd and released by Close()/DoPostClose().
    cGZPersistDBSerialRecord(const cGZPersistResourceKey& key, cIGZPersistDBSegment* segment, bool littleEndian);
    cGZPersistDBSerialRecord(cIGZPersistDBSegment* segment, bool littleEndian);
    virtual ~cGZPersistDBSerialRecord();

    // --- cIGZUnknown (serves both interface bases) ---
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    // --- cIGZPersistDBRecord ---
    cGZPersistResourceKey GetKey() const override { return key_; }
    std::uint32_t GetAccessFlags() const override { return accessFlags_; }
    bool Close() override;
    void SetKey(const cGZPersistResourceKey& key) { key_ = key; }
    void SetAccessFlags(int flags) { accessFlags_ = static_cast<std::uint16_t>(flags); }

    // --- cIGZPersistDBSerialRecord ---
    cIGZPersistDBRecord* AsIGZPersistDBRecord() override { return static_cast<cIGZPersistDBRecord*>(this); }
    bool GetFieldUint8(std::uint8_t& v) override { return GetFieldVoid(&v, 1); }
    bool GetFieldUint8(std::uint8_t* v, std::uint32_t n) override { return GetFieldVoid(v, n); }
    bool GetFieldSint8(std::int8_t& v) override { return GetFieldVoid(&v, 1); }
    bool GetFieldSint8(std::int8_t* v, std::uint32_t n) override { return GetFieldVoid(v, n); }
    bool GetFieldUint16(std::uint16_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldUint16(std::uint16_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldSint16(std::int16_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldSint16(std::int16_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldUint32(std::uint32_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldUint32(std::uint32_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldSint32(std::int32_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldSint32(std::int32_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldUint64(std::uint64_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldUint64(std::uint64_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldSint64(std::int64_t& v) override { return GetScalars(&v, 1); }
    bool GetFieldSint64(std::int64_t* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldFloat32(float& v) override { return GetScalars(&v, 1); }
    bool GetFieldFloat32(float* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldFloat64(double& v) override { return GetScalars(&v, 1); }
    bool GetFieldFloat64(double* v, std::uint32_t n) override { return GetScalars(v, n); }
    bool GetFieldString(cIGZString& v) override;
    bool GetFieldResKey(cGZPersistResourceKey& v) override;
    bool GetFieldVariant(cIGZVariant** v) override;

    bool SetFieldUint8(std::uint8_t v) override { return SetFieldVoid(&v, 1); }
    bool SetFieldUint8(const std::uint8_t* v, std::uint32_t n) override { return SetFieldVoid(v, n); }
    bool SetFieldSint8(std::int8_t v) override { return SetFieldVoid(&v, 1); }
    bool SetFieldSint8(const std::int8_t* v, std::uint32_t n) override { return SetFieldVoid(v, n); }
    bool SetFieldUint16(std::uint16_t v) override { return SetScalars(&v, 1); }
    bool SetFieldUint16(const std::uint16_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldSint16(std::int16_t v) override { return SetScalars(&v, 1); }
    bool SetFieldSint16(const std::int16_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldUint32(std::uint32_t v) override { return SetScalars(&v, 1); }
    bool SetFieldUint32(const std::uint32_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldSint32(std::int32_t v) override { return SetScalars(&v, 1); }
    bool SetFieldSint32(const std::int32_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldUint64(std::uint64_t v) override { return SetScalars(&v, 1); }
    bool SetFieldUint64(const std::uint64_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldSint64(std::int64_t v) override { return SetScalars(&v, 1); }
    bool SetFieldSint64(const std::int64_t* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldFloat32(float v) override { return SetScalars(&v, 1); }
    bool SetFieldFloat32(const float* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldFloat64(double v) override { return SetScalars(&v, 1); }
    bool SetFieldFloat64(const double* v, std::uint32_t n) override { return SetScalars(v, n); }
    bool SetFieldString(const cIGZString& v) override;
    bool SetFieldResKey(const cGZPersistResourceKey& v) override;
    bool SetFieldVariant(const cIGZVariant* v) override;

    // Subclass hooks (shared by both interface bases through one override each).
    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override = 0;
    bool SetFieldVoid(const void* source, std::uint32_t byteCount) override = 0;
    std::uint32_t GetSize() override = 0;
    bool SetSize(std::uint32_t size) override = 0;
    std::uint32_t GetPosition() override = 0;
    bool SeekAbsolute(std::uint32_t position) override = 0;
    bool SeekRelative(std::int32_t delta) override = 0;
    virtual void DoPostClose() = 0;                       // [74]

    // Stream-style helpers of the original (slots 59..73).
    bool Skip(std::uint32_t byteCount) { return SeekRelative(static_cast<std::int32_t>(byteCount)); }
    bool GetVoid(void* destination, std::uint32_t byteCount) { return GetFieldVoid(destination, byteCount); }
    bool GetFieldVoid(void* destination, std::uint32_t count, std::uint32_t elementSize) {
        return GetFieldVoid(destination, count * elementSize);
    }
    bool SetVoid(const void* source, std::uint32_t byteCount) { return SetFieldVoid(source, byteCount); }
    bool SetFieldVoid(const void* source, std::uint32_t count, std::uint32_t elementSize) {
        return SetFieldVoid(source, count * elementSize);
    }
    bool GetError() const { return false; }               // (sic) always "no error"
    bool Flush() { return true; }
    bool SetRZCharStr(const char* s);
    bool SetGZStr(const cIGZString& s) { return SetFieldString(s); }
    bool GetRZCharStr(char*, std::uint32_t) { return false; }  // not implemented in the original
    bool GetGZStr(cIGZString& s) { return GetFieldString(s); }

    bool IsLittleEndian() const { return littleEndian_; }
    std::int32_t Kind() const { return kind_; }

protected:
    template <class T> bool GetScalars(T* values, std::uint32_t count);
    template <class T> bool SetScalars(const T* values, std::uint32_t count);

    std::uint16_t refCount_ = 0;
    std::uint16_t accessFlags_ = 0;
    cGZPersistResourceKey key_{};
    cIGZPersistDBSegment* segment_ = nullptr;   // AddRef'd
    bool littleEndian_;
    std::int32_t kind_ = kKindNone;

private:
    class IStreamView;
    class OStreamView;
    class AudioAccessView;
};

namespace persist_detail {
template <class T> T SwapBytes(T value) {
    std::uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    std::reverse(raw, raw + sizeof(T));
    std::memcpy(&value, raw, sizeof(T));
    return value;
}
}  // namespace persist_detail

template <class T> bool cGZPersistDBSerialRecord::GetScalars(T* values, std::uint32_t count) {
    bool ok = GetFieldVoid(values, count * static_cast<std::uint32_t>(sizeof(T)));
    if (!littleEndian_)
        for (std::uint32_t i = 0; i < count; ++i) values[i] = persist_detail::SwapBytes(values[i]);
    return ok;
}

template <class T> bool cGZPersistDBSerialRecord::SetScalars(const T* values, std::uint32_t count) {
    if (littleEndian_) return SetFieldVoid(values, count * static_cast<std::uint32_t>(sizeof(T)));
    std::vector<T> tmp(values, values + count);   // swapped copy
    for (T& v : tmp) v = persist_detail::SwapBytes(v);
    return SetFieldVoid(tmp.data(), count * static_cast<std::uint32_t>(sizeof(T)));
}
