// cRZOStream / cRZNullOStream — base sequential output stream built on SetVoid(); the mirror of
// cRZIStream (same endianness flag, same LEB128 string-length prefix).
#pragma once
#include <cstdint>

#include "gz/cIGZOStream.h"

class cRZOStream : public cIGZOStream {
public:
    explicit cRZOStream(bool dataIsLittleEndian) : dataIsLittleEndian_(dataIsLittleEndian) {}
    virtual ~cRZOStream();

    bool QueryInterface(std::uint32_t iid, void** out) override;  // cIGZUnknown(1) or cIGZOStream
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool SetSint8(std::int8_t value) override { return SetVoid(&value, 1); }
    bool SetUint8(std::uint8_t value) override { return SetVoid(&value, 1); }
    bool SetSint16(std::int16_t value) override;
    bool SetUint16(std::uint16_t value) override;
    bool SetSint32(std::int32_t value) override;
    bool SetUint32(std::uint32_t value) override;
    bool SetSint64(std::int64_t value) override;
    bool SetUint64(std::uint64_t value) override;
    bool SetFloat32(float value) override;
    bool SetFloat64(double value) override;
    bool SetRZCharStr(const char* value) override;
    bool SetGZStr(const cIGZString& value) override;
    bool SetGZSerializable(const cIGZSerializable& object) override;

    void SetUserData(cIGZVariant* userData) override;
    cIGZVariant* GetUserData() override { return userData_; }

    bool Init(bool dataIsLittleEndian) { dataIsLittleEndian_ = dataIsLittleEndian; return true; }
    bool Shutdown() { return true; }

protected:
    // Writes an unsigned LEB128 length prefix.
    void EncodeStringLength(std::uint32_t length);

private:
    template <class T> bool SetScalar(T value);

    std::int32_t refCount_ = 0;
    bool dataIsLittleEndian_;
    cIGZVariant* userData_ = nullptr;
};

// Stream that accepts and discards everything.
class cRZNullOStream final : public cRZOStream {
public:
    cRZNullOStream() : cRZOStream(false) {}
    void Flush() override {}
    bool SetVoid(const void*, std::uint32_t) override { return true; }
    bool GetError() const override { return false; }
};
