// cIGZOStream — sequential binary output. Slots 3..20 recovered from cRZOStream / cRZNullOStream.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"

class cIGZSerializable;

class cIGZOStream : public cIGZUnknown {
public:
    virtual void Flush() = 0;                                                   // [3]
    virtual bool SetSint8(std::int8_t value) = 0;                               // [4]
    virtual bool SetUint8(std::uint8_t value) = 0;                              // [5]
    virtual bool SetSint16(std::int16_t value) = 0;                             // [6]
    virtual bool SetUint16(std::uint16_t value) = 0;                            // [7]
    virtual bool SetSint32(std::int32_t value) = 0;                             // [8]
    virtual bool SetUint32(std::uint32_t value) = 0;                            // [9]
    virtual bool SetSint64(std::int64_t value) = 0;                             // [10]
    virtual bool SetUint64(std::uint64_t value) = 0;                            // [11]
    virtual bool SetFloat32(float value) = 0;                                   // [12]
    virtual bool SetFloat64(double value) = 0;                                  // [13]
    virtual bool SetRZCharStr(const char* value) = 0;                           // [14] length-prefixed
    virtual bool SetGZStr(const cIGZString& value) = 0;                         // [15]
    virtual bool SetGZSerializable(const cIGZSerializable& object) = 0;         // [16] object.Write(*this)
    virtual bool SetVoid(const void* source, std::uint32_t byteCount) = 0;      // [17] raw bytes
    virtual bool GetError() const = 0;                                          // [18]
    virtual void SetUserData(cIGZVariant* userData) = 0;                        // [19]
    virtual cIGZVariant* GetUserData() = 0;                                     // [20]
};

constexpr std::uint32_t kGZIID_cIGZOStream = 0xc51b11cb;
