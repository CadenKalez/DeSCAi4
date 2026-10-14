// cRZIStream / cRZNullIStream — base sequential input stream: every typed read is built on the
// raw GetVoid()/GetError() pair supplied by a subclass; multi-byte values are byte-swapped when the
// stream's endianness flag does not match the data. Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//
// Original layout: +0x00 vptr, +0x08 refcount, +0x0c bool `dataIsLittleEndian`, +0x10 user data.
#pragma once
#include <cstdint>

#include "gz/cIGZIStream.h"

class cRZIStream : public cIGZIStream {
public:
    explicit cRZIStream(bool dataIsLittleEndian) : dataIsLittleEndian_(dataIsLittleEndian) {}
    virtual ~cRZIStream();

    bool QueryInterface(std::uint32_t iid, void** out) override;  // cIGZUnknown(1) or cIGZIStream
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is <= 1

    bool GetSint8(std::int8_t& value) override { return GetVoid(&value, 1); }
    bool GetUint8(std::uint8_t& value) override { return GetVoid(&value, 1); }
    bool GetSint16(std::int16_t& value) override;
    bool GetUint16(std::uint16_t& value) override;
    bool GetSint32(std::int32_t& value) override;
    bool GetUint32(std::uint32_t& value) override;
    bool GetSint64(std::int64_t& value) override;
    bool GetUint64(std::uint64_t& value) override;
    bool GetFloat32(float& value) override;
    bool GetFloat64(double& value) override;
    bool GetRZCharStr(char* buffer, std::uint32_t maxLength) override;
    bool GetGZStr(cIGZString& value) override;
    bool GetGZSerializable(cIGZSerializable& object) override;

    void SetUserData(cIGZVariant* userData) override;
    cIGZVariant* GetUserData() override { return userData_; }

    bool Init(bool dataIsLittleEndian) { dataIsLittleEndian_ = dataIsLittleEndian; return true; }
    bool Shutdown() { return true; }

protected:
    // Reads an unsigned LEB128 (7 bits per byte, least-significant group first). 0 on empty/failed.
    std::uint32_t DecodeStringLength();

private:
    template <class T> bool GetScalar(T& value);

    std::int32_t refCount_ = 0;
    bool dataIsLittleEndian_;
    cIGZVariant* userData_ = nullptr;
};

// Stream that is always at error: every read fails and latches the error flag.
class cRZNullIStream final : public cRZIStream {
public:
    cRZNullIStream() : cRZIStream(false) {}
    bool Skip(std::uint32_t) override { error_ = true; return false; }
    bool GetVoid(void*, std::uint32_t) override { error_ = true; return false; }
    bool GetError() const override { return error_; }

private:
    bool error_ = false;
};
