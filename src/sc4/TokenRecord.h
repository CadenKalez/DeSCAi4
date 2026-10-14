// "SL token" records — the game's self-describing key/value container used by data resources.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1: cSLTokenRecord (vtable 0x100ab94d0),
// cSLTokenField<T> (12 instantiations), cSLTokenSerialRecordIO / _Text (0x100b0ccf8 / 0x100ab9690),
// cSLTokenResourceFactory (0x100ab9610).
//
// Stream format: a u32 cookie ('TKB1' = 0x31424b54 binary, 'TKT1' = 0x31544b54 text) followed by
// tokens. Each token is a def {u32 id, u16 type, u16 count} and `count` values of the type; the list
// ends with the def {1, u32, 1} (value 0) named "TK_TKR_End". Token ids are unique per record; the
// text form is `\r\nNAME[ id, type, count ]= v, v, ...` (names exist only in text).
//   type: 1 u8, 2 u16, 3 u32, 4 i32, 5 float, 6 i64, 7 string, 8 resource key, 9 i8, 10 i16,
//         11 u64, 12 double.
//
// Quirks kept: only the u8/u16/u32/i32/i64/float/string/key field instances answer Get/Set (an
// i8, i16, u64 or double token reads/writes back false through the record API, though its data is
// serialised); the binary IO fails a field that would end exactly at the record's end (it tests
// `position + size < limit`); the text IO reads u64 values with the binary reader; text output of
// 64-bit integers uses "%I64u"/"%I64d" which this platform prints literally; CreateField fails when
// an identical token already exists; a token with count 0 is never created.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

#include "gz/cGZPersistResourceManager.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "gz/cIGZPersistResource.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"

namespace sc4 {

struct cGZPersistTokenDef {
    std::uint32_t id = 0;
    std::uint16_t type = 0;
    std::uint16_t count = 0;
    friend bool operator==(const cGZPersistTokenDef&, const cGZPersistTokenDef&) = default;
};

enum TokenType : std::uint16_t {
    kTokenUint8 = 1, kTokenUint16 = 2, kTokenUint32 = 3, kTokenSint32 = 4, kTokenFloat32 = 5,
    kTokenSint64 = 6, kTokenString = 7, kTokenResKey = 8, kTokenSint8 = 9, kTokenSint16 = 10,
    kTokenUint64 = 11, kTokenFloat64 = 12,
};

constexpr std::uint32_t kTokenCookieBinary = 0x31424b54;   // 'TKB1'
constexpr std::uint32_t kTokenCookieText = 0x31544b54;     // 'TKT1'
constexpr std::uint32_t kTokenEndId = 1;
constexpr std::uint32_t kGZIID_cSLTokenRecord = 0xa2ace662;
constexpr std::uint32_t kGZIID_cISLTokenResource = 0xa2ace668;

// ---- IO ---------------------------------------------------------------------------------------
class cSLTokenSerialRecordIO {
public:
    // `limit` is the record's size; reading starts after the 4-byte cookie.
    cSLTokenSerialRecordIO(cIGZPersistDBSerialRecord* record, std::uint32_t limit) : record_(record), limit_(limit) {}
    virtual ~cSLTokenSerialRecordIO() = default;

    virtual bool ReadTokenDef(cGZPersistTokenDef& def);                    // [2]
    virtual bool Read(std::uint8_t& v);                                    // [3]
    virtual bool Read(std::int8_t& v);                                     // [4]
    virtual bool Read(std::uint16_t& v);                                   // [5]
    virtual bool Read(std::int16_t& v);                                    // [6]
    virtual bool Read(std::uint32_t& v);                                   // [7]
    virtual bool Read(std::int32_t& v);                                    // [8]
    virtual bool Read(std::uint64_t& v);                                   // [9]
    virtual bool Read(std::int64_t& v);                                    // [10]
    virtual bool Read(float& v);                                           // [11]
    virtual bool Read(double& v);                                          // [12]
    virtual bool Read(cRZString& v);                                       // [13]
    virtual bool Read(cGZPersistResourceKey& v);                           // [14]
    virtual bool WriteTokenDef(cRZString& name, cGZPersistTokenDef& def);  // [15]
    virtual bool Write(std::uint8_t& v);                                   // [16]
    virtual bool Write(std::int8_t& v);                                    // [17]
    virtual bool Write(std::uint16_t& v);                                  // [18]
    virtual bool Write(std::int16_t& v);                                   // [19]
    virtual bool Write(std::uint32_t& v);                                  // [20]
    virtual bool Write(std::int32_t& v);                                   // [21]
    virtual bool Write(std::uint64_t& v);                                  // [22]
    virtual bool Write(std::int64_t& v);                                   // [23]
    virtual bool Write(float& v);                                          // [24]
    virtual bool Write(double& v);                                         // [25]
    virtual bool Write(cRZString& v);                                      // [26]
    virtual bool Write(cGZPersistResourceKey& v);                          // [27]

protected:
    bool Advance(std::uint32_t bytes) {   // the shared "does it still fit" test
        position_ += bytes;
        return position_ < limit_;
    }
    cIGZPersistDBSerialRecord* record_;   // +8
    std::uint32_t position_ = 4;          // +0x10
    std::uint32_t limit_;                 // +0x14
};

class cSLTokenSerialRecordIO_Text final : public cSLTokenSerialRecordIO {
public:
    using cSLTokenSerialRecordIO::cSLTokenSerialRecordIO;
    using cSLTokenSerialRecordIO::Read;
    using cSLTokenSerialRecordIO::Write;

    bool ReadTokenDef(cGZPersistTokenDef& def) override;
    bool Read(std::uint8_t& v) override;
    bool Read(std::int8_t& v) override;
    bool Read(std::uint16_t& v) override;
    bool Read(std::int16_t& v) override;
    bool Read(std::uint32_t& v) override { return ReadUnsigned(v, ','); }
    bool Read(std::int32_t& v) override { return ReadSigned(v, ','); }
    bool Read(std::int64_t& v) override;
    bool Read(float& v) override;
    bool Read(double& v) override;
    bool Read(cRZString& v) override { return GetToken(v, ','); }
    bool Read(cGZPersistResourceKey& v) override;
    bool WriteTokenDef(cRZString& name, cGZPersistTokenDef& def) override;
    bool Write(std::uint8_t& v) override;
    bool Write(std::int8_t& v) override;
    bool Write(std::uint16_t& v) override;
    bool Write(std::int16_t& v) override;
    bool Write(std::uint32_t& v) override;
    bool Write(std::int32_t& v) override;
    bool Write(std::uint64_t& v) override;
    bool Write(std::int64_t& v) override;
    bool Write(float& v) override;
    bool Write(double& v) override;
    bool Write(cRZString& v) override;
    bool Write(cGZPersistResourceKey& v) override;

    // Next token up to and including `delimiter` (whitespace folded to spaces, ';' comments to the
    // end of line skipped, newlines dropped); a NUL is appended and the ends trimmed.
    bool GetToken(cRZString& token, char delimiter);

private:
    bool ReadUnsigned(std::uint32_t& v, char delimiter);
    bool ReadSigned(std::int32_t& v, char delimiter);
    bool WriteText(const cRZString& text);
};

// ---- fields ---------------------------------------------------------------------------------
class cSLTokenFieldBase {
public:
    virtual ~cSLTokenFieldBase() = default;
    const cGZPersistTokenDef& GetDef() const { return def_; }
    cRZString& GetName() { return name_; }
    const cRZString& GetName() const { return name_; }

    virtual bool ReadData(cSLTokenSerialRecordIO& io) = 0;     // [2]
    virtual bool WriteData(cSLTokenSerialRecordIO& io) = 0;    // [3]
    // [4..12] / [13..21]: element access; false unless the field's own element type matches.
    virtual bool Get(std::uint8_t&, std::uint32_t) { return false; }
    virtual bool Get(std::uint16_t&, std::uint32_t) { return false; }
    virtual bool Get(std::uint32_t&, std::uint32_t) { return false; }
    virtual bool Get(std::int32_t&, std::uint32_t) { return false; }
    virtual bool Get(std::int64_t&, std::uint32_t) { return false; }
    virtual bool Get(float&, std::uint32_t) { return false; }
    virtual bool Get(double&, std::uint32_t) { return false; }
    virtual bool Get(cIGZString&, std::uint32_t) { return false; }
    virtual bool Get(cGZPersistResourceKey&, std::uint32_t) { return false; }
    virtual bool Set(const std::uint8_t&, std::uint32_t) { return false; }
    virtual bool Set(const std::uint16_t&, std::uint32_t) { return false; }
    virtual bool Set(const std::uint32_t&, std::uint32_t) { return false; }
    virtual bool Set(const std::int32_t&, std::uint32_t) { return false; }
    virtual bool Set(const std::int64_t&, std::uint32_t) { return false; }
    virtual bool Set(const float&, std::uint32_t) { return false; }
    virtual bool Set(const double&, std::uint32_t) { return false; }
    virtual bool Set(const cIGZString&, std::uint32_t) { return false; }
    virtual bool Set(const cGZPersistResourceKey&, std::uint32_t) { return false; }

protected:
    explicit cSLTokenFieldBase(const cGZPersistTokenDef& def) : def_(def) {}
    cGZPersistTokenDef def_;   // +8
    cRZString name_;           // +0x10 (set by SetTokenString; used by the text format)
};

template <class T>
class cSLTokenField final : public cSLTokenFieldBase {
public:
    explicit cSLTokenField(const cGZPersistTokenDef& def)
        : cSLTokenFieldBase(def), data_(def.count ? new T[def.count]() : nullptr) {}

    bool ReadData(cSLTokenSerialRecordIO& io) override {
        for (std::uint32_t i = 0; i < def_.count; ++i)
            if (!io.Read(data_[i])) return false;
        return true;
    }
    bool WriteData(cSLTokenSerialRecordIO& io) override {
        for (std::uint32_t i = 0; i < def_.count; ++i)
            if (!io.Write(data_[i])) return false;
        return true;
    }

    bool Get(std::uint8_t& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(std::uint16_t& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(std::uint32_t& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(std::int32_t& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(std::int64_t& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(float& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Get(cIGZString& v, std::uint32_t i) override {
        if constexpr (std::is_same_v<T, cRZString>) {
            if (i >= def_.count) return false;
            v.AssignFromCString(data_[i].ToChar());
            return true;
        } else {
            (void)v; (void)i;
            return false;
        }
    }
    bool Get(cGZPersistResourceKey& v, std::uint32_t i) override { return GetAs(v, i); }
    bool Set(const std::uint8_t& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const std::uint16_t& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const std::uint32_t& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const std::int32_t& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const std::int64_t& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const float& v, std::uint32_t i) override { return SetAs(v, i); }
    bool Set(const cIGZString& v, std::uint32_t i) override {
        if constexpr (std::is_same_v<T, cRZString>) {
            if (i >= def_.count) return false;
            data_[i].AssignFromIGZString(v);
            return true;
        } else {
            (void)v; (void)i;
            return false;
        }
    }
    bool Set(const cGZPersistResourceKey& v, std::uint32_t i) override { return SetAs(v, i); }

private:
    template <class V>
    bool GetAs(V& value, std::uint32_t index) {
        if constexpr (std::is_same_v<T, V>) {
            if (index >= def_.count) return false;
            value = data_[index];
            return true;
        } else {
            (void)value; (void)index;
            return false;
        }
    }
    template <class V>
    bool SetAs(const V& value, std::uint32_t index) {
        if constexpr (std::is_same_v<T, V>) {
            if (index >= def_.count) return false;
            data_[index] = value;
            return true;
        } else {
            (void)value; (void)index;
            return false;
        }
    }
    std::unique_ptr<T[]> data_;   // +0x38
};

// ---- the record ---------------------------------------------------------------------------------
class cSLTokenRecord {
public:
    cSLTokenRecord() = default;
    ~cSLTokenRecord();

    // intrusive reference count (+8): starts at 1 when created by the factory
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();                                        // frees at <= 1

    bool QueryInterface(std::uint32_t iid, void** out);             // 1 or 0xa2ace662

    bool InitRead(cIGZPersistDBSerialRecord& record, std::uint32_t cookie);   // cookie already read
    bool InitRead(cIGZPersistDBSerialRecord& record);                         // reads the cookie
    bool InitWriteText(cIGZPersistDBSerialRecord& record);
    bool InitWriteBinary(cIGZPersistDBSerialRecord& record);
    // Writes pending tokens (when opened for writing) and detaches from the record.
    void Shutdown();

    bool GetKey(cGZPersistResourceKey& key) const;
    std::uint32_t GetCookie() const { return valid_ ? cookie_ : 0; }
    bool IsRecordFormatText() const { return isText_; }
    bool IsTokenAvailable(const cGZPersistTokenDef& def);
    bool SetTokenString(const cGZPersistTokenDef& def, cIGZString& name);

    bool GetFieldUint8(const cGZPersistTokenDef& def, std::uint8_t& v, std::uint32_t index);
    bool GetFieldSint8(const cGZPersistTokenDef& def, std::int8_t& v, std::uint32_t index);
    bool GetFieldUint16(const cGZPersistTokenDef& def, std::uint16_t& v, std::uint32_t index);
    bool GetFieldSint16(const cGZPersistTokenDef& def, std::int16_t& v, std::uint32_t index);
    bool GetFieldUint32(const cGZPersistTokenDef& def, std::uint32_t& v, std::uint32_t index);
    bool GetFieldSint32(const cGZPersistTokenDef& def, std::int32_t& v, std::uint32_t index);
    bool GetFieldUint64(const cGZPersistTokenDef& def, std::uint64_t& v, std::uint32_t index);
    bool GetFieldSint64(const cGZPersistTokenDef& def, std::int64_t& v, std::uint32_t index);
    bool GetFieldFloat32(const cGZPersistTokenDef& def, float& v, std::uint32_t index);
    bool GetFieldFloat64(const cGZPersistTokenDef& def, double& v, std::uint32_t index);
    bool GetFieldString(const cGZPersistTokenDef& def, cIGZString& v, std::uint32_t index);
    bool GetFieldResKey(const cGZPersistTokenDef& def, cGZPersistResourceKey& v, std::uint32_t index);
    bool SetFieldUint8(const cGZPersistTokenDef& def, const std::uint8_t& v, std::uint32_t index);
    bool SetFieldSint8(const cGZPersistTokenDef& def, const std::int8_t& v, std::uint32_t index);
    bool SetFieldUint16(const cGZPersistTokenDef& def, const std::uint16_t& v, std::uint32_t index);
    bool SetFieldSint16(const cGZPersistTokenDef& def, const std::int16_t& v, std::uint32_t index);
    bool SetFieldUint32(const cGZPersistTokenDef& def, const std::uint32_t& v, std::uint32_t index);
    bool SetFieldSint32(const cGZPersistTokenDef& def, const std::int32_t& v, std::uint32_t index);
    bool SetFieldUint64(const cGZPersistTokenDef& def, const std::uint64_t& v, std::uint32_t index);
    bool SetFieldSint64(const cGZPersistTokenDef& def, const std::int64_t& v, std::uint32_t index);
    bool SetFieldFloat32(const cGZPersistTokenDef& def, const float& v, std::uint32_t index);
    bool SetFieldFloat64(const cGZPersistTokenDef& def, const double& v, std::uint32_t index);
    bool SetFieldString(const cGZPersistTokenDef& def, const cIGZString& v, std::uint32_t index);
    bool SetFieldResKey(const cGZPersistTokenDef& def, const cGZPersistResourceKey& v, std::uint32_t index);

    bool CreateField(const cGZPersistTokenDef& def, cSLTokenFieldBase** out);
    bool GetField(const cGZPersistTokenDef& def, cSLTokenFieldBase** out);

private:
    bool ReadTokensFromSerialRecord(cSLTokenSerialRecordIO& io);
    bool WriteTokensToSerialRecord(cSLTokenSerialRecordIO& io);
    void DestroyFields();
    template <class Fn>
    bool WithField(const cGZPersistTokenDef& def, Fn&& fn) {
        cSLTokenFieldBase* field = nullptr;
        return GetField(def, &field) && fn(*field);
    }
    template <class Fn>
    bool WithFieldCreate(const cGZPersistTokenDef& def, Fn&& fn) {
        cSLTokenFieldBase* field = nullptr;
        if (!GetField(def, &field) && !CreateField(def, &field)) return false;
        return fn(*field);
    }

    std::uint32_t refCount_ = 1;                              // +8
    cRZAutoRefCount<cIGZPersistDBSerialRecord> record_;       // +0x10
    std::uint32_t cookie_ = 0;                                // +0x18
    bool valid_ = false;                                      // +0x1c
    bool isText_ = false;                                     // +0x1d
    bool writing_ = false;                                    // +0x1e
    std::map<std::uint32_t, std::unique_ptr<cSLTokenFieldBase>> fields_;   // +0x20
};

// ---- the resource side --------------------------------------------------------------------------
// Resources that hold a token record (iid 0xa2ace668). Slot names are provisional: only the call
// pattern of cSLTokenResourceFactory is known (SerialRead hands the freshly read record over at
// +0x18; SerialWrite asks the format at +0x30 and has the resource fill a writable record at +0x20).
class cISLTokenResource : public cIGZUnknown {
public:
    virtual bool SetTokenRecord(cSLTokenRecord* record) = 0;      // [3]
    virtual bool WriteTokens(cSLTokenRecord* record) = 0;         // [4]
    virtual bool IsTextFormat() = 0;                              // [6] (slot [5] unseen)
};

// COM class factory seam: GZCOM()->CreateInstance(classId, iid, out).
class cIGZCOMClassFactory {
public:
    virtual ~cIGZCOMClassFactory() = default;
    virtual bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
};
cIGZCOMClassFactory* GZCOM();
void SetGZCOM(cIGZCOMClassFactory* com);

class cSLTokenResourceFactory : public cGZPersistResourceFactory {
public:
    using cGZPersistResourceFactory::CreateInstance;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;
    bool SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;
    std::uint32_t ReadCookie(cIGZPersistDBSerialRecord& record);
};

}  // namespace sc4
